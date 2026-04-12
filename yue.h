#ifndef YUE_H_
#define YUE_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifndef YUE_STACK_CAP
#define YUE_STACK_CAP 256
#endif

#ifndef YUE_MAX_SCOPE_DEPTH
#define YUE_MAX_SCOPE_DEPTH 32
#endif

#ifndef YUE_API
    #ifdef _WIN32
        #ifdef YUE_BUILD_DLL
            #define YUE_API __declspec(dllexport)
        #else
            #define YUE_API __declspec(dllimport)
        #endif
    #else
        #define YUE_API
    #endif
#endif

#ifndef YUE_DEF
#define YUE_DEF 
#endif

/*#define YUE_TRACEF(...)*/
#ifndef YUE_TRACEF
#include <stdio.h>
#define YUE_TRACEF(...) fprintf(stderr, __VA_ARGS__)
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
    YUE_OBJECT_FUNC,
    YUE_OBJECT_CFUNC,
    YUE_OBJECT_USERDATA,
    YUE_OBJECT_RESOURCE,
} yue_ObjectType;

typedef struct yue_File {
    // the real first character in the entire file
    const char *fst;
    // pointer to the first character in the string 
    // (this can be offset-ed useful for slicing)
    const char *ptr;
    // .ptr + length of file
    const char *eof;
} yue_File;

// recommended bufsz is 64KB
YUE_DEF yue_Context *yue_open(void *buf, size_t bufsz);
YUE_DEF yue_Object *yue_eval(yue_Context *ctx, yue_Object *obj);
YUE_DEF yue_Object *yue_get(yue_Context *ctx, yue_Object *sym);
YUE_DEF void yue_set(yue_Context *ctx, yue_Object *sym, yue_Object *value);

// Executing file

// This will read a single top object and modify the file
YUE_DEF yue_Object *yue_read(yue_Context *ctx, yue_File *file);

// Object accessor
YUE_DEF bool yue_isnil(yue_Object *obj);
YUE_DEF yue_Number yue_tonumber(yue_Context *ctx, yue_Object *obj);
YUE_DEF void *yue_touserdata(yue_Context *ctx, yue_Object *obj);
YUE_DEF size_t yue_getstringlen(yue_Context *ctx, yue_Object *obj);
YUE_DEF char *yue_tostring(yue_Context *ctx, yue_Object *obj, char *dst, size_t dstsz);

// Object constructor
YUE_DEF yue_Object *yue_nil(yue_Context *ctx);
YUE_DEF yue_Object *yue_number(yue_Context *ctx, yue_Number number);
YUE_DEF yue_Object *yue_pair(yue_Context *ctx, yue_Object *head, yue_Object *tail);
YUE_DEF yue_Object *yue_list(yue_Context *ctx, yue_Object **objs, size_t count);
YUE_DEF yue_Object *yue_string_sized(yue_Context *ctx, const char *cstr, size_t n);
YUE_DEF yue_Object *yue_string(yue_Context *ctx, const char *cstr);
YUE_DEF yue_Object *yue_resource(yue_Context *ctx, void *data, void (*destroy)(void *data));
YUE_DEF yue_Object *yue_symbol(yue_Context *ctx, const char *name);
YUE_DEF yue_Object *yue_userdata(yue_Context *ctx, void *userdata);
YUE_DEF yue_Object *yue_func(yue_Context *ctx, yue_Object *params, yue_Object *body);

YUE_DEF yue_Object *yue_nextarg(yue_Context *ctx, yue_Object **p_arg);
YUE_DEF yue_Object *yue_cfunc(yue_Context *ctx, yue_CFunc cfunc);

