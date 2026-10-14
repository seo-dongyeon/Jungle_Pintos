#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* struct thread의 magic 멤버에 사용하는 임의의 값이다.
   스택 오버플로를 감지하는 데 사용한다. 자세한 내용은
   thread.h 상단의 긴 주석을 참고한다. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드에 사용하는 임의의 값이다.
   이 값은 변경하지 않는다. */
#define THREAD_BASIC 0xd42df210

/* THREAD_READY 상태인 스레드들의 목록이다.
   실행할 준비는 되었지만 현재 실행 중이지 않은 스레드가 들어간다. */
static struct list ready_list;

/* 유휴 스레드이다. */
static struct thread *idle_thread;

/* 초기 스레드로, init.c의 main()을 실행하는 스레드이다. */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 락이다. */
static struct lock tid_lock;

/* 스레드 제거 요청 목록이다. */
static struct list destruction_req;

/* 통계 정보이다. */
static long long idle_ticks;    /* 유휴 상태로 보낸 타이머 틱 수이다. */
static long long kernel_ticks;  /* 커널 스레드에서 보낸 타이머 틱 수이다. */
static long long user_ticks;    /* 사용자 프로그램에서 보낸 타이머 틱 수이다. */

/* 스케줄링 정보이다. */
#define TIME_SLICE 4            /* 각 스레드에 할당할 타이머 틱 수이다. */
static unsigned thread_ticks;   /* 마지막 CPU 양보 이후 경과한 타이머 틱 수이다. */

/* false(기본값)이면 라운드 로빈 스케줄러를 사용한다.
   true이면 다단계 피드백 큐 스케줄러를 사용한다.
   커널 명령행 옵션 "-o mlfqs"로 설정한다. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 스레드를 가리키는 것으로 보이면 true를 반환한다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 현재 실행 중인 스레드를 반환한다.
 * CPU의 스택 포인터 rsp를 읽고, 해당 주소를 페이지 시작 주소로 내림한다.
 * struct thread는 항상 페이지 시작 부분에 있고 스택 포인터는
 * 그 페이지 내부에 있으므로, 이 방식으로 현재 스레드를 찾는다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// thread_start에서 사용할 전역 디스크립터 테이블(GDT)이다.
// GDT는 thread_init 이후에 설정되므로,
// 먼저 임시 GDT를 설정해야 한다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

// a와 b의 우선순위를 비교해서 a가 높거나 같으면 true, a가 낮으면 false 반환 
bool priority_higher(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct thread *at = list_entry(a, struct thread, elem);
	struct thread *bt = list_entry(b, struct thread, elem);

	if (at->priority > bt->priority) return true;
	else return false;
}


/* 현재 실행 중인 코드를 하나의 스레드로 구성하여 스레드 시스템을 초기화한다.
   일반적으로는 이런 변환이 가능하지 않다. 여기서는 loader.S가
   스택의 바닥을 페이지 경계에 배치했기 때문에 가능하다.

   실행 대기 큐와 tid 할당용 락도 초기화한다.

   이 함수 호출 후 thread_create()로 스레드를 생성하기 전에
   반드시 페이지 할당기를 초기화해야 한다.

   이 함수가 끝나기 전에는 thread_current()를 호출하면 안전하지 않다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* 커널용 임시 GDT를 다시 로드한다.
	 * 이 GDT에는 사용자 컨텍스트가 포함되지 않는다.
	 * 커널은 gdt_init()에서 사용자 컨텍스트를 포함하여 GDT를 다시 구성한다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 전역 스레드 컨텍스트를 초기화한다. */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	/* 현재 실행 중인 스레드의 구조체를 설정한다. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작한다.
   유휴 스레드도 생성한다. */
void
thread_start (void) {
	/* 유휴 스레드를 생성한다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* 선점형 스레드 스케줄링을 시작한다. */
	intr_enable ();

	/* 유휴 스레드가 idle_thread를 초기화할 때까지 기다린다. */
	sema_down (&idle_started);
}

