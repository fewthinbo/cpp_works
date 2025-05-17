#pragma once
#include <cstdint>


//#define I_HAVE_LOG_SYSTEM
//#define LOG_THREAD_SAFE // activate logs

#if defined(LOG_THREAD_SAFE) && defined(I_HAVE_LOG_SYSTEM)
#include <Singletons/log_manager.h>
#endif

namespace NThreadSafe {
	namespace NLock {
		static constexpr const char* PROGRESSER_THREADNAME = "QueueProgresser";
		static constexpr uint16_t LOCK_ACQUIRE_TIMEOUT = 1000; //ms
		static constexpr uint16_t LOG_HELD_MS_LIMIT = 3000;

		enum class ELockType {
			None,
			Read,
			Write,
		};

		enum class EAcquireResult {
			AVAIL, //kilit alinabilir.
			CANNOT,//kilit alinamaz
			NEED_TO_CONVERT,//read kilidi sil, ayni datayla write kilit olustur. Verinin tek sahibi olmayi gerektirir.
		};

		enum class EWrapperResult {
			SUCCESS, //kilit alindi data senin.
			BUSY, // kilit alinamadi ama data valid, queue'ye operasyon eklenebilir.
			DATA_NOT_EXISTS, //Data yok, hicbir islem yapilamaz.
		};

		enum class EAddOperationResult {
			ADDED, 
			FAILED,
			LOCK_AVAIL,
		};

#if defined(LOG_THREAD_SAFE) && !defined(I_HAVE_LOG_SYSTEM)
#define LOG_INFO(type, message, ...)
#define LOG_ERR(type, message, ...)
#define LOG_TRACE(type, message, ...)
#define LOG_WARN(type, message, ...)
#define LOG_FATAL(type, message, ...)
#endif

	}
}