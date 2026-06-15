#include <stdio.h>
#include "utils.h"
#include "lexer.h"
#include "shtable.h"

typedef enum Op {
    OP_NOP = 0,
    OP_PUSH_INT,
    OP_PUSH_FLT,
    OP_PUSH_STR,

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

    OP_STRING_CONCAT,
    OP_STRING_LENGTH,
    OP_STRING_GET,
    OP_STRING_SET,

    /*OP_ARRAY_NEW,*/
    /*OP_ARRAY_POP,*/
    /*OP_ARRAY_APPEND,*/
    /*OP_ARRAY_INSERT,*/
    /*OP_ARRAY_REMOVE,*/
    /*OP_ARRAY_LENGTH,*/
    /*OP_ARRAY_CONCAT,*/

    OP_LOCAL_SET,
    OP_LOCAL_GET,
    OP_GLOBAL_GET,

    OP_JMP,
    OP_JEZ,
    OP_PRINT,
} Opcode;

const char *opcode_names[] = {
    [OP_NOP] = "OP_NOP",
    [OP_PUSH_INT] = "OP_PUSH_INT",
    [OP_PUSH_FLT] = "OP_PUSH_FLT",
    [OP_PUSH_STR] = "OP_PUSH_STR",

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

    [OP_STRING_CONCAT] = "OP_STRING_CONCAT",
    [OP_STRING_LENGTH] = "OP_STRING_LENGTH",
    [OP_STRING_GET] = "OP_STRING_GET",
    [OP_STRING_SET] = "OP_STRING_SET",

    [OP_LOCAL_SET] = "OP_LOCAL_SET",
    [OP_LOCAL_GET] = "OP_LOCAL_GET",
    [OP_GLOBAL_GET] = "OP_GLOBAL_GET",

    [OP_JMP] = "OP_JMP",
    [OP_JEZ] = "OP_JEZ",
    [OP_PRINT] = "OP_PRINT",
};

typedef struct {
    Opcode opcode;
    int    arg;
} Inst;

typedef struct {
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
        size_t *items;
        size_t  count;
        size_t  capacity;
    } labels;
    struct {
        Inst  *items;
        size_t count;
        size_t capacity;
    } insts;

    size_t params_count; // arity

    // NOTE: locals must be at function level
    //       That means that this is actually 
    //       not Module but function
    size_t locals_count;
} Module;

