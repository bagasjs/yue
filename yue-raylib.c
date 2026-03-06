#include <raylib.h>
#define YUE_IMPLEMENTATION
#define YUE_DEF static
#define YUE_BUILD_DLL
#include "yue.h"

static yue_Object *f_init_window(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    char buf[256] = {0};
    yue_Number width  = yue_tonumber(ctx, yue_nextarg(ctx, &arg));
    yue_Number height = yue_tonumber(ctx, yue_nextarg(ctx, &arg));
    const char *title = yue_tostring(ctx, yue_nextarg(ctx, &arg), buf, sizeof(buf));
    InitWindow(width, height, title);
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

static yue_Object *f_close_window(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    CloseWindow();
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

static yue_Object *f_begin_drawing(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    BeginDrawing();
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

static yue_Object *f_end_drawing(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    EndDrawing();
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

static yue_Object *f_window_should_close(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    yue_restoregc(ctx, gc);
    return WindowShouldClose() ? yue_number(ctx, 1) : yue_nil(ctx);
}

static yue_Object *f_clear_background(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    int r = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int g = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int b = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int a = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    ClearBackground((Color){ .r=r, .g=g, .b=b, .a=a });
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

static yue_Object *f_draw_rectangle(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    int x = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int y = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int w = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int h = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int r = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int g = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int b = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    int a = yue_tonumber(ctx, yue_eval(ctx, yue_nextarg(ctx, &arg)));
    DrawRectangle(x, y, w, h, (Color){ .r=r, .g=g, .b=b, .a=a });
    yue_restoregc(ctx, gc);
    return yue_nil(ctx);
}

static yue_Object *f_is_key_pressed(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    yue_Number keycode = yue_tonumber(ctx, yue_nextarg(ctx, &arg));
    yue_restoregc(ctx, gc);
    return IsKeyPressed(keycode) ? yue_number(ctx, 1) : yue_nil(ctx);
}

static yue_Object *f_is_key_down(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    yue_Number keycode = yue_tonumber(ctx, yue_nextarg(ctx, &arg));
    yue_restoregc(ctx, gc);
    return IsKeyPressed(keycode) ? yue_number(ctx, 1) : yue_nil(ctx);
}

static yue_Object *f_is_key_up(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    yue_Number keycode = yue_tonumber(ctx, yue_nextarg(ctx, &arg));
    yue_restoregc(ctx, gc);
    return IsKeyUp(keycode) ? yue_number(ctx, 1) : yue_nil(ctx);
}

static yue_Object *f_is_key_released(yue_Context *ctx, yue_Object *arg)
{
    size_t gc = yue_savegc(ctx);
    yue_eval_list(ctx, arg);
    yue_Number keycode = yue_tonumber(ctx, yue_nextarg(ctx, &arg));
    yue_restoregc(ctx, gc);
    return IsKeyReleased(keycode) ? yue_number(ctx, 1) : yue_nil(ctx);
}

YUE_API void yue_require_dll(yue_Context *ctx)
{
    yue_set(ctx, yue_symbol(ctx, "init-window"), yue_cfunc(ctx, f_init_window));
    yue_set(ctx, yue_symbol(ctx, "close-window"), yue_cfunc(ctx, f_close_window));
    yue_set(ctx, yue_symbol(ctx, "begin-drawing"), yue_cfunc(ctx, f_begin_drawing));
    yue_set(ctx, yue_symbol(ctx, "end-drawing"), yue_cfunc(ctx, f_end_drawing));
    yue_set(ctx, yue_symbol(ctx, "window-should-close"), yue_cfunc(ctx, f_window_should_close));
    yue_set(ctx, yue_symbol(ctx, "clear-background"), yue_cfunc(ctx, f_clear_background));
    yue_set(ctx, yue_symbol(ctx, "draw-rectangle"), yue_cfunc(ctx, f_draw_rectangle));
    yue_set(ctx, yue_symbol(ctx, "is-key-pressed"), yue_cfunc(ctx, f_is_key_pressed));
    yue_set(ctx, yue_symbol(ctx, "is-key-down"), yue_cfunc(ctx, f_is_key_down));
    yue_set(ctx, yue_symbol(ctx, "is-key-up"), yue_cfunc(ctx, f_is_key_up));
    yue_set(ctx, yue_symbol(ctx, "is-key-released"), yue_cfunc(ctx, f_is_key_released));
}
