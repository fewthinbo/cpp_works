#pragma once
#include "common_types.h"

#include <Singletons/future.h>

#include <atomic>
#include <string>
#include <deque>
#include <functional>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <algorithm>
#include <thread>

namespace NThreadSafe {
	namespace NQueue{

	template<typename TData>
	class CNormalQueue{
		std::atomic<EQueueState> m_state;
		std::mutex m_mutex; //Fonksiyonlar icin kullaniliyor.
	public:
		using DataType = std::decay_t<TData>;
		using ProcessFunc = std::function<bool(DataType&)>;
	private:
		std::deque<QueuedOperation<TData>> m_container{};
		std::mutex m_mutexContainer{};
		std::condition_variable m_cv{};
	private:
		std::atomic<uint8_t> m_workerThreadCount;
		ProcessFunc m_processFunc;// worker thread'de her bir process için disaridan cagirilacak fonksiyon
	public:
		CNormalQueue(ProcessFunc processFunc, uint8_t workerThreadCount = 1)
			: m_state(EQueueState::THREADS_STOPPED), m_processFunc(processFunc) {
		m_workerThreadCount = std::clamp(m_workerThreadCount.load(), MIN_WORKER_THREAD_COUNT, MAX_WORKER_THREAD_COUNT);
			StartThreads();
		}
		~CNormalQueue() {
			StopThreads(true);
		}
		CNormalQueue& operator=(CNormalQueue&& other) = default;
		CNormalQueue(CNormalQueue&& other) = default;

		CNormalQueue(const CNormalQueue& other) = delete;
		CNormalQueue& operator=(const CNormalQueue& other) = delete;
private:
	std::string GetTaskName(uint8_t threadIndex){
		std::lock_guard<std::mutex> funcMute(m_mutex);
		std::stringstream ss{};
		ss << this << "_" << typeid(DataType).name() << "_" << m_workerThreadCount << "_" << threadIndex;
		return ss.str();
	}

	uint32_t Stop(bool bClearTasks){
		std::lock_guard<std::mutex> funcMute(m_mutex);
		uint32_t remainingWorkCount = 0;
		{
			std::unique_lock<std::mutex> lock(m_mutexContainer);
			remainingWorkCount = m_container.size();
			if (bClearTasks){
				m_container.clear();
			}
		}
		return remainingWorkCount;
	}

	void StartCleaner() {
		std::stringstream ss{};
		ss << QUEUE_CLEANER_BASENAME << this;
		futureInstance.addTask<void>(ss.str(), [this](std::atomic_bool& bForce) {
			while (!bForce) {
				{
					auto now = std::chrono::steady_clock::now();
					std::unique_lock<std::mutex> lock(m_mutexContainer);

					// Manual loop to remove expired tasks instead of std::erase_if
					auto it = m_container.begin();
					while (it != m_container.end()) {
						if (std::chrono::duration_cast<std::chrono::seconds>(now - it->m_enqueue_time).count() > OPERATION_TIMEOUT) {
							it = m_container.erase(it);
						}
						else {
							++it;
						}
					}
				}
				std::this_thread::sleep_for(std::chrono::seconds(CLEANER_INTERVAL));
			}
		});
	}

	void StartThreads(){
		if (GetState() == EQueueState::WORKING) return;

		std::lock_guard<std::mutex> funcMute(m_mutex);
		StartCleaner();
		for (uint8_t i = 0; i < m_workerThreadCount; i++){
			futureInstance.addTask<void>(GetTaskName(i), 
				[this](std::atomic_bool& bForce){
					while (!bForce){
						std::unique_lock<std::mutex> lock(m_mutexContainer); // wait icin kilit acilacagindan dolayi unique_lock kullanilir
						m_cv.wait(lock, [this]{
							return !m_container.empty() && 
								   m_state.load(std::memory_order_acquire) == EQueueState::WORKING;
						});
						if (m_container.empty()) continue;

						if (!m_processFunc){
							StopThreads(true);
							break;
						}

						if (GetState() != EQueueState::WORKING) continue;

						// Önce front değerine referans alıp, sonra taşıyarak (move) geçici değişkene aktaralım
						QueuedOperation task = std::move(m_container.front());
						m_container.pop_front();
						lock.unlock();

						if (!m_processFunc(task.m_data)){
							task.m_retry_count++;
							if (task.m_retry_count < MAX_RETRY_COUNT){
								lock.lock();
								m_container.push_back(std::move(task));
								lock.unlock();
							}
						}
					}
				}
			);
		}
	}	

public:
	void SetState(EQueueState state) {
		std::lock_guard<std::mutex> funcMute(m_mutex);
		EQueueState expected = m_state.load(std::memory_order_acquire);
		if (expected == state) return;
		
		// Compare-and-swap operation to atomically update state
		m_state.store(state, std::memory_order_release);
		
		m_cv.notify_all();

	}
	
	// Queuing durumunu almak için yeni metod - testler için
	EQueueState GetState() const {
	    return m_state.load(std::memory_order_acquire);
	}
	
	void StopThreads(bool bClearTasks = false) {
		if (GetState() == EQueueState::THREADS_STOPPED) return;

		// Önce state'i değiştirerek yeni işlerin işlenmesini durdur
		SetState(EQueueState::THREADS_STOPPED);
		
		for (uint8_t i = 0; i < m_workerThreadCount; i++) {
			futureInstance.forceStop(GetTaskName(i));
		}

		std::stringstream ss{};
		ss << QUEUE_CLEANER_BASENAME << this;
		futureInstance.forceStop(ss.str());

		// Kalan işleri temizle
		uint32_t remainingWorkCount = Stop(bClearTasks);
	}

	void AddTask(DataType&& task) {
		std::lock_guard<std::mutex> funcMute(m_mutex);
		{
			std::unique_lock<std::mutex> lock(m_mutexContainer);
			if (m_container.size() >= MAX_QUEUE_SIZE) {
				m_container.pop_front(); // Remove oldest task
			}
			m_container.emplace_back(std::move(task));
		}
		m_cv.notify_one();
	}

	void AddTask(const DataType& task) {
		// For lvalue references, make a move-constructed copy
		AddTask(DataType(task));
	}

	template<typename Iterator>
	void AddBatchTasks(Iterator begin, Iterator end) {
		std::lock_guard<std::mutex> funcMute(m_mutex);
		{
			std::unique_lock<std::mutex> lock(m_mutexContainer);
			for (auto it = begin; it != end; ++it) {
				if (m_container.size() >= MAX_QUEUE_SIZE) {
					m_container.pop_front(); // Remove oldest task
				}
				// Move from the iterator if possible, otherwise copy
				m_container.emplace_back(std::move(*it));
			}
		}
		m_cv.notify_all();
	}
	
	// For backward compatibility
	void AddBatchTasks(std::vector<DataType>& tasks) {
		AddBatchTasks(std::make_move_iterator(tasks.begin()), 
		              std::make_move_iterator(tasks.end()));
	}

	void RemoveTask(std::function<bool(const DataType&)> func, std::function<void(DataType&)> onRemove = nullptr) {
		uint32_t removedCount = 0;
		{
			std::unique_lock<std::mutex> lock(m_mutexContainer);
			auto it = m_container.begin();
			while (it != m_container.end()) {
				if (func(it->m_data)) {
					if (onRemove) {
						onRemove(it->m_data);
					}
					it = m_container.erase(it);
					removedCount++;
				} else {
					++it;
				}
			}
		}
	}
	};
	};
};