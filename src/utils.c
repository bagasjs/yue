#include "utils.h" // nob = skip_line
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

struct ArenaRegion {
    ArenaRegion *next;
    size_t count;
    size_t capacity;
    uintptr_t data[];
};

static ArenaRegion *new_region(size_t requested_capacity)
{
    size_t capacity = 4096; // default capacity
    if(capacity < requested_capacity) capacity = requested_capacity;
    size_t allocated_bytes = sizeof(ArenaRegion) + sizeof(uintptr_t) * capacity;
    ArenaRegion *region = (ArenaRegion*)malloc(allocated_bytes);
    ASSERT(region != NULL && "failed to allocate a region");
    memset(region, 0, sizeof(*region));
    region->capacity = capacity;
    return region;
}

static void free_region(ArenaRegion *region)
{
    if(region) free(region);
}

void *arena_alloc(Arena *arena, size_t bytesize)
{
    size_t size = (bytesize + sizeof(uintptr_t) - 1)/sizeof(uintptr_t);

    ASSERT(arena && "invalid pointer to arena");
    if(arena->end == NULL) {
        ASSERT(arena->begin == NULL && "invalid arena");
        arena->begin = new_region(size);
        arena->end   = arena->begin;
    }

    while(arena->end->count + size > arena->end->capacity && arena->end->next != NULL) {
        arena->end = arena->end->next;
    }

    if(arena->end->count + size > arena->end->capacity) {
        ASSERT(arena->end->next == NULL);
        ArenaRegion *region = new_region(size);
        arena->end->next = region;
        arena->end = region;
    }

    void *result = &arena->end->data[arena->end->count];
    arena->end->count += size;
    memset(result, 0, size * sizeof(uintptr_t));
    return result;
}

void arena_reset(Arena *arena)
{
    ASSERT(arena && "invalid pointer to arena");
    for(ArenaRegion *region = arena->begin; region != NULL; region = region->next) {
        region->count = 0;
    }
    arena->end = arena->begin;
}

void arena_destroy(Arena *arena)
{
    ASSERT(arena && "invalid pointer to arena");
    ArenaRegion *region = arena->begin;
    while(region) {
        ArenaRegion *region_next = region->next;
        free_region(region);
        region = region_next;
    }
    arena->begin = NULL;
    arena->end   = NULL;
}

char *arena_strndup(Arena *a, const char *cstr, size_t cstrlen)
{
    char *result = arena_alloc(a, cstrlen + 1);
    result[cstrlen] = 0;
    memcpy(result, cstr, cstrlen);
    return result;
}

char *arena_strdup(Arena *a, const char *cstr)
{
    return arena_strndup(a, cstr, strlen(cstr));
}

char *arena_sprintf(Arena *a, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    char *dst = arena_alloc(a, n + 1);
    dst[n] = 0;
    va_start(args, fmt);
    vsnprintf(dst, n+1, fmt, args);
    va_end(args);
    return dst;
}

size_t arena_get_usage(Arena *a)
{
    size_t result = 0;
    for (ArenaRegion *r = a->begin; r != NULL; r = r->next) {
        result += r->count;
    }
    return result;
}

int cstrcmp(const char *a, const char *b)
{
    size_t i = 0;
    while(true) {
        int c = a[i] - b[i];
        if(c != 0) return c;
        if(a[i] == 0) break; 
        i += 1;
    }
    return 0;
}

size_t cstrlen(const char *cstr)
{
    size_t n = 0;
    for(; cstr[n] != 0; ++n);
    return n;
}

bool cstreq(const char *a, size_t a_len, const char *b, size_t b_len)
{
    if(a_len != b_len) return false;
    for(size_t i = 0; i < a_len; ++i) {
        if(a[i] != b[i]) return false;
    }
    return true;
}

void *bufcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for(size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *bufset(void *dst, int value, size_t n)
{
    uint8_t *d = dst;
    for(size_t i = 0; i < n; ++i) {
        d[i] = value;
    }
    return dst;
}

#ifdef NDEBUG
#define DEBUGLOG(...)
#else
#define DEBUGLOG(...) fprintf(stderr, __VA_ARGS__)
#endif

int sb_appendf(StringBuilder *sb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    da_reserve(sb, sb->count + n + 1);
    char *dst = sb->items + sb->count;
    va_start(args, fmt);
    vsnprintf(dst, n+1, fmt, args);
    va_end(args);
    sb->count += n;
    return n;
}

bool read_entire_file(const char *filepath, StringBuilder *sb)
{
    FILE *f = fopen(filepath, "rb");
    if(!f) {
        DEBUGLOG("error: Could not open a file '%s'\n", filepath);
        return false;
    }
    if (fseek(f, 0, SEEK_END) < 0) {
        DEBUGLOG("error: Could not seek into file\n");
        fclose(f);
        return false;
    }
    int64_t fsz = ftell(f);
    if (fsz <  0) {
        DEBUGLOG("error: Could not get the file size\n");
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) < 0) {
        DEBUGLOG("error: Could not seek into file\n");
        fclose(f);
        return false;
    }

    da_reserve(sb, sb->count + fsz + 1);
    fread(sb->items + sb->count, fsz, 1, f);
    sb->items[sb->count + fsz] = 0;
    if (ferror(f)) {
        DEBUGLOG("error: Could not read into file\n");
        fclose(f);
        return false;
    }
    sb->count += fsz;
    fclose(f);
    return true;
}

bool write_entire_file(const char *filepath, const void *data, size_t size)
{
    FILE *f = fopen(filepath, "wb");
    if(!f) {
        DEBUGLOG("error: Could not open a file '%s'\n", filepath);
        return false;
    }

    const char *buf = data;
    while (size > 0) {
        size_t n = fwrite(buf, 1, size, f);
        if (ferror(f)) {
            DEBUGLOG("error: Could not write into file '%s': %s\n", filepath, strerror(errno));
            fclose(f);
            return false;
        }
        size -= n;
        buf  += n;
    }

    fclose(f);
    return true;
}
