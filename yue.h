#ifndef YUE_H_
#define YUE_H_

#include <stddef.h>
#include <stdbool.h>

#ifndef YUE_STACK_CAP
#define YUE_STACK_CAP 256
#endif

typedef struct yue_Context yue_Context; 
typedef struct yue_Object yue_Object;
typedef yue_Object *(*yue_CFunc)(yue_Context *ctx, yue_Object *arg);

typedef double yue_Number;

typedef enum {
    YUE_OBJECT_NIL,
    YUE_OBJECT_NUMBER,
    YUE_OBJECT_PAIR,
    YUE_OBJECT_STRING,
    YUE_OBJECT_SYMBOL,
    YUE_OBJECT_CFUNC,
} yue_ObjectType;

// recommended bufsz is 64KB
yue_Context *yue_open(void *buf, size_t bufsz);
yue_Object *yue_read(yue_Context *ctx, const char *source, size_t len);
yue_Object *yue_eval(yue_Context *ctx, yue_Object *obj);
yue_Object *yue_get(yue_Context *ctx, yue_Object *sym);
void yue_set(yue_Context *ctx, yue_Object *sym, yue_Object *value);

// Object constructor
yue_Object *yue_nil(yue_Context *ctx);
yue_Object *yue_number(yue_Context *ctx, yue_Number number);
yue_Object *yue_pair(yue_Context *ctx, yue_Object *head, yue_Object *tail);
yue_Object *yue_list(yue_Context *ctx, yue_Object **objs, size_t count);
yue_Object *yue_string_sized(yue_Context *ctx, const char *cstr, size_t n);
yue_Object *yue_string(yue_Context *ctx, const char *cstr);
yue_Object *yue_symbol(yue_Context *ctx, const char *name);

// Object accessor
bool yue_isnil(yue_Object *obj);
yue_Number yue_tonumber(yue_Context *ctx, yue_Object *obj);
size_t yue_getstringlen(yue_Context *ctx, yue_Object *obj);
char *yue_tostring(yue_Context *ctx, yue_Object *obj, char *dst, size_t dstsz);

yue_Object *yue_nextarg(yue_Context *ctx, yue_Object **p_arg);
yue_Object *yue_cfunc(yue_Context *ctx, yue_CFunc cfunc);

// Builtin functions
yue_Object *yue_builtin_print(yue_Context *ctx, yue_Object *arg);
yue_Object *yue_builtin_add(yue_Context *ctx, yue_Object *arg);
yue_Object *yue_builtin_dolist(yue_Context *ctx, yue_Object *arg);
yue_Object *yue_builtin_assign(yue_Context *ctx, yue_Object *arg);

#endif // YUE_H_

#ifdef YUE_IMPLEMENTATION
#undef YUE_IMPLEMENTATION

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YUE_STRING_DATA_SIZE 16
struct yue_Object {
    yue_ObjectType type;
    yue_Object *next;
    bool marked;
    union {
        yue_Number as_number;
        yue_CFunc  as_cfunc;
        struct {
            yue_Object *head;
            yue_Object *tail;
        } as_pair;
        struct {
            char data[YUE_STRING_DATA_SIZE];
            yue_Object *tail;
        } as_str;
        struct {
            char name[YUE_STRING_DATA_SIZE];
            yue_Object *value;
        } as_symbol;
    };
};

struct yue_Context {
    yue_Object *stack[YUE_STACK_CAP];
    size_t stack_size;

    yue_Object *nil;

    yue_Object *free_list;
    yue_Object *sym_list;
    yue_Object *objects;
    size_t count_objects;
};

void yue_error(yue_Context *ctx, const char *message)
{
    (void)ctx;
    fprintf(stderr, "ERROR: %s\n", message);
    exit(EXIT_FAILURE);
}

yue_Context *yue_open(void *buf, size_t bufsz)
{
    yue_Context *ctx;
    memset(buf, 0, bufsz);
    ctx    = buf;
    buf    = (char*)buf + sizeof(*ctx);
    bufsz -= sizeof(*ctx);

    yue_Object *nil = buf;
    nil->type = YUE_OBJECT_NIL;
    ctx->nil  = nil;
    buf    = (char*)buf + sizeof(*nil);
    bufsz -= sizeof(*nil);

    ctx->objects       = (yue_Object*)buf;
    ctx->count_objects = bufsz / sizeof(*ctx->objects);

    for(size_t i = 0; i < ctx->count_objects; ++i) {
        yue_Object *obj = &ctx->objects[i];
        obj->next = NULL;
        if(ctx->free_list == NULL) {
            ctx->free_list = obj;
        } else {
            obj->next = ctx->free_list;
            ctx->free_list = obj;
        }
    }
    return ctx;
}

