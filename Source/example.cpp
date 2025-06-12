#include <Singletons/future.h> // you can use std::thread or std::async instead of this
#include <Utility/random_generator.h>
#include "improved/data_wrapper.h"
#include "improved/thread_tracker.h"

#include <memory>
#include <functional>
#include <unordered_set>
/*
This is an example program for show all implementations.
*/

using namespace NThreadSafe::NLock;
using namespace NUtility; // for randomgenerator

//Your custom data type which should be thread safe
struct TPerson : public ISafeData {
	int m_age;
	int m_id;
	TPerson(int _age = 0) : m_age(_age), m_id(0) {}

	// Move operations
	TPerson& operator=(TPerson&& other) noexcept = default;
	TPerson(TPerson&& other) noexcept = default;

	//copy : disabled
	TPerson& operator=(const TPerson& other) = delete;
	TPerson(const TPerson& other) = delete;

	~TPerson() = default;

};

static uint32_t s_personID = 1;
using PersonType = std::shared_ptr<TPerson>;
static constexpr const char* ASYNC_THREAD_PREFIX = "AsyncThread_";

//Data manager api
class CPersonManager {
//classic members
	std::unordered_map<int, PersonType> m_person;
	mutable std::mutex m_ContainerMutex;
private:
	std::shared_ptr<CNewThreadTracker<PersonType>> m_threadTracker; // should be in here because of thread tracking
public:
	CPersonManager(size_t dataCount) {
		m_threadTracker = std::make_shared<CNewThreadTracker<PersonType>>(); //init
		m_person.reserve(dataCount);
	}

	std::shared_ptr<CNewThreadTracker<PersonType>> GetThreadTracker() const {
		return m_threadTracker;
	}

	//access function example
	CDataWrapper<PersonType> Access(int personID, ELockType _requestType = ELockType::Read, std::function<void(PersonType)> _ifBusy = nullptr) {
		PersonType per = nullptr;

		{ //THAT SCOPES IMPORTANT FOR WAIT MECHANICSM:
			std::lock_guard<std::mutex> mute(m_ContainerMutex);
			auto found = m_person.find(personID);
			if (found == m_person.end()) {
				LOG_WARN(LogClass::NORMAL, "Access: Person ID ? not found in manager", personID);
				return CDataWrapper<PersonType>(); // Boþ wrapper dönecek, DATA_NOT_EXISTS olacak
			}

			per = found->second;
		}

		if (!per) {
			LOG_WARN(LogClass::NORMAL, "Access: Person ID ? has null pointer", personID);
			return CDataWrapper<PersonType>(); // Boþ wrapper, DATA_NOT_EXISTS olacak
		}

		auto wrapper = CDataWrapper<PersonType>(
			m_threadTracker->shared_from_this(),
			per,
			std::optional<std::reference_wrapper<std::shared_mutex>>(per->m_mutex),
			per->m_mutexID,
			_requestType
		);

		if (wrapper != EWrapperResult::SUCCESS) {
			LOG_WARN(LogClass::NORMAL, "Access: Failed to create wrapper for personID ?, wrapper result is ?, mutexID: ?",
				personID, wrapper.GetResult(), per->mutexID);
		}

		if (wrapper == EWrapperResult::BUSY && _ifBusy != nullptr) {
			//Try to add operation to process when data is available
			auto opRes = m_threadTracker->AddOperationWithData(per->m_mutexID, std::move(_ifBusy), per);
			if (opRes == EAddOperationResult::LOCK_AVAIL)/*lucky*/ {
				//kilidi tekrar al ve yeni wrapper'i dondur.
				auto wrapperUp = CDataWrapper<PersonType>(
					m_threadTracker->shared_from_this(),
					per,
					std::optional<std::reference_wrapper<std::shared_mutex>>(per->m_mutex),
					per->m_mutexID,
					_requestType
				);
				return wrapperUp;
			}

			if (opRes == EAddOperationResult::FAILED) {
				LOG_FATAL(LogClass::NORMAL, "AddOperation failed directly: personID ?, wrapper result is ?, mutexID: ?",
					personID, wrapper.GetResult(), per->mutexID);
			}
		}

		return wrapper;
	}

	void Add(int _id) {
		std::lock_guard<std::mutex> mute(m_ContainerMutex);
		m_person.try_emplace(_id, std::make_shared<TPerson>(_id));
	}
};

