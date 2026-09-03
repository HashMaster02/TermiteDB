/*
 * hashmap.h - a small, type-agnostic hash map in a single header (C99).
 *
 * Keys and values are copied into the map by value, so the map works with any
 * fixed-size type: ints, structs, pointers, ... Sizes are given once at
 * creation, after which the API only deals in `const void *` to your data.
 *
 * Usage:
 *
 *     hashmap *m = hm_new(sizeof(int), sizeof(double));
 *     int k = 42; double v = 3.14;
 *     hm_put(m, &k, &v);                  // insert or overwrite
 *     double *p = hm_get(m, &k);          // NULL if absent
 *     hm_del(m, &k);                      // 1 if removed, 0 if absent
 *
 *     hm_iter it = hm_begin(m);
 *     while (hm_next(&it)) printf("%d -> %f\n", *(int *)it.key, *(double *)it.val);
 *     hm_free(m);
 *
 * String keys: the map stores the `char *` pointer, not the characters, so
 * the string must outlive its entry. Pass the address of the pointer:
 *
 *     hashmap *m = hm_new_str(sizeof(long));
 *     const char *k = "hello"; long off = 128;
 *     hm_put(m, &k, &off);
 *     long *p = hm_get(m, &k);
 *
 * Any other key type can be given custom hash/equality functions with
 * hm_new_ex(). By default keys are compared and hashed as raw bytes, so keys
 * containing padding bytes must be zero-initialised (e.g. memset) before use.
 *
 * Rules:
 *   - Pointers returned by hm_get()/iteration are valid until the next
 *     hm_put()/hm_del()/hm_clear()/hm_free(). Writing through them is fine.
 *   - Do not hm_put() or hm_del() while iterating.
 *   - A map created with val_size == 0 behaves as a set; pass NULL for `val`.
 */
#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t (*hm_hash_fn)(const void *key, size_t key_size);
typedef int (*hm_eq_fn)(const void *a, const void *b, size_t key_size);

typedef struct {
    size_t key_size;
    size_t val_size;
    size_t val_off;        /* byte offset of the value inside a slot */
    size_t stride;         /* bytes per slot (key + value, aligned) */
    size_t len;            /* number of entries */
    size_t cap;            /* number of slots: 0 or a power of two */
    uint64_t *hashes;      /* per-slot hash; 0 marks an empty slot */
    unsigned char *slots;  /* key/value storage */
    hm_hash_fn hash;
    hm_eq_fn eq;
} hashmap;

typedef struct {
    const hashmap *m;
    size_t i;
    void *key;
    void *val;
} hm_iter;

#define HM_MIN_CAP 16
#define HM_ALIGN 16 /* keeps every key and value suitably aligned for any type */

/* ---- hashing helpers --------------------------------------------------- */