/* 매 타이머 틱마다 타이머 인터럽트 핸들러가 호출한다.
   따라서 이 함수는 외부 인터럽트 처리 중에 실행된다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* 통계를 갱신한다. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 할당된 실행 시간이 끝나면 선점을 요청한다. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* 스레드 통계를 출력한다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 이름이 NAME이고 초기 우선순위가 PRIORITY인 새 커널 스레드를 생성한다.
   새 스레드는 AUX를 인자로 전달하여 FUNCTION을 실행하며, 준비 큐에 등록된다.
   생성한 스레드의 식별자를 반환하고, 생성에 실패하면 TID_ERROR를 반환한다.

   thread_start()가 호출된 상태라면 thread_create()가 반환하기 전에
   새 스레드가 스케줄링될 수 있다. 심지어 반환 전에 종료될 수도 있다.
   반대로 새 스레드가 스케줄링되기 전까지 기존 스레드가 얼마든지
   실행될 수도 있다. 실행 순서를 보장해야 한다면 세마포어 등의
   동기화 수단을 사용한다.

   제공된 코드는 새 스레드의 priority 멤버에 PRIORITY를 설정하지만,
   실제 우선순위 스케줄링은 구현되어 있지 않다.
   우선순위 스케줄링 구현은 문제 1-3의 목표이다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* 스레드용 메모리를 할당한다. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드를 초기화한다. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* 스레드가 스케줄링되면 kernel_thread를 호출하도록 설정한다.
	 * 참고: rdi는 첫 번째 인자, rsi는 두 번째 인자를 전달한다. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 대기 큐에 추가한다. */
	thread_unblock (t);

	if ((t->priority) > thread_current()->priority)
		thread_yield();

	return tid;
}

/* 현재 스레드를 대기 상태로 전환한다.
   thread_unblock()으로 깨어날 때까지 다시 스케줄링되지 않는다.

   인터럽트가 비활성화된 상태에서 호출해야 한다.
   일반적으로는 synch.h의 동기화 프리미티브를 사용하는 편이 더 적절하다. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* 대기 상태인 스레드 T를 실행 준비 상태로 전환한다.
   T가 대기 상태가 아니라면 오류이다. 실행 중인 스레드를 준비 상태로
   전환하려면 thread_yield()를 사용한다.

   이 함수는 실행 중인 스레드를 선점하지 않는다. 호출자가 직접
   인터럽트를 비활성화했다면, 스레드를 깨우고 다른 데이터를 갱신하는
   작업을 원자적으로 수행할 수 있다고 기대할 수 있으므로 중요하다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered (&ready_list, &t->elem, priority_higher, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* 현재 실행 중인 스레드의 이름을 반환한다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 현재 실행 중인 스레드를 반환한다.
   running_thread()의 결과에 몇 가지 유효성 검사를 추가한다.
   자세한 내용은 thread.h 상단의 긴 주석을 참고한다. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* T가 실제 스레드인지 확인한다.
	   두 검증 중 하나라도 실패하면 스레드의 스택이 넘쳤을 수 있다.
	   각 스레드의 스택 크기는 4 kB보다 작으므로, 큰 지역 배열 몇 개나
	   어느 정도 깊이의 재귀 호출만으로도 스택 오버플로가 발생할 수 있다. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 현재 실행 중인 스레드의 tid를 반환한다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* 현재 스레드를 스케줄링 대상에서 제외하고 제거한다.
   호출한 곳으로 돌아가지 않는다. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* 현재 상태를 종료 중으로 바꾸고 다른 스레드를 스케줄링한다.
	   현재 스레드는 schedule_tail() 호출 중에 제거된다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* CPU를 양보한다. 현재 스레드를 대기 상태로 만들지는 않으며,
   스케줄러의 선택에 따라 곧바로 다시 스케줄링될 수도 있다. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered (&ready_list, &curr->elem, priority_higher, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정한다. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
	if (list_empty(&ready_list)) return;

	struct list_elem *h_elem = list_front(&ready_list);
	struct thread *ht = list_entry(h_elem, struct thread, elem);
	if (!list_empty(&ready_list) && ((thread_current ()->priority) < ht->priority))
		thread_yield();
}

/* 현재 스레드의 우선순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* 현재 스레드의 nice 값을 NICE로 설정한다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: 여기에 구현을 작성한다. */
}

/* 현재 스레드의 nice 값을 반환한다. */
int
thread_get_nice (void) {
	/* TODO: 여기에 구현을 작성한다. */
	return 0;
}

/* 시스템 평균 부하(load average)의 100배를 반환한다. */
int
thread_get_load_avg (void) {
	/* TODO: 여기에 구현을 작성한다. */
	return 0;
}

/* 현재 스레드의 recent_cpu 값의 100배를 반환한다. */
int
thread_get_recent_cpu (void) {
	/* TODO: 여기에 구현을 작성한다. */
	return 0;
}

