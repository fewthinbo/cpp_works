#pragma once

//#define ENABLE_LOG_MANAGER //You should disable it

#ifdef ENABLE_LOG_MANAGER
	#include "log_manager.h"
#else
	#define LOG_INFO(type, message, ...)
	#define LOG_ERR(type, message, ...)
	#define LOG_TRACE(type, message, ...)
	#define LOG_WARN(type, message, ...)
	#define LOG_FATAL(type, message, ...)
#endif

#include <singleton.h>
#include <future>
#include <shared_mutex>
#include <vector>
#include <functional>
#include <optional>
#include <atomic>
#include <memory>
#include <typeindex>

class CFuture : public CSingleton<CFuture> {
	static constexpr int MAX_WAIT_TIME = 300;
	static constexpr int CLEANER_INTERVAL = 5;

	std::atomic<bool> bShutdowned;

	// Base class for type erasure
	struct FutureBase {
		virtual ~FutureBase() = default;
		virtual bool isReady() const = 0;
		virtual bool waitWithTimeout() const = 0;
		virtual void waitForCompletion() = 0;
		virtual void forceStop() = 0;
		virtual std::string getName() const = 0;
		virtual std::type_index getType() const = 0;
	};

	template <typename ReturnType>
	struct TFuture : public FutureBase {
		std::atomic<bool> m_bForce;
		std::string m_name;
		std::shared_future<ReturnType> m_future;
		std::function<ReturnType(std::atomic<bool>&)> m_func;

		TFuture(std::string name, std::function<ReturnType(std::atomic<bool>&)> func) 
			: m_bForce(false), m_name(std::move(name)), m_func(std::move(func)) {
			m_future = std::async(std::launch::async, [this]() -> ReturnType {
				return m_func(m_bForce);
			}).share();
		}

		~TFuture() {
			m_bForce.store(true, std::memory_order_release);
		}

		bool isReady() const override {
			if (!m_future.valid()) return false;
			return m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}

		bool waitWithTimeout() const override {
			if (!m_future.valid()) return false;
			return m_future.wait_until(std::chrono::system_clock::now() + std::chrono::seconds(MAX_WAIT_TIME)) == std::future_status::ready;
		}

		void waitForCompletion() override {
			if (m_future.valid()) {
				m_future.wait();
			}
		}

		void forceStop() override {
			m_bForce = true;
		}

		std::string getName() const override {
			return m_name;
		}

		std::type_index getType() const override {
			return std::type_index(typeid(ReturnType));
		}

		const std::shared_future<ReturnType>& getSharedFuture() const {
			return m_future;
		}
	};

	std::shared_mutex m_nonBlockMutex;
	std::shared_mutex m_blockMutex;
	std::atomic<bool> m_cleanerRunning{false};
	std::future<void> m_cleanerThread;

	std::vector<std::unique_ptr<FutureBase>> m_nonblockfutures;
	std::vector<std::unique_ptr<FutureBase>> m_blockfutures;

public:
	CFuture();
	~CFuture();
	
	// Core functionality
	void startBackgroundCleaner();
	void stopBackgroundCleaner();
	void cleanup();
	void shutdown();

	// Future management
	template <typename ReturnType>
	void addTask(const std::string& name, std::function<ReturnType(std::atomic<bool>&)> func, bool isBlocking = true) {
		auto& futures = isBlocking ? m_blockfutures : m_nonblockfutures;
		auto& mutex = isBlocking ? m_blockMutex : m_nonBlockMutex;

		std::unique_lock<std::shared_mutex> lock(mutex);
		auto it = std::find_if(futures.begin(), futures.end(),
			[&name](const std::unique_ptr<FutureBase>& f) { return f->getName() == name; });
		
		if (it == futures.end()) {
			auto future = std::make_unique<TFuture<ReturnType>>(std::string(name), std::move(func));
			futures.push_back(std::move(future));
			LOG_TRACE(LogClass::NORMAL, "New task added to: ?", name);
		}
	}

	// Force operations
	void forceStopNonBlockingTask(std::string name);
	void forceStopBlockingTask(std::string name);
	void forceStopNonBlockingTasks();

	void forceStop(std::string _name, bool _bBase = false);

	// Status checks: base isimler icin herhangi bir tanesi tamamlanmamis ise false doner.
	bool isTaskComplete(std::string name, bool _bBase = false);

	template<typename ReturnType>
	bool getTaskFuture(std::string name, std::shared_future<ReturnType>& future) {
		{
			std::shared_lock<std::shared_mutex> lock(m_nonBlockMutex);
			for (const auto& f : m_nonblockfutures) {
				if (f->getName() == name && f->getType() == std::type_index(typeid(ReturnType))) {
					auto* typedFuture = static_cast<TFuture<ReturnType>*>(f.get());
					future = typedFuture->getSharedFuture();
					return true;
				}
			}
		}
		{
			std::shared_lock<std::shared_mutex> lock(m_blockMutex);
			for (const auto& f : m_blockfutures) {
				if (f->getName() == name && f->getType() == std::type_index(typeid(ReturnType))) {
					auto* typedFuture = static_cast<TFuture<ReturnType>*>(f.get());
					future = typedFuture->getSharedFuture();
					return true;
				}
			}
		}
		return false;
	}

	template<typename ReturnType>
	std::optional<ReturnType> getTaskResult(std::string name) {
		std::shared_future<ReturnType> future;
		if (getTaskFuture<ReturnType>(name, future)) {
			if (future.valid()) {
				return future.get();
			}
		}
		return std::nullopt;
	}
};
#define futureInstance CFuture::getInstance()