int main(void) {
	auto dataCount = 100; //veri sayisi az olsun ki cakismalar artsin.
	CPersonManager manager(dataCount);

	for (int i = 0; i < dataCount; ++i) {
		manager.Add(randomInstance.generate_number(1, 90));
	}

	auto accesser = [&](bool sequential = false, int count = 1) -> std::vector<CDataWrapper<PersonType>> {
		std::vector<CDataWrapper<PersonType>> results;
		results.reserve(count); // Rezerve et, böylece yer deðiþtirmeler olmaz
		std::unordered_set<int> accessedIds; // Ayný veriye tekrar eriþimi önlemek için

		// ID'yi 1'den baþlatmak yerine 1 ila s_personID-1 arasýnda olmalý
		// çünkü s_personID bir sonraki eklenecek ID'yi iþaret ediyor
		const int maxValidId = dataCount;

		for (int i = 0; i < count; ++i) {
			int rndPersonID = 0;

			if (sequential) {
				// Sýralý eriþim - her seferinde farklý bir ID'ye eriþim saðlar
				do {
					rndPersonID = randomInstance.generate_number(1, maxValidId);
				} while (accessedIds.find(rndPersonID) != accessedIds.end());

				accessedIds.insert(rndPersonID);
			}
			else {
				// Tamamen rastgele eriþim - geçerli ID aralýðýný kullan
				rndPersonID = randomInstance.generate_number(1, maxValidId);
			}

			auto rndReqType = randomInstance.generate_number(1, 2);
			auto wrapper = manager.Access(rndPersonID, rndReqType == 1 ? ELockType::Read : ELockType::Write,
				[](PersonType _person) {
					if (!_person) return;

					_person->m_id++;
				}
			);

			// Doðrudan move yerine durumu kontrol edelim
			if (wrapper == EWrapperResult::DATA_NOT_EXISTS) {
				// Burada ID'nin geçerli olup olmadýðýný kontrol edelim
				LOG_WARN(LogClass::NORMAL, "Access failed for ID:?, type:?, maxValidId:?", rndPersonID, rndReqType, maxValidId);
				// Yeni bir ID deneyelim - maksimum 3 deneme yapalým
				for (int retry = 0; retry < 3 && wrapper == EWrapperResult::DATA_NOT_EXISTS; retry++) {
					rndPersonID = randomInstance.generate_number(1, maxValidId);
					wrapper = manager.Access(rndPersonID, rndReqType == 1 ? ELockType::Read : ELockType::Write,
						[](PersonType _person) {
							if (!_person) return;

							_person->m_id++;
							LOG_INFO(LogClass::NORMAL, "person id is ?", _person->m_id);
						}
					);
				}
			}

			// Hala baþarýsýzsa bile ekleyelim, error durumu kontrol edilecek
			results.push_back(std::move(wrapper));
		}

		return results;
		};

	for (int i = 0; i < 5; ++i) {
		std::stringstream ss{};
		ss << ASYNC_THREAD_PREFIX << i;
		futureInstance.addTask<void>(ss.str(), [&, i](std::atomic<bool>& bForce) {
			int successCount = 0;
			int busyCount = 0;

			while (!bForce) {
				//baslangicta ayni threadler ayni elemana erismis olsunlar.

				if (i % 2 == 0) {
					auto wr1 = manager.Access(1, ELockType::Read,
						[i](PersonType _person) {
							if (!_person) return;

							_person->m_id++;
							LOG_INFO(LogClass::NORMAL, "Called from thread ?, person name is ?", i, _person->name.c_str());
						}
					);
					if (wr1 == EWrapperResult::SUCCESS) {
						successCount++;
					}
					else if (wr1 == EWrapperResult::BUSY) {
						busyCount++;
					}

					auto wr2 = manager.Access(2, ELockType::Read,
						[i](PersonType _person) {
							if (!_person) return;

							_person->m_id++;
							LOG_INFO(LogClass::NORMAL, "Called from thread ?, person id is ?", i, _person->m_id);
						}
					);
					if (wr2 == EWrapperResult::SUCCESS) {
						successCount++;
					}
					else if (wr2 == EWrapperResult::BUSY) {
						busyCount++;
					}


					for (int m = 0; m < 10; ++m) {
						auto wrtmp = manager.Access(2, ELockType::Read,
							[i](PersonType _person) {
								if (!_person) return;

								_person->m_id++;
								LOG_INFO(LogClass::NORMAL, "Called from thread ?, person id is ?", i, _person->m_id);
							}
						);

						if (wrtmp == EWrapperResult::SUCCESS) {
							successCount++;
						}
						else if (wrtmp == EWrapperResult::BUSY) {
							busyCount++;
						}

					}
				}
				else {
					auto wr1 = manager.Access(2, ELockType::Read,
						[i](PersonType _person) {
							if (!_person) return;

							_person->m_id++;
							LOG_INFO(LogClass::NORMAL, "Called from thread ?, person id is ?", i, _person->m_id);
						}
					);
					if (wr1 == EWrapperResult::SUCCESS) {
						successCount++;
					}
					else if (wr1 == EWrapperResult::BUSY) {
						busyCount++;
					}
					auto wr2 = manager.Access(1, ELockType::Read,
						[i](PersonType _person) {
							if (!_person) return;

							_person->m_id++;
							LOG_INFO(LogClass::NORMAL, "Called from thread ?, person id is ?", i, _person->m_id);
						}
					);
					if (wr2 == EWrapperResult::SUCCESS) {
						successCount++;
					}
					else if (wr2 == EWrapperResult::BUSY) {
						busyCount++;
					}
					auto wr3 = manager.Access(2, ELockType::Read,
						[i](PersonType _person) {
							if (!_person) return;

							_person->m_id++;
							LOG_INFO(LogClass::NORMAL, "Called from thread ?, person id is ?", i, _person->m_id);
						}
					);
					if (wr3 == EWrapperResult::SUCCESS) {
						successCount++;
					}
					else if (wr3 == EWrapperResult::BUSY) {
						busyCount++;
					}

					for (int m = 0; m < 10; ++m) {
						auto wrtmp = manager.Access(1, ELockType::Read,
							[i](PersonType _person) {
								if (!_person) return;

								_person->m_id++;
								LOG_INFO(LogClass::NORMAL, "Called from thread ?, person id is ?", i, _person->m_id);
							}
						);
						if (wrtmp == EWrapperResult::SUCCESS) {
							successCount++;
						}
						else if (wrtmp == EWrapperResult::BUSY) {
							busyCount++;
						}
					}
				}


				// Her 10 iþlemde bir, ayný thread peþ peþe 3 farklý veriye eriþmeyi dener
				if (randomInstance.generate_number(1, 10) == 1) {
					auto wrappers = accesser(true, 3); // 3 farklý veriye sýralý eriþim

					// En az bir wrapper'ýn baþarýlý olmasýný bekleriz
					bool atLeastOneSuccess = false;
					for (auto& wrapper : wrappers) {
						if (wrapper == EWrapperResult::SUCCESS) {
							atLeastOneSuccess = true;

							// Ýçeriði deðiþtirme - yalnýzca baþarýlý eriþimde
							auto* person = wrapper.get();
							if (person) {
								person->m_age++;
							}

							successCount++;
						}
						else if (wrapper == EWrapperResult::BUSY) {
							busyCount++;
						}
					}

					// Kýsa bir süre bekleyerek diðer thread'lere þans verelim
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				else {
					// Normal tekli eriþim
					auto vec = accesser();
					auto& wrapper = vec[0];


					if (wrapper == EWrapperResult::DATA_NOT_EXISTS) {
						LOG_WARN(LogClass::NORMAL, "Data not exist for ID, printing info");

						// Sayma iþlemi ekleyelim
						static std::atomic<int> data_not_exist_count = 0;
						int curr_count = ++data_not_exist_count;
						if (curr_count % 10 == 0) {
							LOG_WARN(LogClass::NORMAL, "Total data not exist count: ?", curr_count);

							// Veritabaný durumunu yazdýr
							int total = 0;
							LOG_WARN(LogClass::NORMAL, "Total persons in database: ?", total);
						}
					}

					if (wrapper == EWrapperResult::SUCCESS) {
						auto* person = wrapper.get();
						if (person) {
							person->m_age++;
						}
						successCount++;
					}
					else if (wrapper == EWrapperResult::BUSY) {
						busyCount++;
					}
				}

				LOG_ERR(LogClass::NORMAL, "Thread ?, success: ?, busy: ?, success rate: ?", i, successCount, busyCount, (successCount * 100.0 / (successCount + busyCount)));
			}
			}, false);
	}

	//saniye boyunca asenkron islemler devam etsin.
	std::this_thread::sleep_for(std::chrono::seconds(20));

	//Hepsini durdur.
	futureInstance.forceStop(ASYNC_THREAD_PREFIX, true);

	std::this_thread::sleep_for(std::chrono::seconds(3));

	//thread tracker'daki tum verileri yaz.
	manager.GetThreadTracker()->PrintAll();
}