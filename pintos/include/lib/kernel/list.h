#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* 이중 연결 리스트.
 *
 * 이 구현은 동적으로 할당한 메모리를 사용할 필요가 없다.
 * 대신 리스트의 요소로 사용할 각 구조체 안에 struct list_elem
 * 멤버를 포함해야 한다. 모든 리스트 함수는 이 struct list_elem을
 * 대상으로 동작한다. list_entry 매크로를 사용하면 struct list_elem에서
 * 이를 포함하는 구조체 객체로 다시 변환할 수 있다.

 * 예를 들어 struct foo의 리스트가 필요하다면,
 * struct foo에 다음과 같이 struct list_elem 멤버를 포함한다.

 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...other members...
 * };

 * 그러면 struct foo의 리스트를 다음과 같이 선언하고 초기화할 수 있다.

 * struct list foo_list;

 * list_init (&foo_list);

 * 리스트를 순회할 때는 struct list_elem을 이를 포함하는 구조체로
 * 변환해야 하는 경우가 많다. 다음은 foo_list를 사용하는 예시다.

 * struct list_elem *e;

 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...do something with f...
 * }

 * 실제 사용 예시는 소스 곳곳에서 찾을 수 있다.
 * 예를 들어 threads 디렉터리의 malloc.c, palloc.c, thread.c는
 * 모두 리스트를 사용한다.

 * 이 리스트의 인터페이스는 C++ STL의 list<> 템플릿을 참고했다.
 * list<>에 익숙하다면 쉽게 사용할 수 있다. 다만 이 리스트는
 * 자료형을 검사하지 않으며, 다른 올바른 사용 여부도 충분히
 * 검사하지 못한다. 잘못 사용하면 문제가 발생할 수 있다.

 * 리스트 용어 설명:

 * - "front": 리스트의 첫 번째 요소. 빈 리스트에서는 정의되지 않는다.
 *   list_front()가 반환한다.

 * - "back": 리스트의 마지막 요소. 빈 리스트에서는 정의되지 않는다.
 *   list_back()이 반환한다.

 * - "tail": 개념적으로 리스트의 마지막 요소 바로 뒤에 있는 요소.
 *   빈 리스트에서도 정의된다. list_end()가 반환하며,
 *   앞에서 뒤로 순회할 때 끝을 표시하는 센티널로 사용한다.

 * - "beginning": 리스트가 비어 있지 않으면 front, 비어 있으면 tail.
 *   list_begin()이 반환하며, 앞에서 뒤로 순회할 때 시작점으로 사용한다.

 * - "head": 개념적으로 리스트의 첫 번째 요소 바로 앞에 있는 요소.
 *   빈 리스트에서도 정의된다. list_rend()가 반환하며,
 *   뒤에서 앞으로 순회할 때 끝을 표시하는 센티널로 사용한다.

 * - "reverse beginning": 리스트가 비어 있지 않으면 back, 비어 있으면 head.
 *   list_rbegin()이 반환하며, 뒤에서 앞으로 순회할 때 시작점으로 사용한다.
 *
 * - "interior element": head나 tail이 아닌 실제 리스트 요소.
 *   빈 리스트에는 이러한 내부 요소가 없다.*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 리스트 요소. */
struct list_elem {
	struct list_elem *prev;     /* 이전 리스트 요소. */
	struct list_elem *next;     /* 다음 리스트 요소. */
};

/* 리스트. */
struct list {
	struct list_elem head;      /* 리스트의 머리 센티널. */
	struct list_elem tail;      /* 리스트의 꼬리 센티널. */
};

/* 리스트 요소를 가리키는 포인터 LIST_ELEM을, 해당 요소를 포함하는
   구조체의 포인터로 변환한다. 바깥 구조체의 자료형 이름 STRUCT와
   리스트 요소의 멤버 이름 MEMBER를 전달한다.
   사용 예시는 파일 상단의 긴 주석을 참고한다. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* 리스트 순회. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* 리스트 삽입. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* 리스트 제거. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* 리스트 요소 조회. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* 리스트 속성 조회. */
size_t list_size (struct list *);
bool list_empty (struct list *);

/* 기타 연산. */
void list_reverse (struct list *);

/* 보조 데이터 AUX를 사용하여 두 리스트 요소 A와 B를 비교한다.
   지정한 정렬 기준에서 A가 B보다 앞에 와야 하면 true를 반환하고,
   그렇지 않으면 false를 반환한다.
   예를 들어 숫자 오름차순이면 A < B,
   우선순위 내림차순이면 A의 우선순위 > B의 우선순위를 기준으로 한다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* 요소의 순서를 다루는 리스트 연산. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* 최댓값과 최솟값. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
