all: main.c
	clang-9 main.c -g -std=c99 -Wall -Wextra -Wpedantic -D_DEFAULT_SOURCE -lncurses
