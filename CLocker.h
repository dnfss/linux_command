
#pragma once

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class CSemaphore {
	public:
		CSemaphore() {
			if( sem_init(&m_sem, 0, 0) != 0 ) {
				throw std::exception();
			}
		}

		~CSemaphore() {
			sem_destroy(&m_sem);
		}

		bool Wait() {
			return sem_wait(&m_sem) == 0;
		}

		bool Post() {
			return sem_post(&m_sem);
		}

	private:
		sem_t m_sem;
};

class CMutex {
	public:
		CMutex() {
			if( pthread_mutex_init(&m_mutex, NULL) != 0 ) {
				throw std::exception();
			}
		}

		~CMutex() {
			pthread_mutex_destroy(&m_mutex);
		}

		bool Lock() {
			return pthread_mutex_lock(&m_mutex) == 0;
		}

		bool Unlock() {
			return pthread_mutex_unlock(&m_mutex) == 0;
		}

	private:
		pthread_mutex_t m_mutex;
};

class CCond {
	public:
		CCond() {
			if( pthread_mutex_init(&m_mutex, NULL) != 0 ) {
				throw std::exception();
			}

			if( pthread_cond_init(&m_cond, NULL) != 0 ) {
				pthread_mutex_destroy(&m_mutex);
				throw std::exception();
			}
		}

		~CCond() {
			pthread_mutex_destroy(&m_mutex);
			pthread_cond_destroy(&m_cond);
		}

		bool Wait() {
			pthread_mutex_lock(&m_mutex);
			int ret = pthread_cond_wait(&m_cond, &m_mutex);
			pthread_mutex_unlock(&m_mutex);
			return ret == 0;
		}

		bool Signal() {
			return pthread_cond_signal(&m_cond) == 0;
		}

	private:
		pthread_mutex_t m_mutex;
		pthread_cond_t m_cond;
};
