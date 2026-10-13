#include "list.h"
#include "../debug.h"

/* 이중 연결 리스트에는 두 개의 경계 요소가 있다.
   첫 데이터 요소 바로 앞에 head가 있고, 마지막 데이터 요소 바로 뒤에 tail이 있다.
   head의 prev와 tail의 next는 NULL이다.
   나머지 두 포인터는 리스트 내부 요소들을 거쳐 서로를 향한다.

   빈 리스트의 구조는 다음과 같다.

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   요소가 두 개인 리스트의 구조는 다음과 같다.

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   이 대칭 구조 덕분에 리스트 처리에서 많은 예외 상황을 따로 다룰 필요가 없다.
   예를 들어 list_remove()는 조건문 없이 포인터 대입 두 번만으로 처리한다.
   경계 요소가 없는 경우보다 코드가 훨씬 단순해진다.

   각 경계 요소에서는 포인터 하나만 사용하므로, 두 요소를 하나로 합쳐도
   이 단순함을 유지할 수 있다. 다만 두 요소를 별도로 두면 일부 연산에서
   추가 검증을 할 수 있어 유용하다. */

static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) UNUSED;

/* ELEM이 head 경계 요소이면 true, 그렇지 않으면 false를 반환한다. */
static inline bool
is_head (struct list_elem *elem) {
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* ELEM이 리스트 내부의 데이터 요소이면 true, 그렇지 않으면 false를 반환한다. */
static inline bool
is_interior (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* ELEM이 tail 경계 요소이면 true, 그렇지 않으면 false를 반환한다. */
static inline bool
is_tail (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* LIST를 빈 리스트로 초기화한다. */
void
list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* LIST의 시작 요소를 반환한다. */
struct list_elem *
list_begin (struct list *list) {
	ASSERT (list != NULL);
	return list->head.next;
}

/* 리스트에서 ELEM의 다음 요소를 반환한다.
   ELEM이 마지막 데이터 요소이면 tail을 반환한다.
   ELEM 자체가 tail이면 결과는 정의되지 않는다. */
struct list_elem *
list_next (struct list_elem *elem) {
	ASSERT (is_head (elem) || is_interior (elem));
	return elem->next;
}

/* LIST의 tail을 반환한다.

   list_end()는 리스트를 앞에서 뒤로 순회할 때 자주 사용한다.
   예제는 list.h 상단의 긴 주석을 참고한다. */
struct list_elem *
list_end (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* LIST를 뒤에서 앞으로 역순으로 순회하기 위한 시작 요소를 반환한다. */
struct list_elem *
list_rbegin (struct list *list) {
	ASSERT (list != NULL);
	return list->tail.prev;
}

/* 리스트에서 ELEM의 이전 요소를 반환한다.
   ELEM이 첫 데이터 요소이면 head를 반환한다.
   ELEM 자체가 head이면 결과는 정의되지 않는다. */
struct list_elem *
list_prev (struct list_elem *elem) {
	ASSERT (is_interior (elem) || is_tail (elem));
	return elem->prev;
}

/* LIST의 head를 반환한다.

   list_rend()는 리스트를 뒤에서 앞으로 역순으로 순회할 때 자주 사용한다.
   list.h 상단의 예제를 바탕으로 한 일반적인 사용법은 다음과 같다.

   for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
   e = list_prev (e))
   {
   struct foo *f = list_entry (e, struct foo, elem);
   ...f를 사용한 작업 수행...
   } */
struct list_elem *
list_rend (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 head를 반환한다.

   list_head()는 다음과 같은 다른 형태의 순회에도 사용할 수 있다.

   e = list_head (&list);
   while ((e = list_next (e)) != list_end (&list))
   {
   ...
   } */
struct list_elem *
list_head (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 tail을 반환한다. */
struct list_elem *
list_tail (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* BEFORE 바로 앞에 ELEM을 삽입한다.
   BEFORE는 내부 데이터 요소 또는 tail일 수 있다.
   BEFORE가 tail이면 list_push_back()과 같은 동작이다. */
void
list_insert (struct list_elem *before, struct list_elem *elem) {
	ASSERT (is_interior (before) || is_tail (before));
	ASSERT (elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* 현재 리스트에서 FIRST부터 LAST 직전까지의 요소들을 떼어 내어
   BEFORE 바로 앞에 삽입한다. LAST는 이동 범위에 포함하지 않는다.
   BEFORE는 내부 데이터 요소 또는 tail일 수 있다. */
void
list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last) {
	ASSERT (is_interior (before) || is_tail (before));
	if (first == last)
		return;
	last = list_prev (last);

	ASSERT (is_interior (first));
	ASSERT (is_interior (last));

	/* 현재 리스트에서 FIRST부터 LAST까지의 요소들을 떼어 낸다. */
	first->prev->next = last->next;
	last->next->prev = first->prev;

	/* FIRST부터 LAST까지의 요소들을 새 리스트에 연결한다. */
	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* LIST의 맨 앞에 ELEM을 삽입하여 첫 번째 데이터 요소로 만든다. */
void
list_push_front (struct list *list, struct list_elem *elem) {
	list_insert (list_begin (list), elem);
}

/* LIST의 맨 뒤에 ELEM을 삽입하여 마지막 데이터 요소로 만든다. */
void
list_push_back (struct list *list, struct list_elem *elem) {
	list_insert (list_end (list), elem);
}

/* 리스트에서 ELEM을 제거하고, 원래 그다음에 있던 요소를 반환한다.
   ELEM이 리스트에 속하지 않으면 동작은 정의되지 않는다.

   제거한 ELEM을 여전히 리스트에 속한 요소처럼 다루면 안전하지 않다.
   특히 제거 후 ELEM에 list_next()나 list_prev()를 사용하면 동작이 정의되지 않는다.
   따라서 다음처럼 단순하게 순회하며 제거하는 반복문은 잘못된 방식이다.

   ** 다음 방식은 사용하지 않는다 **
   for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
   {
   ...e를 사용한 작업 수행...
   list_remove (e);
   }
   ** 다음 방식은 사용하지 않는다 **

   순회하면서 요소를 제거하는 올바른 방법 중 하나는 다음과 같다.

   for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
   {
   ...e를 사용한 작업 수행...
   }

   요소의 메모리까지 free()로 해제해야 한다면 더 주의해야 한다.
   다음 방식은 그런 경우에도 사용할 수 있다.

   while (!list_empty (&list))
   {
   struct list_elem *e = list_pop_front (&list);
   ...e를 사용한 작업 수행...
   } */
struct list_elem *
list_remove (struct list_elem *elem) {
	ASSERT (is_interior (elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* LIST의 첫 데이터 요소를 제거하여 반환한다.
   제거 전에 LIST가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}

/* LIST의 마지막 데이터 요소를 제거하여 반환한다.
   제거 전에 LIST가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_pop_back (struct list *list) {
	struct list_elem *back = list_back (list);
	list_remove (back);
	return back;
}

/* LIST의 첫 데이터 요소를 반환한다.
   LIST가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_front (struct list *list) {
	ASSERT (!list_empty (list));
	return list->head.next;
}

/* LIST의 마지막 데이터 요소를 반환한다.
   LIST가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_back (struct list *list) {
	ASSERT (!list_empty (list));
	return list->tail.prev;
}

/* LIST의 데이터 요소 개수를 반환한다.
   요소 개수 n에 대해 O(n)의 시간이 걸린다. */
size_t
list_size (struct list *list) {
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		cnt++;
	return cnt;
}

/* LIST가 비어 있으면 true, 그렇지 않으면 false를 반환한다. */
bool
list_empty (struct list *list) {
	return list_begin (list) == list_end (list);
}

/* A와 B가 가리키는 struct list_elem * 값을 서로 바꾼다. */
static void
swap (struct list_elem **a, struct list_elem **b) {
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* LIST의 요소 순서를 뒤집는다. */
void
list_reverse (struct list *list) {
	if (!list_empty (list)) {
		struct list_elem *e;

		for (e = list_begin (list); e != list_end (list); e = e->prev)
			swap (&e->prev, &e->next);
		swap (&list->head.next, &list->tail.prev);
		swap (&list->head.next->prev, &list->tail.prev->next);
	}
}

/* 보조 데이터 AUX를 전달받는 비교 함수 LESS를 기준으로,
   A부터 B 직전까지의 요소들이 정렬되어 있을 때만 true를 반환한다. */
static bool
is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	if (a != b)
		while ((a = list_next (a)) != b)
			if (less (a, list_prev (a), aux))
				return false;
	return true;
}

/* A에서 시작하고 B를 넘지 않는, LESS 기준의 비내림차순 연속 구간을 찾는다.
   비교 함수 LESS에는 보조 데이터 AUX를 전달한다.
   구간의 끝 다음 요소를 반환하며, 그 요소는 해당 구간에 포함되지 않는다.
   A부터 B 직전까지의 범위는 비어 있으면 안 된다. */
static struct list_elem *
find_end_of_run (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	ASSERT (a != NULL);
	ASSERT (b != NULL);
	ASSERT (less != NULL);
	ASSERT (a != b);

	do {
		a = list_next (a);
	} while (a != b && !less (a, list_prev (a), aux));
	return a;
}

/* A0부터 A1B0 직전까지의 구간과 A1B0부터 B1 직전까지의 구간을 병합한다.
   병합 결과도 B1 직전에서 끝난다. 두 입력 구간은 비어 있으면 안 되며,
   보조 데이터 AUX를 전달받는 LESS를 기준으로 비내림차순 정렬되어 있어야 한다.
   병합 결과도 같은 기준으로 정렬된다. */
static void
inplace_merge (struct list_elem *a0, struct list_elem *a1b0,
		struct list_elem *b1,
		list_less_func *less, void *aux) {
	ASSERT (a0 != NULL);
	ASSERT (a1b0 != NULL);
	ASSERT (b1 != NULL);
	ASSERT (less != NULL);
	ASSERT (is_sorted (a0, a1b0, less, aux));
	ASSERT (is_sorted (a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);
		}
}

/* 보조 데이터 AUX를 전달받는 비교 함수 LESS를 기준으로 LIST를 정렬한다.
   자연 병합 정렬을 반복 방식으로 수행하며, 요소 개수 n에 대해
   시간 복잡도는 O(n lg n), 추가 공간 복잡도는 O(1)이다. */
void
list_sort (struct list *list, list_less_func *less, void *aux) {
	size_t output_run_cnt;        /* 이번 순회에서 만들어진 정렬 구간 수이다. */

	ASSERT (list != NULL);
	ASSERT (less != NULL);

	/* 정렬된 구간이 하나만 남을 때까지 리스트를 반복해서 순회하며,
	   인접한 비내림차순 구간들을 병합한다. */
	do {
		struct list_elem *a0;     /* 첫 번째 구간의 시작이다. */
		struct list_elem *a1b0;   /* 첫 번째 구간의 끝이자 두 번째 구간의 시작이다. */
		struct list_elem *b1;     /* 두 번째 구간의 끝이다. */

		output_run_cnt = 0;
		for (a0 = list_begin (list); a0 != list_end (list); a0 = b1) {
			/* 각 반복에서 결과 구간 하나가 만들어진다. */
			output_run_cnt++;

			/* 인접한 두 비내림차순 구간 A0...A1B0와 A1B0...B1을 찾는다. */
			a1b0 = find_end_of_run (a0, list_end (list), less, aux);
			if (a1b0 == list_end (list))
				break;
			b1 = find_end_of_run (a1b0, list_end (list), less, aux);

			/* 두 구간을 병합한다. */
			inplace_merge (a0, a1b0, b1, less, aux);
		}
	}
	while (output_run_cnt > 1);

	ASSERT (is_sorted (list_begin (list), list_end (list), less, aux));
}

/* LIST의 정렬 순서에 맞는 위치에 ELEM을 삽입한다.
   LIST는 보조 데이터 AUX를 전달받는 비교 함수 LESS를 기준으로
   이미 정렬되어 있어야 한다. 요소 개수 n에 대해 평균 O(n)의 시간이 걸린다. */
void
list_insert_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;

	ASSERT (list != NULL);
	ASSERT (elem != NULL);
	ASSERT (less != NULL);

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))
			break;
	return list_insert (e, elem);
}

/* LIST를 순회하며, LESS 기준으로 같은 값인 인접 요소들 중 첫 요소만 남기고
   나머지를 제거한다. 비교 함수 LESS에는 보조 데이터 AUX를 전달한다.
   DUPLICATES가 NULL이 아니면 제거한 요소들을 그 리스트의 뒤에 추가한다. */
void
list_unique (struct list *list, struct list *duplicates,
		list_less_func *less, void *aux) {
	struct list_elem *elem, *next;

	ASSERT (list != NULL);
	ASSERT (less != NULL);
	if (list_empty (list))
		return;

	elem = list_begin (list);
	while ((next = list_next (elem)) != list_end (list))
		if (!less (elem, next, aux) && !less (next, elem, aux)) {
			list_remove (next);
			if (duplicates != NULL)
				list_push_back (duplicates, next);
		} else
			elem = next;
}

/* 보조 데이터 AUX를 전달받는 비교 함수 LESS를 기준으로,
   LIST에서 가장 큰 값을 가진 요소를 반환한다.
   최댓값이 여러 개이면 리스트에서 먼저 나오는 요소를 반환한다.
   LIST가 비어 있으면 tail을 반환한다. */
struct list_elem *
list_max (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *max = list_begin (list);
	if (max != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (max); e != list_end (list); e = list_next (e))
			if (less (max, e, aux))
				max = e;
	}
	return max;
}

/* 보조 데이터 AUX를 전달받는 비교 함수 LESS를 기준으로,
   LIST에서 가장 작은 값을 가진 요소를 반환한다.
   최솟값이 여러 개이면 리스트에서 먼저 나오는 요소를 반환한다.
   LIST가 비어 있으면 tail을 반환한다. */
struct list_elem *
list_min (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *min = list_begin (list);
	if (min != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (min); e != list_end (list); e = list_next (e))
			if (less (e, min, aux))
				min = e;
	}
	return min;
}