yue_Object *yue_eval(yue_Context *ctx, yue_Object *obj)
{
    switch(obj->type) {
        case YUE_OBJECT_NIL:
        case YUE_OBJECT_NUMBER:
        case YUE_OBJECT_CFUNC:
        case YUE_OBJECT_STRING:
            return obj;
        case YUE_OBJECT_SYMBOL:
            return obj->as_symbol.value;
        case YUE_OBJECT_PAIR:
            {
                yue_Object *fn  = obj->as_pair.head;
                yue_Object *arg = obj->as_pair.tail;
                fn = yue_eval(ctx, fn);
                switch(fn->type) {
                case YUE_OBJECT_CFUNC:
                    return fn->as_cfunc(ctx, arg);
                default:
                    yue_error(ctx, "Invoking non callable object");
                    return obj;
                }
            } break;
        default:
            return obj;
    }
}

void yue_set(yue_Context *ctx, yue_Object *sym, yue_Object *value)
{
    if(sym->type != YUE_OBJECT_SYMBOL) yue_error(ctx, "set require the first argument to be symbol\n");
    sym->as_symbol.value = value;
}

yue_Object *yue_get(yue_Context *ctx, yue_Object *sym)
{
    if(sym->type != YUE_OBJECT_SYMBOL) yue_error(ctx, "set require the first argument to be symbol\n");
    return sym->as_symbol.value;
}

static size_t gc_run_idx = 0;

static const char *type_names[] = {
    "YUE_OBJECT_NIL",
    "YUE_OBJECT_NUMBER",
    "YUE_OBJECT_PAIR",
    "YUE_OBJECT_STRING",
    "YUE_OBJECT_SYMBOL",
    "YUE_OBJECT_CFUNC",
};

static void mark(yue_Context *ctx, yue_Object *obj)
{
    assert(obj && "Invalid object");
    if(obj->marked) return;

    obj->marked = true;
    if(obj->type == YUE_OBJECT_PAIR) {
        mark(ctx, obj->as_pair.head);
        mark(ctx, obj->as_pair.tail);
    } else if(obj->type == YUE_OBJECT_STRING) {
        yue_Object *tail = obj->as_str.tail;
        if(tail) mark(ctx, tail);
    }
}

static void mark_all(yue_Context *ctx)
{
    for(size_t i = 0; i < ctx->stack_size; ++i) {
        mark(ctx, ctx->stack[i]);
    }
    yue_Object *obj = ctx->sym_list;
    while(obj) {
        mark(ctx, obj);
        mark(ctx, obj->as_symbol.value);
        obj = obj->next;
    }
}

static void sweep(yue_Context *ctx)
{
    ctx->free_list = NULL;
    for(size_t i = 0; i < ctx->count_objects; ++i) {
        yue_Object *obj = &ctx->objects[i];
        if(obj->marked) {
            obj->marked = false;
        } else {
            obj->next = ctx->free_list;
            ctx->free_list = obj;
        }
    }
}

void yue_rungc(yue_Context *ctx)
{
    mark_all(ctx);
    sweep(ctx);
    gc_run_idx += 1;
}

size_t yue_savegc(yue_Context *ctx)
{
    return ctx->stack_size;
}

void yue_restoregc(yue_Context *ctx, size_t gc)
{
    ctx->stack_size = gc;
}

void yue_pushgc(yue_Context *ctx, yue_Object *obj)
{
    assert(obj && "Invalid object to push");
    if(ctx->stack_size >= YUE_STACK_CAP) yue_error(ctx, "Stack overflow!");
    ctx->stack[ctx->stack_size++] = obj;
}

static yue_Object *new_object(yue_Context *ctx, yue_ObjectType type)
{
    if(ctx->free_list == NULL) {
        yue_rungc(ctx);
        if(ctx->free_list == NULL) {
            yue_error(ctx, "Could not allocate more objects");
            return NULL;
        }
    }
    yue_Object *result = ctx->free_list;
    result->type = type;
    ctx->free_list = ctx->free_list->next;
    return result;
}

yue_Object *yue_nil(yue_Context *ctx)
{
    yue_Object *obj = ctx->nil;
    return obj;
}

yue_Object *yue_number(yue_Context *ctx, yue_Number number)
{
    yue_Object *obj = new_object(ctx, YUE_OBJECT_NUMBER);
    obj->as_number = number;
    yue_pushgc(ctx, obj);
    return obj;
}

yue_Object *yue_pair(yue_Context *ctx, yue_Object *head, yue_Object *tail)
{
    yue_Object *obj = new_object(ctx, YUE_OBJECT_PAIR);
    obj->as_pair.head = head;
    obj->as_pair.tail = tail;
    yue_pushgc(ctx, obj);
    return obj;
}

