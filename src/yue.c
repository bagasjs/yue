#include <stdio.h>
#include "utils.h"
#include "lexer.h"
#include "shtable.h"

typedef enum Op {
    OP_NOP = 0,
    OP_PUSH_INT,
    OP_PUSH_FLT,
    OP_PUSH_STR,
    OP_PUSH_FUNC,

    OP_NEW_ARRAY,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_EQ,
    OP_NE,
    OP_GT,
    OP_GE,
    OP_LT,
    OP_LE,
    OP_AND,
    OP_OR,

    OP_GET_ITEM,
    OP_SET_ITEM,

    OP_LOCAL_SET,
    OP_LOCAL_GET,
    OP_GLOBAL_GET,

    OP_JMP,
    OP_JEZ,
    OP_PRINT,

    OP_CALL,
    OP_RET,
} Opcode;

const char *opcode_names[] = {
    [OP_NOP] = "OP_NOP",
    [OP_PUSH_INT] = "OP_PUSH_INT",
    [OP_PUSH_FLT] = "OP_PUSH_FLT",
    [OP_PUSH_STR] = "OP_PUSH_STR",
    [OP_PUSH_FUNC] = "OP_PUSH_FUNC",

    [OP_ADD] = "OP_ADD",
    [OP_SUB] = "OP_SUB",
    [OP_MUL] = "OP_MUL",
    [OP_EQ] = "OP_EQ",
    [OP_NE] = "OP_NE",
    [OP_GT] = "OP_GT",
    [OP_GE] = "OP_GE",
    [OP_LT] = "OP_LT",
    [OP_LE] = "OP_LE",
    [OP_AND] = "OP_AND",
    [OP_OR] = "OP_OR",

    [OP_LOCAL_SET] = "OP_LOCAL_SET",
    [OP_LOCAL_GET] = "OP_LOCAL_GET",
    [OP_GLOBAL_GET] = "OP_GLOBAL_GET",

    [OP_JMP] = "OP_JMP",
    [OP_JEZ] = "OP_JEZ",
    [OP_PRINT] = "OP_PRINT",
    [OP_CALL] = "OP_CALL",
    [OP_RET] = "OP_RET",
};

typedef struct {
    Opcode opcode;
    int    arg;
} Inst;

typedef struct {
    const char *name;
    size_t params_count;
    size_t locals_count;
    struct {
        Inst  *items;
        size_t count;
        size_t capacity;
    } insts;
    struct {
        size_t *items;
        size_t  count;
        size_t  capacity;
    } labels;
} Function;

typedef struct {
    Arena arena;

    struct {
        int   *items;
        size_t count;
        size_t capacity;
    } intconsts;
    struct {
        float *items;
        size_t count;
        size_t capacity;
    } fltconsts;
    struct {
        char  *items;
        size_t count;
        size_t capacity;
    } strconsts; 
    struct {
        Function *items;
        size_t    count;
        size_t    capacity;
    } functions;
} Module;

int add_new_function_to_module(Module *module, const char *name)
{
    arena_da_append(&module->arena, &module->functions, ((Function){0}));
    Function *result = &module->functions.items[module->functions.count - 1];
    result->name = arena_strdup(&module->arena, name);
    return module->functions.count - 1;
}

void add_inst_to_function(Function *function, Inst inst, Module *module)
{
    arena_da_append(&module->arena, &function->insts, inst);
}

int find_function_in_module(const char *name, Module *module)
{
    for(size_t i = 0; i < module->functions.count; ++i) {
        if(cstreq(name, module->functions.items[i].name)) {
            return (int)i;
        }
    }
    return -1;
}

typedef struct Value Value;
typedef enum {
    VALUE_NIL = 0,
    VALUE_INT,
    VALUE_FLT,
    VALUE_FUN,
    VALUE_CFN, // C function
    VALUE_OBJ,
} ValueKind;

typedef enum {
    OBJECT_NIL = 0,
    OBJECT_STRING,
    OBJECT_ARRAY,
} ObjectKind;

typedef struct Object Object;
struct Object {
    Object    *next;
    ObjectKind kind;
    bool       marked;
};

typedef struct {
    Object base;
    char  *items;
    size_t count;
    size_t capacity;
} ObjectString;

typedef struct {
    Object base;
    Value *items;
    size_t count;
    size_t capacity;
} ObjectArray;

typedef struct Context Context;
typedef Value (*Cfn)(Context *ctx, Value *args, size_t argc);

typedef struct Value {
    ValueKind kind;
    union {
        int     intv;
        float   fltv;
        int     func_id;
        Object *objv;
        Cfn     cfn;
    };
} Value;

#define STACK_CAP 1024
typedef struct CallFrame {
    Value  locals[1024];
    Value  stack[STACK_CAP];
    size_t sp;

    Value  rax; // regista
} CallFrame;

typedef struct Runtime {
    struct {
        Value *items;
        size_t count;
        size_t capacity;
    } globals;
    shtable_t globals_nametable;

    struct {
        Object **items;
        size_t   count;
        size_t  capacity;
    } all;

    struct {
        CallFrame *items;
        size_t count;
        size_t capacity;
    } call_frames;
} Runtime;

bool runtime_hasglobal(Runtime *runtime, const char *name)
{
    return (intptr_t)(void*)shtable_geti(&runtime->globals_nametable, name) >= 0;
}

int runtime_getglobal(Runtime *runtime, const char *name)
{
    return (intptr_t)shtable_get(&runtime->globals_nametable, name);
}

void runtime_setglobal(Runtime *runtime, const char *name, Value value)
{
    int shtable_item_slot = shtable_geti(&runtime->globals_nametable, name);
    if(shtable_item_slot < 0) {
        int global = runtime->globals.count;
        sa_append(&runtime->globals, value);
        shtable_set(&runtime->globals_nametable, name, (void*)(intptr_t)global);
    } else {
        int global = (intptr_t)runtime->globals_nametable.items[shtable_item_slot].value;
        runtime->globals.items[global] = value;
    }
}

