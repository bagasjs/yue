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

    OP_LOCAL_SET,
    OP_LOCAL_GET,
    OP_GLOBAL_GET,

    OP_LABEL,
    OP_JMP,
    OP_JNZ,
    OP_PRINT,
} Opcode;

const char *opcode_names[] = {
    [OP_NOP] = "OP_NOP",
    [OP_PUSH_INT] = "OP_PUSH_INT",
    [OP_PUSH_FLT] = "OP_PUSH_FLT",

    [OP_ADD] = "OP_ADD",
    [OP_SUB] = "OP_SUB",
    [OP_MUL] = "OP_MUL",

    [OP_LOCAL_SET] = "OP_LOCAL_SET",
    [OP_LOCAL_GET] = "OP_LOCAL_GET",
    [OP_GLOBAL_GET] = "OP_GLOBAL_GET",

    [OP_LABEL] = "OP_LABEL",
    [OP_JMP] = "OP_JMP",
    [OP_JNZ] = "OP_JNZ",
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
    StringBuilder sb;
} ObjectString;

typedef struct {
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
    object->marked = true;
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

void runtime_sweep_unused_objects(Runtime *runtime)
{
    for(size_t i = 0; i < runtime->all.count;) {
        Object *obj = runtime->all.items[i];
        if(obj->marked) {
            obj->marked = false;
            ++i;
        } else {
            // TODO: object_destroy to handle destroying object
            free(obj);
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
                ASSERT(items != NULL && "Buy More RAM LOL!");
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
    obj->sb.count    = 0;
    obj->sb.capacity = 0;
    obj->sb.items    = 0;
    if(init_text) 
        sb_appendf(&obj->sb, "%s", init_text);
    sa_append(&runtime->stack, ((Value){ .kind = VALUE_OBJ, .objv = (Object*)obj }));
    return (Object*)obj;
}

bool run_module(Module *module, Runtime *runtime)
{
    size_t pc = 0;

    Value stack_values[1024];
    runtime->stack.items = stack_values;
    runtime->stack.capacity = ARRLEN(stack_values);

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

            case OP_ADD:
            case OP_MUL:
            case OP_SUB:
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
                ASSERT(0 && "Not implemented");
                break;
            case OP_JNZ:
                ASSERT(0 && "Not implemented");
                break;
            case OP_LABEL:
                break;
            case OP_PRINT:
                {
                    Value a = sa_pop(&runtime->stack);
                    switch(a.kind) {
                        case VALUE_NIL:
                            printf("<nil>\n");
                            break;
                        case VALUE_FLT:
                            printf("%f\n", a.fltv);
                            break;
                        case VALUE_INT:
                            printf("%d\n", a.intv);
                            break;
                        case VALUE_OBJ:
                            {
                                switch(a.objv->kind) {
                                case OBJECT_STRING:
                                    {
                                        ObjectString *str = (ObjectString*)a.objv;
                                        printf("%.*s\n", (int)str->sb.count, str->sb.items);
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
                } break;
        }

        /*printf("=========================\n");*/
        /*printf("[%zu] %s %d\n", pc, opcode_names[inst.opcode], inst.arg);*/
        /*printf("STACK\n");*/
        /*for(size_t i = 0; i < stack.count; ++i) {*/
        /*    Value a = stack.items[i];*/
        /*    switch(a.kind) {*/
        /*        case VALUE_NIL:*/
        /*            printf("[%zu] <nil>\n", i);*/
        /*            break;*/
        /*        case VALUE_FLT:*/
        /*            printf("[%zu] %f\n", i, a.fltv);*/
        /*            break;*/
        /*        case VALUE_INT:*/
        /*            printf("[%zu] %d\n", i, a.intv);*/
        /*            break;*/
        /*        case VALUE_STR:*/
        /*            printf("[%zu] %s\n", i, a.strv);*/
        /*            break;*/
        /*        case VALUE_OBJ:*/
        /*            printf("[%zu] <object>\n", i);*/
        /*    }*/
        /*}*/

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
    return (intptr_t)shtable_get(&parser->scopes.items[parser->scopes.count - 1], name);
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

bool parse_expr_binop2(Parser *parser, Lexer *lex, Module *module)
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

bool parse_expr_binop1(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop2(parser, lex, module)) return false;
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
        if(!parse_expr_binop2(parser, lex, module)) return false;
        sa_append(&module->insts, ((Inst){ .opcode = op, }));
    }
}

bool parse_expr(Parser *parser, Lexer *lex, Module *module)
{
    if(!parse_expr_binop1(parser, lex, module)) return false;
    return true;
}

bool parse_program(Parser *parser, Lexer *lex, Module *module)
{
    scope_begin(parser);
    while(1) {
        if(!lexer_get_token(lex)) return false;
        if(lex->token == TOKEN_EOF) break;
        switch(lex->token) {
            case TOKEN_VAR:
                {
                    if(!lexer_get_and_expect_token(lex, TOKEN_ID)) return false;
                    const char *name = arena_strdup(parser->temp_arena, lex->string);
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
                    sa_append(&module->insts, ((Inst){ .opcode = OP_LOCAL_SET }));
                } break;
            case TOKEN_PRINT:
                if(!parse_expr(parser, lex, module)) return false;
                sa_append(&module->insts, ((Inst){ .opcode = OP_PRINT }));
                break;
            default:
                break;
        }
        arena_reset(parser->temp_arena);
    }
    scope_end(parser);
    return true;
}

int main(int argc, char *argv[]) {
    StringBuilder source = {0};
    if(argc < 2) {
        fprintf(stderr, "ERROR: provide a file\n");
        fprintf(stderr, "Usage: %s <source.yue>\n", argv[0]);
        return -1;
    }

    const char *source_filepath = argv[1];
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
    if(!run_module(&module, &runtime)) return false;

    arena_destroy(&prog_arena);
    arena_destroy(&temp_arena);
    return 0;
}
