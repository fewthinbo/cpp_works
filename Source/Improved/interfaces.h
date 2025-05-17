#pragma once
#include "constants.h"

#include <memory>
#include <shared_mutex>
#include <utility>


namespace NThreadSafe {
	namespace NLock {

		struct ISafeData {
			std::shared_mutex m_mutex{};
			const uintptr_t m_mutexID;
			ISafeData() : m_mutexID(reinterpret_cast<uintptr_t>(&m_mutex))  {}

			// Move operations
			ISafeData& operator=(ISafeData&& other) noexcept = default;
			ISafeData(ISafeData&& other) noexcept = default;

			//copy : disabled
			ISafeData& operator=(const ISafeData& other) = delete;
			ISafeData(const ISafeData& other) = delete;

			~ISafeData() = default;

		};

		class ILock : public std::enable_shared_from_this<ILock> {
		public:
			virtual ~ILock() = default;  // Virtual destructor for interface
			virtual ELockType GetType() const noexcept = 0;
			virtual uintptr_t GetMutexID() const noexcept = 0;

			//Kilit alinabilir mi?
			virtual EAcquireResult CanAcquire(/*[[maybe_unused]]*/ ELockType _requesttype) noexcept = 0;

			//true: Kilit tamamen kaldirilmali
			virtual bool ShouldRemove() noexcept = 0;

			virtual void RemoveOwnership() noexcept = 0;

			//kilidi almayi dene
			virtual void AcquireLock(/*[[maybe_unused]]*/ELockType _requesttype) noexcept = 0;

			//veriye ait mutex'i getir.
			virtual std::shared_mutex& GetMutex() noexcept = 0;

			//Varolan guard'i kaldir. Reorder isleminde kullanilir.
			virtual void RemoveGuard() noexcept = 0;

			//yeni bir guard olustur. Reorder isleminde kullanilir.
			virtual void CreateGuard() noexcept = 0;

			//Kilit alinabilir olana kadar bekler.
			virtual EAcquireResult Wait(ELockType _requestType) noexcept = 0;

			//Sahiplik ekler
			virtual void AddOwnership() noexcept = 0;

		protected:
			//Aktif bir guard'i var mi?
			virtual bool HasGuard() const noexcept = 0;
		};

		class INewThreadTracker {
		public:
			virtual ~INewThreadTracker() = default;
		public:
			virtual void PrintAll() = 0;
		public:
			// ikinci deger false ise hic denemeye gerek yok.
			virtual std::pair<std::shared_ptr<ILock>, bool> TryAcquireLock(std::shared_mutex& _mutex, uintptr_t _mutexID, ELockType _requestType) noexcept = 0;
		
			//sahipligi kontrol ederek gerektiginde kilidi kayitlardan siler.
			virtual void ReleaseLock(uintptr_t _mutexID, bool bOperationCall = false) noexcept = 0;
		private:
			//Thread'e ait kilitlerin yeniden siralanmaya ihtiyaci var mi?
			virtual bool NeedToReset(uintptr_t _mutexID) noexcept = 0;

			//Thread'e ait tum kilitleri yeniden siralar.
			virtual void ReorderAll() noexcept = 0;

			virtual void RemoveFromMutexes(uintptr_t _mutexID) noexcept = 0;

			//mutex kaydini thread bazli siler.
			virtual void RemoveFromHeldLocks(uintptr_t _mutexID) noexcept = 0;

			//ilk kez olusturulan mutex'leri kaydeder.
			virtual bool RegisterMutex(std::shared_mutex& _mutex, uintptr_t _mutexID, ELockType _requestType) noexcept = 0;
		};
	}
}
