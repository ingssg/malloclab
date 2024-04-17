/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include "mm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

// /* single word (4) or double word (8) alignment */
// #define ALIGNMENT 8

// /* rounds up to the nearest multiple of ALIGNMENT */
// #define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

// #define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// 사이즈 상수 정의
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 10)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))                             // 해당 주소의 val 값을 가져옴
#define PUT(p, val) (*(unsigned int *)(p) = (unsigned int)(val))  // 해당 주소에 val을 넣어줌

#define GET_SIZE(p) (GET(p) & ~0x7)  // 11111000
#define GET_ALLOC(p) (GET(p) & 0x1)  // 00000001

#define HDRP(bp) ((char *)(bp)- WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

#define PRED(bp) (*(void **)(bp))
#define SUCC(bp) (*(void **)(bp + WSIZE))

#define SEG_SIZE (12)
#define GET_ROOT(class) (*(void **)((char *)(heap_listp) + WSIZE*class))

int mm_init(void);
static void *extend_heap(size_t words);
void mm_free(void *ptr);
static void *coalesce_ex(void *ptr);
void *mm_malloc(size_t size);
void *mm_realloc(void *ptr, size_t size);
static void place_ex(void *ptr, size_t asize);
void connect_pred_succ(void *ptr);
void connect_root(void *ptr);
int get_class(size_t size);
static void *find_fit_seg(size_t asize);
static void *find_best_fit_seg(size_t asize);

