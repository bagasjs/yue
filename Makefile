CC := clang
CFLAGS := -Wall -Wextra -pedantic -D_CRT_SECURE_NO_WARNINGS -g -fsanitize=address
LFLAGS := 

build/yue.exe: src/yue.c src/lexer.c src/utils.c src/shtable.c
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)
