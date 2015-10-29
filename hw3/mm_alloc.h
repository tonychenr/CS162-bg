/*
 * mm_alloc.h
 *
 * Exports a clone of the interface documented in "man 3 malloc".
 */

#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void *mm_malloc(size_t size);
void *mm_realloc(void *ptr, size_t size);
void mm_free(void *ptr);

struct list_elem {
    struct list_elem *prev;
    struct list_elem *next;
    bool isFree;
    size_t size;
    char data[0];
};

struct list {
    struct list_elem head;
    struct list_elem tail;
};

static void list_init (struct list *list) {
    list->head.next = &list->tail;
    list->head.prev = NULL;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}

static struct list_elem *list_begin(struct list *list) {
    return list->head.next;
}

static struct list_elem *list_end(struct list *list) {
    return &list->tail;
}

static void list_insert(struct list_elem *before, struct list_elem *elem) {
    elem->prev = before->prev;
    elem->next = before;
    before->prev->next = elem;
    before->prev = elem;
}

static void list_insert_ascending(struct list *list, struct list_elem *elem) {
    struct list_elem *e;
    for (e = list_begin (list); e != list_end (list); e = e->next)
        if (e < elem)
            break;
    return list_insert (e, elem);
}

static void list_remove (struct list_elem *elem) {
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
}

static struct list_elem *getNextAvailableChunk(struct list *list, size_t size) {
    struct list_elem *e = NULL;
    for (e = list_begin (list); e != list_end (list); e = e->next)
        if (e->size >= size && e->isFree == true)
            return e;
    return NULL;
}

static struct list_elem *getChunk(struct list *list, void *ptr) {
    struct list_elem *e = NULL;
    for (e = list_begin (list); e != list_end (list); e = e->next)
        if (e->data == ptr)
            return e;
    return NULL;
}

