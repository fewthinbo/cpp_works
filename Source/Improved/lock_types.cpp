#include "lock_types.h"

namespace NThreadSafe {
	namespace NLock{
	bool AbstractLock::IsOwner() const noexcept {
		std::shared_lock<std::shared_mutex> clMute(m_classMutex);
		const TID& threadID = std::this_thread::get_id();
		auto found = m_owners.find(threadID);
		return found != m_owners.end();
	}
	size_t AbstractLock::GetOwnerCount() const noexcept {
		std::shared_lock<std::shared_mutex> clMute(m_classMutex);
		return m_owners.size();
	}

	void AbstractLock::PrintOwners() noexcept {
#ifdef LOG_THREAD_SAFE
		std::shared_lock<std::shared_mutex> clMute(m_classMutex);
		LOG_TRACE(LogClass::NORMAL, "===================================== STARTING PRINT TO ALL HELD INFO FOR MUTEX: ?, OWNER_COUNT: ? =====================================", m_mutexID, m_owners.size());
		LOG_TRACE(LogClass::NORMAL, "THREAD\t\tHELD(ms)");
		for (const auto& [tid, info] : m_owners) /*Tum thread'lerin bu kilidi ne kadar tuttugunu yazdir.*/ {

			//Logging the held seconds;
			const auto& heldMS = info.GetHeldMs();
			if (heldMS >= LOG_HELD_MS_LIMIT) {
				LOG_TRACE(LogClass::NORMAL, "Thread(?) held mutex(?) for (?) milliseconds.", tid, m_mutexID, heldMS);
			}
			else {
				LOG_TRACE(LogClass::NORMAL, "?\t\t?", tid, heldMS);
			}
		}
		LOG_TRACE(LogClass::NORMAL, "===================================== END OF PRINT FOR MUTEX ? =====================================", m_mutexID);
#endif
	}

	void AbstractLock::RemoveOwnership() noexcept {
		const TID& threadID = std::this_thread::get_id();
#ifdef LOG_THREAD_SAFE
		{
#endif
			std::unique_lock<std::shared_mutex> clMute(m_classMutex);
			auto found = m_owners.find(threadID);
			if (/*[[unlikely]]*/ found == m_owners.end()) return;

			size_t curLockCount = found->second.lockCount.fetch_sub(1, std::memory_order_acq_rel);
			if (curLockCount <= 1) {
				//remove from map
				m_owners.erase(threadID);
			}
#ifdef LOG_THREAD_SAFE
		}
		PrintOwners();
#endif
	}

	void AbstractLock::AddOwnership() noexcept {
		std::unique_lock<std::shared_mutex> clMute(m_classMutex);
		const TID& tid = std::this_thread::get_id();

		auto found = m_owners.find(tid);
		if (found != m_owners.end()) return; //zaten ekli

#ifdef LOG_THREAD_SAFE
		auto [iter, inserted] = m_owners.try_emplace(tid);
		if (inserted) {
			LOG_INFO(LogClass::NORMAL, "New owner(tid:?) added for mutexId(?)", tid, m_mutexID);
		}
		else {
			LOG_WARN(LogClass::NORMAL, "Failed to adding new owner(tid:?), mutexId(?)", tid, m_mutexID);
		}
#else
		m_owners.try_emplace(tid);
#endif
	}

	bool AbstractLock::ShouldRemove() noexcept {
		if (!HasGuard()) return true;

		std::shared_lock<std::shared_mutex> clMute(m_classMutex);
		return m_owners.size() <= 0;
	}

	//don't use any type of lock
	bool AbstractLock::IsOnlyOwner() const noexcept {
		return GetOwnerCount() <= 1 && IsOwner();
	}

	void AbstractLock::CounterIncrease() noexcept {
		std::unique_lock<std::shared_mutex> clMute(m_classMutex);
		const TID& threadID = std::this_thread::get_id();
		auto found = m_owners.find(threadID);
		if (found == m_owners.end()) return;

		found->second.lockCount.store(found->second.lockCount.load(std::memory_order_relaxed) + 1, std::memory_order_release);
	}
	//end of abstractLock Class


