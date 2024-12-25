#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include "debug.h"
#include "fmath.h"

typedef struct freelist_s {
    int num;
    int size;
    atomic_uint_fast64_t used;
    void* data[0];
} freelist_t;

// Given an integer which is a power of 2, return its log2 (index of the set bit)
static int pow2_index(uint64_t pow2)
{
    return (BITCAST_F2I((float)pow2) >> 23) - 126;
}

freelist_t *freelist_create(int elem_size, int max_elem) 
{
    assertf(max_elem <= 64, "freelist supports only up to 64 elements");
    freelist_t *list = malloc(sizeof(freelist_t) + max_elem * sizeof(void*));
    list->num = max_elem;
    list->size = elem_size;
    list->used = 0;
    return list;
}

void* freelist_alloc(freelist_t *list)
{
    while (1) {
        uint64_t used = list->used;
        if (used == 0) break;

        uint64_t mask = used & -used; // Find the rightmost 1
        if (atomic_fetch_and(&list->used, ~mask) & mask)
            return list->data[pow2_index(mask)];
    }

    return malloc(list->size);
}

void freelist_free(freelist_t *list, void *elem)
{
    while (1) {
        uint64_t used = list->used;
        if (used == (1 << list->num) - 1) break;
        
        uint64_t mask = ~used & (used+1); // Find the rightmost 0
        if (!(atomic_fetch_or(&list->used, mask) & mask)) {
            list->data[pow2_index(mask)] = elem;
            return;
        }
    }

    free(list);
}

void freelist_flush(freelist_t *list)
{
    for (int i=0; i<list->num; i++) {
        uint64_t mask = 1ull << i;
        if (atomic_fetch_and(&list->used, ~mask) & mask)
            free(list->data[i]);
    }
}

void freelist_destroy(freelist_t *list)
{
    freelist_flush(list);
    free(list);
}
