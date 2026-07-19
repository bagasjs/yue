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
#include "lexer.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

typedef struct {
    const char *literal;
    Token token;
} TokenToLit;

static TokenToLit PUNCTS[] = {
#define X(TOK, STR) (TokenToLit) { .token = TOKEN_##TOK, .literal = STR, },
    PUNCT_TOKEN_LIST
#undef X
};

static const TokenToLit KEYWORDS[] = {
#define X(TOK, STR) (TokenToLit) { .token = TOKEN_##TOK, .literal = STR, },
    KEYWORD_TOKEN_LIST
#undef X
};


const char *lexer_display_token(Token token)
{
    switch(token) {
        // Terminal
        case TOKEN_EOF           : return "end of file";
        case TOKEN_PARSING_ERROR : return "parsing error";

        // Values
        case TOKEN_ID            : return "identifier";
        case TOKEN_STRING_LIT    : return "string";
        case TOKEN_CHAR_LIT      : return "character";
        case TOKEN_INT_LIT       : return "integer literal";
        case TOKEN_FLOAT_LIT     : return "float literal";

        #define X(TOK, STR) case TOKEN_##TOK: return ""STR"";
            PUNCT_TOKEN_LIST
        #undef X

        #define X(TOK, STR) case TOKEN_##TOK: return "keyword `"STR"`";
            KEYWORD_TOKEN_LIST
        #undef X
    }
    fprintf(stderr, "ERROR: unknown token %d. This is unexpected\n", token);
    return "Unknown token";
}

void compiler_diagf(Loc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: ", loc.input_path, loc.line_number, loc.line_offset);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

#define compiler_missingf(loc, ...) _compiler_missingf(__FILE__, __LINE__, loc, __VA_ARGS__)
void _compiler_missingf(const char *file, int line, Loc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: todo: ", loc.input_path, loc.line_number, loc.line_offset);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n%s:%d: info: implementation should go here\n", file, line);
    fprintf(stderr, "%*d | %.*s\n",  6, loc.line_number, (int)(loc.line_end - loc.line_start), loc.line_start); 
    fprintf(stderr, "%*s | %*s^\n", 6, "", (int)loc.line_offset, ""); 
    va_end(ap);
}

void lexer_diagf(Loc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: ", loc.input_path, loc.line_number, loc.line_offset);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fprintf(stderr, "%*d | %.*s\n",  6, loc.line_number, (int)(loc.line_end - loc.line_start), loc.line_start); 
    fprintf(stderr, "%*s | %*s^\n", 6, "", (int)loc.line_offset, ""); 
    va_end(ap);
}

#define compiler_missingf(loc, ...) _compiler_missingf(__FILE__, __LINE__, loc, __VA_ARGS__)
void _lexer_missingf(const char *file, int line, Loc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: todo: ", loc.input_path, loc.line_number, loc.line_offset);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n%s:%d: info: implementation should go here\n", file, line);
    va_end(ap);
}

Lexer lexer_new(const char *input_path, const char *input_stream, const char *eof)
{
    Lexer lex = {0};
    lex.input_path = input_path;
    lex.input_stream = input_stream;
    lex.eof = eof;
    lex.parse_point.current    = input_stream;
    lex.parse_point.line_start = input_stream;
    const char *c = lex.parse_point.current;
    while(c < lex.eof && *c != '\n') {
        c += 1;
    }
    lex.parse_point.line_end = c;
    lex.parse_point.current  = input_stream;
    lex.parse_point.line_number = 1;
    return lex;
}

void lexer_destroy(Lexer *lex)
{
    free(lex->string_storage.items);
    lex->string_storage.items = 0;
    lex->string_storage.capacity = 0;
    lex->string_storage.count = 0;
}

bool lexer_is_eof(Lexer *lex)
{
    return lex->parse_point.current >= lex->eof;
}

char lexer_peek_char(Lexer *lex)
{
    if(lexer_is_eof(lex)) return 0;
    return *lex->parse_point.current;
}

void lexer_skip_char(Lexer *lex)
{
    assert(!lexer_is_eof(lex));
    char ch = *lex->parse_point.current;
    lex->parse_point.current += 1;
    if(ch == '\n') {
        lex->parse_point.line_start   = lex->parse_point.current;
        lex->parse_point.line_number += 1;
        const char *c = lex->parse_point.current;
        while(c < lex->eof && *c != '\n') {
            c += 1;
        }
        lex->parse_point.line_end = c;
    }
}

void lexer_skip_whitespace(Lexer *lex)
{
    char ch;
    while((ch = lexer_peek_char(lex)) != 0) {
        if(isspace(ch)) {
            lexer_skip_char(lex);
        } else {
            break;
        }
    }
}

bool lexer_skip_prefix(Lexer *lex, const char *prefix)
{
    ParsePoint saved_point = lex->parse_point;
    char ch = 0;
    while(*prefix != 0) {
        ch = lexer_peek_char(lex);
        if(ch == 0 || ch != *prefix) {
            lex->parse_point = saved_point;
            return false;
        }
        lexer_skip_char(lex);
        prefix += 1;
    }
    return true;
}

