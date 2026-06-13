#include <stdio.h>
#include "utils.h"
#include "lexer.h"

typedef enum Op {
    OP_NOP = 0,
    OP_PUSH_INT,
    OP_PUSH_FLT,

    OP_ADD,
    OP_MUL,

    OP_LABEL,
    OP_JMP,
    OP_JNZ,
    OP_PRINT,
} Opcode;

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
} Module;

bool parse_expr(Lexer *lex, Arena *prog_arena, Arena *temp_arena, Module *module);

bool parse_expr_literal(Lexer *lex, Arena *prog_arena, Arena *temp_arena, Module *module)
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
    default:
        break;
    }
    return true;
}

bool parse_expr_unary(Lexer *lex, Arena *prog_arena, Arena *temp_arena, Module *module)
{
    if(!parse_expr_literal(lex, prog_arena, temp_arena, module)) return false;
    return true;
}

bool parse_expr_binop1(Lexer *lex, Arena *prog_arena, Arena *temp_arena, Module *module)
{
    if(!parse_expr_unary(lex, prog_arena, temp_arena, module)) return false;
    ParsePoint savedp = lex->parse_point;
    if(!lexer_get_token(lex)) return false;
    Opcode op = {0};
    switch(lex->token) {
    case TOKEN_PLUS:
        op = OP_ADD;
        break;
    default:
        // Not a binary operation
        lex->parse_point = savedp;
        return true;
    }

    if(!parse_expr(lex, prog_arena, temp_arena, module)) return false;
    sa_append(&module->insts, ((Inst){ .opcode = op, }));
    return true;
}

bool parse_expr_binop2(Lexer *lex, Arena *prog_arena, Arena *temp_arena, Module *module)
{
    if(!parse_expr_binop1(lex, prog_arena, temp_arena, module)) return false;
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

    if(!parse_expr(lex, prog_arena, temp_arena, module)) return false;
    sa_append(&module->insts, ((Inst){ .opcode = op, }));
    return true;
}

bool parse_expr(Lexer *lex, Arena *prog_arena, Arena *temp_arena, Module *module)
{
    if(!parse_expr_binop2(lex, prog_arena, temp_arena, module)) return false;
    return true;
}

bool parse_program(Lexer *lex, Arena *prog_arena, Arena *temp_arena, Module *module)
{
    while(1) {
        if(!lexer_get_token(lex)) return false;
        if(lex->token == TOKEN_EOF) break;
        switch(lex->token) {
            case TOKEN_PRINT:
                if(!parse_expr(lex, prog_arena, temp_arena, module)) return false;
                sa_append(&module->insts, ((Inst){ .opcode = OP_PRINT }));
                break;
            default:
                break;
        }
        arena_reset(temp_arena);
    }
    return true;
}

typedef enum {
    VALUE_NIL = 0,
    VALUE_INT,
    VALUE_FLT,
    VALUE_STR,
    VALUE_OBJ,
} ValueKind;

typedef struct {
    ValueKind kind;
    union {
        int   intv;
        float fltv;
        const char *strv;
    };
} Value;

typedef struct Stack {
    Value *items;
    size_t count;
    size_t capacity;
} Stack;

bool run_module(Module *module)
{
    size_t pc = 0;

    Value stack_values[1024];
    Stack stack = {0};
    stack.items = stack_values;
    stack.capacity = ARRLEN(stack_values);

    while(1) {
        if(pc >= module->insts.count) break;
        Inst inst = module->insts.items[pc++];
        switch(inst.opcode) {
            case OP_NOP:
                break;
            case OP_PUSH_INT:
                sa_append(&stack, ((Value){ .kind = VALUE_INT, .intv = module->intconsts.items[inst.arg] }));
                break;
            case OP_PUSH_FLT:
                sa_append(&stack, ((Value){ .kind = VALUE_FLT, .fltv = module->fltconsts.items[inst.arg] }));
                break;

            case OP_ADD:
                {
                    Value a = sa_pop(&stack);
                    Value b = sa_pop(&stack);
                    ASSERT(a.kind == b.kind && a.kind == VALUE_INT);
                    b.intv += a.intv;
                    sa_append(&stack, b); 
                } break;
            case OP_MUL:
                {
                    Value a = sa_pop(&stack);
                    Value b = sa_pop(&stack);
                    ASSERT(a.kind == b.kind && a.kind == VALUE_INT);
                    b.intv *= a.intv;
                    sa_append(&stack, b); 
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
                    Value a = sa_pop(&stack);
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
                        case VALUE_STR:
                            printf("%s\n", a.strv);
                            break;
                        case VALUE_OBJ:
                            printf("<object>\n");
                    }
                } break;
        }
    }
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
    Arena prog_arena = {0};
    Arena temp_arena = {0};

    if(!parse_program(&lexer, &prog_arena, &temp_arena, &module)) return -1;
    if(!run_module(&module)) return false;

    arena_destroy(&prog_arena);
    arena_destroy(&temp_arena);
    return 0;
}
