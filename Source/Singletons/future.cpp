#include "future.h"
#ifdef ENABLE_LOG_MANAGER
#include "log_manager.h"
#endif
#include <future>
#include <shared_mutex>
#include <string>
#include <functional>
#include <optional>
#include <chrono>
#include <sstream>

CFuture::CFuture() : bShutdowned(false), m_cleanerRunning(false){
	LOG_TRACE(LogClass::NORMAL, "Future system initialized");
}

CFuture::~CFuture() {
	LOG_TRACE(LogClass::NORMAL, "Future system shutting down");
	shutdown();
	m_blockfutures.clear();
}

void CFuture::startBackgroundCleaner() {
	if (!m_cleanerRunning.exchange(true)) {
		LOG_TRACE(LogClass::NORMAL, "Starting background cleaner");
		m_cleanerThread = std::async(std::launch::async, [this]() {
			while(m_cleanerRunning) {
				cleanup();
				std::this_thread::sleep_for(std::chrono::seconds(CLEANER_INTERVAL));
			}
		});
	}
}

void CFuture::stopBackgroundCleaner() {
	if (m_cleanerRunning.exchange(false)) {
		LOG_TRACE(LogClass::NORMAL, "Stopping background cleaner");
		if (m_cleanerThread.valid()) {
			m_cleanerThread.wait();
		}
	}
}

void CFuture::forceStop(std::string _name, bool _bBase) {
	if (_bBase) {
		{
			std::unique_lock<std::shared_mutex> lock(m_blockMutex);
			const size_t blockSizeOld = m_blockfutures.size();
			auto del_begin = std::remove_if(m_blockfutures.begin(), m_blockfutures.end(), [&_name](std::unique_ptr<FutureBase>& task) {
				return task->getName().find(_name) != std::string::npos;
				});
			m_blockfutures.erase(del_begin, m_blockfutures.end());
			const size_t deletedCount = blockSizeOld - m_blockfutures.size();
			if (deletedCount)
				LOG_TRACE(LogClass::NORMAL, "(?) block task stopped with base name (?)", deletedCount, _name);
		}
		{
			std::unique_lock<std::shared_mutex> lock(m_nonBlockMutex);
			const size_t nonblockSizeOld = m_nonblockfutures.size();
			auto del_begin = std::remove_if(m_nonblockfutures.begin(), m_nonblockfutures.end(), [&_name](std::unique_ptr<FutureBase>& task) {
				return task->getName().find(_name) != std::string::npos;
				});
			m_nonblockfutures.erase(del_begin, m_nonblockfutures.end());
			const size_t deletedCount = nonblockSizeOld - m_nonblockfutures.size();
			if (deletedCount)
				LOG_TRACE(LogClass::NORMAL, "(?) non-block task stopped with base name (?)", deletedCount, _name);
		}
	}
	else {
		{
			std::unique_lock<std::shared_mutex> lock(m_blockMutex);
			const size_t blockSizeOld = m_blockfutures.size();
			auto del_begin = std::remove_if(m_blockfutures.begin(), m_blockfutures.end(), [&_name](std::unique_ptr<FutureBase>& task) {
				return task->getName() == _name;
				});
			m_blockfutures.erase(del_begin, m_blockfutures.end());
			const size_t deletedCount = blockSizeOld - m_blockfutures.size();
			if (deletedCount)
				LOG_TRACE(LogClass::NORMAL, "(?) block task stopped (?)", deletedCount, _name);
		}
		{
			std::unique_lock<std::shared_mutex> lock(m_nonBlockMutex);
			const size_t nonblockSizeOld = m_nonblockfutures.size();
			auto del_begin = std::remove_if(m_nonblockfutures.begin(), m_nonblockfutures.end(), [&_name](std::unique_ptr<FutureBase>& task) {
				return task->getName() == _name;
				});
			m_nonblockfutures.erase(del_begin, m_nonblockfutures.end());
			const size_t deletedCount = nonblockSizeOld - m_nonblockfutures.size();
			if (deletedCount)
				LOG_TRACE(LogClass::NORMAL, "(?) non-block task stopped (?)", deletedCount, _name);
		}
	}
}

void CFuture::shutdown() {
	if (bShutdowned) return;
	LOG_TRACE(LogClass::NORMAL, "Beginning shutdown sequence");
	
	// Stop background cleaner
	stopBackgroundCleaner();
	
	// Force stop and clear all non-blocking tasks
	size_t nonBlockingCount = 0;
	{
		std::unique_lock<std::shared_mutex> lock(m_nonBlockMutex);
		nonBlockingCount = m_nonblockfutures.size();
	}
	LOG_TRACE(LogClass::NORMAL, "Stopping ? non-blocking tasks", nonBlockingCount);
	forceStopNonBlockingTasks();
	{
		std::unique_lock<std::shared_mutex> lock(m_nonBlockMutex);
		m_nonblockfutures.clear();
	}


	size_t BlockingCount = 0;
	{
		std::unique_lock<std::shared_mutex> lock(m_blockMutex);
		BlockingCount = m_blockfutures.size();
	}
	
	uint32_t threadCount = 0;
	// Wait for all blocking tasks to complete
	{
		std::unique_lock<std::shared_mutex> lock(m_blockMutex);
		LOG_TRACE(LogClass::NORMAL, "Waiting for ? blocking tasks to complete", BlockingCount);
		
		for (auto& future : m_blockfutures) {
			LOG_TRACE(LogClass::NORMAL, "Waiting for blocking task: ?", future->getName());
			future->forceStop(); // TODO: global shutdownd degiskenini devreye al.
			std::thread th([this, &future](){
				if (!future) return;
				future->waitForCompletion();
				if (future->isReady()) {
					LOG_TRACE(LogClass::NORMAL, "Task ? completed successfully", future->getName());
				}
				else {
					LOG_WARN(LogClass::NORMAL, "Task ? did not complete properly", future->getName());
				}
			});
			threadCount++;
			th.join();
		}
		//m_blockfutures.clear(); // moved into destructor because of singleton lifetime and new thread thing above
	}
	LOG_TRACE(LogClass::NORMAL, "? thread created for complate blocking-tasks", threadCount);
	LOG_TRACE(LogClass::NORMAL, "Shutdown function completed");
	bShutdowned = true;
}