void runtime_mark_object(Runtime *runtime, Object *object)
{
    if(object->marked) return;
    object->marked = true;
    if(object->kind == OBJECT_ARRAY) {
        ObjectArray *arr = (ObjectArray*)object;
        for(size_t i = 0; i < arr->count; ++i) {
            Value val = arr->items[i];
            if(val.kind == VALUE_OBJ) 
                runtime_mark_object(runtime, val.objv);
        }
    }
}

void runtime_mark_used_objects(Runtime *runtime)
{
    // Mark objects in global variables
    for(size_t i = 0; i < runtime->globals.count; ++i) {
        Value val = runtime->globals.items[i];
        if(val.kind == VALUE_OBJ) 
            runtime_mark_object(runtime, val.objv);
    }

    // Mark objects in local variables
    for(size_t i = 0; i < runtime->call_frames.count; ++i) {
        CallFrame *frame = &runtime->call_frames.items[i];
        for(size_t j = 0; j < ARRLEN(frame->locals); ++j) {
            Value val = frame->locals[j];
            if(val.kind == VALUE_OBJ) runtime_mark_object(runtime, val.objv);
        }
        // Mark objects in stack
        for(size_t j = 0; j < STACK_CAP; ++j) {
            Value val = frame->stack[j];
            if(val.kind == VALUE_OBJ) 
                runtime_mark_object(runtime, val.objv);
        }
    }
}

void runtime_destroy_object(Runtime *runtime, Object *object)
{
    (void)runtime;
    switch(object->kind) {
    case OBJECT_NIL:
        free(object);
        break;
    case OBJECT_STRING:
        {
            ObjectString *str = (ObjectString*)object;
            free(str->items);
            free(str);
        } break;
    case OBJECT_ARRAY:
        {
            ObjectArray *arr = (ObjectArray*)object;
            free(arr->items);
            free(arr);
        } break;
    default:
        ASSERT(0 && "Unreachable");
    }
}

void runtime_sweep_unused_objects(Runtime *runtime)
{
    for(size_t i = 0; i < runtime->all.count;) {
        Object *obj = runtime->all.items[i];
        if(obj->marked) {
            obj->marked = false;
            ++i;
        } else {
            runtime_destroy_object(runtime, obj);
            da_remove_unordered(&runtime->all, i);
        }
    }
}

Object *runtime_newobject(Runtime *runtime, ObjectKind kind, size_t size)
{
    ASSERT(size >= sizeof(Object));
    Object *obj  = malloc(size);
    obj->kind = kind;
    obj->marked = false;
    if(runtime->all.count + 1 > runtime->all.capacity) {
        if(runtime->all.capacity == 0) {
            runtime->all.capacity = 256;
            runtime->all.items = malloc(runtime->all.capacity * sizeof(*runtime->all.items));
        } else {
            runtime_mark_used_objects(runtime);
            runtime_sweep_unused_objects(runtime);
            if(runtime->all.count + 1 > runtime->all.capacity) {
                runtime->all.capacity *= 2;
                void *items = malloc(runtime->all.capacity * sizeof(*runtime->all.items));
                ASSERT(items != NULL && "Yue: Your backpack is full!");
                memcpy(items, runtime->all.items, runtime->all.count * sizeof(*runtime->all.items));
                free(runtime->all.items);
                runtime->all.items = items;
            }
        }
    }
    runtime->all.items[runtime->all.count++] = obj;
    return obj;
}

Object *runtime_pushnewstring(Runtime *runtime, CallFrame *frame, const char *init_text)
{
    ObjectString *obj = (ObjectString*)runtime_newobject(runtime, OBJECT_STRING, sizeof(ObjectString));
    obj->count    = 0;
    obj->capacity = 0;
    obj->items    = 0;
    if(init_text) 
        da_append_many(obj, init_text, cstrsz(init_text));
    frame->stack[frame->sp++] = ((Value){ .kind = VALUE_OBJ, .objv = (Object*)obj });
    return (Object*)obj;
}

Object *runtime_pushnewarray(Runtime *runtime, CallFrame *frame)
{
    ObjectArray *obj = (ObjectArray*)runtime_newobject(runtime, OBJECT_ARRAY, sizeof(ObjectArray));
    obj->count    = 0;
    obj->capacity = 0;
    obj->items    = 0;
    frame->stack[frame->sp++] = ((Value){ .kind = VALUE_OBJ, .objv = (Object*)obj });
    return (Object*)obj;
}

void print_value(Value *values, size_t count)
{
    for(size_t i = 0; i < count; ++i) {
        if(i != 0) printf(" ");
        Value a = values[i];
        switch(a.kind) {
            case VALUE_NIL:
                printf("<nil>");
                break;
            case VALUE_FLT:
                printf("%f", a.fltv);
                break;
            case VALUE_INT:
                printf("%d", a.intv);
                break;
            case VALUE_FUN:
                printf("<fun#%d>", a.func_id);
                break;
            case VALUE_OBJ:
                {
                    switch(a.objv->kind) {
                        case OBJECT_STRING:
                            {
                                ObjectString *str = (ObjectString*)a.objv;
                                printf("%.*s", (int)str->count, str->items);
                            } break;
                        case OBJECT_ARRAY:
                            {
                                ObjectArray *arr = (ObjectArray*)a.objv;
                                printf("<array[%zu]>", arr->count);
                            } break;
                        default:
                            ASSERT(0 && "Unreachable");
                            break;
                    }
                } break;
            default:
                ASSERT(0 && "Unreachable");
                break;
        }
    }
    printf("\n");
}

