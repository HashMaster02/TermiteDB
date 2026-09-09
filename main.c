#include "include/hashmap.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOMBSTONE "~DEL~"

// PART 1
// DONE: Append key-value commandline appends to a file
// DONE: Maintain hashmap to byte-offset of the latest entry of a given
// key-value pair
// DONE: Read entry out of file
// TODO: Delete an entry
// NOTE: Tombstone Marker: ~DEL~, CLI Syntax: `termite -d <key>

typedef long BYTE_OFFSET;

hashmap *init_index_hm(void);
static void index_put(hashmap *idx, const char *key, size_t len,
                      BYTE_OFFSET off);
static void index_free(hashmap *idx);

void read_value(hashmap *index, char *const *key, FILE *fileptr);
void write_value(hashmap *index, char *entry, const char *colon, FILE *fileptr);
void rebuild_index(hashmap *memcache, FILE *fileptr);
void delete_value(hashmap *memcache, char *key, FILE *fileptr);

int main(int argc, char *argv[]) {
    hashmap *memcache = init_index_hm();

    FILE *fileptr = fopen("segment.txt", "a+");
    if (!fileptr) {
        perror("segment.txt");
        return 1;
    }

    rebuild_index(memcache, fileptr);

    if (argc <= 1) {
        fclose(fileptr);
        index_free(memcache);
        return 0;
    }

    // Where "a" mode starts is implementation-defined; pin it to the end so
    // ftell() reports the offset each entry will actually be written at.
    fseek(fileptr, 0, SEEK_END);

    for (int i = 1; i < argc; i++) {
        // Check for deletions first
        if (strcmp(argv[i], "-d") == 0) {
            if (!(i + 1 < argc)) {
                fprintf(stderr, "no parameter provided to flag -d\n");
                exit(1);
            }
            delete_value(memcache, argv[i + 1], fileptr);
            i++;
            continue;
        }

        // Cmdline Formatting => termite key:value OR termite "key: value"
        const char *colon = strchr(argv[i], ':');
        if (!colon) {
            read_value(memcache, &argv[i], fileptr);
            fseek(fileptr, 0, SEEK_END);
            continue;
        }

        write_value(memcache, argv[i], colon, fileptr);
    }

    if (fclose(fileptr) != 0) {
        perror("segment.txt");
        index_free(memcache);
        return 1;
    }

    index_free(memcache);
    return 0;
}

void rebuild_index(hashmap *memcache, FILE *fileptr) {

    char buffer[256];
    BYTE_OFFSET curr_byte_offset = 0;
    fseek(fileptr, curr_byte_offset, SEEK_SET);
    while (1) {
        curr_byte_offset = ftell(fileptr);
        char *entry = fgets(buffer, sizeof(buffer), fileptr);
        if (entry == NULL) {
            break;
        }

        char *value = strchr(buffer, ':');
        if (value == NULL) {
            fprintf(stderr, "Malformed entry at byte %li\n", curr_byte_offset);
            continue;
        }

        index_put(memcache, entry, value - entry, curr_byte_offset);
    }
}

void write_value(hashmap *index, char *entry, const char *colon,
                 FILE *fileptr) {

    BYTE_OFFSET off = ftell(fileptr);
    fputs(entry, fileptr);
    fputc('\n', fileptr);

    index_put(index, entry, (size_t)(colon - entry), off);
}

void read_value(hashmap *index, char *const *key, FILE *fileptr) {
    BYTE_OFFSET *byte_offset = (BYTE_OFFSET *)hm_get(index, key);
    if (byte_offset == NULL) {
        fprintf(stderr, "Missing key '%s'\n", *key);
        return;
    }

    fseek(fileptr, *byte_offset, SEEK_SET);
    char buffer[256];
    char *res = fgets(buffer, sizeof(buffer), fileptr);
    if (res == NULL) {
        fprintf(stderr, "Byte past EOF reached\n");
        exit(1);
    }

    char *value = strchr(buffer, ':'); // globe
    if (value == NULL) {
        fprintf(stderr, "strchr() + 1\n");
        exit(1);
    }
    value += 1;

    size_t val_len = strcspn(value, "\n");
    value[val_len] = 0;
    printf("%s\n", value);
}

void delete_value(hashmap *memcache, char *key, FILE *fileptr) {
    fprintf(fileptr, "%s:%s\n", key, TOMBSTONE);
}

// Maps a heap-allocated key string to the byte offset of its latest entry
// in the segment file. The map stores only the char * pointer, so every key
// put into it must stay alive until index_free().
hashmap *init_index_hm(void) {
    hashmap *m = hm_new_str(sizeof(BYTE_OFFSET));
    if (!m) {
        perror("hm_new_str");
        exit(1);
    }
    return m;
}

// Record that `key` (the bytes in [key, key + len)) now lives at `off`.
static void index_put(hashmap *idx, const char *key, size_t len,
                      BYTE_OFFSET off) {
    char *copy = strndup(key, len);
    if (!copy) {
        perror("strndup");
        exit(1);
    }
    // hm_put copies the char * itself, so we pass the address of the
    // pointer. It returns 1 if a new entry was added, 0 if the key already
    // existed (the map keeps its original pointer, so our copy is unused),
    // -1 on failure.
    int rc = hm_put(idx, &copy, &off);
    if (rc != 1) {
        free(copy);
    }
    if (rc < 0) {
        fprintf(stderr, "index: out of memory\n");
        exit(1);
    }
}

// Free the key strings we own, then the map itself.
static void index_free(hashmap *idx) {
    hm_iter it = hm_begin(idx);
    while (hm_next(&it)) {
        free(*(char **)it.key);
    }
    hm_free(idx);
}
