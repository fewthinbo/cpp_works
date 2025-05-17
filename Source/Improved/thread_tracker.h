#pragma once

#include <Singletons/future.h>
#include "interfaces.h"
#include "common_types.h"
#include "lock_types.h"

#include <memory>
#include <type_traits>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <vector>
#include <atomic>
#include <mutex>
#include <sstream>

//Data'yi her halukarda asenkron programlama shared_ptr icerisinde tutmak cok onemlidir cunku ayni anda birden fazla thread veri invalid edilirken kullaniyor olabilir.
//En azindan kullanimlari bitene kadar veri, programda yasamalidir.
namespace NThreadSafe {
	namespace NLock{
		template<typename TData, typename std::enable_if<std::is_same_v<TData, std::shared_ptr<typename TData::element_type>>, int>::type = 0>
		class CNewThreadTracker : public INewThreadTracker, public std::enable_shared_from_this<CNewThreadTracker<TData>>{
		public:
			~CNewThreadTracker() override = default;
		private:
			std::mutex m_mutexData;
			std::unordered_map<uintptr_t/*mutexID*/, std::shared_ptr<TLockData<TData>>> m_mutexes;
		
			std::mutex m_mutexHelds;
			std::unordered_map<TID/*threadID*/, std::vector<uintptr_t>/*kilitledigi mutexId vectoru*/> m_heldLocks; // Bu yapi ile her zaman kucukten buyuge lock alinmasi saglanir.

			//IMPORTANT: Bu sinifta once mmutexData sonra mutexHelds mutex'leri kilitlenmistir.
		private:
			void AddToHeldLocks(uintptr_t _mutexID) noexcept {
				if (_mutexID == 0) return;
				const TID& threadID = std::this_thread::get_id();
				std::lock_guard<std::mutex> mute(m_mutexHelds);
				auto found = m_heldLocks.find(threadID);
				if (found == m_heldLocks.end()) {
					// Yeni bir eleman ekleyelim
					auto [iter, success] = m_heldLocks.emplace(threadID, std::vector<uintptr_t>());
					if (success) {
						iter->second.push_back(_mutexID);
					}
				}
				else {
					//Bu thread bu mutexid'yi daha onceden eklemis mi?
					auto itVec = std::find(found->second.begin(), found->second.end(), _mutexID);
					if (itVec == found->second.end()) {
						found->second.push_back(_mutexID);
					}
				}
			}
			void RemoveFromHeldLocks(uintptr_t _mutexID) noexcept override {
				if (_mutexID == 0) return;
				const TID& threadID = std::this_thread::get_id();
				std::lock_guard<std::mutex> mute(m_mutexHelds);
				auto found = m_heldLocks.find(threadID);
				if (found == m_heldLocks.end()) return;

				auto& vec = found->second;
				// Vector'den belirli bir eleman� silmek i�in algoritma kullanmal�y�z
				/*
				vec.erase(std::remove(vec.begin(), vec.end(), _mutexID), vec.end());*/

				//ya da swap&pop

				auto elem = std::find(vec.begin(), vec.end(), _mutexID);
				if (elem != vec.end()) {
					//En sonda degilse en sona tasi.
					if (elem != vec.end() - 1) {
						*elem = std::move(vec.back());
					}
					//sondaki elemani sil.
					vec.pop_back();
				}
			}
			void RemoveFromMutexes(uintptr_t _mutexID) noexcept override {
				std::lock_guard<std::mutex> muteData(m_mutexData);
				auto found = m_mutexes.find(_mutexID);

				if (found == m_mutexes.end()) {
#ifdef LOG_THREAD_SAFE
					LOG_TRACE(LogClass::NORMAL, "Lock doesn't exist to release: mutexID(?).", _mutexID);
#endif
					return;
				}
				m_mutexes.erase(_mutexID);
			}
		private:
			//Kilitler, her thread icin kucukten buyuge dogru -mutexId bazinda- alinmalidir.
			//Thread'in tum mutex'leri serbest birakip dogru sirada almaya ihtiyaci var mi?
			bool NeedToReset(uintptr_t _mutexID) noexcept override {
				if (_mutexID == 0) return false;
				const TID& threadID = std::this_thread::get_id();
				std::lock_guard<std::mutex> mute(m_mutexHelds);
				auto found = m_heldLocks.find(threadID);

				//henuz eklenmemis bile.
				if (found == m_heldLocks.end()) return false;

				//Tuttugu kilitlerden daha buyuk bir id'de kilidimiz varsa 
				auto maxValue = *(std::max_element(found->second.begin(), found->second.end()));

				return maxValue > _mutexID; //Yeni gelen deger en buyk degerden daha kucukse tum kilitler yeniden duzenlenmelidir.
			}