bool run_function(Runtime *runtime, Module *module, Function *func, Value *args, size_t argc, Value *retval)
{
    size_t pc = 0;
    int print_iter = 0;
    const int PRINT_CAP = 0;

    size_t frame_ptr = runtime->call_frames.count;
    sa_append(&runtime->call_frames, ((CallFrame){0}));
    CallFrame *frame = &runtime->call_frames.items[frame_ptr];

    ASSERT(argc == func->params_count);
    for(size_t i = 0; i < argc; ++i) {
        frame->locals[i] = args[i];
    }

    bool running = true;
    while(running) {
        if(pc >= func->insts.count) break;
        Inst inst = func->insts.items[pc++];
        switch(inst.opcode) {
            case OP_NOP:
                break;
            case OP_PUSH_INT:
                frame->stack[frame->sp++] = ((Value){ .kind = VALUE_INT, .intv = module->intconsts.items[inst.arg] });
                break;
            case OP_PUSH_FLT:
                frame->stack[frame->sp++] = ((Value){ .kind = VALUE_FLT, .fltv = module->fltconsts.items[inst.arg] });
                break;
            case OP_PUSH_FUNC:
                frame->stack[frame->sp++] = ((Value){ .kind = VALUE_FUN, .func_id = inst.arg });
                break;

            case OP_PUSH_STR:
                runtime_pushnewstring(runtime, frame, &module->strconsts.items[inst.arg]);
                break;
            case OP_NEW_ARRAY:
                runtime_pushnewarray(runtime, frame);
                break;
            case OP_SET_ITEM:
                {
                    ASSERT(frame->sp >= 3);
                    Value val = frame->stack[--frame->sp];
                    Value idx = frame->stack[--frame->sp];
                    Value obj = frame->stack[--frame->sp];
                    ASSERT(obj.kind == VALUE_OBJ);
                    switch(obj.objv->kind) {
                    case OBJECT_ARRAY:
                        {
                            ASSERT(idx.kind == VALUE_INT);
                            ObjectArray *arr = (ObjectArray*)obj.objv;
                            if(idx.intv < (int)arr->count) 
                                arr->items[idx.intv] = val;
                        } break;
                    case OBJECT_STRING:
                        {
                            ASSERT(idx.kind == VALUE_INT);
                            ASSERT(val.kind == VALUE_INT);
                            ObjectString *str = (ObjectString*)obj.objv;
                            if(idx.intv < (int)str->count) 
                                str->items[idx.intv] = (char)val.intv;
                        } break;
                    default:
                        ASSERT(0 && "GET_ITEM Unreachable");
                    }
                } break;
            case OP_GET_ITEM:
                {
                    ASSERT(frame->sp > 2);
                    Value idx = frame->stack[--frame->sp];
                    Value obj = frame->stack[--frame->sp];
                    ASSERT(obj.kind == VALUE_OBJ);
                    switch(obj.objv->kind) {
                    case OBJECT_ARRAY:
                        {
                            ObjectArray *arr = (ObjectArray*)obj.objv;
                            ASSERT(idx.kind == VALUE_INT);
                            Value result = {0};
                            if(idx.intv < (int)arr->count) 
                                result = arr->items[idx.intv];
                            frame->stack[frame->sp++] = result;
                        } break;
                    case OBJECT_STRING:
                        {
                            ASSERT(idx.kind == VALUE_INT);
                            ObjectString *str = (ObjectString*)obj.objv;
                            Value result = {0};
                            if(idx.intv < (int)str->count) 
                                result = (Value){ .kind = VALUE_INT, .intv = str->items[idx.intv] };
                            frame->stack[frame->sp++] = result;
                        } break;
                    default:
                        ASSERT(0 && "GET_ITEM Unreachable");
                    }
                } break;

            case OP_CALL:
                {
                    size_t new_argc = inst.arg;
                    ASSERT(frame->sp >= (new_argc + 1));
                    Value newfuncv = frame->stack[frame->sp - new_argc - 1];
                    switch(newfuncv.kind) {
                    case VALUE_FUN:
                        {
                            Function *newfunc = &module->functions.items[newfuncv.func_id];
                            ASSERT(new_argc >= newfunc->params_count);
                            new_argc = newfunc->params_count; // we only pass the neccessary
                            Value *args  = &frame->stack[frame->sp - new_argc];
                            Value retval = {0};
                            if(!run_function(runtime, module, newfunc, args, new_argc, &retval)) return false;
                            frame->sp -= new_argc + 1;
                            frame->stack[frame->sp++] = retval;
                        } break;
                    case VALUE_CFN:
                        {
                            Value *args  = &frame->stack[frame->sp - new_argc];
                            Value retval = newfuncv.cfn(NULL, args, new_argc);
                            frame->sp -= new_argc + 1;
                            frame->stack[frame->sp++] = retval;
                        } break;
                    default:
                        ASSERT(newfuncv.kind == VALUE_FUN || newfuncv.kind == VALUE_CFN);
                        break;
                    }
                } break;
            case OP_RET:
                {
                    Value a = frame->stack[--frame->sp];
                    *retval = a;
                    running = false;
                } break;

            case OP_ADD:
            case OP_MUL:
            case OP_SUB:
            case OP_EQ:
            case OP_NE:
            case OP_GT:
            case OP_GE:
            case OP_LT:
            case OP_LE:
            case OP_AND:
            case OP_OR:
                {
                    Value a = frame->stack[--frame->sp];
                    Value b = frame->stack[--frame->sp];
                    ASSERT(a.kind == b.kind && a.kind == VALUE_INT);
                    switch(inst.opcode) {
                    case OP_ADD:
                        b.intv += a.intv;
                        break;
                    case OP_SUB:
                        b.intv -= a.intv;
                        break;
                    case OP_MUL:
                        b.intv *= a.intv;
                        break;
                    case OP_EQ:
                        b.intv  = b.intv == a.intv;
                        break;
                    case OP_NE:
                        b.intv  = b.intv != a.intv;
                        break;
                    case OP_GT:
                        b.intv  = b.intv > a.intv;
                        break;
                    case OP_GE:
                        b.intv  = b.intv >= a.intv;
                        break;
                    case OP_LT:
                        b.intv  = b.intv <  a.intv;
                        break;
                    case OP_LE:
                        b.intv  = b.intv <= a.intv;
                        break;
                    case OP_AND:
                        b.intv  = b.intv && a.intv;
                        break;
                    case OP_OR:
                        b.intv  = b.intv || a.intv;
                        break;
                    default:
                        ASSERT(0 && "Unreachable");
                        break;
                    }
                    frame->stack[frame->sp++] = b;
                } break;

            case OP_LOCAL_SET:
                frame->locals[inst.arg] = frame->stack[--frame->sp];
                break;
            case OP_LOCAL_GET:
                frame->stack[frame->sp++] = frame->locals[inst.arg]; 
                break;
            case OP_GLOBAL_GET:
                {
                    Value value = runtime->globals.items[inst.arg];
                    frame->stack[frame->sp++] = value;
                } break;

            case OP_JMP:
                {
                    ASSERT(inst.arg < (int)func->labels.count);
                    pc = func->labels.items[inst.arg];
                } break;
            case OP_JEZ:
                {
                    ASSERT(inst.arg < (int)func->labels.count);
                    size_t new_pc = func->labels.items[inst.arg];

                    Value a = frame->stack[--frame->sp];
                    switch(a.kind) {
                    case VALUE_NIL:
                        pc = new_pc;
                        break;
                    case VALUE_INT:
                        if(a.intv == 0) pc = new_pc;
                        break;
                    default:
                        ASSERT(0 && "Invalid value kind");
                        break;
                    }
                } break;
            case OP_PRINT:
                {
                    if(inst.arg > (int)frame->sp) {
                        fprintf(stderr, "%d %zu\n", inst.arg, frame->sp);
                        ASSERT(inst.arg <= (int)frame->sp);
                    }
                    Value *start = &frame->stack[frame->sp - inst.arg];
                    frame->sp -= inst.arg;
                    print_value(start, inst.arg);
                } break;
            default:
                ASSERT(0 && "Not implemented or Unreachable");
                break;
        }

        if(print_iter < PRINT_CAP) {
            print_iter += 1;
            printf("=========================\n");
            printf("[%zu] %s %d\n", pc, opcode_names[inst.opcode], inst.arg);
            printf("STACK [callframe=%zu, sp=%zu]\n", frame_ptr, frame->sp);
            for(size_t i = 0; i < frame->sp; ++i) {
                Value a = frame->stack[i];
                print_value(&a, 1);
            }
            printf("=========================\n");
        }
    }
    return true;
}

