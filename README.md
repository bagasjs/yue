# Yue
A small scripting language

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
#include "raylib.h"
#include <string.h>
#include <assert.h>

...

yue_value f_init_window(yue_context *ctx, yue_value *args, int argc)
{
    if(argc < 3) yue_errorf(ctx, "this function requires more than 3 arguments got %d", argc);
    int width  = yue_toint(ctx, args[0]);
    int height = yue_toint(ctx, args[1]);
    const char * title = yue_tocstr(ctx, args[2]);
    InitWindow(width, height, title);
    return yue_nil(ctx, 0);
}

int main(int argc, char *argv[]) {
    Yue_Context *ctx = yue_open();

    assert(argc > 0)
    char *source = read_entire_file_text(argv[1]);
    if(!source) return -1;

    yue_set_global_value(ctx, "init_window", yue_cfunc(f_init_window));

    int ret = yue_do_string(ctx, argv[1], source, strlen(source));
    yue_close(ctx);

    return ret;
}

```
