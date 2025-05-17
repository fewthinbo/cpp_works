#pragma once
#include <mutex>

template <class T>
class CSingleton {
private:
	static std::mutex m_singleton_mutex; // singleton mutex
	static T* m_instance;
public:
	static T& getInstance() {	
		if (!m_instance) {
			std::lock_guard<std::mutex> lock(m_singleton_mutex);
			if (!m_instance){
				m_instance = new T();
			}
		}
		return *m_instance;
	}
	CSingleton()=default;
	CSingleton& operator=(CSingleton&& other) = delete;
	CSingleton(CSingleton&& other) = delete;
	CSingleton(const CSingleton&) = delete;
	CSingleton& operator=(const CSingleton&) = delete;
	~CSingleton(){
		deleteInstance();
	};
private:
	static void deleteInstance() {
		if (m_instance) {
			std::lock_guard<std::mutex> lock(m_singleton_mutex);
			if (m_instance) {
				delete m_instance;
				m_instance = nullptr;
			}
		}
	}
};


template <typename T> 
T* CSingleton<T>::m_instance = nullptr;

template <typename T>
std::mutex CSingleton<T>::m_singleton_mutex{};