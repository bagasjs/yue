#include <stdio.h>
#include "utils.h"
#include "lexer.h"
#include "shtable.h"
#include "yue.h"

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
    Loc    loc;
    Opcode opcode;
    int    arg;
} Inst;

// TODO: Replace every ((Inst){ ... }) construction with this function
static inline Inst make_inst(Opcode opcode, int arg, Loc loc) {
    return (Inst) { .loc = loc, opcode = opcode, .arg = arg };
}

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

struct Yue_Object {
    Yue_Object     *next;
    Yue_Object_Kind kind;
    bool            marked;
};

typedef struct Yue_Object_String {
    Yue_Object base;
    Yue_String string;
} Yue_Object_String;

typedef struct Yue_Object_Array {
    Yue_Object base;
    Yue_Array  array;
} Yue_Object_Array;

#define STACK_CAP 1024
typedef struct CallFrame {
    Yue_Value  locals[1024];
    Yue_Value  stack[STACK_CAP];
    size_t sp;
} CallFrame;

typedef struct Runtime {
    struct {
        Yue_Value *items;
        size_t     count;
        size_t     capacity;
    } globals;
    shtable_t globals_nametable;

    struct {
        Yue_Object **items;
        size_t       count;
        size_t       capacity;
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
    return (intptr_t)shtable_get_or(&runtime->globals_nametable, name, (void*)-1);
}

void runtime_setglobal(Runtime *runtime, const char *name, Yue_Value value)
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

void runtime_mark_object(Runtime *runtime, Yue_Object *object)
{
    if(object->marked) return;
    object->marked = true;
    if(object->kind == YUE_OBJECT_ARRAY) {
        Yue_Object_Array *arr = (Yue_Object_Array*)object;
        for(size_t i = 0; i < arr->array.count; ++i) {
            Yue_Value val = arr->array.items[i];
            if(val.kind == YUE_VALUE_OBJ) 
                runtime_mark_object(runtime, val.objv);
        }
    }
}

void runtime_mark_used_objects(Runtime *runtime)
{
    // Mark objects in global variables
    for(size_t i = 0; i < runtime->globals.count; ++i) {
        Yue_Value val = runtime->globals.items[i];
        if(val.kind == YUE_VALUE_OBJ) 
            runtime_mark_object(runtime, val.objv);
    }

    // Mark objects in local variables
    for(size_t i = 0; i < runtime->call_frames.count; ++i) {
        CallFrame *frame = &runtime->call_frames.items[i];
        for(size_t j = 0; j < ARRLEN(frame->locals); ++j) {
            Yue_Value val = frame->locals[j];
            if(val.kind == YUE_VALUE_OBJ) runtime_mark_object(runtime, val.objv);
        }
        // Mark objects in stack
        for(size_t j = 0; j < STACK_CAP; ++j) {
            Yue_Value val = frame->stack[j];
            if(val.kind == YUE_VALUE_OBJ) 
                runtime_mark_object(runtime, val.objv);
        }
    }
}

void runtime_destroy_object(Runtime *runtime, Yue_Object *object)
{
    (void)runtime;
    switch(object->kind) {
    case YUE_OBJECT_NIL:
        free(object);
        break;
    case YUE_OBJECT_STRING:
        {
            Yue_Object_String *str = (Yue_Object_String*)object;
            free(str->string.items);
            free(str);
        } break;
    case YUE_OBJECT_ARRAY:
        {
            Yue_Object_Array *arr  = (Yue_Object_Array*)object;
            free(arr->array.items);
            free(arr);
        } break;
    default:
        ASSERT(0 && "Unreachable");
    }
}

void runtime_sweep_unused_objects(Runtime *runtime)
{
    for(size_t i = 0; i < runtime->all.count;) {
        Yue_Object *obj = runtime->all.items[i];
        if(obj->marked) {
            obj->marked = false;
            ++i;
        } else {
            runtime_destroy_object(runtime, obj);
            da_remove_unordered(&runtime->all, i);
        }
    }
}

Yue_Object *runtime_newobject(Runtime *runtime, Yue_Object_Kind kind, size_t size)
{
    ASSERT(size >= sizeof(Yue_Object));
    Yue_Object *obj  = malloc(size);
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

Yue_Object *runtime_pushnewstring(Runtime *runtime, CallFrame *frame, const char *init_text)
{
    Yue_Object_String *obj = (Yue_Object_String*)runtime_newobject(runtime, YUE_OBJECT_STRING, sizeof(Yue_Object_String));
    obj->string.count    = 0;
    obj->string.capacity = 0;
    obj->string.items    = 0;
    if(init_text) 
        da_append_many(&obj->string, init_text, cstrsz(init_text));
    frame->stack[frame->sp++] = ((Yue_Value){ .kind = YUE_VALUE_OBJ, .objv = (Yue_Object*)obj });
    return (Yue_Object*)obj;
}

Yue_Object *runtime_pushnewarray(Runtime *runtime, CallFrame *frame)
{
    Yue_Object_Array *obj = (Yue_Object_Array*)runtime_newobject(runtime, YUE_OBJECT_ARRAY, sizeof(Yue_Object_Array));
    obj->array.count    = 0;
    obj->array.capacity = 0;
    obj->array.items    = 0;
    frame->stack[frame->sp++] = ((Yue_Value){ .kind = YUE_VALUE_OBJ, .objv = (Yue_Object*)obj });
    return (Yue_Object*)obj;
}

void print_value(Yue_Value *values, size_t count)
{
    for(size_t i = 0; i < count; ++i) {
        if(i != 0) printf(" ");
        Yue_Value a = values[i];
        switch(a.kind) {
            case YUE_VALUE_NIL:
                printf("<nil>");
                break;
            case YUE_VALUE_FLT:
                printf("%f", a.fltv);
                break;
            case YUE_VALUE_INT:
                printf("%d", a.intv);
                break;
            case YUE_VALUE_FUN:
                printf("<fun#%d>", a.fun_id);
                break;
            case YUE_VALUE_OBJ:
                {
                    switch(a.objv->kind) {
                        case YUE_OBJECT_STRING:
                            {
                                Yue_Object_String *str = (Yue_Object_String*)a.objv;
                                printf("%.*s", (int)str->string.count, str->string.items);
                            } break;
                        case YUE_OBJECT_ARRAY:
                            {
                                Yue_Object_Array *arr = (Yue_Object_Array*)a.objv;
                                printf("<array[%zu]>", arr->array.count);
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

bool run_function(Runtime *runtime, Module *module, Function *func, Yue_Value *args, size_t argc, Yue_Value *retval)
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
                frame->stack[frame->sp++] = ((Yue_Value){ .kind = YUE_VALUE_INT, .intv = module->intconsts.items[inst.arg] });
                break;
            case OP_PUSH_FLT:
                frame->stack[frame->sp++] = ((Yue_Value){ .kind = YUE_VALUE_FLT, .fltv = module->fltconsts.items[inst.arg] });
                break;
            case OP_PUSH_FUNC:
                frame->stack[frame->sp++] = ((Yue_Value){ .kind = YUE_VALUE_FUN, .fun_id = inst.arg });
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
                    Yue_Value val = frame->stack[--frame->sp];
                    Yue_Value idx = frame->stack[--frame->sp];
                    Yue_Value obj = frame->stack[--frame->sp];
                    ASSERT(obj.kind == YUE_VALUE_OBJ);
                    switch(obj.objv->kind) {
                    case YUE_OBJECT_ARRAY:
                        {
                            ASSERT(idx.kind == YUE_VALUE_INT);
                            Yue_Object_Array *arr = (Yue_Object_Array*)obj.objv;
                            if(idx.intv < (int)arr->array.count) 
                                arr->array.items[idx.intv] = val;
                        } break;
                    case YUE_OBJECT_STRING:
                        {
                            ASSERT(idx.kind == YUE_VALUE_INT);
                            ASSERT(val.kind == YUE_VALUE_INT);
                            Yue_Object_String *str = (Yue_Object_String*)obj.objv;
                            if(idx.intv < (int)str->string.count) 
                                str->string.items[idx.intv] = (char)val.intv;
                        } break;
                    default:
                        ASSERT(0 && "GET_ITEM Unreachable");
                    }
                } break;
            case OP_GET_ITEM:
                {
                    ASSERT(frame->sp > 2);
                    Yue_Value idx = frame->stack[--frame->sp];
                    Yue_Value obj = frame->stack[--frame->sp];
                    ASSERT(obj.kind == YUE_VALUE_OBJ);
                    switch(obj.objv->kind) {
                    case YUE_OBJECT_ARRAY:
                        {
                            Yue_Object_Array *arr = (Yue_Object_Array*)obj.objv;
                            ASSERT(idx.kind == YUE_VALUE_INT);
                            Yue_Value result = {0};
                            if(idx.intv < (int)arr->array.count) 
                                result = arr->array.items[idx.intv];
                            frame->stack[frame->sp++] = result;
                        } break;
                    case YUE_OBJECT_STRING:
                        {
                            ASSERT(idx.kind == YUE_VALUE_INT);
                            Yue_Object_String *str = (Yue_Object_String*)obj.objv;
                            Yue_Value result = {0};
                            if(idx.intv < (int)str->string.count) 
                                result = (Yue_Value){ .kind = YUE_VALUE_INT, .intv = str->string.items[idx.intv] };
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
                    Yue_Value newfuncv = frame->stack[frame->sp - new_argc - 1];
                    switch(newfuncv.kind) {
                    case YUE_VALUE_FUN:
                        {
                            Function *newfunc = &module->functions.items[newfuncv.fun_id];
                            ASSERT(new_argc >= newfunc->params_count);
                            new_argc = newfunc->params_count; // we only pass the neccessary
                            Yue_Value *args  = &frame->stack[frame->sp - new_argc];
                            Yue_Value retval = {0};
                            if(!run_function(runtime, module, newfunc, args, new_argc, &retval)) return false;
                            frame->sp -= new_argc + 1;
                            frame->stack[frame->sp++] = retval;
                        } break;
                    case YUE_VALUE_CFN:
                        {
                            Yue_Value *args  = &frame->stack[frame->sp - new_argc];
                            Yue_Value retval = {0};
                            newfuncv.cfn(NULL, args, new_argc, &retval);
                            frame->sp -= new_argc + 1;
                            frame->stack[frame->sp++] = retval;
                        } break;
                    default:
                        ASSERT(newfuncv.kind == YUE_VALUE_FUN || newfuncv.kind == YUE_VALUE_CFN);
                        break;
                    }
                } break;
            case OP_RET:
                {
                    Yue_Value a = frame->stack[--frame->sp];
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
                    Yue_Value a = frame->stack[--frame->sp];
                    Yue_Value b = frame->stack[--frame->sp];
                    ASSERT(a.kind == b.kind && a.kind == YUE_VALUE_INT);
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
                    Yue_Value value = runtime->globals.items[inst.arg];
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

                    Yue_Value a = frame->stack[--frame->sp];
                    switch(a.kind) {
                    case YUE_VALUE_NIL:
                        pc = new_pc;
                        break;
                    case YUE_VALUE_INT:
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
                    Yue_Value *start = &frame->stack[frame->sp - inst.arg];
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
                Yue_Value a = frame->stack[i];
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
    Yue_Value *stack_values = malloc(sizeof(*stack_values) * 1024);
    runtime->call_frames.items    = frames;
    runtime->call_frames.capacity = 1024;
    Function *mainfn = &module->functions.items[0];
    Yue_Value retval = {0};
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
    Loc loc = lexer_loc(lex);
    if(!lexer_get_token(lex)) return false;
    switch(lex->token) {
    case TOKEN_INT_LIT:
        {
            int arg = module->intconsts.count;
            da_append(&module->intconsts, lex->int_number);
            add_inst_to_function(func, 
                    make_inst(OP_PUSH_INT, arg, loc), 
                    module);
        } break;
    case TOKEN_CHAR_LIT:
        {
            int arg = module->intconsts.count;
            da_append(&module->intconsts, lex->int_number);
            add_inst_to_function(func, 
                    make_inst(OP_PUSH_INT, arg, loc),
                    module);
        } break;
    case TOKEN_FLOAT_LIT:
        {
            int arg = module->fltconsts.count;
            da_append(&module->fltconsts, lex->real_number);
            add_inst_to_function(func, 
                    make_inst(OP_PUSH_FLT, arg, loc),
                    module);
        } break;
    case TOKEN_STRING_LIT:
        {
            int arg = module->strconsts.count;
            da_append_many(&module->strconsts, lex->string, cstrsz(lex->string));
            add_inst_to_function(func, 
                    make_inst(OP_PUSH_STR, arg, loc),
                    module);
        } break;
    case TOKEN_ID:
        {
            const char *name = lex->string;
            if(scope_hasvar(parser, name)) {
                int arg = scope_getvar(parser, name);
                add_inst_to_function(func, 
                        make_inst(OP_LOCAL_GET, arg, loc), 
                        module);
            } else {
                if(runtime_hasglobal(parser->runtime, name)) {
                    int arg = runtime_getglobal(parser->runtime, name);
                    add_inst_to_function(func, 
                            make_inst(OP_GLOBAL_GET, arg, loc),
                            module);
                } else {
                    lexer_diagf(loc, "error: variable with name `%s` is not exists", name);
                    return false;
                }
            }
        } break;
    case TOKEN_OBRACKET:
        {
            if(!lexer_get_and_expect_token(lex, TOKEN_CBRACKET)) return false;
            add_inst_to_function(func, 
                    make_inst(OP_NEW_ARRAY, 0, loc),
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
                    add_inst_to_function(func, make_inst(OP_CALL, argc, HERE()), module);
                } break;
            case TOKEN_OBRACKET:
                {
                    if(!parse_expr(parser, lex, module, func)) return false;
                    if(!lexer_get_and_expect_token(lex, TOKEN_CBRACKET)) return false;
                    add_inst_to_function(func, make_inst(OP_GET_ITEM, 0, HERE()), module);
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
        add_inst_to_function(func, make_inst(op, 0, HERE()), module);
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
        add_inst_to_function(func, make_inst(op, 0, HERE()), module);
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
        add_inst_to_function(func, make_inst(op, 0, HERE()), module);
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
        add_inst_to_function(func, (make_inst(op, 0, HERE())), module);
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
        add_inst_to_function(func, (make_inst(op, 0, HERE())), module);
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
        add_inst_to_function(func, (make_inst(op, 0, HERE())), module);
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
                add_inst_to_function(func, make_inst( OP_RET, 0, HERE()), module);
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
                    add_inst_to_function(newfunc, make_inst(OP_PUSH_FUNC, new_func_id, HERE()), module);
                    add_inst_to_function(newfunc, make_inst(OP_LOCAL_SET, local_slot, HERE()), module);
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
                    add_inst_to_function(func, make_inst( OP_PUSH_FUNC, new_func_id, HERE()), module);
                    add_inst_to_function(func, make_inst( OP_LOCAL_SET, local_slot, HERE()), module);
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
                add_inst_to_function(func, make_inst(OP_LOCAL_SET, local_slot, HERE()), module);
            } break;
        case TOKEN_WHILE:
            {
                size_t label_start = func->labels.count; // nop is what pointed by label_start
                add_inst_to_function(func, make_inst(OP_NOP, 0, HERE()), module);
                size_t label_start_pc = func->insts.count - 1;
                da_append(&func->labels, label_start_pc);

                size_t label_end = func->labels.count;
                da_append(&func->labels, 0);

                if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                if(!parse_expr(parser, lex, module, func)) return false;

                // Check if the top of the stack is true
                add_inst_to_function(func, make_inst(OP_JEZ, label_end, HERE()), module);

                if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                // TODO: support for continue and break statement
                if(!parse_block(parser, lex, module, func)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                add_inst_to_function(func, make_inst(OP_JMP, label_start, HERE()), module);
                add_inst_to_function(func, make_inst(OP_NOP, 0, HERE()), module);

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
                add_inst_to_function(func, make_inst(OP_JEZ, label_next, HERE()), module);

                if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                if(!parse_block(parser, lex, module, func)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;

                ParsePoint savedp = lex->parse_point;
                if(!lexer_get_token(lex)) return false;
                if(lex->token != TOKEN_ELSE) {
                    lex->parse_point = savedp;
                    // patch label_next to the end of this if() {} statement
                    add_inst_to_function(func, make_inst(OP_NOP, 0, HERE()), module);
                    func->labels.items[label_next] = func->insts.count - 1;
                } else {
                    // pre-allocate the end label
                    size_t label_end  = func->labels.count;
                    da_append(&func->labels, 0);
                    // the first if statement will jump to label_end as soon as it finished it's body
                    add_inst_to_function(func, make_inst(OP_JMP, label_end, HERE()), module);

                    while(lex->token == TOKEN_ELSE) {
                        if(!lexer_get_token(lex)) return false;
                        if(lex->token == TOKEN_IF) {
                            // else if
                            // patch label_next for the previous if to jump if it's condition is false
                            add_inst_to_function(func, make_inst(OP_NOP, 0, HERE()), module);
                            func->labels.items[label_next] = func->insts.count - 1;

                            if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                            if(!parse_expr(parser, lex, module, func)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                            // Check if the top of the stack is true and jump to the pre-alloacted label_next
                            label_next = func->labels.count;
                            da_append(&func->labels, 0);
                            add_inst_to_function(func, make_inst(OP_JEZ, label_next , HERE()), module);

                            if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                            if(!parse_block(parser, lex, module, func)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                            // After finished just go to label_end
                            add_inst_to_function(func, make_inst(OP_JMP, label_end, HERE()), module);

                            if(!lexer_get_token(lex)) return false;
                        } else {
                            // else 
                            // patch label_next for the previous if to jump if it's condition is false
                            add_inst_to_function(func, make_inst(OP_NOP, 0, HERE()), module);
                            func->labels.items[label_next] = func->insts.count - 1;

                            if(!lexer_expect_token(lex, TOKEN_OCURLY)) return false;
                            if(!parse_block(parser, lex, module, func)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                            // lastly patch label_end so after each branches finished their body they can
                            // jump to the end of the entire branch statement
                            add_inst_to_function(func, make_inst(OP_NOP, 0, HERE()), module);
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
                    add_inst_to_function(func, make_inst(OP_LOCAL_SET, local_slot , HERE()), module);
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
                                add_inst_to_function(func, make_inst(OP_GET_ITEM, 0, HERE()), module);
                            }
                            if(!lexer_get_and_expect_token(lex, TOKEN_EQ)) return false;
                            if(!parse_expr(parser, lex, module, func)) return false;
                            add_inst_to_function(func, make_inst(OP_SET_ITEM, 0, HERE()), module);
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
                        add_inst_to_function(func, make_inst(OP_PRINT, n , HERE()), module);
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

int f_rand_range(Yue_Context *ctx, Yue_Value *args, int argc, Yue_Value *retval)
{
    ASSERT(argc >= 2 && "rand_range expect more than 2 arguments");
    Yue_Value minv = args[0];
    Yue_Value maxv = args[1];
    ASSERT(minv.kind == YUE_VALUE_INT);
    ASSERT(maxv.kind == YUE_VALUE_INT);
    *retval = (Yue_Value){ .kind = YUE_VALUE_INT, .intv = rand_range(minv.intv, maxv.intv) };
    return 0;
}

int f_append(Yue_Context *ctx, Yue_Value *args, int argc, Yue_Value *retval) 
{
    ASSERT(argc >= 2 && "append expect more than 2 arguments");
    Yue_Value arrv = args[0];
    Yue_Value newv = args[1];
    ASSERT(arrv.kind == YUE_VALUE_OBJ);
    ASSERT(arrv.objv->kind == YUE_OBJECT_ARRAY);

    Yue_Object_Array *arr = (Yue_Object_Array*)arrv.objv;
    da_append(&arr->array, newv);
    return 0;
}

int f_len(Yue_Context *ctx, Yue_Value *args, int argc, Yue_Value *retval) 
{
    ASSERT(argc >= 1 && "len expect more than 1 arguments");
    Yue_Value arrv = args[0];
    Yue_Value newv = args[1];
    ASSERT(arrv.kind == YUE_VALUE_OBJ);
    ASSERT(arrv.objv->kind == YUE_OBJECT_ARRAY);

    Yue_Object_Array *arr = (Yue_Object_Array*)arrv.objv;
    *retval = (Yue_Value){ .kind = YUE_VALUE_INT, .intv = arr->array.count, };
    return 0;
}

/// Context implementation

struct Yue_Context {
    Arena prog_arena;
    Arena temp_arena;

    Parser    parser;
    Lexer     lexer;
    Runtime   runtime;
    Yue_Value globals[1024];
};

Yue_Context *yue_open(void)
{
    Yue_Context *ctx = malloc(sizeof(*ctx));
    bufset(ctx, 0, sizeof(*ctx));

    ctx->runtime.globals.items    = ctx->globals;
    ctx->runtime.globals.capacity = ARRLEN(ctx->globals);
    ctx->parser.runtime           = &ctx->runtime;
    ctx->parser.prog_arena        = &ctx->prog_arena;
    ctx->parser.temp_arena        = &ctx->temp_arena;
    return ctx;
}

void yue_close(Yue_Context *ctx)
{
    arena_destroy(&ctx->prog_arena);
    arena_destroy(&ctx->temp_arena);
    lexer_destroy(&ctx->lexer);
    free(ctx);
}

int yue_set_global_value(Yue_Context *ctx, const char *name, Yue_Value value)
{
    runtime_setglobal(&ctx->runtime, name, value);
    return 0;
}

int yue_get_global_value(Yue_Context *ctx, const char *name, Yue_Value *value)
{
    int slot = runtime_getglobal(&ctx->runtime, name);
    if(slot < 0) {
        return -1;
    }
    *value = ctx->globals[slot];
    return 0;
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

    Yue_Context *ctx = yue_open();
    ctx->lexer = lexer_new(source_filepath, source.items, source.items + source.count);
    yue_set_global_value(ctx, "nil",   (Yue_Value){ .kind = YUE_VALUE_NIL });
    yue_set_global_value(ctx, "true",  (Yue_Value){ .kind = YUE_VALUE_INT, .intv = 1 });
    yue_set_global_value(ctx, "false", (Yue_Value){ .kind = YUE_VALUE_INT, .intv = 0 });
    yue_set_global_value(ctx, "rand_range", (Yue_Value){ .kind = YUE_VALUE_CFN, .cfn = f_rand_range });
    yue_set_global_value(ctx, "append", (Yue_Value){ .kind = YUE_VALUE_CFN, .cfn = f_append });
    yue_set_global_value(ctx, "len", (Yue_Value){ .kind = YUE_VALUE_CFN, .cfn = f_len });

    if(!parse_program(&ctx->parser, &ctx->lexer, &module)) return -1;
    /*dump_module(&module);*/
    if(!run_module(&ctx->runtime, &module)) return false;

    yue_close(ctx);
    return 0;
}
