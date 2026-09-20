#include "include/hashmap.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOMBSTONE "~DEL~"
#define MAX_SEGMENTS 10
#define MAX_SEG_SIZE 1024 // in bytes

#define COMPACTED_SEGMENT_ID 0
#define ACTIVE_SEGMENT_AFTER_COMPACTION_ID 1

// PART 1
// DONE: Append key-value commandline appends to a file
// DONE: Maintain hashmap to byte-offset of the latest entry of a given
// key-value pair
// DONE: Read entry out of file
// DONE: Delete an entry

// PART 2
// DONE: Implement segment rotation
// TODO: Implement compaction

typedef long BYTE_OFFSET;
typedef struct {
    int seg_id;
    BYTE_OFFSET byte_offset;
} Location;

//  Keep every previous segment open to avoid running fopen multiple times
typedef struct {
    FILE *fileptrs[MAX_SEGMENTS]; // hardcoded for now. make dynamic later
    int num_segs;
    int active_seg_id;
} Segments;

hashmap *init_index_hm(void);
static void index_put(hashmap *idx, const char *key, size_t len,
                      Location *value);
static void index_free(hashmap *idx);
static int index_delete(hashmap *idx, const char *key);
static char *read_line(FILE *fileptr);
static int get_latest_segment(Segments *segments);
static void close_all_segments(Segments *segments);
static FILE *rotate_segment(Segments *segments);
static int compact_segments(hashmap *memcache, Segments *segments);
static char *get_segment_filename(int id);

void read_value(hashmap *memcache, char *const *key, Segments *segments);
void write_value(hashmap *memcache, char *entry, const char *colon,
                 Segments *segments);
void rebuild_index(hashmap *memcache, Segments *segments);
void delete_value(hashmap *memcache, char *key, FILE *fileptr);

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        return 0;
    }

    Segments segments = {
        .fileptrs = {0},
        .num_segs = 0,
        .active_seg_id = -1,
    };

    if (get_latest_segment(&segments)) {
        fprintf(stderr, "couldn't open latest segment file\n");
        close_all_segments(&segments);
        return 1;
    }

    hashmap *memcache = init_index_hm();
    rebuild_index(memcache, &segments);

    // Check for the compaction command `-c`
    if (argc == 2 && (strcmp(argv[1], "-c") == 0)) {
        int error_code = 0;
        printf("RUNNING COMPACTION\n");
        if (compact_segments(memcache, &segments)) {
            fprintf(stderr, "compaction operation failed\n");
            error_code = 1;
        }
        close_all_segments(&segments);
        index_free(memcache);
        exit(error_code);
    }

    // Where "a" mode starts is implementation-defined; pin it to the end so
    // ftell() reports the offset each entry will actually be written at.
    FILE *active_file = segments.fileptrs[segments.active_seg_id];
    fseek(active_file, 0, SEEK_END);

    for (int i = 1; i < argc; i++) {
        // Check for compaction first
        if (strcmp(argv[i], "-c") == 0) {
            fprintf(
                stderr,
                "`-c` flag must be passed-in individually and not along with "
                "other operations. Ignoring.\n");
            continue;
        }

        // Check for deletions first
        if (strcmp(argv[i], "-d") == 0) {
            if (!(i + 1 < argc)) {
                fprintf(stderr, "no parameter provided to flag -d\n");
                exit(1);
            }
            // NOTE: We do not check if the paramter passed to -d is a
            // flag. This could lead to silent bugs on the user end. This is
            // worth solving later.
            delete_value(memcache, argv[i + 1], active_file);
            // Begin a new segment if the current one has exceeded its maximum
            // size
            if (ftell(active_file) >= MAX_SEG_SIZE) {
                active_file = rotate_segment(&segments);
                if (!active_file) {
                    fprintf(stderr, "rotate segment failure\n");
                    exit(1);
                }
            }
            i++;
            continue;
        }

        // Cmdline Formatting => termite key:value OR termite "key: value"
        const char *colon = strchr(argv[i], ':');
        if (!colon) {
            read_value(memcache, &argv[i], &segments);
            fseek(active_file, 0, SEEK_END);
            continue;
        }

        write_value(memcache, argv[i], colon, &segments);

        // Begin a new segment if the current one has exceeded its maximum size
        if (ftell(active_file) >= MAX_SEG_SIZE) {
            active_file = rotate_segment(&segments);
            if (!active_file) {
                fprintf(stderr, "rotate segment failure\n");
                exit(1);
            }
        }
    }

    close_all_segments(&segments);
    index_free(memcache);
    return 0;
}

