#include "include/hashmap.h"
#include <asm-generic/errno-base.h>
#include <errno.h>
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
// DONE: Delete an entry

// PART 2
// TODO: Implement segment rotation
// TODO: Implement compaction

typedef long BYTE_OFFSET;
typedef struct {
    int seg_id;
    BYTE_OFFSET byte_offset;
} Location;

hashmap *init_index_hm(void);
static void index_put(hashmap *idx, const char *key, size_t len,
                      Location *value);
static void index_free(hashmap *idx);
static int index_delete(hashmap *idx, const char *key);
static char *read_line(FILE *fileptr);
static FILE *get_latest_segment();

void read_value(hashmap *memcache, char *const *key, FILE *fileptr);
void write_value(hashmap *memcache, char *entry, const char *colon,
                 FILE *fileptr);
void rebuild_index(hashmap *memcache, FILE *fileptr);
void delete_value(hashmap *memcache, char *key, FILE *fileptr);

int main(int argc, char *argv[]) {

    FILE *fileptr = get_latest_segment();
    if (!fileptr) {
        perror("couldn't open latest segment file");
        return 1;
    }

    hashmap *memcache = init_index_hm();
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

static FILE *get_latest_segment() {
    signed int id = 0;
    FILE *fileptr = NULL;

    char filename[256];
    while (fileptr == NULL) {
        sprintf(filename, "./seg/segment-%03d.txt", id);
        fileptr = fopen(filename, "r");
        if (!fileptr && (errno == ENOENT)) {
            id--;
            break;
        }
        if (!fileptr) {
            perror("error while probing segment files");
            exit(1);
        }
        if (fclose(fileptr)) {
            fprintf(stderr, "failed to close file: %s", filename);
            exit(1);
        }
        fileptr = NULL;
        id++;
    }

    if (id <= 0) {
        id = 0;
    }
    sprintf(filename, "./seg/segment-%03d.txt", id);
    fileptr = fopen(filename, "a+");

    return fileptr;
}

void rebuild_index(hashmap *memcache, FILE *fileptr) {

    BYTE_OFFSET curr_byte_offset = 0;
    char *entry;
    fseek(fileptr, curr_byte_offset, SEEK_SET);
    while (1) {

        curr_byte_offset = ftell(fileptr);
        entry = read_line(fileptr);
        if (entry == NULL) {
            break;
        }

        char *value = strchr(entry, ':');
        if (value == NULL) {
            fprintf(stderr, "Malformed entry at byte %li\n", curr_byte_offset);
            free(entry);
            continue;
        }

        if (strncmp(value + 1, TOMBSTONE, strlen(TOMBSTONE)) == 0) {
            *value = '\0'; // create a NULL-terminated string out of 'entry'
            index_delete(memcache, entry);
            free(entry);
            continue;
        }

        Location loc = {0, curr_byte_offset}; // TEMP
        index_put(memcache, entry, value - entry, &loc);

        free(entry);
    }
}

void write_value(hashmap *memcache, char *entry, const char *colon,
                 FILE *fileptr) {

    BYTE_OFFSET off = ftell(fileptr);
    fputs(entry, fileptr);
    fputc('\n', fileptr);

    Location loc = {0, off}; // TEMP
    index_put(memcache, entry, (size_t)(colon - entry), &loc);
}

void read_value(hashmap *memcache, char *const *key, FILE *fileptr) {
    Location *val_location = (Location *)hm_get(memcache, key);
    if (val_location == NULL) {
        fprintf(stderr, "Missing key '%s'\n", *key);
        return;
    }

    fseek(fileptr, val_location->byte_offset, SEEK_SET);
    char *res = read_line(fileptr);
    if (res == NULL) {
        fprintf(stderr, "Byte past EOF reached\n");
        free(res);
        exit(1);
    }

    char *value = strchr(res, ':');
    if (value == NULL) {
        fprintf(stderr, "strchr() + 1\n");
        free(res);
        exit(1);
    }
    value += 1;

    size_t val_len = strcspn(value, "\n");
    value[val_len] = 0;
    printf("%s\n", value);

    free(res);
}

void delete_value(hashmap *memcache, char *key, FILE *fileptr) {
    fprintf(fileptr, "%s:%s\n", key, TOMBSTONE);
    if (!index_delete(memcache, key)) {
        fprintf(stderr, "tried deleting missing key %s\n", key);
    }
}

static char *read_line(FILE *fileptr) {

    size_t buffer_size = 256;
    char *buffer = (char *)calloc(buffer_size, sizeof(char));

    char *chunk;
    while (1) {
        chunk = fgets(buffer + strlen(buffer), buffer_size - strlen(buffer),
                      fileptr);

        if (chunk == NULL) {
            // reached EOF
            break;
        }

        size_t len = strlen(chunk);
        if (*(chunk + len - 1) != '\n') {
            // Since the string is larger than our buffer, we must increase
            // its capacity
            void *tmp = realloc(buffer, buffer_size * 2);
            if (tmp == NULL) {
                fprintf(stderr, "realloc error on buffer");
                exit(1);
            }
            buffer = tmp;

            buffer_size *= 2;

        } else {
            break;
        }
    }

    if (strlen(buffer) > 0) {
        return buffer;
    }

    free(buffer);
    return NULL;
}

// Maps a heap-allocated key string to the byte offset of its latest entry
// in the segment file. The map stores only the char * pointer, so every key
// put into it must stay alive until index_free().
hashmap *init_index_hm(void) {
    hashmap *m = hm_new_str(sizeof(Location));
    if (!m) {
        perror("hm_new_str");
        exit(1);
    }
    return m;
}

// Record that `key` (the bytes in [key, key + len)) now lives at the given
// location.
static void index_put(hashmap *idx, const char *key, size_t len,
                      Location *value) {
    char *copy = strndup(key, len);
    if (!copy) {
        perror("strndup");
        exit(1);
    }
    // hm_put copies the char * itself, so we pass the address of the
    // pointer. It returns 1 if a new entry was added, 0 if the key already
    // existed (the map keeps its original pointer, so our copy is unused),
    // -1 on failure.
    int rc = hm_put(idx, &copy, value);
    if (rc != 1) {
        free(copy);
    }
    if (rc < 0) {
        fprintf(stderr, "index: out of memory\n");
        exit(1);
    }
}

static int index_delete(hashmap *idx, const char *key) {
    void *key_ptr = hm_get_key(idx, &key);
    if (key_ptr == NULL) {
        return 0;
    }
    char *key_str = *(char **)key_ptr;
    hm_del(idx, &key);
    free(key_str);
    return 1;
}

// Free the key strings we own, then the map itself.
static void index_free(hashmap *idx) {
    hm_iter it = hm_begin(idx);
    while (hm_next(&it)) {
        free(*(char **)it.key);
    }
    hm_free(idx);
}