			void ReorderAll() noexcept override {
				const TID& threadID = std::this_thread::get_id();
				//HeldIter'de olup global mutexData'da olmayanlari secer.
				std::vector<uintptr_t> v_garbage{}; //Normalde bu vector daima bos olmalidir ama test icin deneyelim.

				std::lock_guard<std::mutex> muteData(m_mutexData);
				std::lock_guard<std::mutex> muteHeld(m_mutexHelds);
				auto heldIter = m_heldLocks.find(threadID);
				//m_heldLocks 'un bu thread_id'ye sahip oldugundan emin oldugu icin tekrar kontrol etmeye gerek yok.

				//Sadece guard'lari resetleyelim, sayaclar korunsun.
				for (const uintptr_t& mID/*MutexID*/ : heldIter->second) {
					auto iter1 = m_mutexes.find(mID);

					//ilginc bir sekilde bu veri m_mutexes icerisinde yok yani bizim heldlocks'umuz gecersiz bir mutex'e sahip: temizligi dogru yapilmiyor.
					if (iter1 == m_mutexes.end()) {
						v_garbage.push_back(mID);
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "MutexID(?) is garbage.", mID);
#endif
						continue;
					}


					if (!iter1->second.get()) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "MutexDataPtr(id:?) is null.", mID);
#endif
						continue;
					}

					//mutex data
					auto iLock = iter1->second->GetILock();
					if (!iLock) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "Mutex data is not exist in uniqueptr wtf. Line:?", __LINE__);
#endif
						continue;
					}

					iLock->RemoveGuard(); //Bu mutex'e ait tek olan guard'i kaldirir.
				}

				//Copleri temizle
				for (const auto& mID : v_garbage) {
					// Vector'den belirli bir eleman� silmek i�in algoritma kullanmal�y�z
					auto& vec = heldIter->second;

					auto elem = std::find(vec.begin(), vec.end(), mID);
					if (elem != vec.end()) {
						//En sonda degilse en sona tasi.
						if (elem != vec.end() - 1) {
							*elem = std::move(vec.back());
						}
						//sondaki elemani sil.
						vec.pop_back();
					}
					//vec.erase(std::remove(vec.begin(), vec.end(), mID), vec.end());
				}

				//mutexID'leri kucukten buyuge dogru sirala
				std::sort(heldIter->second.begin(), heldIter->second.end());


				//guard'i silinene her mutex'lere ait verileri tekrar ve dogru sirada olusturalim.
				for (const uintptr_t& mID/*MutexID*/ : heldIter->second) {
					//Artik copler olmadigi icin m_mutexes icerisindeki varligini kontrol etmiyorum.
					auto iter2 = m_mutexes.find(mID);
					if (!iter2->second.get()) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "MutexDataPtr(id:?) is null.", mID);
