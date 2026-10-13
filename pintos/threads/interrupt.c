#include "threads/interrupt.h"
#include <debug.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "threads/flags.h"
#include "threads/intr-stubs.h"
#include "threads/io.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/gdt.h"
#endif

/* x86_64 인터럽트의 개수. */
#define INTR_CNT 256

/* FUNCTION을 호출하는 게이트를 생성한다.

   게이트의 디스크립터 권한 수준은 DPL이다. 프로세서가 DPL과 같거나
   더 작은 번호의 권한 링에서 실행 중일 때 이 게이트를 명시적으로 호출할 수 있다.
   실제로 DPL==3이면 사용자 모드에서 게이트를 호출할 수 있고,
   DPL==0이면 이러한 호출을 허용하지 않는다. 사용자 모드에서 발생한
   폴트와 예외는 DPL==0인 게이트도 호출하게 한다.

   TYPE은 14(인터럽트 게이트) 또는 15(트랩 게이트)여야 한다.
   인터럽트 게이트에 진입하면 인터럽트가 비활성화되지만,
   트랩 게이트에 진입할 때는 비활성화되지 않는다는 차이가 있다.
   자세한 내용은 [IA32-v3a]의 5.12.1.2절
   "Flag Usage By Exception- or Interrupt-Handler Procedure"를 참고한다. */

struct gate {
	unsigned off_15_0 : 16;   // 세그먼트 내 오프셋의 하위 16비트
	unsigned ss : 16;         // 세그먼트 선택자
	unsigned ist : 3;        // 인자 수. 인터럽트/트랩 게이트에서는 0
	unsigned rsv1 : 5;        // 예약된 필드(0이어야 하는 것으로 보임)
	unsigned type : 4;        // 유형(STS_{TG,IG32,TG32})
	unsigned s : 1;           // 반드시 0이어야 함(시스템)
	unsigned dpl : 2;         // 디스크립터의 권한 수준(새 권한 수준을 의미)
	unsigned p : 1;           // 존재 여부
	unsigned off_31_16 : 16;  // 세그먼트 내 오프셋의 상위 비트
	uint32_t off_32_63;
	uint32_t rsv2;
};

/* 인터럽트 디스크립터 테이블(IDT). 형식은 CPU에 의해 정해져 있다.
   [IA32-v3a]의 5.10절 "Interrupt Descriptor Table (IDT)",
   5.11절 "IDT Descriptors", 5.12.1.2절
   "Flag Usage By Exception- or Interrupt-Handler Procedure"를 참고한다. */
static struct gate idt[INTR_CNT];

static struct desc_ptr idt_desc = {
	.size = sizeof(idt) - 1,
	.address = (uint64_t) idt
};


#define make_gate(g, function, d, t) \
{ \
	ASSERT ((function) != NULL); \
	ASSERT ((d) >= 0 && (d) <= 3); \
	ASSERT ((t) >= 0 && (t) <= 15); \
	*(g) = (struct gate) { \
		.off_15_0 = (uint64_t) (function) & 0xffff, \
		.ss = SEL_KCSEG, \
		.ist = 0, \
		.rsv1 = 0, \
		.type = (t), \
		.s = 0, \
		.dpl = (d), \
		.p = 1, \
		.off_31_16 = ((uint64_t) (function) >> 16) & 0xffff, \
		.off_32_63 = ((uint64_t) (function) >> 32) & 0xffffffff, \
		.rsv2 = 0, \
	}; \
}

/* 주어진 DPL로 FUNCTION을 호출하는 인터럽트 게이트를 생성한다. */
#define make_intr_gate(g, function, dpl) make_gate((g), (function), (dpl), 14)

/* 주어진 DPL로 FUNCTION을 호출하는 트랩 게이트를 생성한다. */
#define make_trap_gate(g, function, dpl) make_gate((g), (function), (dpl), 15)



/* 각 인터럽트의 처리 함수. */
static intr_handler_func *intr_handlers[INTR_CNT];

/* 디버깅을 위한 각 인터럽트의 이름. */
static const char *intr_names[INTR_CNT];

/* 외부 인터럽트는 타이머처럼 CPU 외부의 장치가 발생시키는 인터럽트이다.
   외부 인터럽트는 인터럽트가 비활성화된 상태에서 처리되므로,
   중첩되지 않으며 선점되지도 않는다. 외부 인터럽트 처리 함수는
   대기 상태로 들어갈 수 없다. 다만 intr_yield_on_return()을 호출하여
   인터럽트에서 돌아오기 직전에 새 프로세스가 스케줄링되도록 요청할 수 있다. */
