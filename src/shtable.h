/**
 *
 * shtable.h - A simple string hashtable implementation in C.
 *
 */

#ifndef SHTABLE_H_
#define SHTABLE_H_

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef SHTABLE_INIT_CAP
#define SHTABLE_INIT_CAP 32
#endif

#if !defined(SHT_MALLOC) && !defined(SHT_FREE)
#include <stdlib.h>
#define SHT_MALLOC malloc
#define SHT_FREE   free
#endif
#if !defined(SHT_ASSERT)
#include <assert.h>
#define SHT_ASSERT assert
#endif

typedef enum shtable_entry_state {
    SHTABLE_ENTRY_EMPTY = 0,
    SHTABLE_ENTRY_USED,
    SHTABLE_ENTRY_TOMBSTONE,
} shtable_entry_state_t;

typedef struct shtable_entry shtable_entry_t;
struct shtable_entry {
    shtable_entry_state_t state;
    const char *key;
    void *value;
};

typedef struct {
    shtable_entry_t *items;
    size_t capacity;
    size_t count;
} shtable_t;

void  shtable_reset(shtable_t *T);
void  shtable_remove(shtable_t *T, const char *key);
void  shtable_set(shtable_t *T, const char *key, void *value);
void *shtable_get(shtable_t *T, const char *key);
void *shtable_get_or(shtable_t *T, const char *key, void *default_value);
bool  shtable_has(shtable_t *T, const char *key);
int64_t shtable_geti(shtable_t *T, const char *key);

#endif // SHTABLE_H_

#ifdef SHTABLE_IMPLEMENTATION

#include <stdint.h>

#define HASH_FUNC fnv1a_hash

uint64_t debug_hash(const char *key) {
    (void)key;
    return 0;  // always collide
}

uint64_t fnv1a_hash(const char *key) {
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    while (*key) {
        hash ^= (unsigned char)(*key++);
        hash *= 1099511628211ULL; // FNV prime
    }
    return hash;
}

void shtable_reset(shtable_t *T)
{
    memset(T->items, 0, T->capacity * sizeof(*T->items));
    T->count = 0;
}

void shtable_set(shtable_t *T, const char *key, void *value)
{
    if(!T->items) {
        T->capacity = SHTABLE_INIT_CAP;
        T->items    = (void*)SHT_MALLOC(T->capacity * sizeof(*T->items));
        memset(T->items, 0, T->capacity * sizeof(*T->items));
        T->count    = 0;
    }

    // Upsizing 
    if(T->count >= T->capacity * 0.7) {
        shtable_entry_t *old  = T->items;
        size_t old_capacity = T->capacity;
        while(T->count >= T->capacity * 0.7) T->capacity *= 2;
        T->items = (void*)SHT_MALLOC(T->capacity * sizeof(*T->items));
        T->count = 0;
        memset(T->items, 0, T->capacity * sizeof(*T->items));
        for(size_t i = 0; i < old_capacity; ++i) {
            if(old[i].state == SHTABLE_ENTRY_USED) shtable_set(T, old[i].key, old[i].value);
        }
        SHT_FREE(old);
    }

    uint64_t hash = HASH_FUNC(key);
    uint64_t begin = hash % T->capacity;
    int64_t  first_tombstone = -1;
    // Linear probing
    for(size_t i = 0; i < T->capacity; ++i) {
        size_t slot = (begin + i) % T->capacity;
        shtable_entry_t *entry = &T->items[slot];
        /*if(entry->state != SHTABLE_ENTRY_USED) { */
        /*    entry->state = SHTABLE_ENTRY_USED; */
        /*    entry->key = key; */
        /*    entry->value = value; */
        /*    T->count += 1; */
        /*    return; */
        /*} */
        /*if(strcmp(entry->key, key) == 0) { */
        /*    entry->value = value; */
        /*    return; */
        /*}*/

        if(entry->state == SHTABLE_ENTRY_EMPTY) {
            uint64_t insert_slot = (first_tombstone >= 0) ? (uint64_t)first_tombstone : slot;
            shtable_entry_t *ins = &T->items[insert_slot];
            ins->state = SHTABLE_ENTRY_USED;
            ins->key = key;
            ins->value = value;
            T->count += 1;
            return;
        }
        if(entry->state == SHTABLE_ENTRY_TOMBSTONE) {
            if(first_tombstone < 0) first_tombstone = (int64_t)slot;
            continue;
        }
        if(strcmp(entry->key, key) == 0) {
            entry->value = value;
            return;
        }
    }
}

shtable_entry_t *shtable__find_entry(shtable_t *T, const char *key)
{
    if(T->capacity == 0) return NULL;
    uint64_t hash  = HASH_FUNC(key);
    uint64_t begin = hash % T->capacity;
    for(uint64_t i = 0; i < T->capacity; ++i) {
        uint64_t slot = (begin + i) % T->capacity;
        shtable_entry_t *entry = &T->items[slot];
        if(entry->state == SHTABLE_ENTRY_EMPTY) {
            return NULL;
        } else {
            bool is_key_equal = strcmp(entry->key, key) == 0;
            if(entry->key && is_key_equal && entry->state == SHTABLE_ENTRY_USED) {
                return entry;
            }
        }
    }
    return NULL;
}

int64_t shtable_geti(shtable_t *T, const char *key)
{
    shtable_entry_t *entry = shtable__find_entry(T, key);
    if(!entry) return -1;
    return ((int64_t)((uint64_t)entry - (uint64_t)T->items))/sizeof(T->items[0]);
}

void shtable_remove(shtable_t *T, const char *key)
{
    shtable_entry_t *entry = shtable__find_entry(T, key);
    if(entry) {
        entry->state = SHTABLE_ENTRY_TOMBSTONE;
        if(T->count > 0) T->count -= 1;
    }
}

void *shtable_get(shtable_t *T, const char *key)
{
    shtable_entry_t *entry = shtable__find_entry(T, key);
    if(!entry) return NULL;
    return entry->value;
}

void *shtable_get_or(shtable_t *T, const char *key, void *default_value)
{
    shtable_entry_t *entry = shtable__find_entry(T, key);
    if(!entry) return default_value;
    return entry->value;
}

bool shtable_has(shtable_t *T, const char *key)
{
    shtable_entry_t *entry = shtable__find_entry(T, key);
    return entry != NULL;
}


#endif