typedef struct Value Value;
typedef enum {
    VALUE_NIL = 0,
    VALUE_INT,
    VALUE_FLT,
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

typedef struct Value {
    ValueKind kind;
    union {
        int     intv;
        float   fltv;
        Object *objv;
    };
} Value;

typedef struct Stack {
    Value *items;
    size_t count;
    size_t capacity;
} Stack;

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

    Stack stack;
    Value locals[1024];
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
    for(size_t i = 0; i < ARRLEN(runtime->locals); ++i) {
        Value val = runtime->locals[i];
        if(val.kind == VALUE_OBJ) 
            runtime_mark_object(runtime, val.objv);
    }
    // Mark objects in stack
    for(size_t i = 0; i < runtime->stack.count; ++i) {
        Value val = runtime->stack.items[i];
        if(val.kind == VALUE_OBJ) 
            runtime_mark_object(runtime, val.objv);
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

Object *runtime_pushnewstring(Runtime *runtime, const char *init_text)
{
    ObjectString *obj = (ObjectString*)runtime_newobject(runtime, OBJECT_STRING, sizeof(ObjectString));
    obj->count    = 0;
    obj->capacity = 0;
    obj->items    = 0;
    if(init_text) 
        da_append_many(obj, init_text, cstrsz(init_text));
    sa_append(&runtime->stack, ((Value){ .kind = VALUE_OBJ, .objv = (Object*)obj }));
    return (Object*)obj;
}

Object *runtime_pushnewarray(Runtime *runtime)
{
    ObjectArray *obj = (ObjectArray*)runtime_newobject(runtime, OBJECT_ARRAY, sizeof(ObjectArray));
    obj->count    = 0;
    obj->capacity = 0;
    obj->items    = 0;
    sa_append(&runtime->stack, ((Value){ .kind = VALUE_OBJ, .objv = (Object*)obj }));
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
            case VALUE_OBJ:
                {
                    switch(a.objv->kind) {
                        case OBJECT_STRING:
                            {
                                ObjectString *str = (ObjectString*)a.objv;
                                printf("%.*s", (int)str->count, str->items);
                            } break;
                        case OBJECT_ARRAY:
                            ASSERT(0 && "Not implemented");
                            break;
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

bool run_module(Module *module, Runtime *runtime)
{
    size_t pc = 0;

    Value stack_values[1024];
    runtime->stack.items = stack_values;
    runtime->stack.capacity = ARRLEN(stack_values);

    int print_n_cycles  = 0;
    int printed_n_times = 0;

    while(1) {
        if(pc >= module->insts.count) break;
        Inst inst = module->insts.items[pc++];
        switch(inst.opcode) {
            case OP_NOP:
                break;
            case OP_PUSH_INT:
                sa_append(&runtime->stack, ((Value){ .kind = VALUE_INT, .intv = module->intconsts.items[inst.arg] }));
                break;
            case OP_PUSH_FLT:
                sa_append(&runtime->stack, ((Value){ .kind = VALUE_FLT, .fltv = module->fltconsts.items[inst.arg] }));
                break;
            case OP_PUSH_STR:
                runtime_pushnewstring(runtime, &module->strconsts.items[inst.arg]);
                break;

            case OP_STRING_LENGTH:
                {
                    Value a = sa_pop(&runtime->stack);
                    ASSERT(a.kind == VALUE_OBJ);
                    ASSERT(a.objv->kind == OBJECT_STRING);
                    ObjectString *str = (ObjectString*)a.objv;
                    sa_append(&runtime->stack, ((Value){ .kind = VALUE_INT, .intv= str->count }));
                } break;
            case OP_STRING_CONCAT:
                {
                    Value a = sa_pop(&runtime->stack);
                    Value b = sa_pop(&runtime->stack);
                    ASSERT(a.kind == VALUE_OBJ);
                    ASSERT(a.objv->kind == OBJECT_STRING);
                    ASSERT(b.kind == VALUE_OBJ);
                    ASSERT(b.objv->kind == OBJECT_STRING);
                    ObjectString *str_a = (ObjectString*)a.objv;
                    ObjectString *str_b = (ObjectString*)b.objv;
                    da_append_many(str_b, str_a->items, str_a->count);
                    sb_append_char(str_b, 0);
                    sa_append(&runtime->stack, b);
                } break;
            case OP_STRING_GET:
                {
                    Value objv = sa_pop(&runtime->stack);
                    Value index = sa_pop(&runtime->stack);
                    ASSERT(objv.kind == VALUE_OBJ);
                    ASSERT(index.kind == VALUE_INT);
                    ASSERT(objv.objv->kind == OBJECT_STRING);
                    ObjectString *str = (ObjectString*)objv.objv;
                    ASSERT(index.intv < (int)str->count);
                    sa_append(&runtime->stack, ((Value){ .kind = VALUE_INT, .intv = str->items[index.intv] }));
                } break;
            case OP_STRING_SET:
                {
                    Value objv  = sa_pop(&runtime->stack);
                    Value index = sa_pop(&runtime->stack);
                    Value item  = sa_pop(&runtime->stack);
                    ASSERT(objv.kind  == VALUE_OBJ);
                    ASSERT(index.kind == VALUE_INT);
                    ASSERT(item.kind  == VALUE_INT);
                    ASSERT(objv.objv->kind == OBJECT_STRING);
                    ObjectString *str = (ObjectString*)objv.objv;
                    ASSERT(index.intv < (int)str->count);
                    str->items[index.intv] = item.intv;
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
                    Value a = sa_pop(&runtime->stack);
                    Value b = sa_pop(&runtime->stack);
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
                    sa_append(&runtime->stack, b); 
                } break;

            case OP_LOCAL_SET:
                {
                    Value a = sa_pop(&runtime->stack);
                    runtime->locals[inst.arg] = a;
                } break;
            case OP_LOCAL_GET:
                {
                    sa_append(&runtime->stack, runtime->locals[inst.arg]);
                } break;
            case OP_GLOBAL_GET:
                {
                    Value value = runtime->globals.items[inst.arg];
                    sa_append(&runtime->stack, value);
                } break;

            case OP_JMP:
                {
                    ASSERT(inst.arg < (int)module->labels.count);
                    pc = module->labels.items[inst.arg];
                } break;
            case OP_JEZ:
                {
                    ASSERT(inst.arg < (int)module->labels.count);
                    size_t new_pc = module->labels.items[inst.arg];

                    Value a = sa_pop(&runtime->stack);
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
                    ASSERT(inst.arg <= (int)runtime->stack.count);
                    Value *start = &runtime->stack.items[runtime->stack.count - inst.arg];
                    runtime->stack.count -= inst.arg;
                    print_value(start, inst.arg);
                } break;
        }

        if(printed_n_times < print_n_cycles) {
            printed_n_times += 1;
            printf("=========================\n");
            printf("[%zu] %s %d\n", pc, opcode_names[inst.opcode], inst.arg);
            printf("STACK\n");
            for(size_t i = 0; i < runtime->stack.count; ++i) {
                Value a = runtime->stack.items[i];
                print_value(&a, 1);
            }
            printf("=========================\n");
        }

    }
    return true;
}

typedef struct {
    Arena *temp_arena;
    Arena *prog_arena;

    Runtime *runtime;
    struct {
        shtable_t *items;
        size_t count;
        size_t capacity;
    } scopes;
} Parser;

bool scope_begin(Parser *parser)
{
    sa_append(&parser->scopes, ((shtable_t){0}));
    return true;
}

bool scope_end(Parser *parser)
{
    sa_pop(&parser->scopes);
    return true;
}

bool scope_hasvar(Parser *parser, const char *name)
{
    int start = parser->scopes.count - 1;
    for(int scope = start; scope >= 0; --scope) {
        if(shtable_geti(&parser->scopes.items[scope], name) >= 0) {
            return true;
        }
    }
    return false;
}

int scope_getvar(Parser *parser, const char *name)
{
    int start = parser->scopes.count - 1;
    for(int scope = start; scope >= 0; --scope) {
        int index = shtable_geti(&parser->scopes.items[scope], name);
        if(index >= 0) {
            return (intptr_t)parser->scopes.items[scope].items[index].value;
        }
    }
    return -1;
}

void scope_setvar(Parser *parser, const char *name, int slot)
{
    shtable_set(&parser->scopes.items[parser->scopes.count - 1], name, (void*)(intptr_t)slot);
}

bool parse_expr_primary(Parser *parser, Lexer *lex, Module *module)
{
    if(!lexer_get_token(lex)) return false;
    switch(lex->token) {
    case TOKEN_INT_LIT:
        {
            int arg = module->intconsts.count;
            sa_append(&module->intconsts, lex->int_number);
            sa_append(&module->insts, ((Inst){ .opcode = OP_PUSH_INT, .arg = arg }));
        } break;
    case TOKEN_FLOAT_LIT:
        {
            int arg = module->fltconsts.count;
            sa_append(&module->fltconsts, lex->real_number);
            sa_append(&module->insts, ((Inst){ .opcode = OP_PUSH_FLT, .arg = arg }));
        } break;
    case TOKEN_STRING_LIT:
        {
            int arg = module->strconsts.count;
            sa_append_many(&module->strconsts, lex->string, cstrsz(lex->string));
            sa_append(&module->insts, ((Inst){ .opcode = OP_PUSH_STR, .arg = arg }));
        } break;
    case TOKEN_ID:
        {
            const char *name = lex->string;
            if(scope_hasvar(parser, name)) {
                sa_append(&module->insts, ((Inst){ .opcode = OP_LOCAL_GET, .arg = scope_getvar(parser, name) }));
            } else {
                if(runtime_hasglobal(parser->runtime, name)) {
                    sa_append(&module->insts, ((Inst){ .opcode = OP_GLOBAL_GET, 
                                .arg = runtime_getglobal(parser->runtime, name) }));
                } else {
                    lexer_diagf(lexer_loc(lex), "error: variable with name `%s` is not exists", name);
                    return false;
                }
            }
        } break;
    default:
        break;
    }
    return true;
}

bool parse_expr_unary(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_primary(parser, lex, module)) return false;
    return true;
}

bool parse_expr_binop1(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_unary(parser, lex, module)) return false;
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
        if(!parse_expr_unary(parser, lex, module)) return false;
        sa_append(&module->insts, ((Inst){ .opcode = op, }));
    }
}

bool parse_expr_binop2(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop1(parser, lex, module)) return false;
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
        if(!parse_expr_binop1(parser, lex, module)) return false;
        sa_append(&module->insts, ((Inst){ .opcode = op, }));
    }
}