void lexer_skip_until(Lexer *lex, const char *prefix)
{
    while(!lexer_is_eof(lex) && !lexer_skip_prefix(lex, prefix)) {
        lexer_skip_char(lex);
    }
}

bool lexer_is_identifier(char ch) 
{
    return isalnum(ch) != 0 || ch == '_';
}

bool lexer_is_identifier_start(char ch)
{
    return isalpha(ch) != 0 || ch == '_';
}

Loc lexer_loc(Lexer *lex)
{
    return (Loc) {
        .input_path = lex->input_path,
        .line_number = lex->parse_point.line_number,
        .line_offset = (int)(size_t)(lex->parse_point.current - lex->parse_point.line_start),
        .line_start  = lex->parse_point.line_start,
        .line_end    = lex->parse_point.line_end,
    };
}

void lexer_storage_append(Lexer *lex, char ch)
{
    if(lex->string_storage.count + 1 > lex->string_storage.capacity) {
        if(lex->string_storage.capacity == 0) {
            lex->string_storage.capacity  = 32;
        } else {
            lex->string_storage.capacity *= 2;
        }

        void *new_items = malloc(lex->string_storage.capacity * sizeof(*lex->string_storage.items));
        assert(new_items != NULL && "Buy more RAM LOL!");
        memcpy(new_items, lex->string_storage.items, lex->string_storage.count * sizeof(*lex->string_storage.items));
        free(lex->string_storage.items);
        lex->string_storage.items = new_items;
    }
    lex->string_storage.items[lex->string_storage.count++] = ch;
}

bool lexer_parse_string_into_storage(Lexer *lex, char delim)
{
    char ch = 0;
    while((ch = lexer_peek_char(lex)) != 0) {
        if(ch == '\\') {
            lexer_skip_char(lex);
            ch = lexer_peek_char(lex);
            if(ch == 0) {
                lex->token = TOKEN_PARSING_ERROR;
                lexer_diagf(lexer_loc(lex), "LEXER ERROR: unfinished escape sequence");
                return false;
            }
            char nch = 0;
            switch(ch) {
                case '0':
                    nch = '\0';
                    break;
                case 'n':
                    nch = '\n';
                    break;
                case 't':
                    nch = '\t';
                    break;
                case '\\':
                    nch = '\\';
                    break;
                default:
                    if(ch == delim) {
                        nch = delim;
                    } else {
                        lex->token = TOKEN_PARSING_ERROR;
                        lexer_diagf(lexer_loc(lex), "LEXER ERROR: unfinished escape sequence");
                        return false;
                    }
                    break;
            }

            lexer_storage_append(lex, nch);
            lexer_skip_char(lex);
        } else if(ch == delim) {
            break;
        } else {
            lexer_storage_append(lex, ch);
            lexer_skip_char(lex);
        }
    }
    return true;
}

