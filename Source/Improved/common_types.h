#pragma once
#include "constants.h"
#include "interfaces.h"

#include <chrono>
#include <exception>
#include <atomic>
#include <thread>
#include <cstdint>
#include <shared_mutex>
#include <optional>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <deque>
#include <type_traits>

namespace NThreadSafe {
	using TID = std::thread::id;
	namespace NLock{

		//lock bazli unordered_set'te her thread'e ait data seklinde tutulur. ilgili thread'in o lock'u ne zmaan aldigina vb dair bilgileri barindirir.
		struct TMutexThreadData {
			const std::chrono::steady_clock::time_point ownTime; // kilidin alinma zamani
			std::atomic<size_t> lockCount; // mevcut kilit sayisi
			TMutexThreadData() : ownTime(std::chrono::steady_clock::now()){
				lockCount.store(1, std::memory_order_relaxed);
			}

			uint64_t GetHeldMs() const noexcept {
				return static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - ownTime).count());
			}
		};


		template<typename TData, typename std::enable_if<std::is_same_v<TData, std::shared_ptr<typename TData::element_type>>, int>::type = 0>
		struct TLockData /*: public std::enable_shared_from_this<TLockData<TData>> */{
			using OperationType = std::function<void(TData)>;
		private:
			std::shared_ptr<ILock> ptr;

			struct TOperation {
				OperationType op;
				TData data;
				TOperation(OperationType&& _op, TData _data) : op(std::move(_op)), data(_data){}
			};
		private:
			std::mutex m_operationMutex;
			std::deque<TOperation> m_operations{};
		public:
			TLockData(std::shared_ptr<ILock> _ptr) : ptr(_ptr){}

			std::shared_ptr<ILock> GetILock() const {
				return ptr;
			}

			size_t GetOperationCount() {
				std::unique_lock<std::mutex> mute(m_operationMutex);
				return m_operations.size();
			}

			//Operasyonlar calisirken bir anda durdurup yeni operasyon ekleme secenegi olmalidir.

			void AddOperation(OperationType&& _op, TData _data) {
				std::unique_lock<std::mutex> mute(m_operationMutex);
				m_operations.emplace_back(std::move(_op), _data);
			}

			//Kilit alindiktan sonra siradaki tum operasyonlar gerceklestirilir.
			void RunOperations(std::atomic<bool>& bForce) {
				std::unique_lock<std::mutex> mute(m_operationMutex);
				while (!m_operations.empty() && !bForce) {
					auto elem = std::move(m_operations.front());
					m_operations.pop_front();
					elem.op(elem.data);
				}
			}
		};
	};
};

namespace NThreadSafe {
	namespace NQueue{
		static constexpr uint8_t MAX_WORKER_THREAD_COUNT = 10;
		static constexpr uint8_t MIN_WORKER_THREAD_COUNT = 1;
		static constexpr uint8_t MAX_RETRY_COUNT = 3;
		static constexpr uint16_t OPERATION_TIMEOUT = 300; // 300 seconds
		static constexpr uint16_t CLEANER_INTERVAL = 120; // 120 seconds
		static constexpr uint32_t MAX_QUEUE_SIZE = 20000;
		static constexpr const char* QUEUE_CLEANER_BASENAME = "ClearQueue_";

		enum class EQueueState {
			WORKING,
			IDLE,
			THREADS_STOPPED,
		};

		template<typename DataType>
		struct QueuedOperation {
			DataType m_data;
			std::chrono::steady_clock::time_point m_enqueue_time;
			std::atomic<int> m_retry_count;

			// Constructors with perfect forwarding
			explicit QueuedOperation(DataType&& data)
				: m_data(std::forward<DataType>(data)),
				m_enqueue_time(std::chrono::steady_clock::now()) {
				m_retry_count.store(0, std::memory_order_relaxed);
			}	
			
			// Move constructor and assignment
			QueuedOperation(QueuedOperation&& other) noexcept
				: m_data(std::move(other.m_data)),
				m_enqueue_time(other.m_enqueue_time){
				m_retry_count.store(other.m_retry_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
			}
			
			QueuedOperation& operator=(QueuedOperation<DataType>&& other) noexcept {
				if (this != &other) {
					m_data = std::move(other.m_data);
					m_enqueue_time = other.m_enqueue_time;
					m_retry_count.store(other.m_retry_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
				}
				return *this;
			}
			
			// Delete copy constructor and assignment
			QueuedOperation(const QueuedOperation&) = delete;
			QueuedOperation& operator=(const QueuedOperation&) = delete;
		};	
	};	
};