static bool in_external_intr;   /* 외부 인터럽트를 처리 중인지 여부. */
static bool yield_on_return;    /* 인터럽트에서 돌아올 때 CPU를 양보해야 하는지 여부. */

/* 프로그래머블 인터럽트 컨트롤러의 보조 함수. */
static void pic_init (void);
static void pic_end_of_interrupt (int irq);

/* 인터럽트 처리 함수. */
void intr_handler (struct intr_frame *args);

/* 현재 인터럽트 상태를 반환한다. */
enum intr_level
intr_get_level (void) {
	uint64_t flags;

	/* 프로세서의 플래그 레지스터를 스택에 넣은 다음,
	   스택에서 꺼내어 `flags'에 저장한다.
	   [IA32-v2b]의 "PUSHF", "POP"과 [IA32-v3a]의 5.8.1절
	   "Masking Maskable Hardware Interrupts"를 참고한다. */
	asm volatile ("pushfq; popq %0" : "=g" (flags));

	return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}

/* LEVEL에 따라 인터럽트를 활성화하거나 비활성화하고,
   이전 인터럽트 상태를 반환한다. */
enum intr_level
intr_set_level (enum intr_level level) {
	return level == INTR_ON ? intr_enable () : intr_disable ();
}

/* 인터럽트를 활성화하고 이전 인터럽트 상태를 반환한다. */
enum intr_level
intr_enable (void) {
	enum intr_level old_level = intr_get_level ();
	ASSERT (!intr_context ());

	/* 인터럽트 플래그를 설정하여 인터럽트를 활성화한다.

	   [IA32-v2b]의 "STI"와 [IA32-v3a]의 5.8.1절
	   "Masking Maskable Hardware Interrupts"를 참고한다. */
	asm volatile ("sti");

	return old_level;
}

/* 인터럽트를 비활성화하고 이전 인터럽트 상태를 반환한다. */
enum intr_level
intr_disable (void) {
	enum intr_level old_level = intr_get_level ();

	/* 인터럽트 플래그를 지워 인터럽트를 비활성화한다.
	   [IA32-v2b]의 "CLI"와 [IA32-v3a]의 5.8.1절
	   "Masking Maskable Hardware Interrupts"를 참고한다. */
	asm volatile ("cli" : : : "memory");

	return old_level;
}

/* 인터럽트 시스템을 초기화한다. */
void
intr_init (void) {
	int i;

	/* 인터럽트 컨트롤러를 초기화한다. */
	pic_init ();

	/* IDT를 초기화한다. */
	for (i = 0; i < INTR_CNT; i++) {
		make_intr_gate(&idt[i], intr_stubs[i], 0);
		intr_names[i] = "unknown";
	}

#ifdef USERPROG
	/* TSS를 로드한다. */
	ltr (SEL_TSS);
#endif

	/* IDT 레지스터를 로드한다. */
	lidt(&idt_desc);

	/* intr_names를 초기화한다. */
	intr_names[0] = "#DE Divide Error";
	intr_names[1] = "#DB Debug Exception";
	intr_names[2] = "NMI Interrupt";
	intr_names[3] = "#BP Breakpoint Exception";
	intr_names[4] = "#OF Overflow Exception";
	intr_names[5] = "#BR BOUND Range Exceeded Exception";
	intr_names[6] = "#UD Invalid Opcode Exception";
	intr_names[7] = "#NM Device Not Available Exception";
	intr_names[8] = "#DF Double Fault Exception";
	intr_names[9] = "Coprocessor Segment Overrun";
	intr_names[10] = "#TS Invalid TSS Exception";
	intr_names[11] = "#NP Segment Not Present";
	intr_names[12] = "#SS Stack Fault Exception";
	intr_names[13] = "#GP General Protection Exception";
	intr_names[14] = "#PF Page-Fault Exception";
	intr_names[16] = "#MF x87 FPU Floating-Point Error";
	intr_names[17] = "#AC Alignment Check Exception";
	intr_names[18] = "#MC Machine-Check Exception";
	intr_names[19] = "#XF SIMD Floating-Point Exception";
}

/* 인터럽트 VEC_NO가 디스크립터 권한 수준 DPL로 HANDLER를 호출하도록 등록한다.
   디버깅을 위해 인터럽트 이름을 NAME으로 지정한다.
   인터럽트 처리 함수는 인터럽트 상태가 LEVEL로 설정된 상태에서 호출된다. */