static char *get_segment_filename(int id) {
    char *filename = (char *)malloc(256 * sizeof(char));
    sprintf(filename, "./seg/segment-%03d.txt", id);
    return filename;
}

static int get_latest_segment(Segments *segments) {
    signed int id = 0;

    while (1) {
        char *filename = get_segment_filename(id);
        FILE *fileptr = fopen(filename, "r");
        free(filename);

        if (!fileptr && (errno == ENOENT)) {
            id--;
            if (id >= 0) {
                if (fclose(segments->fileptrs[id])) {
                    fprintf(stderr, "failed to close segment file with id %d\n",
                            id);
                }
            }
            break;
        }

        if (!fileptr) {
            perror("error while probing segment files");
            exit(1);
        }

        if (id >= MAX_SEGMENTS) {
            fprintf(stderr,
                    "maximum segments reached. increase MAX_SEGMENTS.\n");
            return 1;
        }
        segments->fileptrs[id] = fileptr;
        id++;
    }

    if (id < 0) {
        id = 0;
    }

    char *filename = get_segment_filename(id);
    FILE *fileptr = fopen(filename, "a+");
    free(filename);
    if (!fileptr) {
        fprintf(stderr, "failed to open file with id %d in 'a+' mode\n", id);
        exit(1);
    }
    segments->fileptrs[id] = fileptr;
    segments->active_seg_id = id;
    segments->num_segs = id + 1;

    return 0;
}

static void close_all_segments(Segments *segments) {
    for (int i = 0; i < segments->num_segs; i++) {
        if (fclose(segments->fileptrs[i])) {
            fprintf(stderr, "failed to close segment file with id %d\n", i);
        }
    }
}

static FILE *rotate_segment(Segments *segments) {
    FILE *active_file = segments->fileptrs[segments->active_seg_id];
    if (fclose(active_file)) {
        fprintf(stderr, "failed to close active segment with id %d\n",
                segments->active_seg_id);
        return NULL;
    }
    char *filename = get_segment_filename(segments->active_seg_id);

    active_file = fopen(filename, "r");
    free(filename);
    if (!active_file) {
        fprintf(stderr, "failed to open segment with id %d\n",
                segments->active_seg_id);
        return NULL;
    }
    segments->fileptrs[segments->active_seg_id] = active_file;

    segments->active_seg_id++;
    if (segments->active_seg_id >= MAX_SEGMENTS) {
        fprintf(stderr, "maximum segments reached. increase MAX_SEGMENTS.\n");
        return NULL;
    }
    filename = get_segment_filename(segments->active_seg_id);

    active_file = fopen(filename, "a+");
    free(filename);
    if (!active_file) {
        fprintf(stderr, "failed to open new active segment with id %d\n",
                segments->active_seg_id);
        return NULL;
    }
    segments->fileptrs[segments->active_seg_id] = active_file;
    segments->num_segs++;

    return active_file;
}

