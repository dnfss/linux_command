
/**
  *
  **/

#pragma once

#include <iostream>
#include <netinet/in.h>
#include <time.h>

static const int BUFFER_SIZE = 64;

struct HeapTimerNode {
	HeapTimerNode(int delay, void (*cbf)(void*), void *data):
			cbFunc(cbf), userData(data) {
		expire = time(NULL) + delay;
	}

	time_t expire;
	void (*cbFunc)(void*);
	void *userData;
};

class CHeapTimer {
	public:
		CHeapTimer(int cap):
			m_capacity(cap), m_curSize(0) {
				m_heap = new HeapTimerNode*[m_capacity];
				for(int i = 0; i < m_capacity; ++i) {
					m_heap[i] = NULL;
				}
		}

		~CHeapTimer() {
			if( m_heap ) {
				for(int i = 0; i < m_curSize; ++i) {
					delete m_heap[i];
				}
				delete[] m_heap;
			}
		}

		bool Empty() const {
			return m_curSize == 0;
		}

		HeapTimerNode* Top() const {
			return Empty()
				? NULL
				: m_heap[0];
		}

		void PopTimer() {
			if( Empty() ) {
				return;
			}

			if( m_heap[0] ) {
				delete m_heap[0];
				m_heap[0] = m_heap[--m_curSize];
				PercolateDown(0);
			}
		}

		void Tick() {
			HeapTimerNode *tmp = m_heap[0];
			time_t cur = time(NULL);
			while( !Empty() ) {
				if( !tmp ) {
					break;
				}

				if( tmp->expire > cur ) {
					break;
				}

				if( tmp->cbFunc ) {
					tmp->cbFunc(tmp->userData);
				}

				PopTimer();
				tmp = m_heap[0];
			}
		}
		
		HeapTimerNode* AddTimer(time_t expire, void (*cbf)(void*), void *data) {
			if( m_curSize >= m_capacity ) {
				Resize();
			}

			int hole = m_curSize++, parent = 0;
			for(;hole > 0; hole = parent) {
				parent = (hole - 1) / 2;
				if( m_heap[parent]->expire <= expire ) {
					break;
				}
				m_heap[hole] = m_heap[parent];
			}
			m_heap[hole] = new HeapTimerNode(expire, cbf, data);
			return m_heap[hole];
		}

		// lazy delete
		void DelTimer(HeapTimerNode *timer) {
			if( timer ) {
				timer->cbFunc = NULL;
			}
		}

	private:
		int Resize() {
			HeapTimerNode **tmp = new HeapTimerNode[2 * m_capacity];
			if( tmp == NULL ) {
				return -1;
			}
			m_capacity *= 2;
			for(int i = 0; i < m_capacity; ++i) {
				tmp[i] = NULL;
			}

			for(int i = 0; i < m_curSize; ++i) {
				tmp[i] = m_heap[i];
			}
			delete[] m_heap;
			m_heap = tmp;
		}

		void PercolateDown(int root) {
			int child = 0;
			HeapTimerNode *tmp = m_heap[root];
			for(; (root * 2 + 1) <= (m_curSize - 1); root = child) {
				child = root * 2 + 1;
				if( child < (m_curSize - 1)
						&& m_heap[child]->expire > m_heap[child + 1]->expire ) {
					child += 1;
				}

				if( m_heap[child]->expire < tmp->expire ) {
					m_heap[root] = m_heap[child];
				}
				else {
					break;
				}
			}
			m_heap[root] = tmp;
		}
			
		int m_capacity;
		int m_curSize;
		HeapTimerNode **m_heap;
};
