# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2

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
