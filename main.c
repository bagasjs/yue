#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define YUE_IMPLEMENTATION
#include "yue.h"

static void debug_obj(yue_Object *obj, int level) { 
    printf("%*s> ", (level * 2), "");
    switch(obj->type) { 
        case YUE_OBJECT_NIL:
            printf("nil\n");
            break;
        case YUE_OBJECT_NUMBER:
            printf("number(%f)\n", obj->as_number);
            break;
        case YUE_OBJECT_CFUNC:
            printf("cfunc(%p)\n", obj->as_cfunc);
            break;
        case YUE_OBJECT_SYMBOL:
            printf("%.*s\n", YUE_STRING_DATA_SIZE, obj->as_symbol.name);
            break;
        case YUE_OBJECT_USERDATA:
            printf("userdata(%p)\n", obj->as_userdata);
            break;
        case YUE_OBJECT_STRING:
            { 
                printf("string(");
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
                printf(")\n");
            }
            break;
        case YUE_OBJECT_PAIR:
            {
                printf("pair: \n");
                while(!yue_isnil(obj)) {
                    debug_obj(obj->as_pair.head, level + 1);
                    obj = obj->as_pair.tail;
                }
            } break;
    }
}

bool read_entire_file(const char *filepath, yue_File *file)
{
    FILE *f = fopen(filepath, "rb");
    if(!f) {
        fprintf(stderr, "error: Could not open a file '%s'\n", filepath);
        return false;
    }
    if (fseek(f, 0, SEEK_END) < 0) {
        fprintf(stderr, "error: Could not seek into file\n");
        fclose(f);
        return false;
    }
    size_t fsz = ftell(f);
    if (fsz <  0) {
        fprintf(stderr, "error: Could not get the file size\n");
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) < 0) {
        fprintf(stderr, "error: Could not seek into file\n");
        fclose(f);
        return false;
    }
    char *buf = malloc(fsz + 1);
    fread(buf, fsz, 1, f);
    if (ferror(f)) {
        fprintf(stderr, "error: Could not read into file\n");
        fclose(f);
        return false;
    }
    buf[fsz] = 0;
    fclose(f);
    if(file) {
        file->fst = buf;
        file->ptr = buf;
        file->eof = buf + fsz;
    }
    return true;
}

#include <stdlib.h>
yue_Object *yue_builtin_openfile(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    char buf[256] = {0};
    yue_Object *x = yue_eval(ctx, yue_nextarg(ctx, &arg));
    const char *filename = yue_tostring(ctx, x, buf, sizeof(buf));
    printf("'%s' '%s'\n", filename, buf);
    yue_File *file = malloc(sizeof(*file));
    if(!read_entire_file(filename, file)) {
        yue_restoregc(ctx, gc);
        return yue_nil(ctx);
    }
    yue_restoregc(ctx, gc);
    return yue_userdata(ctx, file);
}

yue_Object *yue_builtin_closefile(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_File *file = yue_touserdata(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    free((void*)file->fst);
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

int main(int argc, char *argv[])
{
    if(argc < 2) {
        fprintf(stderr, "ERROR: provide input file path\n");
        fprintf(stderr, "USAGE: %s program.yue\n", argv[0]);
        return -1;
    }
    size_t size = 0;
    yue_File source = {0};
    if(!read_entire_file(argv[1], &source)) {
        fprintf(stderr, "ERROR: failed to read file %s\n", argv[1]);
        return -1;
    }

    static char buf[8 * 1024];
    yue_Context *ctx = yue_open(buf, sizeof(buf));
    size_t gc = yue_savegc(ctx);
    yue_set(ctx, yue_symbol(ctx, "print"), yue_cfunc(ctx, yue_builtin_print));
    yue_set(ctx, yue_symbol(ctx, "+"), yue_cfunc(ctx, yue_builtin_add));
    yue_set(ctx, yue_symbol(ctx, "="), yue_cfunc(ctx, yue_builtin_assign));
    yue_set(ctx, yue_symbol(ctx, "not"), yue_cfunc(ctx, yue_builtin_not));
    yue_set(ctx, yue_symbol(ctx, "exit"), yue_cfunc(ctx, yue_builtin_exit));
    yue_set(ctx, yue_symbol(ctx, "<"), yue_cfunc(ctx, yue_builtin_lt));
    yue_set(ctx, yue_symbol(ctx, "do"), yue_cfunc(ctx, yue_builtin_dolist));
    yue_set(ctx, yue_symbol(ctx, "while"), yue_cfunc(ctx, yue_builtin_while));
    yue_set(ctx, yue_symbol(ctx, "if"), yue_cfunc(ctx, yue_builtin_if));

    yue_set(ctx, yue_symbol(ctx, "openfile"), yue_cfunc(ctx, yue_builtin_openfile));
    yue_set(ctx, yue_symbol(ctx, "closefile"), yue_cfunc(ctx, yue_builtin_closefile));
    yue_restoregc(ctx, gc);

    yue_File copy = source;
    for(;;) {
        yue_restoregc(ctx, gc);
        yue_Object *obj = yue_read(ctx, &copy);
        if(yue_isnil(obj)) break;
        yue_eval(ctx, obj);
    }

    free((void*)source.ptr);
    return 0;
}