bool run_module(Runtime *runtime, Module *module)
{
    CallFrame *frames = malloc(sizeof(*frames) * 1024);
    Value *stack_values = malloc(sizeof(*stack_values) * 1024);
    runtime->call_frames.items    = frames;
    runtime->call_frames.capacity = 1024;
    Function *mainfn = &module->functions.items[0];
    Value retval = {0};
    bool result = run_function(runtime, module, mainfn, NULL, 0, &retval);
    free(frames);
    return result;
}

typedef struct {
    bool is_function_root;
    shtable_t name_table;
} Scope;

typedef struct {
    Arena *temp_arena;
    Arena *prog_arena;

    Runtime *runtime;
    struct {
        Scope *items;
        size_t count;
        size_t capacity;
    } scopes;
} Parser;

bool scope_begin(Parser *parser, bool is_function_root)
{
    Scope scope = {0};
    scope.is_function_root = is_function_root;
    arena_da_append(parser->prog_arena, &parser->scopes, scope);
    return true;
}

bool scope_end(Parser *parser, bool is_function_root)
{
    Scope scope = da_pop(&parser->scopes);
    ASSERT(scope.is_function_root == is_function_root);
    return true;
}

bool scope_hasvar(Parser *parser, const char *name)
{
    int start = parser->scopes.count - 1;
    for(int scopeptr = start; scopeptr >= 0; --scopeptr) {
        Scope *scope = &parser->scopes.items[scopeptr];
        if(shtable_geti(&scope->name_table, name) >= 0) {
            return true;
        }
        if(scope->is_function_root) break;
    }
    return false;
}

int scope_getvar(Parser *parser, const char *name)
{
    int start = parser->scopes.count - 1;
    for(int scopeptr = start; scopeptr >= 0; --scopeptr) {
        Scope *scope = &parser->scopes.items[scopeptr];
        int index = shtable_geti(&scope->name_table, name);
        if(index >= 0) {
            return (intptr_t)scope->name_table.items[index].value;
        }
        if(scope->is_function_root) break;
    }
    return -1;
}

void scope_setvar(Parser *parser, const char *name, int slot)
{
    shtable_set(&parser->scopes.items[parser->scopes.count - 1].name_table, name, (void*)(intptr_t)slot);
}