bool parse_expr_binop3(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop2(parser, lex, module)) return false;
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
        if(!parse_expr_binop2(parser, lex, module)) return false;
        sa_append(&module->insts, ((Inst){ .opcode = op, }));
    }
}

bool parse_expr_binop4(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop3(parser, lex, module)) return false;
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
        if(!parse_expr_binop3(parser, lex, module)) return false;
        sa_append(&module->insts, ((Inst){ .opcode = op, }));
    }
}

bool parse_expr_binop5(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop4(parser, lex, module)) return false;
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
        if(!parse_expr_binop4(parser, lex, module)) return false;
        sa_append(&module->insts, ((Inst){ .opcode = op, }));
    }
}

bool parse_expr_binop6(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop5(parser, lex, module)) return false;
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
        if(!parse_expr_binop5(parser, lex, module)) return false;
        sa_append(&module->insts, ((Inst){ .opcode = op, }));
    }
}

bool parse_expr(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop6(parser, lex, module)) return false;
    return true;
}

bool parse_block(Parser *parser, Lexer *lex, Module *module);
bool parse_stmt(Parser *parser, Lexer *lex, Module *module)
{
    if(!lexer_get_token(lex)) return false;
    switch(lex->token) {
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
                size_t local_slot = module->locals_count++;
                scope_setvar(parser, name, local_slot);
                if(!lexer_get_and_expect_token(lex, TOKEN_EQ)) return false;
                if(!parse_expr(parser, lex, module)) return false;
                sa_append(&module->insts, ((Inst){ .opcode = OP_LOCAL_SET, .arg = local_slot }));
            } break;
        case TOKEN_WHILE:
            {
                size_t label_start = module->labels.count; // nop is what pointed by label_start
                sa_append(&module->insts, ((Inst){ .opcode = OP_NOP, }));
                size_t label_start_pc = module->insts.count - 1;
                sa_append(&module->labels, label_start_pc);

                size_t label_end = module->labels.count;
                sa_append(&module->labels, 0);

                if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                if(!parse_expr(parser, lex, module)) return false;

                // Check if the top of the stack is true
                sa_append(&module->insts, ((Inst){ .opcode = OP_JEZ, .arg = label_end }));

                if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                // TODO: support for continue and break statement
                if(!parse_block(parser, lex, module)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                sa_append(&module->insts, ((Inst){ .opcode = OP_JMP, .arg = label_start }));
                sa_append(&module->insts, ((Inst){ .opcode = OP_NOP, }));

                size_t label_end_pc = module->insts.count - 1;
                module->labels.items[label_end] = label_end_pc;
            } break;
        case TOKEN_IF:
            {
                if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                if(!parse_expr(parser, lex, module)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                // Check if the top of the stack is true and jump to the pre-alloacted label_next
                size_t label_next = module->labels.count;
                sa_append(&module->labels, 0);
                sa_append(&module->insts, ((Inst){ .opcode = OP_JEZ, .arg = label_next }));

                if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                if(!parse_block(parser, lex, module)) return false;
                if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;

                ParsePoint savedp = lex->parse_point;
                if(!lexer_get_token(lex)) return false;
                if(lex->token != TOKEN_ELSE) {
                    lex->parse_point = savedp;
                    // patch label_next to the end of this if() {} statement
                    sa_append(&module->insts, ((Inst){ .opcode = OP_NOP, }));
                    module->labels.items[label_next] = module->insts.count - 1;
                } else {
                    // pre-allocate the end label
                    size_t label_end  = module->labels.count;
                    sa_append(&module->labels, 0);
                    // the first if statement will jump to label_end as soon as it finished it's body
                    sa_append(&module->insts, ((Inst){ .opcode = OP_JMP, .arg = label_end }));

                    while(lex->token == TOKEN_ELSE) {
                        if(!lexer_get_token(lex)) return false;
                        if(lex->token == TOKEN_IF) {
                            // else if
                            // patch label_next for the previous if to jump if it's condition is false
                            sa_append(&module->insts, ((Inst){ .opcode = OP_NOP, }));
                            module->labels.items[label_next] = module->insts.count - 1;

                            if(!lexer_get_and_expect_token(lex, TOKEN_OPAREN)) return false;
                            if(!parse_expr(parser, lex, module)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CPAREN)) return false;
                            // Check if the top of the stack is true and jump to the pre-alloacted label_next
                            label_next = module->labels.count;
                            sa_append(&module->labels, 0);
                            sa_append(&module->insts, ((Inst){ .opcode = OP_JEZ, .arg = label_next }));

                            if(!lexer_get_and_expect_token(lex, TOKEN_OCURLY)) return false;
                            if(!parse_block(parser, lex, module)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                            // After finished just go to label_end
                            sa_append(&module->insts, ((Inst){ .opcode = OP_JMP, .arg = label_end }));

                            if(!lexer_get_token(lex)) return false;
                        } else {
                            // else 
                            // patch label_next for the previous if to jump if it's condition is false
                            sa_append(&module->insts, ((Inst){ .opcode = OP_NOP, }));
                            module->labels.items[label_next] = module->insts.count - 1;

                            if(!lexer_expect_token(lex, TOKEN_OCURLY)) return false;
                            if(!parse_block(parser, lex, module)) return false;
                            if(!lexer_get_and_expect_token(lex, TOKEN_CCURLY)) return false;
                            // lastly patch label_end so after each branches finished their body they can
                            // jump to the end of the entire branch statement
                            sa_append(&module->insts, ((Inst){ .opcode = OP_NOP, }));
                            module->labels.items[label_end] = module->insts.count - 1;
                            break;
                        }
                    }
                }
            } break;
        case TOKEN_ID:
            {
                const char *name = arena_strdup(parser->prog_arena, lex->string);
                if(!lexer_get_and_expect_token(lex, TOKEN_EQ)) return false;
                if(!scope_hasvar(parser, name)) {
                    lexer_diagf(lexer_loc(lex), 
                            "error: variable with name `%s` is not exists",
                            name);
                    return false;
                }
                size_t local_slot = scope_getvar(parser, name);
                if(!parse_expr(parser, lex, module)) return false;
                sa_append(&module->insts, ((Inst){ .opcode = OP_LOCAL_SET, .arg = local_slot }));
            } break;
        case TOKEN_PRINT:
            {
                size_t n = 1;
                while(true) {
                    if(!parse_expr(parser, lex, module)) return false;
                    ParsePoint savedp = lex->parse_point;
                    if(!lexer_get_token(lex)) return false;
                    if(lex->token != TOKEN_COMMA) {
                        sa_append(&module->insts, ((Inst){ .opcode = OP_PRINT, .arg = n }));
                        lex->parse_point = savedp;
                        break;
                    }
                    n += 1;
                }
            } break;
        case TOKEN_SEMICOLON:
            break;
        default:
            fprintf(stderr, "invalid token %s\n", lexer_display_token(lex->token));
            return false;
    }
    return true;
}

bool parse_block(Parser *parser, Lexer *lex, Module *module)
{
    scope_begin(parser);
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        if(lex->token == TOKEN_CCURLY) {
            lex->parse_point = savedp;
            break;
        }
        lex->parse_point = savedp;
        if(!parse_stmt(parser, lex, module)) return false;
        arena_reset(parser->temp_arena);
    }
    scope_end(parser);
    return true;
}

