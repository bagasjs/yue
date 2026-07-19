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
typedef struct Yue_Call_Info Yue_Call_Info;

typedef void (*Yue_Cfn)(Yue_Context *ctx, Yue_Call_Info *info); // Native Function

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

struct Yue_Call_Info {
    Yue_Value *argv; // argument values
    int        argc; // count of argv
    Yue_Value  retv; // return values
    int        retc; // count of retv (for now only support 0 and 1)
};

typedef struct Yue_String {
    char   *items;
    size_t  count;
    size_t  capacity;
} Yue_String;

typedef struct Yue_Array {
    Yue_Value *items;
    size_t     count;
    size_t     capacity;
} Yue_Array;

typedef struct Yue_Table_Entry {
    // Although it's Yue_Value it must be YUE_OBJECT_STRING
    Yue_Value key;
    Yue_Value value;
} Yue_Table_Entry;

typedef struct Yue_Table {
    Yue_Table_Entry *items;
    size_t  count;
    size_t  capacity;
} Yue_Table;

Yue_Context *yue_open(void);
void         yue_close(Yue_Context *ctx);

int yue_set_global_value(Yue_Context *ctx, const char *name, Yue_Value  value);
int yue_get_global_value(Yue_Context *ctx, const char *name, Yue_Value *value);

int yue_do_string(Yue_Context *ctx, const char *file, const char *source, size_t length);

Yue_Value yue_new_string(Yue_Context *ctx, const char *init_text);
Yue_Value yue_new_array(Yue_Context *ctx);

Yue_String *yue_to_string(Yue_Value value);
Yue_Array  *yue_to_array(Yue_Value value);

/// Object Operations (yue_objects.c)
void yue_array_append(Yue_Array *array, Yue_Value item);
void yue_array_insert(Yue_Array *array, Yue_Value item);
void      yue_table_setitem(Yue_Table *table, Yue_Value key, Yue_Value value);
Yue_Value yue_table_getitem(Yue_Table *table, Yue_Value key);

#endif // YUE_H_
