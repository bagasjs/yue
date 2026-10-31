CC := clang
CFLAGS := -Wall -Wextra -pedantic -fsanitize=address -Wno-deprecated-declarations -Wno-unused-parameter
LFLAGS := 

build/yue.exe: ./src/yue.c ./src/main.c ./src/lexer.c ./src/utils.c
	$(CC) $(CFLAGS) -o $@ ./src/yue.c ./src/main.c $(LFLAGS)
