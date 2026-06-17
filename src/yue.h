#ifndef YUE_H_
#define YUE_H_

typedef int   Yue_Int;
typedef float Yue_Flt;

typedef struct Yue_Context Yue_Context;
typedef struct Yue_Object  Yue_Object;
typedef struct Yue_Value   Yue_Value;
typedef struct Yue_Table   Yue_Table;
typedef struct Yue_String  Yue_String;
typedef struct Yue_Array   Yue_Array;

// Native function
typedef int (*Yue_Cfn)(Yue_Context *ctx, Yue_Value *args, Yue_Int argc, Yue_Value *retval);

typedef enum Yue_Value_Kind {
    YUE_VALUE_NIL = 0,
    YUE_VALUE_INT,
    YUE_VALUE_FLT,
    YUE_VALUE_FUN,
    YUE_VALUE_OBJ,
    YUE_VALUE_CFN,
} Yue_Value_Kind;

typedef enum Yue_Object_Kind {
    YUE_OBJECT_NIL = 0, // This should never happened though
    YUE_OBJECT_STRING,
    YUE_OBJECT_ARRAY,
    YUE_OBJECT_TABLE
} Yue_Object_Kind;

struct Yue_Value {
    Yue_Value_Kind kind;
    union {
        Yue_Int     intv;
        Yue_Flt     fltv;
        Yue_Object *objv;
        Yue_Int     fun_id;
        Yue_Cfn     cfn;
    };
};

Yue_Context *yue_open(void);
void         yue_close(Yue_Context *ctx);

int yue_set_global_value(Yue_Context *ctx, const char *name, Yue_Value  value);
int yue_get_global_value(Yue_Context *ctx, const char *name, Yue_Value *value);

Yue_Value yue_new_string(Yue_Context *ctx, const char *init_text);
Yue_Value yue_new_array(Yue_Context *ctx);
Yue_Value yue_new_table(Yue_Context *ctx);

bool yue_isstring(Yue_Value value);
bool yue_isarray(Yue_Value value);
bool yue_istable(Yue_Value value);


// yue_array.c
void yue_array_append(Yue_Value array, Yue_Value item);
void yue_array_insert(Yue_Value array, Yue_Value item);

#endif // YUE_H_
