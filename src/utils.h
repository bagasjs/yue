/**
 * My C utilities
 *
 * Notes for myself:
 * 1. This utility provides macros to work with dynamic array and static array such as da_append, sa_append and
 *    arena_da_append. I found out that you want to wrap this operation into a function so in the future change
 *    is easier.
 */
#ifndef UTILS_H_
#define UTILS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef ASSERT
#include <assert.h>
#define ASSERT(COND) assert(COND)
#endif

#ifndef UNREACHABLE
#define UNREACHABLE(MSG) (0 && (MSG))
#endif

#ifndef DA_INIT_CAP
#define DA_INIT_CAP 256
#endif

typedef struct ArenaRegion ArenaRegion;
typedef struct Arena {
    ArenaRegion *begin;
    ArenaRegion *end;
} Arena;

void *arena_alloc(Arena *a, size_t bytesize);
void  arena_reset(Arena *a);
void  arena_destroy(Arena *a);
char *arena_strndup(Arena *a, const char *cstr, size_t cstrlen);
char *arena_strdup(Arena *a, const char *cstr);
char *arena_sprintf(Arena *a, const char *fmt, ...);
size_t arena_get_usage(Arena *a);

#define arena_da_reserve(arena, da, required_cap)                               \
    do {                                                                        \
        size_t item_size = sizeof(*(da)->items);                                \
        if(required_cap > (da)->capacity) {                                     \
            if((da)->capacity == 0) (da)->capacity = DA_INIT_CAP;               \
            while((da)->capacity < required_cap)                                \
                (da)->capacity *= 2;                                            \
            void *items = arena_alloc((arena), (da)->capacity * item_size);     \
            ASSERT(items != NULL && "Buy More RAM LOL!");                       \
            if((da)->items) memcpy(items, (da)->items, (da)->count*item_size);  \
            (da)->items = items;                                                \
        }                                                                       \
    } while(0)

#define arena_da_append(arena, da, item)                            \
    do {                                                            \
        arena_da_reserve((arena), (da), (da)->count + 1);           \
        (da)->items[(da)->count++] = (item);                        \
    } while(0)

void *bufcpy(void *dst, const void *src, size_t n);
void *bufset(void *dst, const int value, size_t n);
bool  cstreq(const char *a, const char *b);
size_t cstrlen(const char *cstr); // doesn't include the zero termination
size_t cstrsz(const char *cstr); // size includes the zero termination

// NOTE: Remove everything below
//       We should operate dynamic array also inside an arena

#include <stdio.h>
#if !defined(UTILS_MALLOC) && !defined(UTILS_FREE)
#include <stdlib.h>
#define UTILS_MALLOC malloc
#define UTILS_FREE   free
#endif
#if !defined(UTILS_MALLOC) || !defined(UTILS_FREE)
#error "Please define both UTILS_MALLOC and UTILS_FREE macros"
#endif

#define da_free(da) do {        \
        if((da)->items)         \
            free((da)->items);  \
        (da)->items    = 0;     \
        (da)->capacity = 0;     \
        (da)->count    = 0;     \
    } while(0)

#define da_reserve(da, required_cap)                                \
    do {                                                            \
        size_t item_size = sizeof(*(da)->items);                    \
        if(required_cap > (da)->capacity) {                         \
            if((da)->capacity == 0) (da)->capacity = DA_INIT_CAP;   \
            while((da)->capacity < required_cap)                    \
                (da)->capacity *= 2;                                \
            void *items = UTILS_MALLOC((da)->capacity * item_size); \
            ASSERT(items != NULL && "Buy More RAM LOL!");           \
            if((da)->items) {                                       \
                memcpy(items, (da)->items, (da)->count*item_size);  \
                UTILS_FREE((da)->items);                            \
            }                                                       \
            (da)->items = items;                                    \
        }                                                           \
    } while(0)

#define da_last_ptr(da) (&((da)->items[(da)->count - 1]))

#define da_append(da, item)                                         \
    do {                                                            \
        da_reserve((da), (da)->count + 1);                          \
        (da)->items[(da)->count++] = (item);                        \
    } while(0)

#define da_append_many(da, new_items, new_items_count)              \
    do {                                                            \
        size_t item_size = sizeof(*(da)->items);                    \
        da_reserve((da), (da)->count + new_items_count);            \
        memcpy((da)->items + (da)->count, (new_items),              \
            (new_items_count)*item_size);                           \
        (da)->count += (new_items_count);                           \
    } while(0)

#define da_remove_unordered(da, index)                     \
    do {                                                   \
        ASSERT((index) < (da)->count);                     \
        (da)->items[(index)] = (da)->items[--(da)->count]; \
    } while(0)

#define da_pop(da)          \
(                           \
ASSERT((da)),               \
ASSERT((da)->items),        \
ASSERT((da)->count >= 1),   \
(da)->items[--(da)->count]  \
)                           \


#define ARRLEN(xs) (sizeof(xs)/sizeof(*xs))

#define sa_pop(sa)          \
(                           \
ASSERT((sa)),               \
ASSERT((sa)->items),        \
ASSERT((sa)->count >= 1),   \
(sa)->items[--(sa)->count]  \
)                           \

#define sa_append(sa, item)             \
(                                       \
ASSERT((sa)),                           \
ASSERT((sa)->items),                    \
ASSERT((sa)->count < (sa)->capacity),   \
(sa)->items[(sa)->count++] = item       \
)
#define sa_append_many(sa, newitems, n_items)   \
(                                               \
ASSERT((sa)),                                   \
ASSERT((sa)->items),                            \
ASSERT((sa)->count < (sa)->capacity),           \
memcpy((sa)->items + (sa)->count, (newitems),   \
    (n_items)*sizeof(*(sa)->items)),            \
(sa)->count += (n_items)                        \
)

typedef struct StringBuilder {
    char *items;
    size_t count;
    size_t capacity;
} StringBuilder;

int sb_appendf(StringBuilder *sb, const char *fmt, ...);
#define sb_append_char(SB, CH) da_append((SB), (CH))

bool read_entire_file(const char *filepath, StringBuilder *sb);
bool write_entire_file(const char *filepath, const void *data, size_t datasize);

#endif // UTILS_H_
