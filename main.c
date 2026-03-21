#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define YUE_IMPLEMENTATION
#include "yue.h"

static void dump_obj(yue_Object *obj, int level) { 
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
        case YUE_OBJECT_FUNC:
            printf("func(%p)\n", obj->as_cfunc);
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
                    dump_obj(obj->as_pair.head, level + 1);
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

void dump_ctx(yue_Context *ctx)
{
    printf("Variables:\n");
    for(size_t i = 0; i < ctx->scope_size; ++i) {
        yue_Object *obj = ctx->scope[i];
        while(obj) {
            if(obj->type != YUE_OBJECT_SYMBOL) yue_error(ctx, "what the fuck at scope %zu", i);
            printf("[SCOPE=%zu] %s %s\n", i, obj->as_symbol.name, _yue_type_names[obj->as_symbol.value->type]);
            obj = obj->next;
        }
    }
}

#ifdef _WIN32
#include <windows.h>
typedef HMODULE yue_DLL;
#else
#include <dlfcn.h>
typedef void *yue_DLL;
#endif

#define DLL_CAP 32
static yue_DLL dlls[DLL_CAP] = {0};
static size_t dlls_count = 0;

typedef void (*yue_RequireDLLLoader)(yue_Context *ctx);
yue_Object *yue_builtin_require_dll(yue_Context *ctx, yue_Object *arg)
{
    char buf[256] = {0};
    const char *filepath = yue_tostring(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)), buf, sizeof(buf));
    if(dlls_count > DLL_CAP) yue_error(ctx, "Could not load more dll. This happened during loading %s\n", filepath);
#ifdef _WIN32
    yue_DLL dll = LoadLibrary(filepath);
    if(dll == NULL) yue_error(ctx, "Failed to load %s\n", filepath);
    yue_RequireDLLLoader proc = (yue_RequireDLLLoader)GetProcAddress(dll, "yue_require_dll");
    if(proc == NULL) yue_error(ctx, "Failed to load yue_require_dll from %s\n", filepath);
    proc(ctx);
    dlls[dlls_count++] = dll;
#else
    yue_DLL dll = dlopen(filepath, RTLD_NOW);
    if(dll == NULL) yue_error(ctx, "Failed to load %s\n", filepath);
    yue_RequireDLLLoader proc = (yue_RequireDLLLoader)dlsym(dll, "yue_require_dll");
    if(proc == NULL) yue_error(ctx, "Failed to load yue_require_dll from %s\n", filepath);
    proc(ctx);
    dlls[dlls_count++] = dll;
#endif
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

    static char buf[32 * 1024];
    yue_Context *ctx = yue_open(buf, sizeof(buf));
    yue_load_builtins(ctx);

    size_t gc = yue_savegc(ctx);
    yue_set(ctx, yue_symbol(ctx, "require-dll"), yue_cfunc(ctx, yue_builtin_require_dll));
    yue_restoregc(ctx, gc);

    yue_File copy = source;
    for(;;) {
        yue_restoregc(ctx, gc);
        yue_Object *obj = yue_read(ctx, &copy);
        if(yue_isnil(obj)) break;
        yue_eval(ctx, obj);
    }

    for(int i = 0; i < dlls_count; ++i) {
        yue_DLL dll = dlls[i];
#ifdef _WIN32
        FreeLibrary(dll);
#else
        dlclose(dll);
#endif
    }

    free((void*)source.ptr);
    return 0;
}