// Builtin functions
YUE_DEF yue_Object *yue_builtin_while(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_if(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_lt(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_print(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_add(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_dolist(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_assign(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_not(yue_Context *ctx, yue_Object *arg);
YUE_DEF yue_Object *yue_builtin_exit(yue_Context *ctx, yue_Object *arg);
YUE_DEF void yue_load_builtins(yue_Context *ctx);

#endif // YUE_H_

#ifdef YUE_IMPLEMENTATION
#undef YUE_IMPLEMENTATION

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YUE_STRING_DATA_SIZE 32
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
        // TODO: we need to change symbol's implementation to be based on string (look below).
        //       With this, we can reduce YUE_STRING_DATA_SIZE. Thus sizeof(yue_Object) is smaller
        //       which in turn will make our object pool can contain more objects
        // struct {
        //     yue_Object *name;
        //     yue_Object *value;
        // } as_symbol;
        struct {
            char name[YUE_STRING_DATA_SIZE];
            yue_Object *value;
        } as_symbol;
        struct {
            yue_Object *params;
            yue_Object *body;
        } as_func;
        struct {
            void *data;
            void (*destroy)(void *data);
        } as_resource;
        void *as_userdata;
    };
};

struct yue_Context {
    yue_Object *stack[YUE_STACK_CAP];
    size_t stack_size;
    yue_Object *scope[YUE_MAX_SCOPE_DEPTH];
    size_t scope_size;
    yue_Object *nil;

    yue_Object *free_list;
    yue_Object *objects;
    size_t count_objects;
};

static const char *_yue_type_names[] = {
    [YUE_OBJECT_NIL] = "YUE_OBJECT_NIL",
    [YUE_OBJECT_NUMBER] = "YUE_OBJECT_NUMBER",
    [YUE_OBJECT_PAIR] = "YUE_OBJECT_PAIR",
    [YUE_OBJECT_STRING] = "YUE_OBJECT_STRING",
    [YUE_OBJECT_SYMBOL] = "YUE_OBJECT_SYMBOL",
    [YUE_OBJECT_USERDATA] = "YUE_OBJECT_USERDATA",
    [YUE_OBJECT_FUNC] = "YUE_OBJECT_FUNC",
    [YUE_OBJECT_CFUNC] = "YUE_OBJECT_CFUNC",
    [YUE_OBJECT_RESOURCE] = "YUE_OBJECT_RESOURCE",
};


void yue_error(yue_Context *ctx, const char *fmt, ...)
{
    (void)ctx;
    fprintf(stderr, "ERROR: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
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

    ctx->scope_size    = 1; // global scope
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

static void dump_obj(yue_Object *obj, int level);
static yue_Object *_eval_list(yue_Context *ctx, yue_Object *obj)
{
    switch(obj->type) {
    case YUE_OBJECT_PAIR:
        {
            yue_Object *base = obj;
            while(!yue_isnil(obj)) {
                obj->as_pair.head = yue_eval(ctx, obj->as_pair.head);
                obj = obj->as_pair.tail;
            }
            return base;
        } break;
    default:
        return yue_eval(ctx, obj);
    }
}

static void begin_scope(yue_Context *ctx)
{
    if(ctx->scope_size > YUE_MAX_SCOPE_DEPTH) yue_error(ctx, "Max scope depth exceeded");
    ctx->scope[ctx->scope_size] = NULL;
    ctx->scope_size += 1;
}

static void end_scope(yue_Context *ctx)
{
    if(((int)ctx->scope_size - 1) < 0) yue_error(ctx, "Min scope depth reached");
    ctx->scope_size -= 1;
    ctx->scope[ctx->scope_size] = NULL;
}

yue_Object *yue_eval(yue_Context *ctx, yue_Object *obj)
{
    switch(obj->type) {
        case YUE_OBJECT_NIL:
        case YUE_OBJECT_NUMBER:
        case YUE_OBJECT_CFUNC:
        case YUE_OBJECT_STRING:
        case YUE_OBJECT_USERDATA:
        case YUE_OBJECT_RESOURCE:
        case YUE_OBJECT_FUNC:
            return obj;
        case YUE_OBJECT_SYMBOL:
            return yue_get(ctx, obj);
        case YUE_OBJECT_PAIR:
            {
                yue_Object *base = obj->as_pair.head;
                yue_Object *arg = obj->as_pair.tail;
                yue_Object *fn = yue_eval(ctx, base);
                switch(fn->type) {
                case YUE_OBJECT_FUNC:
                    {
                        // evaluate all args first
                        arg = _eval_list(ctx, arg);
                        
                        yue_Object *symbols = fn->as_func.params;
                        // load arguments
                        yue_Object *a = NULL;
                        begin_scope(ctx);
                        while(true) {
                            if(yue_isnil((a = yue_nextarg(ctx, &arg)))) break;
                            if(yue_isnil(symbols)) break;
                            yue_Object *symbol= symbols->as_pair.head;
                            if(symbol->type != YUE_OBJECT_SYMBOL) 
                                yue_error(ctx, "Function parameter is not a symbol but %s", 
                                        _yue_type_names[symbol->type]);
                            yue_set(ctx, symbol, yue_eval(ctx, a));
                        }
                        yue_Object *obj = yue_eval(ctx, fn->as_func.body);
                        end_scope(ctx);
                        return obj;
                    }
                case YUE_OBJECT_CFUNC:
                    return fn->as_cfunc(ctx, arg);
                default:
                    if(base->type == YUE_OBJECT_SYMBOL) {
                        yue_error(ctx, "Invoking non callable object `%s` %s", base->as_symbol.name, _yue_type_names[fn->type]);
                    } else {
                        yue_error(ctx, "Invoking non callable object %s", _yue_type_names[fn->type]);
                    }
                    return obj;
                }
            } break;
        default:
            return obj;
    }
    return obj;
}

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
    } else if(obj->type == YUE_OBJECT_FUNC) {
        mark(ctx, obj->as_func.params);
        mark(ctx, obj->as_func.body);
    } else if(obj->type == YUE_OBJECT_SYMBOL) {
        mark(ctx, obj->as_symbol.value);
    }
}

static void mark_all(yue_Context *ctx)
{
    for(size_t i = 0; i < ctx->stack_size; ++i) {
        mark(ctx, ctx->stack[i]);
    }
    for(size_t i = 0; i < ctx->scope_size; ++i) {
        yue_Object *obj = ctx->scope[i];
        while(obj) {
            mark(ctx, obj);
            obj = obj->next;
        }
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
            if(obj->type == YUE_OBJECT_RESOURCE)
                obj->as_resource.destroy(obj->as_resource.data);
            obj->next = ctx->free_list;
            ctx->free_list = obj;
        }
    }
}

void yue_rungc(yue_Context *ctx)
{
    mark_all(ctx);
    sweep(ctx);
}

size_t yue_savegc(yue_Context *ctx)
{
    return ctx->stack_size;
}

void yue_restoregc(yue_Context *ctx, size_t gc)
{
    ctx->stack_size = gc;
}

static void yue_pushgc(yue_Context *ctx, yue_Object *obj)
{
    assert(obj && "Invalid object to push");
    if(ctx->stack_size >= YUE_STACK_CAP) yue_error(ctx, "Stack overflow!");
    ctx->stack[ctx->stack_size++] = obj;
}

void yue_set(yue_Context *ctx, yue_Object *sym, yue_Object *value)
{
    if(sym->type != YUE_OBJECT_SYMBOL) 
        yue_error(ctx, "set require the first argument to be symbol but found %s\n", _yue_type_names[sym->type]);
    for(int i = (int)ctx->scope_size - 1; i >= 0; --i) {
        yue_Object *obj = ctx->scope[i];
        while(obj) {
            if(obj->type == YUE_OBJECT_SYMBOL) {
                if(strcmp(sym->as_symbol.name, obj->as_symbol.name) == 0) {
                    obj->as_symbol.value = value;
                    yue_pushgc(ctx, obj);
                    return;
                }
            }
            obj = obj->next;
        }
    }
    sym->as_symbol.value = value;
    sym->next = ctx->scope[ctx->scope_size - 1];
    ctx->scope[ctx->scope_size - 1] = sym;
}

yue_Object *yue_get(yue_Context *ctx, yue_Object *sym)
{
    if(sym->type != YUE_OBJECT_SYMBOL) yue_error(ctx, "set require the first argument to be symbol\n");
    for(int i = (int)ctx->scope_size - 1; i >= 0; --i) {
        yue_Object *obj = ctx->scope[i];
        while(obj) {
            if(obj->type == YUE_OBJECT_SYMBOL) {
                if(strcmp(sym->as_symbol.name, obj->as_symbol.name) == 0) {
                    return obj->as_symbol.value;
                }
            }
            obj = obj->next;
        }
    }
    return yue_nil(ctx);
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

yue_Object *yue_userdata(yue_Context *ctx, void *userdata)
{
    yue_Object *obj = new_object(ctx, YUE_OBJECT_USERDATA);
    obj->as_userdata = userdata;
    yue_pushgc(ctx, obj);
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
    assert(ctx->scope_size > 0);

    size_t name_len = strlen(name);
    if(name_len + 1 > YUE_STRING_DATA_SIZE) 
        yue_error(ctx, "symbol name '%s' is too long. It's length is %zu but maximum length is %d\n", 
                name, name_len + 1, YUE_STRING_DATA_SIZE);

    yue_Object *obj = new_object(ctx, YUE_OBJECT_SYMBOL);
    memcpy(obj->as_symbol.name, name, name_len);
    obj->as_symbol.name[name_len] = 0;
    obj->as_symbol.value = yue_nil(ctx);
    yue_pushgc(ctx, obj);
    return obj;
}

yue_Object *yue_resource(yue_Context *ctx, void *data, void (*destroy)(void *data))
{
    yue_Object *obj = new_object(ctx, YUE_OBJECT_RESOURCE);
    obj->as_resource.data    = data;
    obj->as_resource.destroy = destroy;
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

yue_Object *yue_func(yue_Context *ctx, yue_Object *params, yue_Object *body)
{
    yue_Object *obj = new_object(ctx, YUE_OBJECT_FUNC);
    obj->as_func.body = body;
    obj->as_func.params = params;
    yue_pushgc(ctx, obj);
    return obj;
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

bool yue_isfunc(yue_Object *obj)
{
    return obj->type == YUE_OBJECT_FUNC;
}

bool yue_islist(yue_Object *obj)
{
    return obj->type == YUE_OBJECT_PAIR;
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
    memset(dst, 0, dstsz);
    char *base = dst;
    int cap = dstsz;
    yue_Object *curr = obj;
    while(curr) {
        yue_Object *tail = curr->as_str.tail;
        if(tail) {
            size_t n = cap < YUE_STRING_DATA_SIZE ? cap : YUE_STRING_DATA_SIZE;
            memcpy(dst, curr->as_str.data, n);
            base += n;
            cap -= n;
        } else {
            size_t i = 0;
            while(cap > 1 && curr->as_str.data[i] != 0) {
                *base++ = curr->as_str.data[i];
                i += 1;
                cap -= 1;
            }
        }
        curr = tail;
    }
    dst[dstsz - 1] = 0;
    return dst;
}

yue_Number yue_tonumber(yue_Context *ctx, yue_Object *obj)
{
    if(obj->type != YUE_OBJECT_NUMBER) yue_error(ctx, "Expected a number");
    return obj->as_number;
}

void *yue_touserdata(yue_Context *ctx, yue_Object *obj)
{
    if(obj->type != YUE_OBJECT_USERDATA) yue_error(ctx, "Expected an userdata");
    return obj->as_userdata;
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
        case YUE_OBJECT_FUNC:
            printf("<func>");
            break;
        case YUE_OBJECT_USERDATA:
            printf("<userdata: %p>", obj->as_userdata);
            break;
        case YUE_OBJECT_RESOURCE:
            printf("<resource: %p>", obj->as_userdata);
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
    size_t gc = yue_savegc(ctx);
    yue_Object *a = NULL;
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        print_object_inner(yue_eval(ctx, a), 0);
        printf(" ");
    }
    printf("\n");
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

yue_Object *yue_builtin_add(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number result = 0;
    yue_Object *a = NULL;
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        result += yue_tonumber(ctx, yue_eval(ctx, a));
    }
    yue_restoregc(ctx, gc);
    return yue_number(ctx, result);
}

yue_Object *yue_builtin_sub(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number result = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_Object *a = NULL;
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        result -= yue_tonumber(ctx, yue_eval(ctx, a));
    }
    yue_restoregc(ctx, gc);
    return yue_number(ctx, result);
}

yue_Object *yue_builtin_mul(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number result = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_Object *a = NULL;
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        result *= yue_tonumber(ctx, yue_eval(ctx, a));
    }
    yue_restoregc(ctx, gc);
    return yue_number(ctx, result);
}

yue_Object *yue_builtin_dolist(yue_Context *ctx, yue_Object *arg) 
{
    size_t gc = yue_savegc(ctx);
    yue_Object *a = NULL;
    size_t eval_gc = yue_savegc(ctx);
    while(!yue_isnil((a = yue_nextarg(ctx, &arg)))) {
        yue_restoregc(ctx, gc);
        a = yue_eval(ctx, a);
    }
    yue_restoregc(ctx, gc);
    yue_pushgc(ctx, a);
    return a;
}

yue_Object *yue_builtin_assign(yue_Context *ctx, yue_Object *arg) 
{
    size_t gc = yue_savegc(ctx);
    yue_Object *symbol = yue_nextarg(ctx, &arg);
    yue_Object *value  = yue_eval(ctx, yue_nextarg(ctx, &arg));
    yue_restoregc(ctx, gc);
    yue_set(ctx, symbol, value);
    return yue_nil(ctx);
}

yue_Object *yue_builtin_while(yue_Context *ctx, yue_Object *arg)
{
    size_t global_gc = yue_savegc(ctx);
    yue_Object *cond = yue_nextarg(ctx, &arg);
    yue_Object *body = yue_nextarg(ctx, &arg);
    yue_Object *res  = yue_nil(ctx);
    size_t eval_gc = yue_savegc(ctx);
    for(;;) {
        yue_restoregc(ctx, eval_gc);
        if(yue_isnil(yue_eval(ctx, cond))) break;
        res = yue_eval(ctx, body);
    }
    yue_restoregc(ctx, global_gc);
    return res;
}

yue_Object *yue_builtin_if(yue_Context *ctx, yue_Object *arg)
{
    size_t eval_gc = yue_savegc(ctx);
    yue_Object *cond = yue_nextarg(ctx, &arg);
    yue_Object *if_true  = yue_nextarg(ctx, &arg);
    yue_Object *if_false = yue_nextarg(ctx, &arg);
    if(!yue_isnil(yue_eval(ctx, cond))) {
        yue_eval(ctx, if_true);
    } else {
        yue_eval(ctx, if_false);
    }
    yue_restoregc(ctx, eval_gc);
    yue_Object *res  = yue_nil(ctx);
    return res;
}

yue_Object *yue_builtin_lt(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number lhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_Number rhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_restoregc(ctx, gc);
    if(lhs < rhs) {
        return yue_number(ctx, 1);
    } else {
        return yue_nil(ctx);
    }
}

yue_Object *yue_builtin_gt(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number lhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_Number rhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_restoregc(ctx, gc);
    if(lhs > rhs) {
        return yue_number(ctx, 1);
    } else {
        return yue_nil(ctx);
    }
}

yue_Object *yue_builtin_le(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number lhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_Number rhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_restoregc(ctx, gc);
    if(lhs <= rhs) {
        return yue_number(ctx, 1);
    } else {
        return yue_nil(ctx);
    }
}

yue_Object *yue_builtin_ge(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number lhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_Number rhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_restoregc(ctx, gc);
    if(lhs >= rhs) {
        return yue_number(ctx, 1);
    } else {
        return yue_nil(ctx);
    }
}

yue_Object *yue_builtin_ne(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Number lhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_Number rhs = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_restoregc(ctx, gc);
    if(lhs != rhs) {
        return yue_number(ctx, 1);
    } else {
        return yue_nil(ctx);
    }
}

yue_Object *yue_builtin_not(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Object *any = yue_eval(ctx, yue_nextarg(ctx, &arg));
    bool isnil = yue_isnil(any);
    yue_restoregc(ctx, gc);
    return isnil ? yue_number(ctx, 1) : yue_nil(ctx);
}

yue_Object *yue_builtin_exit(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    int exit_code = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    yue_restoregc(ctx, gc);
    // TODO: maybe exit should be a flag in ctx that we can change 
    //       so when it's exit we refuse to do anymore evaluation
    //       or spit error.
    exit(exit_code);
    return yue_nil(ctx);
}

yue_Object *yue_builtin_fn(yue_Context *ctx, yue_Object *arg)
{
    yue_Object *params = yue_nextarg(ctx, &arg);
    yue_Object *body   = yue_nextarg(ctx, &arg);
    return yue_func(ctx, params, body);
}

yue_Object *yue_builtin_list(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Object *result = _eval_list(ctx, arg);
    yue_restoregc(ctx, gc);
    return result;
}

yue_Object *yue_builtin_head(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Object *list = yue_eval(ctx, yue_nextarg(ctx, &arg));
    yue_restoregc(ctx, gc);
    if(list->type != YUE_OBJECT_PAIR) yue_error(ctx, "`head` requires a list");
    return list->as_pair.head;
}

yue_Object *yue_builtin_tail(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_Object *list = yue_eval(ctx, yue_nextarg(ctx, &arg));
    yue_restoregc(ctx, gc);
    if(list->type != YUE_OBJECT_PAIR) yue_error(ctx, "`head` requires a list");
    return list->as_pair.tail;
}

void yue_load_builtins(yue_Context *ctx)
{
    size_t gc = yue_savegc(ctx);
    yue_set(ctx, yue_symbol(ctx, "print"), yue_cfunc(ctx, yue_builtin_print));
    yue_set(ctx, yue_symbol(ctx, "+"), yue_cfunc(ctx, yue_builtin_add));
    yue_set(ctx, yue_symbol(ctx, "-"), yue_cfunc(ctx, yue_builtin_sub));
    yue_set(ctx, yue_symbol(ctx, "*"), yue_cfunc(ctx, yue_builtin_mul));
    yue_set(ctx, yue_symbol(ctx, "="), yue_cfunc(ctx, yue_builtin_assign));
    yue_set(ctx, yue_symbol(ctx, "not"), yue_cfunc(ctx, yue_builtin_not));
    yue_set(ctx, yue_symbol(ctx, "lt"), yue_cfunc(ctx, yue_builtin_lt));
    yue_set(ctx, yue_symbol(ctx, "gt"), yue_cfunc(ctx, yue_builtin_gt));
    yue_set(ctx, yue_symbol(ctx, "le"), yue_cfunc(ctx, yue_builtin_le));
    yue_set(ctx, yue_symbol(ctx, "ge"), yue_cfunc(ctx, yue_builtin_ge));
    yue_set(ctx, yue_symbol(ctx, "ne"), yue_cfunc(ctx, yue_builtin_ne));
    yue_set(ctx, yue_symbol(ctx, "exit"), yue_cfunc(ctx, yue_builtin_exit));
    yue_set(ctx, yue_symbol(ctx, "do"), yue_cfunc(ctx, yue_builtin_dolist));
    yue_set(ctx, yue_symbol(ctx, "while"), yue_cfunc(ctx, yue_builtin_while));
    yue_set(ctx, yue_symbol(ctx, "if"), yue_cfunc(ctx, yue_builtin_if));
    yue_set(ctx, yue_symbol(ctx, "fn"), yue_cfunc(ctx, yue_builtin_fn));
    yue_set(ctx, yue_symbol(ctx, "list"), yue_cfunc(ctx, yue_builtin_list));
    yue_set(ctx, yue_symbol(ctx, "head"), yue_cfunc(ctx, yue_builtin_head));
    yue_set(ctx, yue_symbol(ctx, "tail"), yue_cfunc(ctx, yue_builtin_tail));
    yue_restoregc(ctx, gc);
}

static inline bool _isdigit(int c) { return '0' <= c && c <= '9'; }
static inline bool _isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

yue_Object *yue_read(yue_Context *ctx, yue_File *source)
{
    while(_isspace(*source->ptr)) source->ptr++;
    if(source->ptr >= source->eof) return yue_nil(ctx);
    if(*source->ptr == '"') {
        source->ptr++;
        const char *start = source->ptr;
        while(source->ptr < source->eof && *source->ptr != '"') source->ptr++;
        size_t size = source->ptr - start;
        source->ptr++;
        return yue_string_sized(ctx, start, size);
    } else if(_isdigit(*source->ptr)) {
        yue_Number val = 0;
        while(source->ptr < source->eof) {
            if(_isdigit(*source->ptr)) {
                val *= 10;
                val += *source->ptr - '0';
                source->ptr++;
            } else {
                break;
            }
        }
        return yue_number(ctx, val);
    } else if(*source->ptr == '(') {
        source->ptr++;
        while(_isspace(*source->ptr)) source->ptr++;
        if(source->ptr < source->eof && *source->ptr == ')') {
            source->ptr++; // empty list
            return yue_nil(ctx);
        }

        size_t gc = yue_savegc(ctx);
        yue_Object *r =  yue_read(ctx, source);
        yue_Object *root = yue_pair(ctx, r, yue_nil(ctx));
        yue_Object *prev = root;
        for(;;) {
            while(source->ptr < source->eof && _isspace(*source->ptr)) source->ptr++;
            if(source->ptr >= source->eof) yue_error(ctx, "Unclosed '('");
            if(*source->ptr == ')') {
                source->ptr++;
                break;
            }
            yue_Object *r = yue_read(ctx, source);
            yue_Object *curr = yue_pair(ctx, r, yue_nil(ctx));
            prev->as_pair.tail = curr;
            prev = curr;
        }
        yue_restoregc(ctx, gc);
        yue_pushgc(ctx, root);
        return root;
    } else {
        size_t i = 0;
        char name[YUE_STRING_DATA_SIZE] = {0};
        memset(name, 0, YUE_STRING_DATA_SIZE);
        while(source->ptr < source->eof) {
            if(_isspace(*source->ptr)) break;
            if(*source->ptr == '(' || *source->ptr == ')') break;
            if(i + 1 >= YUE_STRING_DATA_SIZE) yue_error(ctx, "symbol name is too long");
            name[i] = *source->ptr;
            i++;
            source->ptr++;
        }
        name[YUE_STRING_DATA_SIZE-1] = 0;
        return yue_symbol(ctx, name);
    }

    return yue_nil(ctx);
}


#endif // YUE_IMPLEMENTATION
