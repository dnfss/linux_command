
/**
  * Note
  *	 1. an efficitive timer
  *	 2. understand the meaning of rotation and how to calculate the the position of slot 
  **/

#pragma once

#include <time.h>
#include <netinet/in.h>
#include <stdio.h>

const int N = 60;
const int SI = 1;

struct WheelTimerNode {
	WheelTimerNode(int rot, int ts, void (*func)(void*), void *data):
		next(NULL), prev(NULL), rotation(rot), timeSlot(ts), cb_func(func), userData(data) {}

	int rotation;
	int timeSlot;
	WheelTimerNode *next;
	WheelTimerNode *prev;
	void *userData;
	void (*cb_func)(void *data);
};

class CWheelTimer {
	public:
		CWheelTimer(): m_curSlot(0) {
			for(int i = 0; i < N; ++i) {
				m_slots[i] = NULL;
			}
		}	

		~CWheelTimer() {
			for(int i = 0; i < N; ++i) {
				WheelTimerNode *tmp = m_slots[i];
				while( tmp ) {
					m_slots[i] = tmp->next;
					delete tmp;
					tmp = m_slots[i];
				}
			}
		}

		WheelTimerNode* AddTimer(int timeout, void (*func)(void*), void *data) {
			if( timeout <= 0 ) {
				return NULL;
			}

			int ticks = 0;
			if( timeout < SI ) {
				ticks = 1;
			}
			else {
				ticks = timeout / SI;
			}

			int rotation = ticks / N;
			int ts = (m_curSlot + (ticks % N)) % N;	// There must be an offset
			WheelTimerNode *timer = new WheelTimerNode(rotation, ts, func, data);

			if( !m_slots[ts] ) {
				m_slots[ts] = timer;
			}
			else {
				timer->next = m_slots[ts];
				m_slots[ts]->prev = timer;
				m_slots[ts] = timer;
			}

			return timer;
		}

		WheelTimerNode* DelayTimer(WheelTimerNode *timer, int delaySeconds) {
			if( !timer || delaySeconds <= 0 ) {
				return NULL;
			}

			int remain = timer->rotation * N + (timer->timeSlot - m_curSlot + N) % N;
			void (*func)(void*) = timer->cb_func;
			void *userData = timer->userData;
			printf("in DelayTimer fd[%d]\n", *(int*)userData);

			DelTimer(timer);
			return AddTimer(remain + delaySeconds, func, userData);
		}
		
		void DelTimer(WheelTimerNode *timer) {
			if( !timer ) {
				return;
			}

			int ts = timer->timeSlot;
			if( timer == m_slots[ts] ) {
				m_slots[ts] = m_slots[ts]->next;
				if( m_slots[ts] ) {
					m_slots[ts]->prev = NULL;
				}
				delete timer;
			}
			else {
				timer->prev->next = timer->next;
				if( timer->next ) {
					timer->next->prev = timer->prev;
				}
				delete timer;
			}
		}

		void Tick() {
			WheelTimerNode *tmp = m_slots[m_curSlot];
			while( tmp ) {
				if( tmp->rotation > 0 ) {
					--(tmp->rotation);
					tmp = tmp->next;
				}
				else {
					tmp->cb_func(tmp->userData);
					WheelTimerNode *tmp2 = tmp->next;
					DelTimer(tmp);
					tmp = tmp2;
				}
			}

			m_curSlot = (++m_curSlot) % N;
		}

	private:
		int m_curSlot;
		WheelTimerNode *m_slots[N];
};

