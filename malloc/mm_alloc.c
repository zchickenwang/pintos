/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines.
 */

#include "mm_alloc.h"
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

struct metadata *create_metadata(bool free, int size, struct metadata *prev);
struct metadata *base = NULL;

void *mm_malloc(size_t size) {

    if (size == 0) {
        return NULL;
    }

    if (base == NULL) {
        // initialize metadata
        struct metadata *first = create_metadata(false, size, NULL);
        // set base
        base = first;
        return first == NULL ? NULL : memset(first->brr, 0, size);
    } else {
        struct metadata *lag;
        struct metadata *curr = base;
        while (curr != NULL) {
            if (curr->free && curr->size >= size) {
                // split if needed
                // TODO factor this out
                if (curr->size > size + sizeof(struct metadata)) {
                    char *move = (char *)curr;
                    move += size + sizeof(struct metadata);

                    struct metadata *split = (struct metadata *)move;
                    split->free = true;
                    split->size = curr->size - size - sizeof(struct metadata);
                    split->prev = curr;
                    split->next = curr->next;
                    curr->next = split;
                }
                curr->free = false;
                curr->size = size;
                return memset(curr->brr, 0, size);
            }
            lag = curr;
            curr = curr->next;
        }

        // add new one at end
        struct metadata *anewone = create_metadata(false, size, lag);
        lag->next = anewone;
        return anewone == NULL ? NULL : memset(anewone->brr, 0, size);
    }
}

struct metadata *create_metadata(bool free, int size, struct metadata *prev) {
    void *start = sbrk(size + sizeof(struct metadata));
    if (start == (void *)-1) {
        return NULL;
    }
    struct metadata *meta = (struct metadata *)start;
    meta->free = free;
    meta->size = size;
    meta->prev = prev;
    meta->next = NULL;
    return meta;
}

void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL && size == 0) {
        return NULL;
    } else if (ptr == NULL) {
        return mm_malloc(size);
    } else if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    // find ptr
    struct metadata *curr = base;
    while (curr != NULL) {
        if (curr->brr == ptr) {
            // malloc new size
            void *newspace = mm_malloc(size);
            if (newspace == NULL) {
                return NULL;
            }
            // copy over
            memcpy(newspace, curr->brr, MIN(curr->size, size));
            // free old
            mm_free(ptr);
            return newspace;
        }
        curr = curr->next;
    }
    // ptr not found
    return NULL;
}

void mm_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct metadata *curr = base;
    while (curr != NULL) {
        if (curr->brr == ptr) {
            curr->free = true;
            // coalesce to left
            // TODO new method for both left and right coalescing!
            struct metadata *cleft = curr->prev;
            while (cleft != NULL && cleft->free) {
                cleft->size += curr->size + sizeof(struct metadata);
                cleft->next = curr->next;
                if (curr->next != NULL) {
                    curr->next->prev = cleft;
                }
                curr = cleft;
                cleft = curr->prev;
            }
            // coalesce to right
            struct metadata *cright = curr->next;
            while (cright != NULL && cright->free) {
                curr->size += cright->size + sizeof(struct metadata);
                curr->next = cright->next;
                if (cright->next != NULL) {
                    cright->next->prev = curr;
                }
                cright = curr->next;
            }
            return;
        }
        curr = curr->next;
    }
    // memory not found
    return;
}
