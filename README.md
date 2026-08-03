# Yue
A small scripting language.
I promise anything in ./src will never be more than 5k LoC okay.

## Features
1. Small 
1. Simple FFI API with C (no stack wrangling)
1. Garbage Collected
1. Slow but small
1. Lack of Closure but small
1. No Object Oriented Programming but again it's small
1. Cute and Funny

## Example
1. Hello World
```yue
print("Hello, World")
```

2. Variable
```yue
var a = 10;   // integer
var b = 20.5; // float
var c = "Hello, World"; // string
var d = [ a, b, c ]; // array
var e = { "name": "foo", "age": d[0] }; // table
print(e["name"])
```

3. Branching
```yue
if(age < 10) {
    print("You are child")
} else if(10 <= age && age < 17) {
    print("You are teenage")
} else {
    print("You are adult")
}
```

4. Loop
```yue
var i = 0;
while(i < 10) {
    i = i + 1;
}
```

5. Function
```yue
fun main(args) {
    print("Hello, World")
    return 0;
}
```

6. C FFI
```c
#include "yue.h"
#include "utils.h"

int rand_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

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
```