#endif
						continue;
					}
					auto iLock = iter2->second->GetILock();
					if (!iLock) {
#if defined(LOG_THREAD_SAFE)
						LOG_TRACE(LogClass::NORMAL, "Mutex data is not exist in uniqueptr wtf. Line:?", __LINE__);
#endif
						continue;
					}
					//Tekrardan guard olustur ama sayaclara dokunmadan (bu ozel bir islem)..
					iLock->CreateGuard();
				}
			}

			std::shared_ptr<ILock> GetLockData(uintptr_t _mutexID) noexcept {
				if (_mutexID == 0) return nullptr;
				std::lock_guard<std::mutex> muteData(m_mutexData);
				auto found = m_mutexes.find(_mutexID);
				if (found == m_mutexes.end()) return nullptr;

				auto secondPtr = found->second.get();
				if (!secondPtr) return nullptr;
				return secondPtr->GetILock();
			}

		public:
			std::shared_ptr<TLockData<TData>> GetMutexData(uintptr_t _mutexID) noexcept {
				if (_mutexID == 0) return nullptr;
				std::lock_guard<std::mutex> muteData(m_mutexData);
				auto found = m_mutexes.find(_mutexID);
				if (found == m_mutexes.end()) return nullptr;

				return found->second;
			}
		public://test
			void PrintAll() override {
#ifdef LOG_THREAD_SAFE
				{
					std::lock_guard<std::mutex> muteData(m_mutexData);
					for (auto& [mutexID, lockData] : m_mutexes) {
						if (!lockData) continue;
						LOG_INFO(LogClass::NORMAL, "=========================== PRINTING MUTEX DATA FOR MUTEX_ID: ? ===========================", mutexID);
						LOG_INFO(LogClass::NORMAL, "Operation count: ?", lockData->GetOperationCount());
						if (std::shared_ptr<ILock> iLock = lockData->GetILock()) {
							auto resRead = iLock->CanAcquire(ELockType::Read);
							if (resRead == EAcquireResult::AVAIL) {
								LOG_INFO(LogClass::NORMAL, "Can acquire read: AVAIL");
							}
							else if (resRead == EAcquireResult::CANNOT) {
								LOG_INFO(LogClass::NORMAL, "Can acquire read: CANNOT");
							}
							else {
								LOG_INFO(LogClass::NORMAL, "Can acquire read: NEED_CONVERT");
							}


							auto resWrite = iLock->CanAcquire(ELockType::Write);
							if (resWrite == EAcquireResult::AVAIL) {
								LOG_INFO(LogClass::NORMAL, "Can acquire write: AVAIL");
							}
							else if (resWrite == EAcquireResult::CANNOT) {
								LOG_INFO(LogClass::NORMAL, "Can acquire write: CANNOT");
							}
							else {
								LOG_INFO(LogClass::NORMAL, "Can acquire write: NEED_CONVERT");
							}
						}
						LOG_INFO(LogClass::NORMAL, "=========================== END OF PRINT ===========================");
					}
				}

				{
					std::lock_guard<std::mutex> muteHeld(m_mutexHelds);
					for (auto& [threadID, heldLocks] : m_heldLocks) {
						LOG_INFO(LogClass::NORMAL, "=========================== PRINTING HELD_LOCKS FOR THREAD_ID: ? ===========================", threadID);
						for (auto& lockID : heldLocks) {
							LOG_INFO(LogClass::NORMAL, "Held lock ID: ?", lockID);
						}
						LOG_INFO(LogClass::NORMAL, "=========================== END OF PRINT HELD ===========================");
					}
				}
#endif
			}
		private:
			//ilk kez olusturulan mutex'leri kaydeder.
			bool RegisterMutex(std::shared_mutex& _mutex, uintptr_t _mutexID, ELockType _requestType) noexcept {
				if (_mutexID == 0) return false;
				if (_requestType == ELockType::Read) {
					std::lock_guard<std::mutex> muteData(m_mutexData);
					auto [iter, success] = m_mutexes.try_emplace(_mutexID, std::make_shared<TLockData<TData>>(std::make_shared<CReadLock>(_mutexID, _mutex)));
					if (!success) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "Failed to register new mutex(?). Line:?", _mutexID, __LINE__);
#endif
						return false;

					}
					AddToHeldLocks(_mutexID);
					iter->second->GetILock()->AcquireLock(_requestType);
				}
				else if (_requestType == ELockType::Write) {
					std::lock_guard<std::mutex> muteData(m_mutexData);
					auto [iter, success] = m_mutexes.try_emplace(_mutexID, std::make_shared<TLockData<TData>>(std::make_shared<CWriteLock>(_mutexID, _mutex)));

					if (!success) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "An error occured while adding mutex. Line:?", __LINE__);
