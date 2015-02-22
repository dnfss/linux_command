
/**
  * Note
  * 	1. demonstrate how to create threads in c++ class
  */

#pragma

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include <CLocker.h>

template<typename T>
class CThreadPool {
	public:
		CThreadPool(int threadCnt = 8, int maxReqCnt = 10240):
				m_threadCnt(threadCnt), m_maxReqCnt(maxReqCnt), m_stop(false), m_threads(NULL) {
			if( m_threadCnt <= 0 || m_maxReqCnt <= 0 ) {
				throw std::exception();
			}

			m_threads = new pthread_t[m_threadCnt];
			if( !m_threads ) {
				throw std::exception();
			}

			for(int i = 0; i < m_threadCnt; ++i) {
				// Note: the routine is static, the argument is "this"
				if( pthread_create(m_threads + i, NULL, worker, this) != 0
						|| pthread_detach(m_threads[i]) != 0 ) {
					delete[] m_threads;
					throw std::exception();
				}
			}
		}

		~CThreadPool() {
			m_stop = true;
			delete[] m_threads;
		}

		bool Append(T *request) {
			m_queueLocker.Lock();
			if( m_workqueue.size() > m_maxReqCnt ) {
				m_queueLocker.Unlock();
				return false;
			}

			m_workqueue.push_back(request);
			m_queueLocker.Unlock();
			m_queueStat.Post();
			return true;
		}

	private:
		static void* Worker(void *arg) {
			CThreadPool *pool = static_cast<CThreadPool*>(arg);
			pool->Run();
			return pool;
		}

		void Run() {
			while( !m_stop ) {
				m_queueStat.Wait();
				m_queueLocker.Lock();
				if( m_workqueue.empty() ) {
					m_queueLocker.Unlock();
					continue;
				}

				T* req = m_workqueue.front();
				m_workqueue.pop_front();
				m_queueLocker.Unlock();

				if( !req ) {
					continue;
				}

				req->Process();
			}
		}

		int m_threadCnt;
		int m_maxReqCnt;

		pthread_t *m_threads;
		std::list<T*> m_workqueue;

		CMutex m_queueLocker;
		CSemaphore m_queueStat;

		bool m_stop;
};
		
