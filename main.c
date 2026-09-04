#include "include/hashmap.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// PART 1
// DONE: Append key-value commandline appends to a file
// DONE: Maintain hashmap to byte-offset of the latest entry of a given
// key-value pair
// TODO: Read latest entry out of file
//

typedef long BYTE_OFFSET;

hashmap *init_index_hm(void);
static void index_put(hashmap *idx, const char *key, size_t len,
                      BYTE_OFFSET off);
static void index_free(hashmap *idx);

void read_value(FILE *fileptr);

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        return 0;
    }

    hashmap *idx = init_index_hm();

    FILE *fileptr = fopen("segment.txt", "a+");
    if (!fileptr) {
        perror("segment.txt");
        return 1;
    }

    // Where "a" mode starts is implementation-defined; pin it to the end so
    // ftell() reports the offset each entry will actually be written at.
    fseek(fileptr, 0, SEEK_END);

    for (int i = 1; i < argc; i++) {
        // Cmdline Formatting => termite key:value OR termite "key: value"
        const char *colon = strchr(argv[i], ':');
        if (!colon) {
            read_value(fileptr);
            continue;
        }

        BYTE_OFFSET off = ftell(fileptr);
        fputs(argv[i], fileptr);
        fputc('\n', fileptr);

        index_put(idx, argv[i], (size_t)(colon - argv[i]), off);
    }

    if (fclose(fileptr) != 0) {
        perror("segment.txt");
        index_free(idx);
        return 1;
    }

    index_free(idx);
    return 0;
}

void read_value(FILE *fileptr) {
    fseek(fileptr, 12, SEEK_SET);
    char buffer[256];
    char *res = fgets(buffer, sizeof(buffer), fileptr);
    if (res == NULL) {
        perror("fgets");
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
    printf("%s", value);
}

// Maps a heap-allocated key string to the byte offset of its latest entry in
// the segment file. The map stores only the char * pointer, so every key put
// into it must stay alive until index_free().
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
    // hm_put copies the char * itself, so we pass the address of the pointer.
    // It returns 1 if a new entry was added, 0 if the key already existed (the
    // map keeps its original pointer, so our copy is unused), -1 on failure.
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