/* FNV-1a over the raw bytes of the key (the default). */
static inline uint64_t hm_hash_bytes(const void *key, size_t key_size) {
    const unsigned char *p = key;
    uint64_t h = 14695981039346656037ULL;
    size_t i;
    for (i = 0; i < key_size; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h ^ (h >> 32); /* fold high bits down; the table masks the low bits */
}

/* memcmp over the raw bytes of the key (the default). */
static inline int hm_eq_bytes(const void *a, const void *b, size_t key_size) {
    return memcmp(a, b, key_size) == 0;
}

/* For maps whose key is a `char *` (key_size == sizeof(char *)). */
static inline uint64_t hm_hash_str(const void *key, size_t key_size) {
    const char *s;
    (void)key_size;
    memcpy(&s, key, sizeof s);
    return hm_hash_bytes(s, strlen(s));
}

static inline int hm_eq_str(const void *a, const void *b, size_t key_size) {
    const char *x, *y;
    (void)key_size;
    memcpy(&x, a, sizeof x);
    memcpy(&y, b, sizeof y);
    return strcmp(x, y) == 0;
}

/* ---- internals --------------------------------------------------------- */

static inline size_t hm__align(size_t n) {
    return (n + HM_ALIGN - 1) & ~(size_t)(HM_ALIGN - 1);
}

static inline void *hm__key(const hashmap *m, size_t i) {
    return m->slots + i * m->stride;
}

static inline void *hm__val(const hashmap *m, size_t i) {
    return m->slots + i * m->stride + m->val_off;
}

static inline uint64_t hm__hash(const hashmap *m, const void *key) {
    uint64_t h = m->hash(key, m->key_size);
    return h ? h : 1; /* 0 is reserved for empty slots */
}

/* Index of `key`, or SIZE_MAX if absent. */
static inline size_t hm__find(const hashmap *m, const void *key, uint64_t h) {
    size_t mask, i;
    if (m->cap == 0) return SIZE_MAX;
    mask = m->cap - 1;
    for (i = (size_t)h & mask; m->hashes[i] != 0; i = (i + 1) & mask) {
        if (m->hashes[i] == h && m->eq(hm__key(m, i), key, m->key_size)) return i;
    }
    return SIZE_MAX;
}

/* First empty slot on the probe sequence of `h`. Requires a free slot. */
static inline size_t hm__find_empty(const hashmap *m, uint64_t h) {
    size_t mask = m->cap - 1, i;
    for (i = (size_t)h & mask; m->hashes[i] != 0; i = (i + 1) & mask) {}
    return i;
}

/* Rehash into a table of `new_cap` slots. On failure the map is unchanged. */
static inline int hm__resize(hashmap *m, size_t new_cap) {
    uint64_t *old_hashes = m->hashes;
    unsigned char *old_slots = m->slots;
    size_t old_cap = m->cap, i;
    uint64_t *hashes;
    unsigned char *slots;

    if (new_cap > SIZE_MAX / m->stride || new_cap > SIZE_MAX / sizeof *hashes) return -1;
    hashes = calloc(new_cap, sizeof *hashes);
    slots = malloc(new_cap * m->stride);
    if (!hashes || !slots) {
        free(hashes);
        free(slots);
        return -1;
    }

    m->hashes = hashes;
    m->slots = slots;
    m->cap = new_cap;
    for (i = 0; i < old_cap; i++) {
        size_t j;
        if (old_hashes[i] == 0) continue;
        j = hm__find_empty(m, old_hashes[i]);
        m->hashes[j] = old_hashes[i];
        memcpy(hm__key(m, j), old_slots + i * m->stride, m->stride);
    }
    free(old_hashes);
    free(old_slots);
    return 0;
}

/* Ensure room for one more entry, keeping the load factor at or below 3/4. */
static inline int hm__grow_if_needed(hashmap *m) {
    if ((m->len + 1) * 4 <= m->cap * 3) return 0;
    return hm__resize(m, m->cap ? m->cap * 2 : HM_MIN_CAP);
}

/* ---- public API -------------------------------------------------------- */

/* Create a map with custom hash/equality; NULL selects the byte-wise defaults.
 * Returns NULL on allocation failure or if key_size is 0. */
static inline hashmap *hm_new_ex(size_t key_size, size_t val_size, hm_hash_fn hash, hm_eq_fn eq) {
    hashmap *m;
    if (key_size == 0 || key_size > SIZE_MAX / 4 || val_size > SIZE_MAX / 4) return NULL;
    m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->key_size = key_size;
    m->val_size = val_size;
    m->val_off = hm__align(key_size);
    m->stride = hm__align(m->val_off + val_size);
    m->hash = hash ? hash : hm_hash_bytes;
    m->eq = eq ? eq : hm_eq_bytes;
    return m;
}

/* Create a map keyed by the raw bytes of a fixed-size key. */
static inline hashmap *hm_new(size_t key_size, size_t val_size) {
    return hm_new_ex(key_size, val_size, NULL, NULL);
}

/* Create a map keyed by `char *` strings (see header comment). */
static inline hashmap *hm_new_str(size_t val_size) {
    return hm_new_ex(sizeof(char *), val_size, hm_hash_str, hm_eq_str);
}

/* Release the map. Safe to call with NULL. */
static inline void hm_free(hashmap *m) {
    if (!m) return;
    free(m->hashes);
    free(m->slots);
    free(m);
}

/* Number of entries. */
static inline size_t hm_len(const hashmap *m) {
    return m->len;
}

/* Insert `key` -> `val`, overwriting any existing value.
 * Returns 1 if the key was added, 0 if it already existed, -1 on allocation
 * failure (map unchanged). */
static inline int hm_put(hashmap *m, const void *key, const void *val) {
    uint64_t h = hm__hash(m, key);
    size_t i = hm__find(m, key, h);
    if (i != SIZE_MAX) {
        if (m->val_size) memcpy(hm__val(m, i), val, m->val_size);
        return 0;
    }
    if (hm__grow_if_needed(m) != 0) return -1;
    i = hm__find_empty(m, h);
    m->hashes[i] = h;
    memcpy(hm__key(m, i), key, m->key_size);
    if (m->val_size) memcpy(hm__val(m, i), val, m->val_size);
    m->len++;
    return 1;
}

/* Pointer to the value stored for `key`, or NULL if absent. */
static inline void *hm_get(const hashmap *m, const void *key) {
    size_t i = hm__find(m, key, hm__hash(m, key));
    return i == SIZE_MAX ? NULL : hm__val(m, i);
}

/* Remove `key`. Returns 1 if it was present, 0 otherwise. */
static inline int hm_del(hashmap *m, const void *key) {
    uint64_t h = hm__hash(m, key);
    size_t i = hm__find(m, key, h), j, mask;
    if (i == SIZE_MAX) return 0;

    /* Backward-shift deletion: pull later entries of the same probe run into
     * the hole so lookups never hit a false empty slot. No tombstones. */
    mask = m->cap - 1;
    for (j = (i + 1) & mask; m->hashes[j] != 0; j = (j + 1) & mask) {
        size_t home = (size_t)m->hashes[j] & mask;
        if (((j - home) & mask) >= ((j - i) & mask)) { /* home is not in (i, j] */
            m->hashes[i] = m->hashes[j];
            memcpy(hm__key(m, i), hm__key(m, j), m->stride);
            i = j;
        }
    }
    m->hashes[i] = 0;
    m->len--;
    return 1;
}

/* Remove every entry, keeping the allocated capacity. */
static inline void hm_clear(hashmap *m) {
    if (m->cap) memset(m->hashes, 0, m->cap * sizeof *m->hashes);
    m->len = 0;
}

/* Pre-size the map so at least `n` entries fit without rehashing.
 * Returns 0 on success, -1 on allocation failure (map unchanged). */
static inline int hm_reserve(hashmap *m, size_t n) {
    size_t cap = HM_MIN_CAP;
    if (n > SIZE_MAX / 4) return -1;
    while (cap * 3 < n * 4) cap *= 2;
    return cap > m->cap ? hm__resize(m, cap) : 0;
}

/* Iteration: `hm_iter it = hm_begin(m); while (hm_next(&it)) use(it.key, it.val);`
 * Entries are visited in unspecified order. */
static inline hm_iter hm_begin(const hashmap *m) {
    hm_iter it;
    it.m = m;
    it.i = 0;
    it.key = NULL;
    it.val = NULL;
    return it;
}

static inline int hm_next(hm_iter *it) {
    const hashmap *m = it->m;
    while (it->i < m->cap) {
        size_t i = it->i++;
        if (m->hashes[i] != 0) {
            it->key = hm__key(m, i);
            it->val = hm__val(m, i);
            return 1;
        }
    }
    it->key = NULL;
    it->val = NULL;
    return 0;
}

#endif /* HASHMAP_H */
