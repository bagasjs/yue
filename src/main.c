#include "yue.h"
#include "utils.h"

#ifndef YUE_NO_EASTER_EGG
#include <stdlib.h>
#include <time.h>
int rand_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}
#endif

void f_rand_range(Yue_Context *ctx, Yue_Call_Info *info)
{
    ASSERT(info->argc >= 2 && "rand_range expect more than 2 arguments");
    Yue_Value minv = info->argv[0];
    Yue_Value maxv = info->argv[1];
    ASSERT(minv.kind == YUE_VALUE_INT);
    ASSERT(maxv.kind == YUE_VALUE_INT);
    info->retv = (Yue_Value){ .kind = YUE_VALUE_INT, .intv = rand_range(minv.intv, maxv.intv) };
    info->retc = 1;
}

void f_append(Yue_Context *ctx, Yue_Call_Info *info) 
{
    ASSERT(info->argc >= 2 && "append expect more than 2 arguments");
    Yue_Value arrv = info->argv[0];
    Yue_Value newv = info->argv[1];
    Yue_Array *arr = yue_to_array(arrv);
    yue_array_append(arr, newv);
    info->retc = 0;
}

void f_len(Yue_Context *ctx, Yue_Call_Info *info) 
{
    ASSERT(info->argc >= 1 && "len expect more than 1 arguments");
    Yue_Value val  = info->argv[0];
    if(yue_is_array(val)) {
        Yue_Array *arr = yue_to_array(val);
        info->retv = (Yue_Value){ .kind = YUE_VALUE_INT, .intv = arr->count, };
        info->retc = 1;
    } else if (yue_is_string(val)) {
        Yue_String *str = yue_to_string(val);
        info->retv = (Yue_Value){ .kind = YUE_VALUE_INT, .intv = str->count - 1, };
        info->retc = 1;
    } else {
        ASSERT(0 && (yue_is_array(val) || yue_is_string(val)));
    }

}

void f_putchar(Yue_Context *ctx, Yue_Call_Info *info)
{
    ASSERT(info->argc == 1 && "len expect more than 1 arguments");
    ASSERT(info->argv[0].kind == YUE_VALUE_INT);
    putchar(info->argv[0].intv);
}

int main(int argc, char *argv[]) 
{
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

    Yue_Context *ctx = yue_open();

    Yue_Value args_v = yue_new_array(ctx);
    Yue_Array *args = yue_to_array(args_v);

    for(int i = 1; i < argc; ++i) {
        yue_array_append(args, yue_new_string(ctx, argv[i]));
    }

    yue_set_global_value(ctx, "ARGS",  args_v);
    yue_set_global_value(ctx, "nil",   (Yue_Value){ .kind = YUE_VALUE_NIL });
    yue_set_global_value(ctx, "true",  (Yue_Value){ .kind = YUE_VALUE_INT, .intv = 1 });
    yue_set_global_value(ctx, "false", (Yue_Value){ .kind = YUE_VALUE_INT, .intv = 0 });
    yue_set_global_value(ctx, "rand_range", (Yue_Value){ .kind = YUE_VALUE_CFN, .cfn = f_rand_range });
    yue_set_global_value(ctx, "append", (Yue_Value){ .kind = YUE_VALUE_CFN, .cfn = f_append });
    yue_set_global_value(ctx, "putchar", (Yue_Value){ .kind = YUE_VALUE_CFN, .cfn = f_putchar });
    yue_set_global_value(ctx, "len", (Yue_Value){ .kind = YUE_VALUE_CFN, .cfn = f_len });

    yue_do_string(ctx, source_filepath, source.items, source.count);

    yue_close(ctx);
    return 0;
}