#endif
						return false;
					}

					AddToHeldLocks(_mutexID);
					iter->second->GetILock()->AcquireLock(_requestType);
				}
				else {
#ifdef LOG_THREAD_SAFE
					LOG_TRACE(LogClass::NORMAL, "request type(?) doesn't exist", _requestType);
#endif
					return false;
				}


				//write olmayan kilitler icin yeniden duzenleme sistemine gerek yok.
				if (_requestType == ELockType::Write && NeedToReset(_mutexID)) {
					//Bu thread'e ait tum locklari yeniden duzenle.
					ReorderAll();
				}

				return true;
			}
		public:
		// ikinci deger false ise hic denemeye gerek yok.
			std::pair<std::shared_ptr<ILock>, bool> 
			TryAcquireLock(std::shared_mutex& _mutex, uintptr_t _mutexID, ELockType _requestType) noexcept override {
				if (_mutexID == 0) return { nullptr, false };
				const TID& threadID = std::this_thread::get_id();

				auto mData = GetLockData(_mutexID);

				//Henuz kilidi alan yok.
				if (!mData) {
					return { nullptr , RegisterMutex(_mutex, _mutexID, _requestType) };
				}

				//Sighandler dogrudan bu fonk icinde kullanilmiyor cunku bu fonksiyon mesgul edilmemelidir. 
				//DataWrapper'a tasinip orada islem gormelidir.
				auto result = mData->CanAcquire(_requestType);

				switch (result)
				{
				case EAcquireResult::CANNOT: {
					return { mData, true };
				}
				case EAcquireResult::NEED_TO_CONVERT: {
					ReleaseLock(_mutexID); //Varolan dataya ait mutex'i serbest birak.
					return { nullptr , RegisterMutex(_mutex, _mutexID, ELockType::Write) }; //yeniden kaydet.
				}
				default:
					break;
				}

				//buradan sonra kilit alinabilir durumdadir.
				AddToHeldLocks(_mutexID);
				mData->AcquireLock(_requestType);

				if (_requestType != ELockType::Write) return { nullptr , true }; // kilit alindi ve write olmayan kilitler icin yeniden duzenleme sistemine gerek yok.

				if (!NeedToReset(_mutexID)) return { nullptr , true }; // siralamaya gerek yok ve kilit alindi.

				//Bu thread'e ait tum locklari yeniden duzenle.
				ReorderAll();

				//Siralama islemleri bitti ve kilit alindi.
				return { nullptr , true };
			}

			void ReleaseLock(uintptr_t _mutexID, bool bOperationCall = false) noexcept {
				if (_mutexID == 0) return;

				if (bOperationCall) {
					RemoveFromMutexes(_mutexID);
					RemoveFromHeldLocks(_mutexID); //thread kayitlarindan da sil
					return;
				}

				std::shared_ptr<TLockData<TData>> mutexData = nullptr;

				{
					std::lock_guard<std::mutex> muteData(m_mutexData);
					auto found = m_mutexes.find(_mutexID);
					if (found == m_mutexes.end()) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "mutexData of mutexID(?) is doesn't exists.", _mutexID);
#endif
						return;
					}
					mutexData = found->second;
				}

				if (!mutexData) return;

				std::shared_ptr<ILock> iLock = mutexData->GetILock();
				if (!iLock) {
#ifdef LOG_THREAD_SAFE
					LOG_TRACE(LogClass::NORMAL, "lockData of mutexID(?) is doesn't exists.", _mutexID);
#endif
					return;
				}

				iLock->RemoveOwnership(); //thread'in sahipligini kaldir.
				bool bShouldRemove = iLock->ShouldRemove();/*kayitlardan tamamen kaldirilmali mi*/

				if (bShouldRemove) {
					//Bekleyen operasyon varsa
					if (mutexData->GetOperationCount() > 0) {
						//Mutex kaldirilacagi icin bekleyen operasyonlari gerceklestir.
						iLock->AddOwnership();
						RunOperationsOfMutex(_mutexID);
						return;
					}
					RemoveFromMutexes(_mutexID);
					RemoveFromHeldLocks(_mutexID); //thread kayitlarindan da sil
				}
			}
		private:
			void RunOperationsOfMutex(uintptr_t _mutexID) {
				std::stringstream ss{};
				ss << "Operations_" << _mutexID;
				futureInstance.addTask<void>(ss.str(), [self = this->shared_from_this(), _mutexID](std::atomic<bool>& bForce) {
					auto mutexInfo = self->GetMutexData(_mutexID);
					if (!mutexInfo) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "OP: MutexInfo doesn't exists. Line: ?.", __LINE__);
#endif
						return;
					}

					auto iLock = mutexInfo->GetILock();

					if (!iLock) {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "OP: LockData ptr is null. Line: ?.", __LINE__);
#endif
						return;
					}

