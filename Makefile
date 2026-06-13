CC := clang
CFLAGS := -Wall -Wextra -pedantic -D_CRT_SECURE_NO_WARNINGS -g
LFLAGS := 

build/yue.exe: src/main.c src/lexer.c src/utils.c 
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)
