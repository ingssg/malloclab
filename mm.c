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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
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
    ""
};

// /* single word (4) or double word (8) alignment */
// #define ALIGNMENT 8

// /* rounds up to the nearest multiple of ALIGNMENT */
// #define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


// #define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// 사이즈 상수 정의
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)

#define MAX(x, y) ((x) > (y)? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *) (p))  // 해당 주소의 val 값을 가져옴
#define PUT(p, val) (*(unsigned int *)(p) = (val))  // 해당 주소에 val을 넣어줌

#define GET_SIZE(p) (GET(p) & ~0x7) // 11111000
#define GET_ALLOC(p) (GET(p) & 0x1) // 00000001

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

int mm_init(void);
static void *extend_heap(size_t words);
void mm_free(void *ptr);
static void *coalesce(void *ptr);
void *mm_malloc(size_t size);
void *mm_realloc(void *ptr, size_t size);
static void *first_fit(size_t asize);
static void *next_fit(size_t asize);
static void *best_fit(size_t asize);
static void place(void *ptr, size_t asize);


void *heap_listp;
void *current_listp;

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)   // 초기 힙 할당과 같은 필요한 초기화를 수행한다, 
{                   // 영역 초기화 문제 시 -1, 문제 없으면 0

    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *) -1)
        return -1;
    PUT(heap_listp, 0); // 패딩
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 프롤로그 푸터
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // 에필로그 헤더 (끝을 나타내기  위해 4이지만 0 넣기)
    heap_listp += (2*WSIZE);
    current_listp = heap_listp; // next_fit 전용

    if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; // 참: 나누어떨어지지 않음 > 패딩 추가
    if((long)(bp = mem_sbrk(size)) == -1)   // 총 힙영역을 늘려라, 늘리고 시작 부분 뱉어줌
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));   // 그 헤더에다가 사이즈랑, 할당여부 저장
    PUT(FTRP(bp), PACK(size, 0));   // 푸터에다가도 저장.
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   // 에필로그 헤더

    return coalesce(bp);
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 *      최소 size 바이트의 할당된 블록 페이로드에 대한 포인터 반환(블록 전체는 힙 영역내에 있어야함)
 *      다른 할당된 청크와 겹쳐서도 안 됨. 8바이트로 정렬된 포인터를 반환해야함.
 */
void *mm_malloc(size_t size)   
{
    size_t asize;
    size_t extendsize;
    char *ptr;

    if (size == 0) 
        return NULL;

    if (size <= DSIZE) 
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    
    if ((ptr = (char *)best_fit(asize)) != NULL) {    // NULL이 아니면 >> 가용블록 찾았다! 
        place(ptr, asize);  
        return ptr;
    }

    extendsize = MAX(asize, CHUNKSIZE);

    if ((ptr = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(ptr, asize);
    return ptr;
    // int newsize = ALIGN(size + SIZE_T_SIZE);
    // void *p = mem_sbrk(newsize);
    // if (p == (void *)-1)
    // return NULL;
    // else {
    //     *(size_t *)p = size;
    //     return (void *)((char *)p + SIZE_T_SIZE);
    // }
}

static void *first_fit(size_t asize) {  // 처음부터 나보다 사이즈 큰 가용블록 찾기
    void *ptr;
    for(ptr = heap_listp; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if(!GET_ALLOC(HDRP(ptr)) && asize <= GET_SIZE(HDRP(ptr))) {
            return ptr;     // 가용블럭이면서 블럭 사이즈가 요청 사이즈보다 더 큰 경우
        }
    }
    return NULL;    // 맞는게 없음.
}

static void *next_fit(size_t asize) {   // 앞으로 쭉 가면서 찾기
    void *ptr;
    for(ptr = current_listp; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if(!GET_ALLOC(HDRP(ptr)) && asize <= GET_SIZE(HDRP(ptr))) {
            current_listp = ptr;
            return ptr;     // 가용블럭이면서 블럭 사이즈가 요청 사이즈보다 더 큰 경우
        }
    }
    return NULL;
}

static void *best_fit(size_t asize) {
    void *ptr, *min_ptr = NULL;
    for(ptr = heap_listp; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if(!GET_ALLOC(HDRP(ptr)) && asize <= GET_SIZE(HDRP(ptr))) {
            if(min_ptr == NULL) min_ptr = ptr;
            if(GET_SIZE(HDRP(ptr)) < GET_SIZE(HDRP(min_ptr)))
                min_ptr = ptr;
        }
    }
    if(min_ptr == NULL) return NULL;
    return min_ptr;
}

static void place(void *ptr, size_t asize) {    // 가용블록을 (할당블록 / 가용블록)으로 나눠주는 함수
    size_t csize = GET_SIZE(HDRP(ptr));     // 나눠먹을 수 있는 가용 블록

    if ((csize - asize) >= 2*DSIZE) {   // 내가 사용하고 남은 블록의 사이즈가 2더블워드보다 클 때
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1)); // 할당블록 해주시고~
        ptr = NEXT_BLKP(ptr);
        PUT(HDRP(ptr), PACK(csize-asize, 0));
        PUT(FTRP(ptr), PACK(csize-asize, 0));   // 사용하고 남은 가용 블록 해주시고
    }
    else {      // 내가 사용하고 남은 블록의 사이즈가 2더블워드 사이즈보다 작을 때
        PUT(HDRP(ptr), PACK(csize, 1));     
        PUT(FTRP(ptr), PACK(csize, 1));     // 그럼 패딩을 쳐서 끝까지
    }
}

/*
 * mm_free - Freeing a block does nothing.
 * ptr이 가리키는 블록을 해제함, 반환 값 x , 아직 해제되지 않은 블록에 경우만 작동이 보장
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

static void *coalesce(void *ptr) {      // 합체함수, 블록단위로 검사하니까 가운데에 남는 전 블록 푸터, 내 블록헤더 푸터, 뒷블록 헤더는 읽히지가 않아서 어차피 데이터 넣으면 갈아끼워짐
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    if (prev_alloc && next_alloc) { // 이전, 이후 모두 할당 되어 있는 경우 > 안 합침
        return ptr;
    }

    else if (prev_alloc && !next_alloc) { // 이전 할당, 이후 가용 > 이후랑만 합치면 됨.
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));  // 푸터는 헤더의 주소로 가져오니까 이렇게하면 끝을 가리킴
    }

    else if (!prev_alloc && next_alloc) { // 이전 가용, 이후 할당 > 이전이랑만 합치면 됨.
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));   // 헤더 앞으로 댕겨주고
        ptr = PREV_BLKP(ptr);   // 포인터 앞으로 옮겨주기
    }

    else {  // 이전, 이후 모두 가용 > 싹다 합쳐
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + 
            GET_SIZE(FTRP(NEXT_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }

    current_listp = ptr;
    return ptr;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 * 최소한 크기의 할당된 영역에 대한 포인터를 반환.
 * 포인터가 null인 경우 호출은 mm malloc(size)와 동일, 크기가 0이면 호출은 mm free(prt)과 동일
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}