#ifdef LOG_THREAD_SAFE
					size_t opCount = mutexInfo->GetOperationCount();
					LOG_TRACE(LogClass::NORMAL, "? operations are going to process for mutexID: ?", opCount, _mutexID);
#endif
					
					//auto isWriteLock = dynamic_cast<CWriteLock*>(iLock.get());
					auto isWriteLock = iLock->GetType() == ELockType::Write;
					if (!isWriteLock) {
						iLock->RemoveGuard(); //varolan okuma kilidini kaldir, ayni mutex ile yazma kilidi alacagiz.
						std::shared_mutex& _mutex = iLock->GetMutex();
						std::unique_lock<std::shared_mutex> lockMute(_mutex);
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "Operations running for mutex:? with convert mutex", _mutexID);
#endif
						mutexInfo->RunOperations(bForce);
					}
					else {
						//Zaten writelock'a sahip oldugu icin dogrudan operasyonlara gecelim.
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "Operations running for mutex:? normally", _mutexID);
#endif
						mutexInfo->RunOperations(bForce);
					}
					self->ReleaseLock(_mutexID, true);
				});
			}
		public:
			//Datayi yoneten sinif kullanir.
			EAddOperationResult AddOperationWithData(uintptr_t _mutexID, std::function<void(TData)>&& _op, TData _data) {
				std::shared_ptr<TLockData<TData>> mutexData = nullptr;

				{
					std::lock_guard<std::mutex> muteData(m_mutexData);
					auto found = m_mutexes.find(_mutexID);
					if (found == m_mutexes.end())/*eger bulamadiysa o zaman operasyonlar bitmistir. Tekrar kilit almayi dene*/ {
#ifdef LOG_THREAD_SAFE
						LOG_TRACE(LogClass::NORMAL, "There is no mutexData of mutexID(?): to add operation, so lock is available", _mutexID);
#endif
						return EAddOperationResult::LOCK_AVAIL;
					}
					else {
						mutexData = found->second;
					}
				}
				if (!mutexData) return EAddOperationResult::FAILED;
#ifdef LOG_THREAD_SAFE
				LOG_TRACE(LogClass::NORMAL, "Operation addded for mutexID :? ", _mutexID);
#endif
				mutexData->AddOperation(std::move(_op), _data);
				return EAddOperationResult::ADDED;
			}
		};
	};
};

