CC := clang
CFLAGS := -Wall -Wextra -pedantic -D_CRT_SECURE_NO_WARNINGS
LFLAGS := 

NDEBUG ?= n
ifeq ($(NDEBUG),y)
	CFLAGS += -O2
else
	CFLAGS += -g -fsanitize=address
endif

all: yue.exe raylib.yuedll

yue.exe: main.c 
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)

RAYLIB_CFLAGS := -I$(HOME)/Software/include
RAYLIB_LFLAGS := -L$(HOME)/Software/lib -lraylibdll

raylib.yuedll: yue-raylib.c
	$(CC) -shared $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $^ $(LFLAGS) $(RAYLIB_LFLAGS)