void *heap_listp;

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)  // 초기 힙 할당과 같은 필요한 초기화를 수행한다,
{                  // 영역 초기화 문제 시 -1, 문제 없으면 0

    if ((heap_listp = mem_sbrk((SEG_SIZE+4) * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                                 // 패딩
    PUT(heap_listp + (1 * WSIZE), PACK((SEG_SIZE+2) * WSIZE, 1));  // 프롤로그 헤더
    for(int i = 0; i <= SEG_SIZE; i++) {
        PUT(heap_listp + ((2+i) * WSIZE), NULL);    // segregated 리스트 포인터 저장
    }
    PUT(heap_listp + ((SEG_SIZE+2) * WSIZE), PACK((SEG_SIZE+2) * WSIZE, 1));   // 프롤로그 푸터
    PUT(heap_listp + ((SEG_SIZE+3) * WSIZE) + WSIZE, PACK(0, 1));   // 에필로그 헤더 (끝을 나타내기  위해 4이지만 0 넣기)
    heap_listp += (2 * WSIZE);


    // if (extend_heap(4) == NULL)
    //     return -1;
    // CHUNKSIZE만큼 힙 확장 (2^12)
    if (extend_heap(CHUNKSIZE / DSIZE) == NULL)
        return -1;

    return 0;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = words * DSIZE;                   // 참: 나누어떨어지지 않음 > 패딩 추가
    if ((long)(bp = mem_sbrk(size)) == -1)  // 총 힙영역을 늘려라, 늘리고 시작 부분 뱉어줌
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));          // 그 헤더에다가 사이즈랑, 할당여부 저장
    PUT(FTRP(bp), PACK(size, 0));          // 푸터에다가도 저장.
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  // 에필로그 헤더

    return coalesce_ex(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 *      최소 size 바이트의 할당된 블록 페이로드에 대한 포인터 반환(블록 전체는 힙 영역내에 있어야함)
 *      다른 할당된 청크와 겹쳐서도 안 됨. 8바이트로 정렬된 포인터를 반환해야함.
 */
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *ptr;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((ptr = find_best_fit_seg(asize)) != NULL) {  // NULL이 아니면 >> 가용블록 찾았다!
        place_ex(ptr, asize);
        return ptr;
    }
    // fit 못찾으면,, ( = NULL이면)
    extendsize = MAX(asize, CHUNKSIZE);
    // printf("사이즈 부족으로 Chuncksize %d 연장\n", extendsize);B
    if ((ptr = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place_ex(ptr, asize);
    return ptr;
}

// 해당 사이즈에 대한 클래스를 반환
int get_class(size_t size) {
    if(size < 16) {
        return -1;
    }
    size_t class_size[SEG_SIZE];
    class_size[0] = 16;

    for(int i = 0; i < SEG_SIZE; i++) {
        if(i != 0) {
            class_size[i] = class_size[i-1] << 1;
        }
        if(size <= class_size[i]) {
            return i;
        }
    }

    return SEG_SIZE - 1;
}

// first_fit
static void *find_fit_seg(size_t asize) {
    int class = get_class(asize);
    void *ptr = GET_ROOT(class);
    while(class < SEG_SIZE) {
        ptr = GET_ROOT(class);
        while(ptr != NULL) {
            if(asize <= GET_SIZE(HDRP(ptr))) {
                return ptr;
            }
            ptr = SUCC(ptr);
        }
        class += 1;
    }
    return NULL;
}

// best_fit
static void *find_best_fit_seg(size_t asize) {
    int class = get_class(asize);
    void *ptr = GET_ROOT(class);
    void *min_ptr = NULL;
    while(class < SEG_SIZE) {
        ptr = GET_ROOT(class);
        while(ptr != NULL) {
            if(asize <= GET_SIZE(HDRP(ptr))) {
                if(min_ptr == NULL) min_ptr = ptr;
                else if(GET_SIZE(HDRP(ptr)) < GET_SIZE(HDRP(min_ptr)))
                    min_ptr = ptr;
            }
            ptr = SUCC(ptr);
        }
        if(min_ptr == NULL)
            class += 1;
        else
            break;
    }
    if(min_ptr == NULL) return NULL;
    return min_ptr;
}

static void place_ex(void *ptr, size_t asize) {
    size_t csize = GET_SIZE(HDRP(ptr));

    connect_pred_succ(ptr);

    if ((csize - asize) >= 2 * DSIZE) {  // 내가 사용하고 남은 블록의 사이즈가 2더블워드보다 클 때
        // printf("block 위치 %p | 들어갈 list의 크기 %d | 넣어야 할 size 크기 %d\n", (int *)ptr, GET_SIZE(HDRP(ptr)), asize);
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));  // 할당블록 해주시고~
        ptr = NEXT_BLKP(ptr);
        // printf("free block 위치 %p | 나머지 block 크기 %d\n", (int *)NEXT_BLKP(ptr), csize - asize);

        PUT(HDRP(ptr), PACK(csize - asize, 0));
        PUT(FTRP(ptr), PACK(csize - asize, 0));  // 사용하고 남은 가용 블록 해주시고
        connect_root(ptr);
    } else {  // 내가 사용하고 남은 블록의 사이즈가 2더블워드 사이즈보다 작을 때(풀로 채움, 새로 생기는 가용블록 없음) > 내 앞뒤 가용블록끼리 연결해줘야함
        // printf("block 위치 %p | padding으로 넣은 size 크기 %d\n", (unsigned int *)ptr, csize - asize);

        PUT(HDRP(ptr), PACK(csize, 1));
        PUT(FTRP(ptr), PACK(csize, 1));  // 그럼 패딩을 쳐서 끝까지
    }
}

/*
 * mm_free - Freeing a block does nothing.
 * ptr이 가리키는 블록을 해제함, 반환 값 x , 아직 해제되지 않은 블록에 경우만 작동이 보장
 */
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce_ex(ptr);
}

void connect_pred_succ(void *ptr) {  // 사라지는(할당되는) 가용 블록의 이전 이후를 서로 연결
    int class = get_class(GET_SIZE(HDRP(ptr)));
    if (ptr == GET_ROOT(class)) {
        GET_ROOT(class) = SUCC(ptr);
        return;
    }
    SUCC(PRED(ptr)) = SUCC(ptr);        // 이전 가용블록에는 이후가용블록이 내 이후 가용블록임
    if(SUCC(ptr) != NULL) {
        PRED(SUCC(ptr)) = PRED(ptr);    // 이후 가용블록에는 이전 가용블록이 현재 내 블록의 이전 가용블록 주소라고 알려줘야함.
    }
}

void connect_root(void *ptr) {
    int class = get_class(GET_SIZE(HDRP(ptr)));
    SUCC(ptr) = GET_ROOT(class);
    if(GET_ROOT(class) != NULL) {       // NULL이라는 것은 블록이 없다는 것.
        PRED(GET_ROOT(class)) = ptr;
    }
    GET_ROOT(class) = ptr;
}

static void *coalesce_ex(void *ptr) {                     
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));  // 이전 블록의 가용 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));  // 다음 블록의 가용 여부
    size_t size = GET_SIZE(HDRP(ptr));                    // 현재 블록의 크기

    if (prev_alloc && next_alloc) {  // 이전, 이후 모두 할당 되어 있는 경우 > 안 합침 , 내가 루트 다음이 된다.
        connect_root(ptr);
        return ptr;
    }

    else if (prev_alloc && !next_alloc) {  // 이전 할당, 이후 가용 > 이후랑만 합치면 됨.
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        connect_pred_succ(NEXT_BLKP(ptr));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));  // 푸터는 헤더의 주소로 가져오니까 이렇게하면 끝을 가리킴.
    }

    else if (!prev_alloc && next_alloc) {  // 이전 가용, 이후 할당 > 이전이랑만 합치면 됨.
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        connect_pred_succ(PREV_BLKP(ptr));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));  // 헤더 앞으로 댕겨주고
        ptr = PREV_BLKP(ptr);                      // 포인터 앞으로 옮겨주기
    }

    else {  // 이전, 이후 모두 가용 > 싹다 합쳐
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(FTRP(NEXT_BLKP(ptr)));
        connect_pred_succ(NEXT_BLKP(ptr));
        connect_pred_succ(PREV_BLKP(ptr));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }

    connect_root(ptr);  // 가용 리스트에 블록 추가
    return ptr;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 * 최소한 크기의 할당된 영역에 대한 포인터를 반환.
 * 크기가 0이면 호출은 mm free(prt)과 동일
 */
