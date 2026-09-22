# Compiler settings
CC = gcc

# Single source of truth for the version. Bump this, then tag to match.
VERSION = 1.0.2
VERFLAG = -DVERSION='"$(VERSION)"'

# Platform-specific bits. gcc appends .exe on Windows, so the target has to
# carry the suffix too or make never sees the binary it just built.
ifeq ($(OS),Windows_NT)
    SANFLAGS =
    EXE = .exe
    PLATFORM = windows-x86_64
    ARCHIVE = $(DISTNAME).zip
else
    SANFLAGS = -fsanitize=address
    EXE =
    PLATFORM = linux-x86_64
    ARCHIVE = $(DISTNAME).tar.gz
endif

ifeq ($(MODE), release)
	CFLAGS = -Wall -Wextra -O3 -Iinclude -s -static $(VERFLAG)
	TARGET = termite$(EXE)
else
	CFLAGS = -Wall -Wextra -O0 -g $(SANFLAGS) -Iinclude $(VERFLAG)
	TARGET = termite_d$(EXE)
endif

# Source file
SRC = main.c

# Packaging
DISTNAME = termite-v$(VERSION)-$(PLATFORM)
RELEASE_BIN = termite$(EXE)

# Default rule
$(TARGET): makefile $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Build the release binary and package it with a checksum for the GitHub release
dist:
	$(MAKE) MODE=release
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -Command "Compress-Archive -Path '$(RELEASE_BIN)','README.md' -DestinationPath '$(ARCHIVE)' -Force"
	powershell -NoProfile -Command "((Get-FileHash '$(ARCHIVE)' -Algorithm SHA256).Hash.ToLower() + '  $(ARCHIVE)') | Set-Content -Encoding ascii '$(ARCHIVE).sha256'"
else
	tar -czf $(ARCHIVE) $(RELEASE_BIN) README.md
	sha256sum $(ARCHIVE) > $(ARCHIVE).sha256
endif

# Clean up build files
clean:
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -Command "Remove-Item -Force -ErrorAction Ignore termite.exe,termite_d.exe,termite-v*.zip,termite-v*.zip.sha256"
else
	rm -f termite termite_d termite-v*.tar.gz termite-v*.tar.gz.sha256
endif

.PHONY: clean dist