/* 유휴 스레드이다. 실행 준비가 된 다른 스레드가 없을 때 실행된다.

   처음에는 thread_start()가 유휴 스레드를 준비 목록에 넣는다.
   최초로 스케줄링되면 idle_thread를 초기화하고, 전달받은 세마포어에
   up 연산을 수행하여 thread_start()가 계속 실행되도록 한 뒤 즉시 대기한다.
   이후에는 유휴 스레드가 준비 목록에 들어가지 않는다.
   준비 목록이 비어 있을 때 next_thread_to_run()이 특별히 이 스레드를 반환한다. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* 다른 스레드가 실행될 수 있도록 대기한다. */
		intr_disable ();
		thread_block ();

		/* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다린다.

		   sti 명령은 다음 명령이 끝날 때까지 인터럽트 처리를 지연시키므로,
		   이 두 명령은 원자적으로 실행된다. 이 원자성이 중요하다.
		   그렇지 않으면 인터럽트를 다시 활성화한 뒤 대기를 시작하기 전에
		   인터럽트가 처리되어, 최대 한 타이머 틱만큼 시간을 낭비할 수 있다.

		   [IA32-v2a]의 "HLT", [IA32-v2b]의 "STI",
		   [IA32-v3a] 7.11.1의 "HLT Instruction"을 참고한다. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드의 실행 기반이 되는 함수이다. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* 스케줄러는 인터럽트를 비활성화한 상태로 실행된다. */
	function (aux);       /* 스레드 함수를 실행한다. */
	thread_exit ();       /* function()이 반환하면 스레드를 종료한다. */
}


/* T를 이름이 NAME인 대기 상태의 스레드로 기본 초기화한다. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* 다음에 스케줄링할 스레드를 선택하여 반환한다.
   실행 대기 큐가 비어 있지 않으면 그 큐에서 스레드를 반환해야 한다.
   현재 스레드가 계속 실행할 수 있다면 그 스레드도 실행 대기 큐에 들어간다.
   실행 대기 큐가 비어 있으면 idle_thread를 반환한다. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* iretq 명령으로 스레드 실행을 시작한다. */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* 새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고,
   이전 스레드가 종료 중이라면 제거한다.

   이 함수가 호출되는 시점에는 PREV 스레드에서 막 전환한 상태이며,
   새 스레드가 이미 실행 중이고 인터럽트는 여전히 비활성화되어 있다.

   스레드 전환이 끝나기 전에는 printf()를 호출하면 안전하지 않다.
   따라서 printf() 호출은 함수의 끝부분에 추가해야 한다. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* 스레드 전환의 핵심 로직이다.
	 * 먼저 실행 컨텍스트 전체를 intr_frame에 복원한 뒤,
	 * do_iret를 호출하여 다음 스레드로 전환한다.
	 * 여기부터 스레드 전환이 끝날 때까지는 스택을 사용하면 안 된다. */
	__asm __volatile (
			/* 이후에 사용할 레지스터를 저장한다. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* 입력값을 한 번만 가져온다. */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // 저장해 둔 rcx 값이다.
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // 저장해 둔 rbx 값이다.
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // 저장해 둔 rax 값이다.
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // 현재 명령어 포인터(rip)를 읽는다.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // 명령어 포인터(rip)이다.
			"movw %%cs, 8(%%rax)\n"  // 코드 세그먼트(cs)이다.
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // 플래그 레지스터(eflags)이다.
			"mov %%rsp, 24(%%rax)\n" // 스택 포인터(rsp)이다.
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* 새 스레드를 스케줄링한다. 진입 시 인터럽트가 비활성화되어 있어야 한다.
 * 현재 스레드의 상태를 status로 변경한 다음,
 * 실행할 다른 스레드를 찾아 그 스레드로 전환한다.
 * schedule() 안에서는 printf()를 호출하면 안전하지 않다. */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

//스케줄링
static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* 새로 선택한 스레드를 실행 중 상태로 표시한다. */
	next->status = THREAD_RUNNING;

	/* 새 타임 슬라이스를 시작한다. */
	thread_ticks = 0;

#ifdef USERPROG
	/* 새 주소 공간을 활성화한다. */
	process_activate (next);
#endif

	if (curr != next) {
		/* 이전 스레드가 종료 중이면 해당 struct thread를 제거한다.
		   thread_exit()가 자기 실행에 필요한 메모리를 먼저 제거하지 않도록,
		   이 처리는 충분히 뒤에서 수행해야 한다.
		   현재 스택이 해당 페이지를 사용 중이므로, 여기서는 페이지 해제
		   요청을 큐에 넣기만 한다. 실제 제거 처리는 schedule()의 시작 부분에서 수행한다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* 스레드를 전환하기 전에 현재 실행 중인 스레드의 정보를 먼저 저장한다. */
		thread_launch (next);
	}
}

/* 새 스레드에 사용할 tid를 반환한다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}