bool lexer_get_token(Lexer *lex)
{
    while(true) {
        lexer_skip_whitespace(lex);
        if(lexer_skip_prefix(lex, "//")) {
            lexer_skip_until(lex, "\n");
            continue;
        }
        if(lexer_skip_prefix(lex, "#")) {
            lexer_skip_until(lex, "\n");
            continue;
        }

        if(lexer_skip_prefix(lex, "/*")) {
            lexer_skip_until(lex, "*/");
            continue;
        }

        break;
    }

    lex->loc = lexer_loc(lex);

    char ch = lexer_peek_char(lex);
    if(ch == 0) {
        lex->token = TOKEN_EOF;
        return true;
    }

    for(int i = 0; i < (int)ARRAY_LEN(PUNCTS); ++i) {
        TokenToLit t = PUNCTS[i];
        if(lexer_skip_prefix(lex, t.literal)) {
            lex->token = t.token;
            return true;
        }
    }

    if(lexer_is_identifier_start(ch)) {
        lex->token = TOKEN_ID;
        lex->string_storage.count = 0;
        while((ch = lexer_peek_char(lex)) != 0) {
            if(lexer_is_identifier(ch)) {
                lexer_storage_append(lex, ch);
                lexer_skip_char(lex);
            } else {
                break;
            }
        }

        lexer_storage_append(lex, 0);
        lex->string = lex->string_storage.items;
        for(int i = 0; i < (int)ARRAY_LEN(KEYWORDS); ++i) {
            TokenToLit t = KEYWORDS[i];
            if(strcmp(lex->string, t.literal) == 0) {
                lex->token = t.token;
                return true;
            }
        }
        return true;
    }

    if(lexer_skip_prefix(lex, "0x")) {
        lex->token = TOKEN_INT_LIT;
        lex->int_number = 0;
        while((ch = lexer_peek_char(lex)) != 0) {
            if(isdigit(ch) != 0) {
                lex->int_number *= 16;
                lex->int_number += ((int64_t)ch) - ((int64_t)'0');
                lexer_skip_char(lex);
            } else if('a' <= ch && ch <= 'f') {
                lex->int_number *= 16;
                lex->int_number += ((int64_t)ch) - ((int64_t)'a') + 10;
                lexer_skip_char(lex);
            } else if('A' <= ch && ch <= 'F') {
                lex->int_number *= 16;
                lex->int_number += ((int64_t)ch) - ((int64_t)'A') + 10;
                lexer_skip_char(lex);
            } else {
                break;
            }
        }
        return true;
    }

    if(isdigit(ch) != 0) {
        lex->token = TOKEN_INT_LIT;
        lex->int_number = 0;
        while((ch = lexer_peek_char(lex)) != 0) {
            // TODO: check for overflows?
            if(isdigit(ch) != 0) {
                lex->int_number *= 10;
                lex->int_number += (int64_t)ch - (int64_t)'0';
                lexer_skip_char(lex);
            } else {
                break;
            }
        }
        return true;
    }

    if(ch == '"') {
        lexer_skip_char(lex);
        lex->token = TOKEN_STRING_LIT;
        lex->string_storage.count = 0;
        if(!lexer_parse_string_into_storage(lex, '"')) {
            return false;
        }
        if(lexer_is_eof(lex)) {
            lexer_diagf(lexer_loc(lex), "LEXER ERROR: unfinished string literal"); 
            lexer_diagf(lexer_loc(lex), "LEXER INFO: literal starts here"); 
            lex->token = TOKEN_PARSING_ERROR;
            return false;
        }
        lexer_skip_char(lex);
        lexer_storage_append(lex, 0);
        lex->string = lex->string_storage.items;
        return true;
    }

    if(ch == '\'') {
        lexer_skip_char(lex);
        lex->token = TOKEN_CHAR_LIT;
        lex->string_storage.count = 0;
        if(!lexer_parse_string_into_storage(lex, '\'')) {
            return false;
        }
        if(lexer_is_eof(lex)) {
            lexer_diagf(lexer_loc(lex), "LEXER ERROR: Unfinished character literal");
            lexer_diagf(lexer_loc(lex), "LEXER INFO: Literal starts here");
            lex->token = TOKEN_PARSING_ERROR;
            return false;
        }
        lexer_skip_char(lex);
        if(lex->string_storage.count == 0) {
            lexer_diagf(lexer_loc(lex), "LEXER ERROR: Empty character literal");
            lex->token = TOKEN_PARSING_ERROR;
            return false;
        }
        if (lex->string_storage.count > 2) {
            // TODO: maybe we should allow more on targets with 64 bits?
            // TODO: such error should not terminate the compilation
            lexer_diagf(lexer_loc(lex), "LEXER ERROR: Character literal contains more than two characters");
            lex->token = TOKEN_PARSING_ERROR;
            return false;
        }
        lex->int_number = 0;
        for(size_t i = 0; i < lex->string_storage.count; ++i) {
            lex->int_number *= 0x100;
            lex->int_number += lex->string_storage.items[i];
        }
        return true;
    }

    lexer_diagf(lexer_loc(lex), "LEXER ERROR: invalid char '%c' -> '%d' for any token parsing", 
            lex->parse_point.current,lex->parse_point.current );
    lex->token = TOKEN_PARSING_ERROR;
    return false;
}

bool lexer_expect_token(Lexer *lex, Token token)
{
    if(lex->token == token) return true;
    lexer_diagf(lexer_loc(lex), "ERROR: expected token `%s`, but got token `%s`", lexer_display_token(token), lexer_display_token(lex->token));
    return false;
}

bool lexer_expect_id(Lexer *lex, const char *id)
{
    if(!lexer_expect_token(lex, TOKEN_ID)) return false;
    if(strcmp(lex->string, id) == 0) return true;
    lexer_diagf(lexer_loc(lex), "ERROR: expected identifier `%s`, but got `%s`", id, lex->string);
    return false;
}

Token lexer_expect_token2(Lexer *lex, Token token_1, Token token_2)
{
    if(lex->token == token_1 || lex->token == token_2) return lex->token;
    lexer_diagf(lexer_loc(lex), "ERROR: expected %s or %s, but got %s", 
            lexer_display_token(token_1), 
            lexer_display_token(token_2), 
            lexer_display_token(lex->token));
    return TOKEN_PARSING_ERROR;
}

bool lexer_get_and_expect_token(Lexer *lex, Token token)
{
    if(!lexer_get_token(lex) && lex->token != TOKEN_EOF) return false;
    return lexer_expect_token(lex, token);
}

bool lexer_get_and_expect_id(Lexer *lex, const char *id)
{
    if(!lexer_get_token(lex) && lex->token != TOKEN_EOF) return false;
    return lexer_expect_id(lex, id);
}

Token lexer_get_and_expect_token2(Lexer *lex, Token token_1, Token token_2)
{
    if(!lexer_get_token(lex) && lex->token != TOKEN_EOF) return false;
    return lexer_expect_token2(lex, token_1, token_2);
}

int lexer_match_ids(Lexer *lex, const char **ids, size_t count_ids)
{
    if(!lexer_expect_token(lex, TOKEN_ID)) return false;
    for(size_t i = 0; i < count_ids; ++i) {
        if(strcmp(ids[i], lex->string) == 0) {
            return (int)i;
        }
    }
    lexer_diagf(lexer_loc(lex), "ERROR: expected multiple id, but got token `%s`", lex->string);
    return -1;
}