static int compact_segments(hashmap *memcache, Segments *segments) {
    if (segments->active_seg_id == 0) {
        return 0;
    }

    FILE *tempfile = fopen("./seg/segment.temp", "w");
    if (!tempfile) {
        fprintf(stderr, "failed to open ./seg/segment.temp\n");
        return 1;
    }

    hm_iter it = hm_begin(memcache);
    while (hm_next(&it)) {
        Location *loc = it.val;
        if (loc->seg_id == segments->active_seg_id) {
            loc->seg_id = ACTIVE_SEGMENT_AFTER_COMPACTION_ID;
            continue;
        }
        FILE *fileptr = segments->fileptrs[loc->seg_id];
        fseek(fileptr, loc->byte_offset, SEEK_SET);
        char *entry = read_line(fileptr);
        if (entry == NULL) {
            continue;
        }
        loc->seg_id = COMPACTED_SEGMENT_ID;
        loc->byte_offset = ftell(tempfile);
        fputs(entry, tempfile);
        free(entry);
    }

    if (fclose(tempfile)) {
        fprintf(stderr, "failed to close segment.temp");
        return 1;
    }

    for (int id = 0; id < segments->active_seg_id; id++) {
        if (fclose(segments->fileptrs[id])) {
            fprintf(stderr, "failed to close segment file with id %d\n", id);
            return 1;
        }
    }

    if (rename("./seg/segment.temp", "./seg/segment-000.txt")) {
        fprintf(stderr, "failed to rename segment.temp to segment-000.txt\n");
        return 1;
    }

    for (int id = 1; id < segments->active_seg_id; id++) {
        char *filename = get_segment_filename(id);
        if (remove(filename)) {
            fprintf(stderr, "failed to remove segment file with id %d\n", id);
            free(filename);
            return 1;
        }
        free(filename);
    }

    char *filename = get_segment_filename(segments->active_seg_id);
    if (rename(filename, "./seg/segment-001.txt")) {
        fprintf(stderr,
                "failed to rename active segment file to segment-001.txt\n");
        free(filename);
        return 1;
    }
    free(filename);

    tempfile = fopen("./seg/segment-000.txt", "r");
    if (!tempfile) {
        fprintf(stderr, "failed to open ./seg/segment-000.txt\n");
        return 1;
    }
    segments->fileptrs[COMPACTED_SEGMENT_ID] = tempfile;

    segments->fileptrs[ACTIVE_SEGMENT_AFTER_COMPACTION_ID] =
        segments->fileptrs[segments->active_seg_id];

    for (int id = 2; id <= segments->active_seg_id; id++) {
        segments->fileptrs[id] = NULL;
    }

    segments->active_seg_id = ACTIVE_SEGMENT_AFTER_COMPACTION_ID;
    segments->num_segs = 2;

    return 0;
}

void rebuild_index(hashmap *memcache, Segments *segments) {

    for (int i = 0; i <= segments->active_seg_id; i++) {

        BYTE_OFFSET curr_byte_offset = 0;
        char *entry;
        FILE *fileptr = segments->fileptrs[i];
        fseek(fileptr, curr_byte_offset, SEEK_SET);
        while (1) {

            curr_byte_offset = ftell(fileptr);
            entry = read_line(fileptr);
            if (entry == NULL) {
                break;
            }

            char *value = strchr(entry, ':');
            if (value == NULL) {
                fprintf(stderr, "Malformed entry at byte %li\n",
                        curr_byte_offset);
                free(entry);
                continue;
            }

            if (strncmp(value + 1, TOMBSTONE, strlen(TOMBSTONE)) == 0) {
                *value = '\0'; // create a NULL-terminated string out of 'entry'
                index_delete(memcache, entry);
                free(entry);
                continue;
            }

            Location loc = {i, curr_byte_offset};
            index_put(memcache, entry, value - entry, &loc);

            free(entry);
        }
    }
}

void write_value(hashmap *memcache, char *entry, const char *colon,
                 Segments *segments) {

    FILE *fileptr = segments->fileptrs[segments->active_seg_id];
    BYTE_OFFSET off = ftell(fileptr);
    fputs(entry, fileptr);
    fputc('\n', fileptr);

    Location loc = {segments->active_seg_id, off};
    index_put(memcache, entry, (size_t)(colon - entry), &loc);
}

void read_value(hashmap *memcache, char *const *key, Segments *segments) {
    Location *val_location = (Location *)hm_get(memcache, key);
    if (val_location == NULL) {
        fprintf(stderr, "Missing key '%s'\n", *key);
        return;
    }

    FILE *fileptr = segments->fileptrs[val_location->seg_id];
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