bool parse_program(Parser *parser, Lexer *lex, Module *module)
{
    scope_begin(parser);
    while(1) {
        ParsePoint savedp = lex->parse_point;
        if(!lexer_get_token(lex)) return false;
        if(lex->token == TOKEN_EOF) break;
        lex->parse_point = savedp;
        if(!parse_stmt(parser, lex, module)) return false;
        arena_reset(parser->temp_arena);
    }
    scope_end(parser);
    return true;
}

void dump_module(Module *module)
{
    for(size_t pc = 0; pc < module->insts.count; ++pc) {
        Inst inst = module->insts.items[pc];
        printf("[%zu] %s %d\n", pc, opcode_names[inst.opcode], inst.arg);
    }
    printf("int_consts = {\n");
    for(size_t i = 0; i < module->intconsts.count; ++i) {
        printf("    [%zu] = %d\n", i, module->intconsts.items[i]);
    }
    printf("}\n");
    printf("labels = {\n");
    for(size_t i = 0; i < module->labels.count; ++i) {
        printf("    [%zu] = %zu\n", i, module->labels.items[i]);
    }
    printf("}\n");
}

#ifndef YUE_NO_EASTER_EGG
#include <stdlib.h>
#include <time.h>
int rand_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}
#endif

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

    Inst  insts[1024] = {0};
    int   ints[1024] = {0};
    float flts[1024] = {0};
    char  strs[8 * 1024] = {0};
    size_t labels[1024] = {0};
    Module module = {0};
    module.insts.items      = insts;
    module.insts.capacity   = ARRLEN(insts);
    module.intconsts.items  = ints;
    module.intconsts.capacity = ARRLEN(ints);
    module.fltconsts.items  = flts;
    module.fltconsts.capacity = ARRLEN(flts);
    module.strconsts.items  = strs;
    module.strconsts.capacity = ARRLEN(strs);
    module.labels.items  = labels;
    module.labels.capacity = ARRLEN(labels);

    Lexer lexer = lexer_new(source_filepath, source.items, source.items + source.count);
    shtable_t scopes[64] = {0};
    Parser parser = {0};
    parser.scopes.items    = scopes;
    parser.scopes.capacity = ARRLEN(scopes);

    Arena prog_arena = {0};
    Arena temp_arena = {0};
    parser.temp_arena = &temp_arena;
    parser.prog_arena = &prog_arena;

    Value globals[1024] = {0};
    Runtime runtime = {0};
    runtime.globals.items = globals;
    runtime.globals.capacity = ARRLEN(globals);
    runtime_setglobal(&runtime, "pi", (Value){ .kind = VALUE_INT, .intv = 3 });

    parser.runtime = &runtime;

    if(!parse_program(&parser, &lexer, &module)) return -1;
    /*dump_module(&module);*/
    if(!run_module(&module, &runtime)) return false;

    arena_destroy(&prog_arena);
    arena_destroy(&temp_arena);
    return 0;
}