static void
register_handler (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name) {
	ASSERT (intr_handlers[vec_no] == NULL);
	if (level == INTR_ON) {
		make_trap_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	else {
		make_intr_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	intr_handlers[vec_no] = handler;
	intr_names[vec_no] = name;
}

/* 외부 인터럽트 VEC_NO가 HANDLER를 호출하도록 등록하고,
   디버깅을 위해 이름을 NAME으로 지정한다.
   처리 함수는 인터럽트가 비활성화된 상태에서 실행된다. */
void
intr_register_ext (uint8_t vec_no, intr_handler_func *handler,
		const char *name) {
	ASSERT (vec_no >= 0x20 && vec_no <= 0x2f);
	register_handler (vec_no, 0, INTR_OFF, handler, name);
}

/* 내부 인터럽트 VEC_NO가 HANDLER를 호출하도록 등록하고,
   디버깅을 위해 이름을 NAME으로 지정한다.
   인터럽트 처리 함수는 인터럽트 상태 LEVEL로 호출된다.

   처리 함수의 디스크립터 권한 수준은 DPL이다. 프로세서가 DPL과 같거나
   더 작은 번호의 권한 링에서 실행 중일 때 명시적으로 호출할 수 있다.
   실제로 DPL==3이면 사용자 모드에서 인터럽트를 호출할 수 있고,
   DPL==0이면 이러한 호출을 허용하지 않는다. 사용자 모드에서 발생한
   폴트와 예외는 DPL==0인 인터럽트도 호출하게 한다.
   자세한 내용은 [IA32-v3a]의 4.5절 "Privilege Levels"와
   4.8.1.1절 "Accessing Nonconforming Code Segments"를 참고한다. */
void
intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name)
{
	ASSERT (vec_no < 0x20 || vec_no > 0x2f);
	register_handler (vec_no, dpl, level, handler, name);
}

/* 외부 인터럽트를 처리 중이면 true를 반환하고,
   그 외의 경우에는 false를 반환한다. */
bool
intr_context (void) {
	return in_external_intr;
}

/* 외부 인터럽트 처리 중, 인터럽트에서 돌아오기 직전에
   새 프로세스에 CPU를 양보하도록 요청한다.
   다른 상황에서는 호출할 수 없다. */
void
intr_yield_on_return (void) {
	ASSERT (intr_context ());
	yield_on_return = true;
}

/* 8259A 프로그래머블 인터럽트 컨트롤러. */

/* 각 PC에는 두 개의 8259A 프로그래머블 인터럽트 컨트롤러(PIC) 칩이 있다.
   하나는 포트 0x20과 0x21로 접근하는 "마스터"이다.
   다른 하나는 마스터의 IRQ 2 라인에 계단식으로 연결된 "슬레이브"이며,
   포트 0xa0과 0xa1로 접근한다. 포트 0x20에 접근하면 A0 라인을 0으로,
   포트 0x21에 접근하면 A1 라인을 1로 설정한다.
   슬레이브 PIC도 비슷한 방식으로 동작한다.

   기본적으로 PIC가 전달하는 인터럽트 0...15는 인터럽트 벡터 0...15로 전달된다.
   그러나 이 벡터들은 CPU의 트랩과 예외에도 사용된다.
   따라서 PIC를 다시 설정하여 인터럽트 0...15가
   인터럽트 벡터 32...47(0x20...0x2f)로 전달되도록 한다. */

/* PIC를 초기화한다. 자세한 내용은 [8259A]를 참고한다. */
static void
pic_init (void) {
	/* 두 PIC의 모든 인터럽트를 마스킹한다. */
	outb (0x21, 0xff);
	outb (0xa1, 0xff);

	/* 마스터를 초기화한다. */
	outb (0x20, 0x11); /* ICW1: 단일 모드, 에지 트리거 방식, ICW4 사용 예정. */
	outb (0x21, 0x20); /* ICW2: IR0...7 라인 -> IRQ 0x20...0x27. */
	outb (0x21, 0x04); /* ICW3: IR2 라인에 슬레이브 PIC가 연결됨. */
	outb (0x21, 0x01); /* ICW4: 8086 모드, 일반 EOI, 비버퍼 방식. */

	/* 슬레이브를 초기화한다. */
	outb (0xa0, 0x11); /* ICW1: 단일 모드, 에지 트리거 방식, ICW4 사용 예정. */
	outb (0xa1, 0x28); /* ICW2: IR0...7 라인 -> IRQ 0x28...0x2f. */
	outb (0xa1, 0x02); /* ICW3: 슬레이브 ID는 2. */
	outb (0xa1, 0x01); /* ICW4: 8086 모드, 일반 EOI, 비버퍼 방식. */

	/* 모든 인터럽트의 마스킹을 해제한다. */
	outb (0x21, 0x00);
	outb (0xa1, 0x00);
}

/* 주어진 IRQ에 대해 PIC에 인터럽트 처리 종료 신호를 보낸다.
   IRQ 처리를 확인해 주지 않으면 해당 IRQ가 다시 전달되지 않으므로 중요하다. */
static void
pic_end_of_interrupt (int irq) {
	ASSERT (irq >= 0x20 && irq < 0x30);

	/* 마스터 PIC에 처리 완료를 알린다. */
	outb (0x20, 0x20);

	/* 슬레이브 인터럽트이면 슬레이브 PIC에도 처리 완료를 알린다. */
	if (irq >= 0x28)
		outb (0xa0, 0x20);
}
/* 인터럽트 처리 함수. */

/* 모든 인터럽트, 폴트, 예외를 처리하는 함수.
   intr-stubs.S에 있는 어셈블리 언어 인터럽트 스텁이 이 함수를 호출한다.
   FRAME에는 인터럽트 정보와 인터럽트가 발생한 스레드의 레지스터 정보가 담겨 있다. */
void
intr_handler (struct intr_frame *frame) {
	bool external;
	intr_handler_func *handler;

	/* 외부 인터럽트는 특별하게 처리한다.
	   한 번에 하나만 처리하므로 인터럽트가 비활성화되어 있어야 하며,
	   PIC에 처리 완료를 알려야 한다(아래 참고).
	   외부 인터럽트 처리 함수는 대기 상태로 들어갈 수 없다. */
	external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (!intr_context ());

		in_external_intr = true;
		yield_on_return = false;
	}

	/* 해당 인터럽트의 처리 함수를 호출한다. */
	handler = intr_handlers[frame->vec_no];
	if (handler != NULL)
		handler (frame);
	else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {
		/* 처리 함수가 없지만, 하드웨어 오류나 경쟁 상태로 인해
		   이 인터럽트가 잘못 발생할 수 있다. 무시한다. */
	} else {
		/* 처리 함수가 없으며 잘못 발생한 인터럽트도 아니다.
		   예상치 못한 인터럽트로 처리한다. */
		intr_dump_frame (frame);
		PANIC ("Unexpected interrupt");
	}

	/* 외부 인터럽트 처리를 마무리한다. */
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (intr_context ());

		in_external_intr = false;
		pic_end_of_interrupt (frame->vec_no);

		if (yield_on_return)
			thread_yield ();
	}
}

