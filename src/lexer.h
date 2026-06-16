/*
 * This is a C rewrite of the lexer from: https://github.com/bext-lang/b/blob/main/src/lexer.rs
 * Original version by Tsoding Licensed under the MIT License
 * 
 * Copyright 2025 bagasjs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef LEXER_H_
#define LEXER_H_

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>

#ifndef ARRAY_LEN
#define ARRAY_LEN(xs) (sizeof(xs)/sizeof(*(xs)))
#endif

typedef struct {
    const char *input_path;
    int line_offset;
    int line_number;

    const char *line_start;
    const char *line_end;
} Loc;

#define HERE() (Loc){ __FILE__, -1, __LINE__ }

#define compiler_missingf(loc, ...) _compiler_missingf(__FILE__, __LINE__, loc, __VA_ARGS__)
void _compiler_missingf(const char *file, int line, Loc loc, const char *fmt, ...);
void compiler_diagf(Loc loc, const char *fmt, ...);

#define lexer_missingf(loc, ...) _lexer_missingf(__FILE__, __LINE__, loc, __VA_ARGS__)
void _lexer_missingf(const char *file, int line, Loc loc, const char *fmt, ...);
void lexer_diagf(Loc loc, const char *fmt, ...);

// TODO: instead of registering tokens like this maybe 
//       having the user to register the tokens would be 
//       then I can use the lexer for multiple programming
//       language.
#define PUNCT_TOKEN_LIST    \
    X(QUESTION     , "?"  ) \
    X(OCURLY       , "{"  ) \
    X(CCURLY       , "}"  ) \
    X(OPAREN       , "("  ) \
    X(CPAREN       , ")"  ) \
    X(OBRACKET     , "["  ) \
    X(CBRACKET     , "]"  ) \
    X(SEMICOLON    , ";"  ) \
    X(COLON        , ":"  ) \
    X(COMMA        , ","  ) \
    X(ARROW        , "->") \
    X(MINUSMINUS   , "--" ) \
    X(MINUSEQ      , "-=" ) \
    X(MINUS        , "-"  ) \
    X(PLUSPLUS     , "++" ) \
    X(PLUSEQ       , "+=" ) \
    X(PLUS         , "+"  ) \
    X(MULEQ        , "*=" ) \
    X(MUL          , "*"  ) \
    X(MODEQ        , "%=" ) \
    X(MOD          , "%"  ) \
    X(DIVEQ        , "/=" ) \
    X(DIV          , "/"  ) \
    X(OREQ         , "|=" ) \
    X(OR           , "|"  ) \
    X(OROR         , "||" ) \
    X(ANDEQ        , "&=" ) \
    X(ANDAND       , "&&" ) \
    X(AND          , "&"  ) \
    X(EQEQ         , "==" ) \
    X(EQ           , "="  ) \
    X(NOTEQ        , "!=" ) \
    X(NOT          , "!"  ) \
    X(SHLEQ        , "<<=") \
    X(SHL          , "<<" ) \
    X(LESSEQ       , "<=" ) \
    X(LESS         , "<"  ) \
    X(SHREQ        , ">>=") \
    X(SHR          , ">>" ) \
    X(GREATEREQ    , ">=" ) \
    X(GREATER      , ">"  ) \
    X(DOT          , "."  ) \

#define KEYWORD_TOKEN_LIST      \
    X(VAR       , "var"         ) \
    X(IF        , "if"          ) \
    X(ELSE      , "else"        ) \
    X(WHILE     , "while"       ) \
    X(BREAK     , "break"       ) \
    X(CONTINUE  , "continue"    ) \
    X(FUN       , "fun"         ) \
    X(RETURN    , "return"      ) \
    X(PRINT     , "print"       ) \

typedef enum {
    // Terminal
    TOKEN_EOF = 0,
    TOKEN_PARSING_ERROR,

    // Values
    TOKEN_ID,
    TOKEN_STRING_LIT,
    TOKEN_CHAR_LIT,
    TOKEN_INT_LIT,
    TOKEN_FLOAT_LIT,

#define X(TOK, STR) TOKEN_##TOK,
    // Punctuations
    PUNCT_TOKEN_LIST
    // Keywords 
    KEYWORD_TOKEN_LIST
#undef X
} Token;

typedef struct {
    const char *current;
    const char *line_start;
    const char *line_end;
    size_t line_number;
} ParsePoint;

typedef struct {
    int token;
    const char *literal;
    bool is_keyword;
} TokenInfo;

typedef struct {
    const char *input_path;
    const char *input_stream;
    const char *eof;
    ParsePoint parse_point;
    struct {
        char *items;
        size_t count;
        size_t capacity;
    } string_storage;
    Token token;
    char *string;
    int64_t int_number;
    double real_number;
    Loc loc;

    TokenInfo tokens[128];
    size_t count_tokens;
} Lexer;

Lexer lexer_new(const char *input_path, const char *input_stream, const char *eof);
void lexer_destroy(Lexer *lex);
bool lexer_get_token(Lexer *lex);
bool lexer_expect_token(Lexer *lex, Token token);
bool lexer_expect_id(Lexer *lex, const char *id);
Token lexer_expect_token2(Lexer *lex, Token token_1, Token token_2);
bool lexer_get_and_expect_token(Lexer *lex, Token token);
bool lexer_get_and_expect_id(Lexer *lex, const char *id);
int  lexer_match_ids(Lexer *lex, const char **ids, size_t count_ids);
Token lexer_get_and_expect_token2(Lexer *lex, Token token_1, Token token_2);
const char *lexer_display_token(Token token);
void lexer_skip_whitespace(Lexer *lex);
Loc lexer_loc(Lexer *lex);

// TODO: let user register their own tokens
// void lexer_register_punct(Lexer *lex, const char *punct, int token);
// void lexer_register_keyword(Lexer *lex, const char *keyword, int token);
// void lexer_register_default_tokens(Lexer *lex); // C tokens

#endif // LEXER_H_
