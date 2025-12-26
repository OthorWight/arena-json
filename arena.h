/*
    arena.h - Single-file arena allocator with scratchpad support.
    Define ARENA_IMPLEMENTATION in one source file before including.
*/

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef ARENA_DEFAULT_BLOCK_SIZE
#define ARENA_DEFAULT_BLOCK_SIZE (8 * 1024) 
#endif

#ifndef ARENA_ALIGNMENT
#define ARENA_ALIGNMENT (2 * sizeof(void*)) 
#endif

/* --- Types --- */

typedef struct ArenaRegion ArenaRegion;

struct ArenaRegion {
    ArenaRegion *next;
    size_t capacity;
    size_t count;
    uint8_t data[];
};

typedef struct Arena {
    ArenaRegion *begin;
    ArenaRegion *end;
} Arena;

typedef struct ArenaTemp {
    Arena *arena;
    ArenaRegion *old_end;
    size_t old_count;
} ArenaTemp;

/* --- API --- */

#ifdef __cplusplus
extern "C" {
#endif

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);
void *arena_alloc_zero(Arena *a, size_t size);
void arena_reset(Arena *a);
void arena_free(Arena *a);
void arena_print_stats(const Arena *a);

/* Scope-based memory management */
ArenaTemp arena_temp_begin(Arena *a);
void arena_temp_end(ArenaTemp temp);

#ifdef __cplusplus
}
#endif

/* --- Helper Macros --- */

#define arena_alloc_struct(a, T) ((T*)arena_alloc(a, sizeof(T)))
#define arena_alloc_array(a, T, count) ((T*)arena_alloc(a, sizeof(T) * (count)))

#endif /* ARENA_H */

/* -------------------------------------------------------------------------- */

#ifdef ARENA_IMPLEMENTATION

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static uintptr_t arena__align_forward(uintptr_t ptr, size_t align) {
    uintptr_t p = ptr;
    uintptr_t a = (uintptr_t)align;
    uintptr_t modulo = p & (a - 1);
    if (modulo != 0) p += a - modulo;
    return p;
}

static ArenaRegion *arena__new_region(size_t capacity) {
    size_t size = sizeof(ArenaRegion) + capacity;
    ArenaRegion *r = (ArenaRegion *)malloc(size);
    if (!r) return NULL;
    r->next = NULL;
    r->capacity = capacity;
    r->count = 0;
    return r;
}

void arena_init(Arena *a) {
    a->begin = NULL;
    a->end = NULL;
}

void *arena_alloc(Arena *a, size_t size) {
    if (size == 0) return NULL;

    /* Initialize or Reuse Start */
    if (a->end == NULL) {
        if (a->begin != NULL) {
            a->end = a->begin;
            a->end->count = 0;
        } else {
            size_t cap = ARENA_DEFAULT_BLOCK_SIZE;
            if (size > cap) cap = size;
            a->begin = arena__new_region(cap);
            if (!a->begin) return NULL;
            a->end = a->begin;
        }
    }

    /* 1. Try to fit in the current block */
    uintptr_t curr_ptr = (uintptr_t)a->end->data + a->end->count;
    uintptr_t next_ptr = arena__align_forward(curr_ptr, ARENA_ALIGNMENT);
    size_t padding = next_ptr - curr_ptr;

    if (a->end->count + padding + size > a->end->capacity) {
        
        /* 2. Current block full. Look for a 'next' block that is big enough.
              We "Garbage Collect" small blocks that are no longer useful. */
        while (a->end->next != NULL) {
            ArenaRegion *next = a->end->next;
            
            /* Does the next block have enough capacity? 
               (We assume it will be empty since we are expanding into it) */
            size_t needed_cap = size + ARENA_ALIGNMENT; 
            
            if (next->capacity >= needed_cap) {
                /* Found a good block! Reuse it. */
                a->end = next;
                a->end->count = 0;
                
                curr_ptr = (uintptr_t)a->end->data;
                next_ptr = arena__align_forward(curr_ptr, ARENA_ALIGNMENT);
                padding = next_ptr - curr_ptr;
                goto ALLOC_PROCEED;
            } else {
                /* Block is too small. Delete it to save memory. */
                a->end->next = next->next; /* Unlink */
                free(next);                /* Free */
            }
        }

        /* 3. No valid next block found. Allocate a new one. */
        size_t new_cap = a->end->capacity * 2;
        if (size + ARENA_ALIGNMENT > new_cap) {
            new_cap = size + ARENA_ALIGNMENT;
        }
        if (new_cap < ARENA_DEFAULT_BLOCK_SIZE) new_cap = ARENA_DEFAULT_BLOCK_SIZE;

        ArenaRegion *next = arena__new_region(new_cap);
        if (!next) return NULL;

        next->next = NULL; /* New end */
        a->end->next = next;
        a->end = next;

        curr_ptr = (uintptr_t)a->end->data;
        next_ptr = arena__align_forward(curr_ptr, ARENA_ALIGNMENT);
        padding = next_ptr - curr_ptr;
    }

ALLOC_PROCEED:
    a->end->count += padding + size;
    return (void *)next_ptr;
}

void *arena_alloc_zero(Arena *a, size_t size) {
    void *ptr = arena_alloc(a, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void arena_reset(Arena *a) {
    /* Don't free, just rewind end to begin and reset count */
    if (a->begin) {
        a->end = a->begin;
        a->end->count = 0;
    } else {
        a->end = NULL;
    }
}

void arena_free(Arena *a) {
    ArenaRegion *curr = a->begin;
    while (curr) {
        ArenaRegion *next = curr->next;
        free(curr);
        curr = next;
    }
    a->begin = NULL;
    a->end = NULL;
}

void arena_print_stats(const Arena *a) {
    size_t total_cap = 0;
    size_t used = 0;
    size_t count = 0;
    ArenaRegion *curr = a->begin;
    while (curr) {
        total_cap += curr->capacity;
        used += curr->count;
        count++;
        curr = curr->next;
    }
    printf("Arena: %zu regions, %zu/%zu bytes used\n", count, used, total_cap);
}

ArenaTemp arena_temp_begin(Arena *a) {
    ArenaTemp temp;
    temp.arena = a;
    temp.old_end = a->end;
    temp.old_count = a->end ? a->end->count : 0;
    return temp;
}

void arena_temp_end(ArenaTemp temp) {
    temp.arena->end = temp.old_end;
    if (temp.arena->end) {
        temp.arena->end->count = temp.old_count;
    }
}

#endif /* ARENA_IMPLEMENTATION */