/* 디버깅을 위해 인터럽트 프레임 F의 내용을 콘솔에 출력한다. */
void
intr_dump_frame (const struct intr_frame *f) {
	/* CR2는 마지막 페이지 폴트가 발생한 선형 주소이다.
	   [IA32-v2a]의 "MOV--Move to/from Control Registers"와
	   [IA32-v3a]의 5.14절 "Interrupt 14--Page Fault Exception (#PF)"를 참고한다. */
	uint64_t cr2 = rcr2();
	printf ("Interrupt %#04llx (%s) at rip=%llx\n",
			f->vec_no, intr_names[f->vec_no], f->rip);
	printf (" cr2=%016llx error=%16llx\n", cr2, f->error_code);
	printf ("rax %016llx rbx %016llx rcx %016llx rdx %016llx\n",
			f->R.rax, f->R.rbx, f->R.rcx, f->R.rdx);
	printf ("rsp %016llx rbp %016llx rsi %016llx rdi %016llx\n",
			f->rsp, f->R.rbp, f->R.rsi, f->R.rdi);
	printf ("rip %016llx r8 %016llx  r9 %016llx r10 %016llx\n",
			f->rip, f->R.r8, f->R.r9, f->R.r10);
	printf ("r11 %016llx r12 %016llx r13 %016llx r14 %016llx\n",
			f->R.r11, f->R.r12, f->R.r13, f->R.r14);
	printf ("r15 %016llx rflags %08llx\n", f->R.r15, f->eflags);
	printf ("es: %04x ds: %04x cs: %04x ss: %04x\n",
			f->es, f->ds, f->cs, f->ss);
}

/* 인터럽트 VEC의 이름을 반환한다. */
const char *
intr_name (uint8_t vec) {
	return intr_names[vec];
}