bool parse_expr(Parser *parser, Lexer *lex, Module *module, Function *func);
bool parse_expr_primary(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!lexer_get_token(lex)) return false;
    switch(lex->token) {
    case TOKEN_INT_LIT:
        {
            int arg = module->intconsts.count;
            da_append(&module->intconsts, lex->int_number);
            add_inst_to_function(func, 
                    ((Inst){ .opcode = OP_PUSH_INT, .arg = arg }), 
                    module);
        } break;
    case TOKEN_CHAR_LIT:
        {
            int arg = module->intconsts.count;
            da_append(&module->intconsts, lex->int_number);
            add_inst_to_function(func, 
                    ((Inst){ .opcode = OP_PUSH_INT, .arg = arg }), 
                    module);
        } break;
    case TOKEN_FLOAT_LIT:
        {
            int arg = module->fltconsts.count;
            da_append(&module->fltconsts, lex->real_number);
            add_inst_to_function(func, 
                    ((Inst){ .opcode = OP_PUSH_FLT, .arg = arg }), 
                    module);
        } break;
    case TOKEN_STRING_LIT:
        {
            int arg = module->strconsts.count;
            da_append_many(&module->strconsts, lex->string, cstrsz(lex->string));
            add_inst_to_function(func, 
                    ((Inst){ .opcode = OP_PUSH_STR, .arg = arg }), 
                    module);
        } break;
    case TOKEN_ID:
        {
            const char *name = lex->string;
            if(scope_hasvar(parser, name)) {
                int arg = scope_getvar(parser, name);
                add_inst_to_function(func, 
                        ((Inst){ .opcode = OP_LOCAL_GET, .arg = arg }), 
                        module);
            } else {
                if(runtime_hasglobal(parser->runtime, name)) {
                    int arg = runtime_getglobal(parser->runtime, name);
                    add_inst_to_function(func, 
                            ((Inst){ .opcode = OP_GLOBAL_GET, .arg = arg }), 
                            module);
                } else {
                    lexer_diagf(lexer_loc(lex), "error: variable with name `%s` is not exists", name);
                    return false;
                }
            }
        } break;
    case TOKEN_OBRACKET:
        {
            if(!lexer_get_and_expect_token(lex, TOKEN_CBRACKET)) return false;
            add_inst_to_function(func, 
                    ((Inst){ .opcode = OP_NEW_ARRAY }), 
                    module);
        } break;
    default:
        break;
    }
    return true;
}

bool parse_expr_postfix(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_primary(parser, lex, module, func)) return false;

    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        switch(lex->token) {
            case TOKEN_OPAREN:
                {
                    size_t argc = 0;
                    while(1) {
                        ParsePoint savedp = lex->parse_point;
                        if(!lexer_get_token(lex)) return false;
                        if(lex->token == TOKEN_CPAREN) break;
                        lex->parse_point = savedp;

                        if(!parse_expr(parser, lex, module, func)) return false;
                        argc += 1;
                        if(!lexer_get_token(lex)) return false;
                        if(lex->token == TOKEN_CPAREN) break;
                        if(!lexer_expect_token(lex, TOKEN_COMMA)) return false;
                    }
                    add_inst_to_function(func, (Inst){ .opcode = OP_CALL, .arg = argc }, module);
                } break;
            case TOKEN_OBRACKET:
                {
                    if(!parse_expr(parser, lex, module, func)) return false;
                    if(!lexer_get_and_expect_token(lex, TOKEN_CBRACKET)) return false;
                    add_inst_to_function(func, (Inst){ .opcode = OP_GET_ITEM, }, module);
                } break;
            default:
                lex->parse_point = savedp;
                return true;
        }
    }

    return false;
}

bool parse_expr_unary(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_postfix(parser, lex, module, func)) return false;
    return true;
}

bool parse_expr_binop1(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_unary(parser, lex, module, func)) return false;
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        Opcode op = {0};
        switch(lex->token) {
            case TOKEN_MUL:
                op = OP_MUL;
                break;
            default:
                // Not a binary operation
                lex->parse_point = savedp;
                return true;
        }
        if(!parse_expr_unary(parser, lex, module, func)) return false;
        add_inst_to_function(func, ((Inst){ .opcode = op }), module);
    }
}

bool parse_expr_binop2(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_binop1(parser, lex, module, func)) return false;
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        Opcode op = {0};
        switch(lex->token) {
            case TOKEN_PLUS:
                op = OP_ADD;
                break;
            case TOKEN_MINUS:
                op = OP_SUB;
                break;
            default:
                // Not a binary operation
                lex->parse_point = savedp;
                return true;
        }
        if(!parse_expr_binop1(parser, lex, module, func)) return false;
        add_inst_to_function(func, ((Inst){ .opcode = op }), module);
    }
}

bool parse_expr_binop3(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_binop2(parser, lex, module, func)) return false;
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        Opcode op = {0};
        switch(lex->token) {
            case TOKEN_LESS:
                op = OP_LT;
                break;
            case TOKEN_LESSEQ:
                op = OP_LE;
                break;
            case TOKEN_GREATER:
                op = OP_GT;
                break;
            case TOKEN_GREATEREQ:
                op = OP_GE;
                break;
            default:
                // Not a binary operation
                lex->parse_point = savedp;
                return true;
        }
        if(!parse_expr_binop2(parser, lex, module, func)) return false;
        add_inst_to_function(func, ((Inst){ .opcode = op }), module);
    }
}

bool parse_expr_binop4(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_binop3(parser, lex, module, func)) return false;
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        Opcode op = {0};
        switch(lex->token) {
            case TOKEN_EQEQ:
                op = OP_EQ;
                break;
            case TOKEN_NOTEQ:
                op = OP_NE;
                break;
            default:
                // Not a binary operation
                lex->parse_point = savedp;
                return true;
        }
        if(!parse_expr_binop3(parser, lex, module, func)) return false;
        add_inst_to_function(func, ((Inst){ .opcode = op }), module);
    }
}

bool parse_expr_binop5(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_binop4(parser, lex, module, func)) return false;
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        Opcode op = {0};
        switch(lex->token) {
            case TOKEN_ANDAND:
                op = OP_AND;
                break;
            default:
                // Not a binary operation
                lex->parse_point = savedp;
                return true;
        }
        if(!parse_expr_binop4(parser, lex, module, func)) return false;
        add_inst_to_function(func, ((Inst){ .opcode = op }), module);
    }
}

