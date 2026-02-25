NDEBUG ?= n

CC := clang
CFLAGS := -Wall -Wextra -pedantic -D_CRT_SECURE_NO_WARNINGS

ifeq ($(NDEBUG),y)
	CFLAGS += -O2
else
	CFLAGS += -g -fsanitize=address
endif

yue.exe: main.c 
	$(CC) $(CFLAGS) -o $@ $^