void *mm_realloc(void *ptr, size_t size) {
    void *oldptr = ptr;
    void *newptr;
    void *prev_ptr = PREV_BLKP(ptr);
    void *next_ptr = NEXT_BLKP(ptr);
    size_t copySize;
    size_t asize;
    size_t csize = GET_SIZE(HDRP(ptr));
    size_t prev_alloc = GET_ALLOC(HDRP(prev_ptr));
    size_t next_alloc = GET_ALLOC(HDRP(next_ptr));
    size_t prev_size = GET_SIZE(HDRP(prev_ptr));
    size_t next_size = GET_SIZE(HDRP(next_ptr));

    if(size == 0) {
        mm_free(ptr);
        return NULL;
    }

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    
    // 이전 가용블록이 요청 크기보다 크다면 그냥 거기에 덮어씌우기
    if(!prev_alloc && prev_size > asize) {
        memcpy(prev_ptr, ptr, asize);
        connect_pred_succ(prev_ptr);
        PUT(HDRP(prev_ptr), PACK(prev_size, 1));
        PUT(FTRP(prev_ptr), PACK(prev_size, 1));
        mm_free(ptr);

        return prev_ptr;
    }

    // 이후 가용블록이 요청 크기보다 크다면 그냥 거기에 덮어씌우기
    if(!next_alloc && next_size > asize) {
        memcpy(next_ptr, ptr, asize);
        connect_pred_succ(next_ptr);
        PUT(HDRP(next_ptr), PACK(next_size, 1));
        PUT(FTRP(next_ptr), PACK(next_size, 1));
        mm_free(ptr);

        return next_ptr;
    }
    // 이전 가용블록 + 현재 블록 > 요청 크기면 이전 가용블록 + 현재로 덮어씌우기, 근데오류남(why???)
    // if(!prev_alloc && csize + prev_size > asize) {
    //     memmove(prev_ptr, ptr, asize);
    //     connect_pred_succ(prev_ptr);
    //     PUT(HDRP(prev_ptr), PACK(csize + prev_size, 1));
    //     PUT(HDRP(prev_ptr), PACK(csize + prev_size, 1));
    //     return prev_ptr;
    // }

    // 이후 가용블록 + 현재 블록 > 요청 크기면 이후 가용블록 + 현재로 덮어씌우기
    if(!next_alloc && csize + next_size > asize) {
        // 빠진 가용블록에 대한 처리
        connect_pred_succ(next_ptr);
        // 헤더 푸터 갱신
        PUT(HDRP(ptr), PACK(csize+next_size, 1));
        PUT(FTRP(ptr), PACK(csize+next_size, 1));

        return ptr;
    }




    newptr = mm_malloc(size);   // 말록에서 어차피 두개 나눠서 저장해줌.
    if (newptr == NULL)
        return NULL;
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
