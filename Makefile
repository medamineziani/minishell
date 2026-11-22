CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -g
TARGET = minishell
SRC = minishell.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

re: clean all

.PHONY: all clean re
