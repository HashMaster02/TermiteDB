ifeq ($(OS),Windows_NT)
    SANFLAGS =
else
    SANFLAGS = -fsanitize=address
endif

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O0 -g $(SANFLAGS) -Iinclude 

# Target executable name
TARGET = termite

# Source file
SRC = main.c

# Default rule
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Clean up build files
clean:
	rm -f $(TARGET)