bool parse_expr_binop6(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_binop5(parser, lex, module, func)) return false;
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        Opcode op = {0};
        switch(lex->token) {
            case TOKEN_OROR:
                op = OP_OR;
                break;
            default:
                // Not a binary operation
                lex->parse_point = savedp;
                return true;
        }
        if(!parse_expr_binop5(parser, lex, module, func)) return false;
        add_inst_to_function(func, ((Inst){ .opcode = op }), module);
    }
}

bool parse_expr(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    if(!parse_expr_binop6(parser, lex, module, func)) return false;
    return true;
}

bool parse_block(Parser *parser, Lexer *lex, Module *module, Function *func);
bool parse_stmt(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    ParsePoint savedp = lex->parse_point;
    if(!lexer_get_token(lex)) return false;
    switch(lex->token) {
        case TOKEN_RETURN:
            {
                if(!parse_expr(parser, lex, module, func)) return false;
                add_inst_to_function(func, ((Inst){ .opcode = OP_RET }), module);
            } break;
        case TOKEN_FUN:
            {
                if(!lexer_get_and_expect_token(lex, TOKEN_ID)) return false;
                int new_func_id = add_new_function_to_module(module, lex->string);

                scope_begin(parser, true);
                Function *newfunc = &module->functions.items[new_func_id];
                if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                while(1) {
                    ParsePoint savedp = lex->parse_point;
                    if(!lexer_get_token(lex)) return false;
                    if(lex->token != TOKEN_ID) {
                        lex->parse_point = savedp;
                        break;
                    }
                    if(scope_hasvar(parser, lex->string)) {
                        lexer_diagf(lexer_loc(lex), 
                                "error: variable with name `%s` is already exists. Could not re-initialize one",
                                lex->string);
                        return false;
                    }
                    newfunc->params_count += 1;
                    size_t local_slot = newfunc->locals_count++;
                    scope_setvar(parser, arena_strdup(parser->prog_arena, lex->string), local_slot);

                    savedp = lex->parse_point;
                    if(!lexer_get_token(lex)) return false;
                    if(lex->token != TOKEN_COMMA) {
                        lex->parse_point = savedp;
                        break;
                    }
                }
                if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;

                { // add a local for the function itself
                    Function *newfunc = &module->functions.items[new_func_id];
                    size_t local_slot = newfunc->locals_count++;
                    // TODO: We use function name here for the table.
                    //       In the future we might want to use an index
                    //       to the string constants in Module for function
                    //       name consider using Parser's prog_arena for
                    //       name that's in the scope's name table
                    scope_setvar(parser, newfunc->name, local_slot);
                    add_inst_to_function(newfunc, ((Inst){ .opcode = OP_PUSH_FUNC, .arg = new_func_id, }), module);
                    add_inst_to_function(newfunc, ((Inst){ .opcode = OP_LOCAL_SET, .arg = local_slot, }), module);
                }

                if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                while(1) {
                    ParsePoint savedp = lex->parse_point;
                    if(!lexer_get_token(lex)) return false;
                    if(lex->token == TOKEN_CCURLY) break;
                    lex->parse_point = savedp;
                    // in case the pointer is changed because appending new function inside the function's body
                    Function *newfunc = &module->functions.items[new_func_id];
                    if(!parse_stmt(parser, lex, module, newfunc)) return false;
                    arena_reset(parser->temp_arena);
                }
                scope_end(parser, true);

                {
                    Function *newfunc = &module->functions.items[new_func_id];
                    size_t local_slot = func->locals_count++;
                    // TODO: We use function name here for the table.
                    //       In the future we might want to use an index
                    //       to the string constants in Module for function
                    //       name consider using Parser's prog_arena for
                    //       name that's in the scope's name table
                    scope_setvar(parser, newfunc->name, local_slot);
                    add_inst_to_function(func, ((Inst){ .opcode = OP_PUSH_FUNC, .arg = new_func_id, }), module);
                    add_inst_to_function(func, ((Inst){ .opcode = OP_LOCAL_SET, .arg = local_slot, }), module);
                }
            } break;
        case TOKEN_VAR:
            {
                if(!lexer_get_and_expect_token(lex, TOKEN_ID)) return false;
                const char *name = arena_strdup(parser->prog_arena, lex->string);
                if(scope_hasvar(parser, name)) {
                    lexer_diagf(lexer_loc(lex), 
                            "error: variable with name `%s` is already exists. Could not re-initialize one",
                            name);
                    return false;
                }
                size_t local_slot = func->locals_count++;
                scope_setvar(parser, name, local_slot);
                if(!lexer_get_and_expect_token(lex, TOKEN_EQ)) return false;
                if(!parse_expr(parser, lex, module, func)) return false;
                add_inst_to_function(func, ((Inst){ .opcode = OP_LOCAL_SET, .arg = local_slot, }), module);
            } break;
        case TOKEN_WHILE:
            {
                size_t label_start = func->labels.count; // nop is what pointed by label_start
                add_inst_to_function(func, ((Inst){ .opcode = OP_NOP }), module);
                size_t label_start_pc = func->insts.count - 1;
                da_append(&func->labels, label_start_pc);

                size_t label_end = func->labels.count;
                da_append(&func->labels, 0);

                if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                if(!parse_expr(parser, lex, module, func)) return false;

                // Check if the top of the stack is true
                add_inst_to_function(func, ((Inst){ .opcode = OP_JEZ, .arg = label_end }), module);

                if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                // TODO: support for continue and break statement
                if(!parse_block(parser, lex, module, func)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                add_inst_to_function(func, ((Inst){ .opcode = OP_JMP, .arg = label_start }), module);
                add_inst_to_function(func, ((Inst){ .opcode = OP_NOP }), module);

                size_t label_end_pc = func->insts.count - 1;
                func->labels.items[label_end] = label_end_pc;
            } break;
        case TOKEN_IF:
            {
                if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                if(!parse_expr(parser, lex, module, func)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                // Check if the top of the stack is true and jump to the pre-alloacted label_next
                size_t label_next = func->labels.count;
                da_append(&func->labels, 0);
                add_inst_to_function(func, ((Inst){ .opcode = OP_JEZ, .arg = label_next }), module);

                if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                if(!parse_block(parser, lex, module, func)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;

                ParsePoint savedp = lex->parse_point;
                if(!lexer_get_token(lex)) return false;
                if(lex->token != TOKEN_ELSE) {
                    lex->parse_point = savedp;
                    // patch label_next to the end of this if() {} statement
                    add_inst_to_function(func, ((Inst){ .opcode = OP_NOP, }), module);
                    func->labels.items[label_next] = func->insts.count - 1;
                } else {
                    // pre-allocate the end label
                    size_t label_end  = func->labels.count;
                    da_append(&func->labels, 0);
                    // the first if statement will jump to label_end as soon as it finished it's body
                    add_inst_to_function(func, ((Inst){ .opcode = OP_JMP, .arg = label_end }), module);

                    while(lex->token == TOKEN_ELSE) {
                        if(!lexer_get_token(lex)) return false;
                        if(lex->token == TOKEN_IF) {
                            // else if
                            // patch label_next for the previous if to jump if it's condition is false
                            add_inst_to_function(func, ((Inst){ .opcode = OP_NOP, }), module);
                            func->labels.items[label_next] = func->insts.count - 1;

                            if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                            if(!parse_expr(parser, lex, module, func)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                            // Check if the top of the stack is true and jump to the pre-alloacted label_next
                            label_next = func->labels.count;
                            da_append(&func->labels, 0);
                            add_inst_to_function(func, ((Inst){ .opcode = OP_JEZ, .arg = label_next }), module);

                            if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                            if(!parse_block(parser, lex, module, func)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                            // After finished just go to label_end
                            add_inst_to_function(func, ((Inst){ .opcode = OP_JMP, .arg = label_end }), module);

                            if(!lexer_get_token(lex)) return false;
                        } else {
                            // else 
                            // patch label_next for the previous if to jump if it's condition is false
                            add_inst_to_function(func, ((Inst){ .opcode = OP_NOP, }), module);
                            func->labels.items[label_next] = func->insts.count - 1;

                            if(!lexer_expect_token(lex, TOKEN_OCURLY)) return false;
                            if(!parse_block(parser, lex, module, func)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                            // lastly patch label_end so after each branches finished their body they can
                            // jump to the end of the entire branch statement
                            add_inst_to_function(func, ((Inst){ .opcode = OP_NOP, }), module);
                            func->labels.items[label_end] = func->insts.count - 1;
                            break;
                        }
                    }
                }
            } break;
        case TOKEN_ID:
            {
                const char *name = arena_strdup(parser->prog_arena, lex->string);
                if(!lexer_get_token(lex)) return false;
                if(lex->token == TOKEN_EQ) {
                    if(!scope_hasvar(parser, name)) {
                        lexer_diagf(lexer_loc(lex), 
                                "error: variable with name `%s` is not exists",
                                name);
                        return false;
                    }
                    size_t local_slot = scope_getvar(parser, name);
                    if(!parse_expr(parser, lex, module, func)) return false;
                    add_inst_to_function(func, ((Inst){ .opcode = OP_LOCAL_SET, .arg = local_slot }), module);
                } else {
                    Token tok = lex->token;
                    lex->parse_point = savedp;
                    switch(tok) {
                    case TOKEN_OBRACKET:
                        {
                            // TODO: we only support xs[0], how about xs[0][1] ??
                            if(!parse_expr_primary(parser, lex, module, func)) return false;
                            while(1) {
                                if(!lexer_get_and_expect_token(lex, TOKEN_OBRACKET)) return false;
                                if(!parse_expr(parser, lex, module, func)) return false;
                                if(!lexer_get_and_expect_token(lex, TOKEN_CBRACKET)) return false;

                                ParsePoint savedp = lex->parse_point;
                                if(!lexer_get_token(lex)) return false;
                                Token next_token = lex->token;
                                lex->parse_point = savedp;
                                if(next_token != TOKEN_OBRACKET) break;
                                add_inst_to_function(func, (Inst){ .opcode = OP_GET_ITEM, }, module);
                            }
                            if(!lexer_get_and_expect_token(lex, TOKEN_EQ)) return false;
                            if(!parse_expr(parser, lex, module, func)) return false;
                            add_inst_to_function(func, (Inst){ .opcode = OP_SET_ITEM, }, module);
                        } break;
                    default:
                        if(!parse_expr(parser, lex, module, func)) return false;
                        break;
                    }
                }
            } break;
        case TOKEN_PRINT:
            {
                if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                size_t n = 1;
                while(true) {
                    if(!parse_expr(parser, lex, module, func)) return false;
                    ParsePoint savedp = lex->parse_point;
                    if(!lexer_get_token(lex)) return false;
                    if(lex->token != TOKEN_COMMA) {
                        add_inst_to_function(func, ((Inst){ .opcode = OP_PRINT, .arg = n }), module);
                        lex->parse_point = savedp;
                        break;
                    }
                    n += 1;
                }
                if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
            } break;
        case TOKEN_SEMICOLON:
            break;
        default:
            lexer_diagf(lexer_loc(lex), "invalid token %s", lexer_display_token(lex->token));
            return false;
    }
    return true;
}

bool parse_block(Parser *parser, Lexer *lex, Module *module, Function *func)
{
    scope_begin(parser, false);
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        if(lex->token == TOKEN_CCURLY) {
            lex->parse_point = savedp;
            break;
        }
        lex->parse_point = savedp;
        if(!parse_stmt(parser, lex, module, func)) return false;
        arena_reset(parser->temp_arena);
    }
    scope_end(parser, false);
    return true;
}

bool parse_program(Parser *parser, Lexer *lex, Module *module)
{
    int mainfn = add_new_function_to_module(module, "__main__");

    scope_begin(parser, true);
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        if(lex->token == TOKEN_EOF) break;
        lex->parse_point = savedp;
        if(!parse_stmt(parser, lex, module, &module->functions.items[mainfn])) return false;
        arena_reset(parser->temp_arena);
    }
    scope_end(parser, true);
    return true;
}

void dump_module(Module *module)
{
    printf("const ints = {\n");
    for(size_t i = 0; i < module->intconsts.count; ++i) {
        printf("    [%zu] = %d\n", i, module->intconsts.items[i]);
    }

    printf("}\n");
    for(size_t i = 0; i < module->functions.count; ++i) {
        Function *func = &module->functions.items[i];
        printf("func.%s(%zu) {\n", func->name, func->params_count);
        if(func->insts.count > 0) {
            printf("@code\n");
            for(size_t pc = 0; pc < func->insts.count; ++pc) {
                Inst inst = func->insts.items[pc];
                printf("    [%zu] %s %d\n", pc, opcode_names[inst.opcode], inst.arg);
            }
        }
        if(func->labels.count > 0) {
            printf("@labels\n");
            for(size_t i = 0; i < func->labels.count; ++i) {
                printf("    [%zu] = %zu\n", i, func->labels.items[i]);
            }
        }
        printf("}\n");
    }
}

#ifndef YUE_NO_EASTER_EGG
#include <stdlib.h>
#include <time.h>
int rand_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}
#endif

Value f_rand_range(Context *ctx, Value *args, size_t argc)
{
    ASSERT(argc >= 2 && "rand_range expect more than 2 arguments");
    Value minv = args[0];
    Value maxv = args[1];
    ASSERT(minv.kind == VALUE_INT);
    ASSERT(maxv.kind == VALUE_INT);
    Value result = { .kind = VALUE_INT, .intv = rand_range(minv.intv, maxv.intv) };
    return result;
}

Value f_append(Context *ctx, Value *args, size_t argc) 
{
    ASSERT(argc >= 2 && "append expect more than 2 arguments");
    Value arrv = args[0];
    Value newv = args[1];
    ASSERT(arrv.kind == VALUE_OBJ);
    ASSERT(arrv.objv->kind == OBJECT_ARRAY);

    ObjectArray *arr = (ObjectArray*)arrv.objv;
    da_append(arr, newv);

    return (Value){0};
}

Value f_len(Context *ctx, Value *args, size_t argc) 
{
    ASSERT(argc >= 1 && "len expect more than 1 arguments");
    Value arrv = args[0];
    Value newv = args[1];
    ASSERT(arrv.kind == VALUE_OBJ);
    ASSERT(arrv.objv->kind == OBJECT_ARRAY);

    ObjectArray *arr = (ObjectArray*)arrv.objv;
    return (Value){ .kind = VALUE_INT, .intv = arr->count, };
}

int main(int argc, char *argv[]) {
    StringBuilder source = {0};
    if(argc < 2) {
        fprintf(stderr, "ERROR: provide a file\n");
        fprintf(stderr, "Usage: %s <source.yue>\n", argv[0]);
        return -1;
    }
    const char *source_filepath = argv[1];

#ifndef YUE_NO_EASTER_EGG
    // The name Yue is inspired by a character in one of
    // my favorite game, Rune Factory 2. Never finished it though.
    // The second generation kinda tough to beat.
    srand(time(NULL));
    if(strcmp(source_filepath, "A") == 0) { // You clicked the A button LUL
        static const char *talks[] = {
            "Morning!",
            "Hello!",
            "Good evening!",
            "Thank you!",
            "Welcome!",
            "What's up!",
            "The name's Yue, I am a traveling merchant",
            "Is that for me? Thanks!♪ I really love these! ♪",
            "You can't have my Aquamarine! Is that for me? Thanks!♪ Sparkle sparkle! What can I say, "
                "I just can't resist them!♪",
            "Is that really for me? Thanks! I can certainly put that to good use.",
            "Oh... you don't have to give me that much! I don't like slimy things... "
                "Don't like slimy things at all! Ugh, they make me feel sick! I certainly don't need all this!",
        };
        int d = rand_range(0, ARRLEN(talks) - 1);
        printf("Yue: %s\n", talks[d]);
        return 0;
    }
#endif
    if(!read_entire_file(source_filepath, &source)) return -1;

    Module module = {0};

    Lexer lexer = lexer_new(source_filepath, source.items, source.items + source.count);
    Parser parser = {0};

    Arena prog_arena = {0};
    Arena temp_arena = {0};
    parser.temp_arena = &temp_arena;
    parser.prog_arena = &prog_arena;

    Value globals[1024] = {0};
    Runtime runtime = {0};
    runtime.globals.items = globals;
    runtime.globals.capacity = ARRLEN(globals);
    runtime_setglobal(&runtime, "nil", (Value){  .kind = VALUE_NIL });
    runtime_setglobal(&runtime, "true", (Value){ .kind = VALUE_INT, .intv = 1 });
    runtime_setglobal(&runtime, "false", (Value){ .kind = VALUE_INT, .intv = 0 });
    runtime_setglobal(&runtime, "rand_range", (Value){ .kind = VALUE_CFN, .cfn = f_rand_range });
    runtime_setglobal(&runtime, "append", (Value){ .kind = VALUE_CFN, .cfn = f_append });
    runtime_setglobal(&runtime, "len", (Value){ .kind = VALUE_CFN, .cfn = f_len });

    parser.runtime = &runtime;

    if(!parse_program(&parser, &lexer, &module)) return -1;
    /*dump_module(&module);*/
    if(!run_module(&runtime, &module)) return false;

    arena_destroy(&prog_arena);
    arena_destroy(&temp_arena);
    return 0;
}
