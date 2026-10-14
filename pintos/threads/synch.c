/* 이 파일은 교육용 운영체제 Nachos의 소스 코드에서 파생되었다.
   아래에는 Nachos의 저작권 고지 원문 전체를 수록한다. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화한다. 세마포어는 0 이상의 정수 값과,
   그 값을 조작하는 두 가지 원자적 연산으로 구성된다.

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 1을 감소시킨다.

   - up 또는 "V": 값을 1 증가시키고, 기다리는 스레드가 있으면 하나를 깨운다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어의 down 또는 "P" 연산이다.
   SEMA의 값이 양수가 될 때까지 기다린 뒤 원자적으로 1을 감소시킨다.

   이 함수는 스레드를 대기 상태로 만들 수 있으므로 인터럽트 핸들러에서
   호출하면 안 된다. 인터럽트가 비활성화된 상태에서 호출할 수 있지만,
   대기하게 되면 다음에 스케줄링된 스레드가 인터럽트를 다시 활성화할 수 있다.
   이것이 sema_down 함수이다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어 값이 0이 아닐 때만 down 또는 "P" 연산을 수행한다.
   값을 감소시켰으면 true, 그렇지 않으면 false를 반환한다.

   인터럽트 핸들러에서도 호출할 수 있다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어의 up 또는 "V" 연산이다. SEMA의 값을 1 증가시키고,
   SEMA를 기다리는 스레드가 있으면 그중 하나를 깨운다.

   인터럽트 핸들러에서도 호출할 수 있다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	struct thread *ht = NULL;
	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
	{
		list_sort(&sema->waiters, priority_higher, NULL);
		ht = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
		thread_unblock (ht);
		
	}
	sema->value++;
	intr_set_level (old_level);

	if ((ht != NULL) && ((thread_current ()->priority) < ht->priority))
		thread_yield();
}

static void sema_test_helper (void *sema_);

/* 두 스레드가 실행 권한을 번갈아 주고받는 세마포어 자체 테스트이다.
   동작 과정을 확인하려면 printf() 호출을 추가한다. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수이다. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화한다. 락은 한 시점에 최대 하나의 스레드만 소유할 수 있다.
   여기서 사용하는 락은 재귀적이지 않다. 즉, 이미 락을 소유한 스레드가
   같은 락을 다시 획득하려고 하면 오류이다.

   락은 초기값이 1인 세마포어를 특수화한 것이다. 둘의 차이는 두 가지이다.
   첫째, 세마포어의 값은 1보다 클 수 있지만 락은 한 번에 하나의 스레드만
   소유할 수 있다. 둘째, 세마포어에는 소유자가 없다. 따라서 한 스레드가
   down 연산을 하고 다른 스레드가 up 연산을 할 수 있다.
   반면 락은 획득한 스레드가 직접 해제해야 한다.
   이런 제약이 부담스럽다면 락 대신 세마포어를 사용하는 편이 적절하다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* LOCK을 획득한다. 필요하면 락을 사용할 수 있을 때까지 대기한다.
   현재 스레드가 이미 이 락을 소유하고 있어서는 안 된다.

   이 함수는 스레드를 대기 상태로 만들 수 있으므로 인터럽트 핸들러에서
   호출하면 안 된다. 인터럽트가 비활성화된 상태에서 호출할 수 있지만,
   대기해야 한다면 다른 스레드의 실행 과정에서 인터럽트가 다시 활성화된다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	if((lock->semaphore.value == 0) && lock->holder != NULL && (lock->holder->priority < thread_current()->priority))
	{
		lock->holder->priority = thread_current()->priority;
	}
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* LOCK 획득을 시도하여 성공하면 true, 실패하면 false를 반환한다.
   현재 스레드가 이미 이 락을 소유하고 있어서는 안 된다.

   이 함수는 스레드를 대기 상태로 만들지 않으므로 인터럽트 핸들러에서도
   호출할 수 있다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 소유한 LOCK을 해제한다. 이것이 lock_release 함수이다.

   인터럽트 핸들러는 락을 획득할 수 없으므로,
   인터럽트 핸들러에서 락 해제를 시도하는 것은 의미가 없다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	thread_current()->priority = thread_current()->origin_priority;
	sema_up (&lock->semaphore);
	
}

/* 현재 스레드가 LOCK을 소유하면 true, 그렇지 않으면 false를 반환한다.
   다른 스레드가 락을 소유하는지 검사하는 것은 경쟁 조건을 일으킬 수 있다. */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 목록에 들어가는 세마포어 하나를 나타낸다. */
struct semaphore_elem {
	struct list_elem elem;              /* 연결 리스트에 등록하기 위한 요소이다. */
	struct semaphore semaphore;         /* 이 요소에 포함된 세마포어이다. */
};

/* 조건 변수 COND를 초기화한다. 조건 변수를 사용하면 한쪽 코드가
   조건에 관한 신호를 보내고, 협력하는 다른 코드가 그 신호를 받아 동작할 수 있다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 해제하고 다른 코드가 COND에 신호를 보낼 때까지 기다리는
   과정을 원자적으로 수행한다. 신호를 받으면 반환하기 전에 LOCK을 다시 획득한다.
   이 함수를 호출하기 전에 LOCK을 소유하고 있어야 한다.

   이 함수의 모니터는 Hoare 방식이 아닌 Mesa 방식이다.
   즉, 신호를 보내는 작업과 받는 작업이 하나의 원자적 연산이 아니다.
   따라서 호출자는 보통 대기가 끝난 뒤 조건을 다시 확인하고,
   필요하면 다시 기다려야 한다.

   하나의 조건 변수는 하나의 락에만 연결되지만,
   하나의 락에는 여러 조건 변수가 연결될 수 있다.
   즉, 락과 조건 변수의 관계는 일대다이다.

   이 함수는 스레드를 대기 상태로 만들 수 있으므로 인터럽트 핸들러에서
   호출하면 안 된다. 인터럽트가 비활성화된 상태에서 호출할 수 있지만,
   대기해야 한다면 다른 스레드의 실행 과정에서 인터럽트가 다시 활성화된다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* a와 b의 semaphore_elem에 대응하는 대기 스레드의 우선순위를 비교한다.
 a의 대기 스레드가 더 높은 우선순위이면 true를 반환한다.
 우선순위가 같거나 낮으면 false를 반환하여 같은 우선순위의 대기 순서를 유지한다. */
static bool cond_priority_higher(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct semaphore_elem *as = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *ab = list_entry(b, struct semaphore_elem, elem);
	struct list_elem *ae = list_front(&as->semaphore.waiters);
	struct list_elem *be = list_front(&ab->semaphore.waiters);
	struct thread *at = list_entry(ae, struct thread, elem);
	struct thread *bt = list_entry(be, struct thread, elem);

	if (at->priority > bt->priority) return true;
	else return false;
}

/* LOCK으로 보호되는 COND를 기다리는 스레드가 있으면,
   그중 하나에 신호를 보내 대기에서 깨어나게 한다.
   이 함수를 호출하기 전에 LOCK을 소유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로,
   인터럽트 핸들러에서 조건 변수에 신호를 보내려는 것은 의미가 없다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));
	struct semaphore *sm = NULL;

	if (!list_empty (&cond->waiters))
	{
		list_sort((&cond->waiters), cond_priority_higher, NULL);
		sm = &list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore;
		sema_up (sm);
	}
}

/* LOCK으로 보호되는 COND를 기다리는 스레드가 있으면 모두 깨운다.
   이 함수를 호출하기 전에 LOCK을 소유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로,
   인터럽트 핸들러에서 조건 변수에 신호를 보내려는 것은 의미가 없다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
