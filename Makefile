CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99

all: words

words: words.c
	$(CC) $(CFLAGS) words.c -o words

clean:
	rm -f words