void CFuture::cleanup() {
	size_t nonBlockingRemoved = 0;
	size_t blockingRemoved = 0;
	
	{
		std::unique_lock<std::shared_mutex> lock(m_nonBlockMutex);
		auto originalSize = m_nonblockfutures.size();
		auto it = std::remove_if(m_nonblockfutures.begin(), m_nonblockfutures.end(),
			[this](const std::unique_ptr<FutureBase>& f) {
				if (f->isReady()) {
					LOG_TRACE(LogClass::NORMAL, "Non-blocking task ? completed successfully", f->getName());
					return true;
				}
				return false;
			});
		m_nonblockfutures.erase(it, m_nonblockfutures.end());
		nonBlockingRemoved = originalSize - m_nonblockfutures.size();
	}

	{
		std::unique_lock<std::shared_mutex> lock(m_blockMutex);
		auto originalSize = m_blockfutures.size();
		auto it = std::remove_if(m_blockfutures.begin(), m_blockfutures.end(),
			[this](const std::unique_ptr<FutureBase>& f) {
				if (f->isReady()) {
					LOG_TRACE(LogClass::NORMAL, "Blocking task ? completed successfully", f->getName());
					return true;
				}
				return false;
			});
		m_blockfutures.erase(it, m_blockfutures.end());
		blockingRemoved = originalSize - m_blockfutures.size();
	}

	if (nonBlockingRemoved > 0 || blockingRemoved > 0) {
		LOG_TRACE(LogClass::NORMAL, "Cleaned up ? non-blocking and ? blocking tasks", nonBlockingRemoved, blockingRemoved);
	}
}

bool CFuture::isTaskComplete(std::string name, bool _bBase) {
	if (_bBase) {
		{
			std::shared_lock<std::shared_mutex> lock(m_blockMutex);
			for (const auto& f : m_blockfutures) {
				if (f->getName().find(name) != std::string::npos) {
					if (!f->isReady()) {
						return false;
					}
				}
			}
		}
		{
			std::shared_lock<std::shared_mutex> lock(m_nonBlockMutex);
			for (const auto& f : m_nonblockfutures) {
				if (f->getName().find(name) != std::string::npos) {
					if (!f->isReady()) {
						return false;
					}
				}
			}
		}
	}
	else {
		{
			std::shared_lock<std::shared_mutex> lock(m_nonBlockMutex);
			for (const auto& f : m_nonblockfutures) {
				if (f->getName() == name) {
					return f->isReady();
				}
			}
		}
		{
			std::shared_lock<std::shared_mutex> lock(m_blockMutex);
			for (const auto& f : m_blockfutures) {
				if (f->getName() == name) {
					return f->isReady();
				}
			}
		}
	}
#ifdef DEBUG
	LOG_TRACE(LogClass::NORMAL, "Task ? not found, considering it complete", name);
#endif
	return true;
}

void CFuture::forceStopNonBlockingTask(std::string name) {
	std::unique_lock<std::shared_mutex> lock(m_nonBlockMutex);
	auto it = std::find_if(m_nonblockfutures.begin(), m_nonblockfutures.end(),
		[&name](const std::unique_ptr<FutureBase>& f) { return f->getName() == name; });

	if (it != m_nonblockfutures.end()) {
		LOG_TRACE(LogClass::NORMAL, "Force stopping non-blocking task: ?", name);
		(*it)->forceStop();
	}
	else {
		LOG_WARN(LogClass::NORMAL, "non-blocking task not exist to force stop: ?", name);
	}
}

void CFuture::forceStopBlockingTask(std::string name) {
	std::unique_lock<std::shared_mutex> lock(m_blockMutex);
	auto it = std::find_if(m_blockfutures.begin(), m_blockfutures.end(),
		[&name](const std::unique_ptr<FutureBase>& f) { return f->getName() == name; });
	
	if (it != m_blockfutures.end()) {
		LOG_TRACE(LogClass::NORMAL, "Force stopping blocking task: ?", name);
		(*it)->forceStop();
	} else {
		LOG_WARN(LogClass::NORMAL, "blocking task not exist to force stop: ?", name);
	}
}

void CFuture::forceStopNonBlockingTasks() {
	std::unique_lock<std::shared_mutex> lock(m_nonBlockMutex);
	LOG_TRACE(LogClass::NORMAL, "Force stopping ? non-blocking tasks", m_nonblockfutures.size());
	for (auto& f : m_nonblockfutures) {
		LOG_TRACE(LogClass::NORMAL, "Force stopping non-blocking task: ?", f->getName());
		f->forceStop();
	}
}


