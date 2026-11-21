CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
TARGET := server
SOURCE := server.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) $(ARGS)

clean:
	rm -f $(TARGET)