yue_Object *yue_list(yue_Context *ctx, yue_Object **objs, size_t count) 
{
    yue_Object *res = yue_nil(ctx);
    while(count--) {
        res = yue_pair(ctx, objs[count], res);
    }
    yue_pushgc(ctx, res);
    return res;
}

yue_Object *yue_symbol(yue_Context *ctx, const char *name)
{
    size_t name_len = strlen(name);
    if(name_len + 1 > YUE_STRING_DATA_SIZE) yue_error(ctx, "symbol name is too long\n");
    yue_Object *obj = ctx->sym_list;
    while(obj) {
        if(strcmp(obj->as_symbol.name, name) == 0) {
            yue_pushgc(ctx, obj);
            return obj;
        }
        obj = obj->next;
    }

    obj = new_object(ctx, YUE_OBJECT_SYMBOL);
    memcpy(obj->as_symbol.name, name, name_len);
    obj->as_symbol.value = yue_nil(ctx);
    obj->next = ctx->sym_list;
    ctx->sym_list = obj;
    yue_pushgc(ctx, obj);
    return obj;
}

yue_Object *yue_string_sized(yue_Context *ctx, const char *cstr, size_t n)
{
    yue_Object *root = NULL;
    yue_Object *obj = NULL;
    for(size_t i = 0; i < n + 1; ++i) {
        if(i % YUE_STRING_DATA_SIZE == 0) {
            yue_Object *newobj = new_object(ctx, YUE_OBJECT_STRING);
            memset(&newobj->as_str, 0, sizeof(newobj->as_str));
            yue_pushgc(ctx, newobj);
            if(!root) {
                root = newobj;
                obj  = newobj;
            } else {
                obj->as_str.tail = newobj;
                obj = newobj;
            }
        }
        // the n'th char is null terminator
        if(i < n) obj->as_str.data[i % YUE_STRING_DATA_SIZE] = cstr[i];
    }
    return root;
}

yue_Object *yue_string(yue_Context *ctx, const char *cstr)
{
    return yue_string_sized(ctx, cstr, strlen(cstr));
}

yue_Object *yue_cfunc(yue_Context *ctx, yue_CFunc cfunc)
{
    yue_Object *obj = new_object(ctx, YUE_OBJECT_CFUNC);
    obj->as_cfunc = cfunc;
    yue_pushgc(ctx, obj);
    return obj;
}

yue_Object *yue_nextarg(yue_Context *ctx, yue_Object **p_arg)
{
    (void)ctx;
    yue_Object *arg = *p_arg;
    if(arg->type != YUE_OBJECT_PAIR) return arg;
    yue_Object *res = arg->as_pair.head;
    *p_arg = arg->as_pair.tail;
    return res;
}

bool yue_isnil(yue_Object *obj)
{
    return obj->type == YUE_OBJECT_NIL;
}

size_t yue_getstringlen(yue_Context *ctx, yue_Object *obj)
{
    if(obj->type != YUE_OBJECT_STRING) yue_error(ctx, "Expected a string");
    size_t length = 0;
    yue_Object *curr = obj;
    while(curr) {
        yue_Object *tail = curr->as_str.tail;
        if(tail) {
            length += YUE_STRING_DATA_SIZE;
        } else {
            size_t i = 0;
            while(curr->as_str.data[i++] != 0) length++;
        }
        curr = tail;
    }
    return length;
}

char *yue_tostring(yue_Context *ctx, yue_Object *obj, char *dst, size_t dstsz)
{
    if(obj->type != YUE_OBJECT_STRING) yue_error(ctx, "Expected a string");
    char *base = dst;
    size_t baselen = dstsz;
    yue_Object *curr = obj;
    while(curr) {
        yue_Object *tail = curr->as_str.tail;
        if(tail) {
            size_t n = dstsz < YUE_STRING_DATA_SIZE ? dstsz : YUE_STRING_DATA_SIZE;
            memcpy(dst, curr->as_str.data, n);
            dstsz -= n;
        } else {
            size_t i = 0;
            while(curr->as_str.data[i++] != 0) dstsz--;
        }
        curr = tail;
    }
    base[baselen - 1] = 0;
    return base;
}

yue_Number yue_tonumber(yue_Context *ctx, yue_Object *obj)
{
    if(obj->type != YUE_OBJECT_NUMBER) yue_error(ctx, "Expected a number");
    return obj->as_number;
}


/////////////////////////
///
/// built-in functions
///