	void CReadLock::RemoveGuard() noexcept {
		std::unique_lock<std::shared_mutex> clMute(m_classMutex);
		m_lockGuard.reset();
		m_cv.notify_all();
	}

	void CReadLock::CreateGuard() noexcept {
		std::unique_lock<std::shared_mutex> clMute(m_classMutex);
		m_lockGuard.emplace(m_mutex);
		m_cv.notify_all();
	}


	EAcquireResult CReadLock::Wait(ELockType _requestType) noexcept {
		//define the return value
		EAcquireResult ret = EAcquireResult::CANNOT;
		std::unique_lock<std::mutex> mute(m_cvMutex);
		m_cv.wait_for(mute, std::chrono::milliseconds(LOCK_ACQUIRE_TIMEOUT), [&]() -> bool {
			if (!HasGuard()) {
				ret = EAcquireResult::AVAIL;
				return true;
			}

			if (_requestType == ELockType::Read) {
				ret = EAcquireResult::AVAIL;
				return true;  // Read locks are compatible with other read locks
			}
			else if (_requestType == ELockType::Write) {
				if (IsOnlyOwner()) {
					ret = EAcquireResult::NEED_TO_CONVERT;
					return true;
				}
				return false;
			}

			return false;
		});

		return ret;
	}

	EAcquireResult CReadLock::CanAcquire(ELockType _requesttype) noexcept {
		if (!HasGuard()) return EAcquireResult::AVAIL;

		if (_requesttype == ELockType::Read) {
			return EAcquireResult::AVAIL;  // Read locks are compatible with other read locks
		}
		else if (_requesttype == ELockType::Write) {
			if (IsOnlyOwner()) {
				return EAcquireResult::NEED_TO_CONVERT;
			}
		}

		return EAcquireResult::CANNOT; 
	}

	bool CReadLock::HasGuard() const noexcept {
		std::shared_lock<std::shared_mutex> clMute(m_classMutex);
		return m_lockGuard.has_value();
	}

	void CReadLock::AcquireLock(ELockType _requesttype) noexcept {
		if (_requesttype != ELockType::Read) return;
		CreateGuard();
		if (IsOwner()) {
			CounterIncrease();
		}
		else {
			AddOwnership();
		}
	}
	//end of readlock class


	EAcquireResult CWriteLock::CanAcquire(ELockType _requesttype) noexcept {
		if (!HasGuard()) return EAcquireResult::AVAIL;
		if (IsOwner()) {
			return EAcquireResult::AVAIL;
		}
		return EAcquireResult::CANNOT;
	}

	EAcquireResult CWriteLock::Wait(ELockType _requestType) noexcept {
		//define the return value
		EAcquireResult ret = EAcquireResult::CANNOT;
		std::unique_lock<std::mutex> mute(m_cvMutex);
		m_cv.wait_for(mute, std::chrono::milliseconds(LOCK_ACQUIRE_TIMEOUT), [&]() -> bool {
			if (!HasGuard()) {
				ret = EAcquireResult::AVAIL;
				return true;
			}

			if (IsOwner()) {
				ret = EAcquireResult::AVAIL;
				return true;
			}

			return false;
		});

		return ret;
	}

	void CWriteLock::RemoveGuard() noexcept {
		std::unique_lock<std::shared_mutex> clMute(m_classMutex);
		m_lockGuard.reset();
		m_cv.notify_all();
	}

	void CWriteLock::CreateGuard() noexcept {
		std::unique_lock<std::shared_mutex> clMute(m_classMutex);
		m_lockGuard.emplace(m_mutex);
		m_cv.notify_all();
	}

	bool CWriteLock::HasGuard() const noexcept {
		std::shared_lock<std::shared_mutex> clMute(m_classMutex);
		return m_lockGuard.has_value();
	}

	void CWriteLock::AcquireLock(ELockType _requesttype) noexcept {
		CreateGuard();
		if (IsOwner()) {
			CounterIncrease();
		}
		else {
			AddOwnership();
		}
	}
	};
}