#ifndef YUE_H_
#define YUE_H_

typedef struct yue_context yue_context;

typedef struct yue_value {
} yue_value;

yue_context *yue_open(void);
void yue_close(yue_context *ctx);

#endif // YUE_H_