static void print_object_inner(yue_Object *obj, int level)
{
    switch(obj->type) {
        case YUE_OBJECT_NIL:
            printf("<nil>");
            break;
        case YUE_OBJECT_CFUNC:
            printf("<cfunc>");
            break;
        case YUE_OBJECT_NUMBER:
            printf("%f", obj->as_number);
            break;
        case YUE_OBJECT_SYMBOL:
            printf("%.*s", YUE_STRING_DATA_SIZE, obj->as_symbol.name);
            break;
        case YUE_OBJECT_STRING:
            {
                yue_Object *curr = obj;
                while(curr) {
                    yue_Object *tail = curr->as_str.tail;
                    if(tail) {
                        printf("%.*s", YUE_STRING_DATA_SIZE, curr->as_str.data);
                    } else {
                        printf("%s", curr->as_str.data);
                    }
                    curr = tail;
                }
            } break;
        case YUE_OBJECT_PAIR:
            printf("(");
            print_object_inner(obj->as_pair.head, level + 1);
            printf(" . ");
            print_object_inner(obj->as_pair.tail, level + 1);
            printf(")");
        default:
            break;
    }
}

yue_Object *yue_builtin_print(yue_Context *ctx, yue_Object *arg)
{
    yue_Object *a = NULL;
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        print_object_inner(yue_eval(ctx, a), 0);
        printf(" ");
    }
    printf("\n");
    return yue_nil(ctx);
}

yue_Object *yue_builtin_add(yue_Context *ctx, yue_Object *arg)
{
    yue_Number result = 0;
    yue_Object *a = NULL;
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        result += yue_tonumber(ctx, yue_eval(ctx, a));
    }
    return yue_number(ctx, result);
}

yue_Object *yue_builtin_dolist(yue_Context *ctx, yue_Object *arg) 
{
    yue_Object *a = NULL;
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        size_t gc = yue_savegc(ctx);
        a = yue_eval(ctx, a);
        yue_restoregc(ctx, gc);
    }
    yue_pushgc(ctx, arg);
    return a;
}

yue_Object *yue_builtin_assign(yue_Context *ctx, yue_Object *arg) 
{
    yue_Object *symbol = yue_nextarg(ctx, &arg);
    yue_Object *value  = yue_eval(ctx, yue_nextarg(ctx, &arg));
    yue_set(ctx, symbol, value);
    return yue_nil(ctx);
}

static inline bool _isdigit(int c) { return '0' <= c && c <= '9'; }
static inline bool _isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

typedef struct Source {
    const char *data;
    const char *eof;
} Source;

static yue_Object *_read(yue_Context *ctx, Source *source)
{
    while(_isspace(*source->data)) source->data++;
    if(source->data >= source->eof) return yue_nil(ctx);
    if(*source->data == '"') {
        source->data++;
        const char *start = source->data;
        while(source->data < source->eof && *source->data != '"') source->data++;
        size_t size = source->data - start;
        source->data++;
        return yue_string_sized(ctx, start, size);
    } else if(_isdigit(*source->data)) {
        yue_Number val = 0;
        while(source->data < source->eof) {
            if(_isdigit(*source->data)) {
                val *= 10;
                val += *source->data - '0';
                source->data++;
            } else {
                break;
            }
        }
        return yue_number(ctx, val);
    } else if(*source->data == '(') {
        source->data++;
        yue_Object *r =  _read(ctx, source);
        yue_Object *root = yue_pair(ctx, r, yue_nil(ctx));
        yue_Object *prev = root;
        while(*source->data != ')') {
            yue_Object *r = _read(ctx, source);
            yue_Object *curr = yue_pair(ctx, r, yue_nil(ctx));
            prev->as_pair.tail = curr;
            prev = curr;
        }
        source->data++;
        return root;
    } else {
        size_t i = 0;
        char name[YUE_STRING_DATA_SIZE] = {0};
        while(!_isspace(*source->data)) {
            if(*source->data == '(' || *source->data == ')') break;
            if(i + 1 >= YUE_STRING_DATA_SIZE) yue_error(ctx, "symbol name is too long");
            name[i] = *source->data;
            i++;
            source->data++;
        }
        name[YUE_STRING_DATA_SIZE-1] = 0;
        return yue_symbol(ctx, name);
    }

    return yue_nil(ctx);
}

yue_Object *yue_read(yue_Context *ctx, const char *_source, size_t _len)
{
    Source source_, *source;
    source_.data = _source;
    source_.eof  = _source + _len;
    source = &source_;

    yue_Object *r = _read(ctx, source);
    yue_Object *root = yue_pair(ctx, r, yue_nil(ctx));
    yue_Object *prev = root;
    while(source->data < source->eof) {
        while(_isspace(*source->data)) source->data++;
        if(source->data >= source->eof) break;
        yue_Object *r = _read(ctx, source);
        yue_Object *curr = yue_pair(ctx, r, yue_nil(ctx));
        prev->as_pair.tail = curr;
        prev = curr;
    }
    return yue_pair(ctx, yue_cfunc(ctx, yue_builtin_dolist), root);
}


#endif // YUE_IMPLEMENTATION
