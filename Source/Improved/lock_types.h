#pragma once

#include "common_types.h"

#include <mutex>
#include <shared_mutex>
#include <thread>
#include <memory>
#include <optional>
#include <atomic>
#include <unordered_map>
#include <cstdint>
#include <condition_variable>

namespace NThreadSafe {
	namespace NLock{
	//Read ve write lock'lar icin ortak yapilari barindirir.
	class AbstractLock : public ILock {
	protected: //newly added
		std::condition_variable m_cv{};
		std::mutex m_cvMutex{};
	protected:
		const ELockType m_lockType;
		const uintptr_t m_mutexID; // sadece loglama icin
		std::shared_mutex& m_mutex; // ulasilacak verinin mutex'i
	protected:
		std::unordered_map<TID, TMutexThreadData> m_owners{};
		mutable std::shared_mutex m_classMutex{};
	protected:
		AbstractLock(ELockType _type, uintptr_t _mutexID, std::shared_mutex& _mutex) 
		: m_lockType(_type), m_mutexID(_mutexID), m_mutex(_mutex) {}
	protected:
		bool IsOwner() const noexcept;
		size_t GetOwnerCount() const noexcept;
		bool IsOnlyOwner() const noexcept;
		void CounterIncrease() noexcept;
		void PrintOwners() noexcept;
	public:
		ELockType GetType() const noexcept override { return m_lockType; }
		uintptr_t GetMutexID() const noexcept override { return m_mutexID; }
		std::shared_mutex& GetMutex() noexcept override { return m_mutex; }
	public: // virtuals
		~AbstractLock() override = default;
		virtual EAcquireResult CanAcquire(ELockType _requesttype) noexcept override = 0;
		
		virtual void AddOwnership() noexcept override;
		virtual void RemoveOwnership() noexcept override;
		virtual bool ShouldRemove() noexcept override;

		virtual void AcquireLock(ELockType _requesttype) noexcept override = 0;
		virtual void RemoveGuard() noexcept override = 0;
		virtual void CreateGuard() noexcept override = 0;
		EAcquireResult Wait(ELockType _requestType) noexcept override = 0;
	protected:
		virtual bool HasGuard() const noexcept override = 0;
	};

	class CReadLock : public AbstractLock {
		std::optional<std::shared_lock<std::shared_mutex>> m_lockGuard;
	public:
		CReadLock(uintptr_t _mutexID, std::shared_mutex& _mutex) : AbstractLock(ELockType::Read, _mutexID, _mutex) {}

		~CReadLock() override = default;

		void RemoveGuard() noexcept override;

		void CreateGuard() noexcept override;

		//[[maybe_unused]]  buraya da mi gerekir?
		EAcquireResult CanAcquire(ELockType _requesttype) noexcept override;

		void AcquireLock(ELockType _requesttype) noexcept override;

		bool HasGuard() const noexcept override;

		EAcquireResult Wait(ELockType _requestType) noexcept override;
	};

	class CWriteLock : public AbstractLock {
		std::optional<std::unique_lock<std::shared_mutex>> m_lockGuard;
	public:
		CWriteLock(uintptr_t _mutexID, std::shared_mutex& _mutex) : AbstractLock(ELockType::Write, _mutexID, _mutex) {}

		~CWriteLock() override = default;

		EAcquireResult CanAcquire(ELockType _requesttype) noexcept override;

		void RemoveGuard() noexcept override;

		void CreateGuard() noexcept override;

		void AcquireLock(ELockType _requesttype) noexcept override;

		bool HasGuard() const noexcept override;

		EAcquireResult Wait(ELockType _requestType) noexcept override;
	};
	};
};