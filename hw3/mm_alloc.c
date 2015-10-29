/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines.
 */

#include "mm_alloc.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

struct list *mem_chunks = NULL;

void mem_init(struct list* list) {
    if (list == NULL) {
        mem_chunks = sbrk(sizeof(struct list));
        list_init(mem_chunks);
    }
}

void *mm_malloc(size_t size) {
    /* YOUR CODE HERE */
    mem_init(mem_chunks);
    if (size <= 0) {
        return NULL;
    } else {
        struct list_elem *nextChunk = getNextAvailableChunk(mem_chunks, size);
        if (nextChunk == NULL) {
            void* oldbrk = sbrk(size + sizeof(struct list_elem));
            if (oldbrk == (void *) -1) {
                return NULL;
            }
            struct list_elem *newChunk;
            newChunk = oldbrk;
            newChunk->isFree = false;
            newChunk->size = size;
            list_insert_ascending(mem_chunks, newChunk);
            nextChunk = newChunk;
            memset(nextChunk->data, 0, size);
            return nextChunk->data;
        } else {
            if (nextChunk->size > size + sizeof(struct list_elem)) {
                nextChunk->size = nextChunk->size - size - sizeof(struct list_elem);
                struct list_elem *newChunk = nextChunk + sizeof(struct list_elem) + nextChunk->size;
                newChunk->isFree = true;
                newChunk->size = size;
                list_insert_ascending(mem_chunks, newChunk);
                memset(nextChunk->data, 0, nextChunk->size);
                memset(nextChunk->data, 0, newChunk->size);
                return newChunk->data;
            } else {
                nextChunk->isFree = false;
                memset(nextChunk->data, 0, nextChunk->size);
                return nextChunk->data;
            }
        }
    }
}

void *mm_realloc(void *ptr, size_t size) {
    /* YOUR CODE HERE */
    if (ptr == NULL) {
        return mm_malloc(size);
    }

    mem_init(mem_chunks);
    struct list_elem *oldChunk = getChunk(mem_chunks, ptr);
    if (oldChunk != NULL && oldChunk->size >= size) {
        return oldChunk->data;
    }

    void *newBlock = mm_malloc(size);
    if (newBlock != NULL) {
        memcpy(newBlock, oldChunk->data, size);
        mm_free(ptr);
        return newBlock;
    }

    return NULL;
}

void mm_free(void *ptr) {
    /* YOUR CODE HERE */
    mem_init(mem_chunks);
    if (ptr != NULL) {
        struct list_elem *chunk = getChunk(mem_chunks, ptr);
        if (chunk == NULL) {
            return;
        }
        struct list_elem *prevChunk = chunk->prev;
        struct list_elem *nextChunk = chunk->next;
        if (chunk != list_begin(mem_chunks) && prevChunk->isFree == true) {
            prevChunk->size += sizeof(struct list_elem) + chunk->size;
            list_remove(chunk);
            chunk = prevChunk;
        }

        if (chunk->next != list_end(mem_chunks) && nextChunk->isFree == true) {
            chunk->size += sizeof(struct list_elem) + nextChunk->size;
            list_remove(nextChunk);
            chunk->isFree = true;
        }
    }
}
