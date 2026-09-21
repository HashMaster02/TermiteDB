# Compiler settings
ifeq ($(OS),Windows_NT)
    SANFLAGS =
else
    SANFLAGS = -fsanitize=address
endif
CC = gcc

# Single source of truth for the version. Bump this, then tag to match.
VERSION = 1.0.0
VERFLAG = -DVERSION='"$(VERSION)"'

ifeq ($(MODE), release)
	CFLAGS = -Wall -Wextra -O3 -Iinclude -s $(VERFLAG)
	TARGET = termite
else
	CFLAGS = -Wall -Wextra -O0 -g $(SANFLAGS) -Iinclude $(VERFLAG)
	TARGET = termite_d
endif

# Source file
SRC = main.c

# Packaging
DISTNAME = termite-v$(VERSION)-linux-x86_64

# Default rule
$(TARGET): makefile $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Build the release binary and package it with a checksum for the GitHub release
dist:
	$(MAKE) MODE=release
	tar -czf $(DISTNAME).tar.gz termite README.md
	sha256sum $(DISTNAME).tar.gz > $(DISTNAME).tar.gz.sha256

# Clean up build files
clean:
	rm -f termite termite_d termite-v*.tar.gz termite-v*.tar.gz.sha256

.PHONY: clean dist
