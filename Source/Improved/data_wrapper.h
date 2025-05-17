#pragma once

/*
Dataya ait ptr'yi barindirir.
Olusturuldugunda data nullptr degilse
addheldlock calistirilir.

yok olurken de removeheldlock calistirilir.
*/
#include "interfaces.h"

#include <type_traits>
#include <memory>
#include <atomic>
#include <cstdint>
#include <functional>


namespace NThreadSafe {
	namespace NLock {
		//sadece shared_ptr tipindeki verileri kabul eder.
		template<typename TData, typename std::enable_if<std::is_same_v<TData, std::shared_ptr<typename TData::element_type>>, int>::type = 0>
		class CDataWrapper {
		private:
			std::shared_ptr<INewThreadTracker> m_tracker;
			TData m_data; //Data pointer
			uintptr_t m_mutexID; //data'ya ait mutex id'si
			std::atomic<EWrapperResult> m_result; // wrapper sonucu
		public:
			using TMutexRef = std::optional<std::reference_wrapper<std::shared_mutex>>;

			CDataWrapper(std::shared_ptr<INewThreadTracker> _thTracker = nullptr, TData _data = nullptr, TMutexRef _mutex = std::nullopt, uintptr_t _mutexId = 0, ELockType _requestType = ELockType::Read)
				: m_tracker(std::move(_thTracker)), m_data(std::move(_data)), m_mutexID(_mutexId) {
				m_result.store(EWrapperResult::DATA_NOT_EXISTS, std::memory_order_release);				
				
				//Once veriye bak.
				if (!m_tracker || !m_data.get() || m_mutexID == 0 || !_mutex.has_value()) return;

				// TryAcquireLock'u structured binding kullanmadan çağır
				auto result_pair = m_tracker->TryAcquireLock(_mutex.value().get(), m_mutexID, _requestType);
				auto& lockData = result_pair.first;
				auto& canAcquire = result_pair.second;

				//Kilidi asla alamayacaksak
				if (!canAcquire) {
					m_result.store(EWrapperResult::BUSY, std::memory_order_release);
					m_data.reset(); //data'yi invalid et cunku kilit alinamadi.
					return;
				}

				//Eger kilit almak icin beklememiz gerekiyorsa, hata durumlarinda wrapper'i invalid ettikten sonra return et.
				if (lockData.get()) {
					auto result = lockData->Wait(_requestType); // kilidin alinabilir olmasini bekle.

					if (result == EAcquireResult::NEED_TO_CONVERT) {
						//kilit tipini degistir.
						m_tracker->ReleaseLock(m_mutexID);
						auto result_pair = m_tracker->TryAcquireLock(_mutex.value().get(), m_mutexID, ELockType::Write);
						bool& bSuccess = result_pair.second;
						if (!bSuccess) {
							m_result.store(EWrapperResult::BUSY, std::memory_order_release);
							m_data.reset(); //data'yi invalid et cunku kilit alinamadi.
							return;
						}
						//kilit alindi.
					}
					else if (result == EAcquireResult::AVAIL) {
						//Kilidi kendin almalisin.
						auto result_pair = m_tracker->TryAcquireLock(_mutex.value().get(), m_mutexID, _requestType);
						auto& cnAcquire = result_pair.second;
						if (!cnAcquire) {
							m_result.store(EWrapperResult::BUSY, std::memory_order_release);
							return;
						}
						//kilit alinmistir.
					}
					else { //timeout durumu
						return;
					}
				}
				
				//kilidi dogrudan alabildiysek
				m_result.store(EWrapperResult::SUCCESS, std::memory_order_release);
			}
		public:
			
			~CDataWrapper() {
				//LOG_INFO(LogClass::NORMAL, "CDataWrapper destructor called with data: ?, result: ?, mutexId: ?", m_data.get(), m_result.load(std::memory_order_acquire), m_mutexID);
				if (m_result == EWrapperResult::SUCCESS) {
					//m_tracker varligini kontrol etmiyorum cunku basarili olduysa kesinlikle var olmalidir.
					m_tracker->ReleaseLock(m_mutexID);
				}
			}

			// Move constructor ve move assignment operator
			CDataWrapper(CDataWrapper&& other) noexcept : 
				m_tracker(std::move(other.m_tracker)),
				m_data(std::move(other.m_data)), 
				m_mutexID(other.m_mutexID) // std::move kullanmadık çünkü primitive tip 
			{
				//LOG_INFO(LogClass::NORMAL, "CDataWrapper move constructor called with data: ?, result: ?, mutexId: ?", m_data.get(), m_result.load(std::memory_order_acquire), m_mutexID);
				m_result.store(other.m_result.load(std::memory_order_acquire), std::memory_order_release);
				other.m_result.store(EWrapperResult::DATA_NOT_EXISTS, std::memory_order_release);
			}
			
			CDataWrapper& operator=(CDataWrapper&& other) noexcept {
				//LOG_INFO(LogClass::NORMAL, "CDataWrapper move assignment operator called with data: ?, result: ?, mutexId: ?", m_data.get(), m_result.load(std::memory_order_acquire), m_mutexID);
				if (this != &other) {		
					m_tracker = std::move(other.m_tracker);
					m_data = std::move(other.m_data);
					m_mutexID = other.m_mutexID; // std::move kullanmadık çünkü primitive tip	
					m_result.store(other.m_result.load(std::memory_order_acquire), std::memory_order_release);
					other.m_result.store(EWrapperResult::DATA_NOT_EXISTS, std::memory_order_release);
				}
				return *this;
			}

			// Copy constructor ve copy assignment operator STL icin gerekli olabilir
			// Ancak bu sınıf icin uygun olmadigindan tamamen disable edildi
			CDataWrapper(const CDataWrapper& other) = delete;
			CDataWrapper& operator=(const CDataWrapper& other) = delete;

			//Data'ya erisen her sinif wrapper sonucunu kontrol etmeli ve ona gore operasyon eklemeli veya yapacagi islemden vazgecmelidir.
			explicit operator bool() const noexcept {
				return m_result.load(std::memory_order_acquire) == EWrapperResult::SUCCESS && m_data && m_data.get();
			}

			EWrapperResult GetResult() const noexcept {
				return m_result.load(std::memory_order_acquire);
			}

			// Karşılaştırma operatörü - EWrapperResult ile karşılaştırma için
			bool operator==(EWrapperResult result) const noexcept {
				return m_result.load(std::memory_order_acquire) == result;
			}

			// Karşılaştırma operatörü - eşit değilse
			bool operator!=(EWrapperResult result) const noexcept {
				return m_result.load(std::memory_order_acquire) != result;
			}

			auto operator->() noexcept {
				return m_data.get();
			}

			auto get() noexcept {
				return m_data.get();
			}
		};
	};
};
