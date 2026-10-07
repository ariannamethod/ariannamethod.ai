/* Ordered numeric maps, structural keys, ownership and allocation failures. */
#include "ariannamethod.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL map API line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#ifdef AML_MAP_ALLOC_WRAP
/* Fixed bookkeeping avoids allocating while testing the allocator itself. */
static void* live[4096];
static int live_count;
static int allocation_budget = -1;
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
char* __real_realpath(const char*, char*);

static int allowed_allocation(void) {
    if (allocation_budget == 0) return 0;
    if (allocation_budget > 0) allocation_budget--;
    return 1;
}

static int allocation_index(void* ptr) {
    for (int i = 0; i < live_count; i++) if (live[i] == ptr) return i;
    fprintf(stderr, "FAIL map API: freeing/reallocating an untracked allocation\n");
    exit(1);
}

static void* track(void* ptr) {
    if (ptr) {
        if (live_count == (int)(sizeof(live) / sizeof(live[0]))) abort();
        live[live_count++] = ptr;
    }
    return ptr;
}

void* __wrap_malloc(size_t bytes) {
    return allowed_allocation() ? track(__real_malloc(bytes)) : NULL;
}

void* __wrap_calloc(size_t count, size_t bytes) {
    return allowed_allocation() ? track(__real_calloc(count, bytes)) : NULL;
}

void* __wrap_realloc(void* ptr, size_t bytes) {
    if (!allowed_allocation()) return NULL;
    if (!ptr) return track(__real_realloc(NULL, bytes));
    int index = allocation_index(ptr);
    void* replacement = __real_realloc(ptr, bytes);
    if (replacement) live[index] = replacement;
    else if (bytes == 0) live[index] = live[--live_count];
    return replacement;
}

void __wrap_free(void* ptr) {
    if (ptr) {
        int index = allocation_index(ptr);
        live[index] = live[--live_count];
    }
    __real_free(ptr);
}

/* realpath(path, NULL) allocates inside libc, outside --wrap=malloc. */
char* __wrap_realpath(const char* path, char* resolved) {
    if (resolved) return __real_realpath(path, resolved);
    if (!allowed_allocation()) { errno = ENOMEM; return NULL; }
    return track(__real_realpath(path, NULL));
}
#endif

static AM_String* key(const char* value) {
    AM_String* result = am_string_new(value);
    CHECK(result != NULL);
    return result;
}

static void put(AM_Map* map, const char* name, float value) {
    AM_String* text = key(name);
    CHECK(am_map_set(map, text, value) == 0);
    am_string_free(text);
}

static void value_is(const AM_Map* map, const char* name, float expected) {
    AM_String* text = key(name);
    float value = -999;
    CHECK(am_map_has(map, text) == 1);
    CHECK(am_map_get(map, text, &value) == 1 && value == expected);
    am_string_free(text);
}

static void order_is(const AM_Map* map, int length, const char* const* expected) {
    AM_List* keys = am_map_keys(map);
    CHECK(keys && keys->len == length);
    for (int i = 0; i < length; i++) CHECK(!strcmp(keys->items[i]->data, expected[i]));
    am_list_free(keys);
}

static void test_ownership_order(void) {
    AM_Map* map = am_map_new();
    AM_String* first = key("é");
    AM_String* equal = key("é");
    CHECK(map && map->len == 0 && map->refcount == 1);
    CHECK(am_map_set(map, first, 0) == 0 && first->refcount == 2);
    CHECK(am_map_set(map, equal, -7) == 0 && equal->refcount == 1);
    AM_List* held = am_map_keys(map);
    CHECK(held && held->len == 1 && held->items[0] == first && first->refcount == 3);
    put(map, "שלום", 2); put(map, "Привет", 3); put(map, "🦉", 4); put(map, "é", 5);
    put(map, "", 6);
    const char* initial[] = {"é", "שלום", "Привет", "🦉", "é", ""};
    order_is(map, 6, initial);
    value_is(map, "é", -7); value_is(map, "é", 5); value_is(map, "", 6);
    AM_Map* clone = am_map_clone(map);
    CHECK(clone && clone != map && clone->entries != map->entries && clone->buckets != map->buckets);
    CHECK(am_map_set(clone, equal, 17) == 0);
    value_is(map, "é", -7);
    CHECK(am_map_delete(map, equal) == 1 && am_map_delete(map, equal) == 0);
    CHECK(am_map_set(map, equal, 8) == 0);
    const char* changed[] = {"שלום", "Привет", "🦉", "é", "", "é"};
    order_is(map, 6, changed); order_is(clone, 6, initial);
    CHECK(!strcmp(held->items[0]->data, "é"));
    am_map_ref(map); CHECK(map->refcount == 2); am_map_free(map);
    CHECK(map->refcount == 1);
    am_map_free(map); am_map_free(clone);
    CHECK(first->refcount == 2 && equal->refcount == 1);
    am_string_free(first); am_string_free(equal);
    CHECK(held->items[0]->refcount == 1);
    am_list_free(held);
    am_map_ref(NULL); am_map_free(NULL);
    puts("PASS map ownership/order: byte-equal keys, replacement, delete/reinsert, independent copies and retained key snapshots");
}

static void test_invalid(void) {
    AM_Map* map = am_map_new();
    AM_String* name = key("key");
    CHECK(map && am_map_set(map, name, 2) == 0);
    CHECK(am_map_get(NULL, name, NULL) == -1);
    CHECK(am_map_get(map, NULL, NULL) == -1 && am_map_get(map, name, NULL) == -1);
    CHECK(am_map_has(NULL, name) == 0 && am_map_has(map, NULL) == 0);
    CHECK(am_map_delete(NULL, name) == -1 && am_map_delete(map, NULL) == -1);
    CHECK(am_map_set(NULL, name, 2) == -1 && am_map_set(map, NULL, 2) == -1);
    CHECK(am_map_keys(NULL) == NULL && am_map_clone(NULL) == NULL);
    const float invalid[] = {INFINITY, -INFINITY, NAN};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(am_map_set(map, name, invalid[i]) == -1);
        value_is(map, "key", 2);
        CHECK(map->len == 1 && name->refcount == 2);
    }
    AM_String* missing = key("missing");
    float unchanged = 37;
    CHECK(am_map_get(map, missing, &unchanged) == 0 && unchanged == 37);
    CHECK(am_map_has(map, missing) == 0 && am_map_delete(map, missing) == 0);
    CHECK(am_map_set(map, missing, INFINITY) == -1 && map->len == 1 && missing->refcount == 1);
    am_string_free(missing); am_string_free(name); am_map_free(map);
    puts("PASS map invalid values: NULLs, absent vs zero, finite-only replacement and insertion");
}

static void test_rehash_delete(void) {
    AM_Map* map = am_map_new();
    CHECK(map != NULL);
    /* Dense keys with shared prefixes force repeated table growth. */
    for (int i = 0; i < 513; i++) {
        char name[40]; snprintf(name, sizeof(name), "same-prefix-%04d", i);
        put(map, name, (float)i);
    }
    for (int i = 0; i < 513; i += 2) {
        char name[40]; snprintf(name, sizeof(name), "same-prefix-%04d", i);
        AM_String* text = key(name);
        CHECK(am_map_delete(map, text) == 1);
        am_string_free(text);
    }
    CHECK(map->len == 256);
    for (int i = 1; i < 513; i += 2) {
        char name[40]; snprintf(name, sizeof(name), "same-prefix-%04d", i);
        value_is(map, name, (float)i);
    }
    for (int i = 0; i < 513; i += 2) {
        char name[40]; snprintf(name, sizeof(name), "same-prefix-%04d", i);
        put(map, name, (float)(i + 1000));
    }
    AM_List* keys = am_map_keys(map);
    CHECK(keys && keys->len == 513);
    for (int i = 0; i < 513; i++) {
        int original = i < 256 ? i * 2 + 1 : (i - 256) * 2;
        char expected[40]; snprintf(expected, sizeof(expected), "same-prefix-%04d", original);
        CHECK(!strcmp(keys->items[i]->data, expected));
    }
    am_list_free(keys); am_map_free(map);
    puts("PASS map index: repeated rehash, interleaved deletes, surviving lookup and reinsertion order");
}

static AM_List* strings(int count, const char* const* values) {
    AM_List* list = am_list_new(); CHECK(list != NULL);
    for (int i = 0; i < count; i++) {
        AM_String* item = key(values[i]);
        CHECK(am_list_push(list, item) == i + 1);
        am_string_free(item);
    }
    return list;
}

static void test_structural_keys(void) {
    const struct { int count; const char* values[3]; const char* encoded; } cases[] = {
        {0, {NULL}, "0:"}, {1, {""}, "1:0:"},
        {2, {"a", "bc"}, "2:1:a2:bc"}, {2, {"ab", "c"}, "2:2:ab1:c"},
        {1, {"a|bc"}, "1:4:a|bc"}, {2, {"a|", "bc"}, "2:2:a|2:bc"},
        {2, {"é", "🦉"}, "2:2:é4:🦉"}, {2, {"é", "🦉"}, "2:3:é4:🦉"},
        {3, {"", ":", "\n\x01"}, "3:0:1::2:\n\x01"},
        {1, {"2:1:a2:bc"}, "1:9:2:1:a2:bc"},
    };
    CHECK(am_list_key(NULL) == NULL);
    AM_Map* map = am_map_new(); CHECK(map != NULL);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        AM_List* input = strings(cases[i].count, cases[i].values);
        AM_String* encoded = am_list_key(input);
        CHECK(encoded && !strcmp(encoded->data, cases[i].encoded));
        CHECK(encoded->byte_len == (int)strlen(cases[i].encoded));
        CHECK(am_map_has(map, encoded) == 0);
        CHECK(am_map_set(map, encoded, (float)i) == 0);
        am_list_free(input);
        CHECK(!strcmp(encoded->data, cases[i].encoded));
        am_string_free(encoded);
    }
    CHECK(map->len == (int)(sizeof(cases) / sizeof(cases[0])));
    am_map_free(map);
    int exact_length = AM_MAX_STRING_BYTES - 10; /* '1:1048566:' is 10 bytes. */
    char* large = (char*)malloc((size_t)exact_length + 2); CHECK(large != NULL);
    memset(large, 'x', (size_t)exact_length + 1); large[exact_length] = 0;
    const char* one[] = {large};
    AM_List* input = strings(1, one);
    AM_String* encoded = am_list_key(input);
    CHECK(encoded && encoded->byte_len == AM_MAX_STRING_BYTES);
    am_string_free(encoded); am_list_free(input);
    large[exact_length] = 'x'; large[exact_length + 1] = 0;
    input = strings(1, one);
    CHECK(am_list_key(input) == NULL);
    am_list_free(input); free(large);
    puts("PASS structural keys: byte lengths, boundaries, empty/control/delimiter text, distinct tuples and exact 1 MiB limit");
}

#ifndef AML_MAP_ALLOC_WRAP
static void test_limit(void) {
    AM_Map* map = am_map_new(); CHECK(map != NULL);
    for (int i = 0; i < AM_MAX_MAP_ITEMS; i++) {
        char name[24]; snprintf(name, sizeof(name), "key-%05d", i);
        AM_String* text = am_string_new(name);
        if (!text || am_map_set(map, text, (float)i)) {
            fprintf(stderr, "FAIL map capacity at %d\n", i); exit(1);
        }
        am_string_free(text);
    }
    CHECK(map->len == AM_MAX_MAP_ITEMS);
    AM_String* existing = key("key-00000"); AM_String* extra = key("overflow");
    CHECK(am_map_set(map, extra, 1) == -1 && extra->refcount == 1);
    CHECK(am_map_set(map, existing, 99) == 0 && map->len == AM_MAX_MAP_ITEMS);
    value_is(map, "key-00000", 99);
    CHECK(am_map_delete(map, existing) == 1 && am_map_set(map, extra, 1) == 0);
    AM_List* order = am_map_keys(map);
    CHECK(order && order->len == AM_MAX_MAP_ITEMS);
    CHECK(!strcmp(order->items[0]->data, "key-00001"));
    CHECK(order->items[AM_MAX_MAP_ITEMS - 1] == extra);
    am_list_free(order); am_string_free(existing); am_string_free(extra); am_map_free(map);
    puts("PASS map capacity: 65536 distinct keys, failed insertion, successful replacement and reclaimed slot");
}
#endif

#ifdef AML_MAP_ALLOC_WRAP
static void test_faults(void) {
    CHECK(live_count == 0);
    int succeeded = 0;
    for (int budget = 0; budget < 12; budget++) {
        allocation_budget = budget;
        AM_Map* map = am_map_new();
        allocation_budget = -1;
        if (map) { am_map_free(map); succeeded = 1; }
        CHECK(live_count == 0);
        if (succeeded) break;
    }
    CHECK(succeeded);
    AM_Map* map = am_map_new(); CHECK(map != NULL);
    for (int i = 0; i < 4; i++) { char name[24]; snprintf(name, sizeof(name), "key-%d", i); put(map, name, (float)i); }
    AM_String* candidate = NULL;
    for (int i = 4; i < 4096; i++) {
        char name[24]; snprintf(name, sizeof(name), "key-%d", i);
        candidate = key(name);
        allocation_budget = 0;
        int rc = am_map_set(map, candidate, (float)i);
        allocation_budget = -1;
        if (rc != 0) break;
        am_string_free(candidate); candidate = NULL;
    }
    CHECK(candidate && map->len >= 4);
    int length = map->len, capacity = map->capacity, bucket_capacity = map->bucket_capacity;
    AM_MapEntry* entries = map->entries; int* buckets = map->buckets;
    AM_List* original = am_map_keys(map); CHECK(original != NULL);
    int baseline = live_count;
    succeeded = 0;
    for (int budget = 0; budget < 12; budget++) {
        allocation_budget = budget;
        int rc = am_map_set(map, candidate, 99);
        allocation_budget = -1;
        if (!rc) { succeeded = 1; break; }
        CHECK(map->len == length && map->capacity == capacity && map->bucket_capacity == bucket_capacity);
        CHECK(map->entries == entries && map->buckets == buckets && live_count == baseline);
        CHECK(candidate->refcount == 1);
        for (int i = 0; i < length; i++) {
            float value = -1;
            CHECK(am_map_get(map, original->items[i], &value) == 1 && value == (float)i);
        }
    }
    CHECK(succeeded && map->len == length + 1);
    am_list_free(original);
    baseline = live_count;
    for (int operation = 0; operation < 2; operation++) {
        succeeded = 0;
        for (int budget = 0; budget < 24; budget++) {
            allocation_budget = budget;
            AM_Map* clone = operation == 0 ? am_map_clone(map) : NULL;
            AM_List* keys = operation == 1 ? am_map_keys(map) : NULL;
            allocation_budget = -1;
            succeeded = clone != NULL || keys != NULL;
            am_map_free(clone); am_list_free(keys);
            CHECK(live_count == baseline && candidate->refcount == 2);
            if (succeeded) break;
        }
        CHECK(succeeded);
    }
    AM_List* keys = am_map_keys(map); CHECK(keys != NULL);
    baseline = live_count;
    succeeded = 0;
    for (int budget = 0; budget < 12; budget++) {
        allocation_budget = budget;
        AM_String* encoded = am_list_key(keys);
        allocation_budget = -1;
        if (encoded) succeeded = 1;
        am_string_free(encoded);
        CHECK(live_count == baseline);
        if (succeeded) break;
    }
    CHECK(succeeded);
    am_list_free(keys);
    allocation_budget = 0;
    int replace = am_map_set(map, candidate, 101);
    int removed = am_map_delete(map, candidate);
    allocation_budget = -1;
    CHECK(replace == 0 && removed == 1 && candidate->refcount == 1);
    am_map_free(map); am_string_free(candidate);
    CHECK(live_count == 0);
    puts("PASS map allocation faults: published growth is atomic, clone/keys/encoding cleanup, replacement/delete need no allocation");
}

static int run_fault(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program = am_program_open(source); if (!program) return 1;
        while (!am_program_step(program, 1)) {}
        return am_program_close(program);
    }
    void* compiled = am_compile(source); if (!compiled) return 1;
    int rc = am_exec_compiled(compiled); am_free_compiled(compiled); return rc;
}

static void test_persistent_faults(void) {
    am_init(); CHECK(live_count == 0);
    AM_Map* source = am_map_new(); AM_String* item = key("before");
    CHECK(source && am_map_set(source, item, 3) == 0);
    AM_List* list = am_list_new(); CHECK(list && am_list_push(list, item) == 1);
    int owner_blocks = live_count;
    for (int mode = 0; mode < 3; mode++) {
        int succeeded = 0;
        for (int budget = 0; budget < 160; budget++) {
            am_persistent_clear(); CHECK(live_count == owner_blocks);
            CHECK(am_set_var_text("text", "before") == 0);
            CHECK(am_set_var_map("counts", source) == 0);
            CHECK(am_set_var_list("words", list) == 0);
            float data[] = {1, 2}; CHECK(am_set_var_array("a", data, 2) == 0);
            int baseline = live_count, refs = item->refcount;
            allocation_budget = budget;
            int rc = run_fault(mode, "map_set(counts, 'before', 4)\nmap_set(counts, 'after', 5)\nlist_push(words, 'after')\na[0] = 9\ntext = text_concat(text, 'after')\n");
            allocation_budget = -1;
            const AM_Map* stored = am_get_var_map("counts");
            const AM_List* words = am_get_var_list("words");
            int len = 0; const float* a = am_get_var_array("a", &len);
            const char* text = am_get_var_text("text");
            CHECK(stored && words && a && text && len == 2);
            float value = -1; CHECK(am_map_get(stored, item, &value) == 1);
            if (rc == 0) {
                CHECK(stored->len == 2 && value == 4 && words->len == 2 && a[0] == 9);
                CHECK(!strcmp(text, "beforeafter")); succeeded = 1;
            } else {
                CHECK(am_get_error()[0] != 0);
                CHECK(stored->len == 1 && value == 3 && words->len == 1 && a[0] == 1);
                CHECK(!strcmp(text, "before"));
                CHECK(live_count == baseline && item->refcount == refs);
            }
            am_persistent_clear(); CHECK(live_count == owner_blocks && item->refcount == 3);
            if (succeeded) { printf("PASS map persistent mode=%d: %d refused allocation budgets and one complete commit\n", mode, budget); break; }
        }
        CHECK(succeeded);
    }
    CHECK(am_set_var_map("counts", source) == 0);
    const AM_Map* old = am_get_var_map("counts");
    int baseline = live_count;
    allocation_budget = 0; int rc = am_set_var_map("counts", source); allocation_budget = -1;
    CHECK(rc != 0 && am_get_var_map("counts") == old && live_count == baseline);
    am_persistent_mode(0); am_map_free(source); am_list_free(list); am_string_free(item);
    CHECK(live_count == 0);
}
#endif

int main(void) {
    test_ownership_order(); test_invalid(); test_rehash_delete(); test_structural_keys();
#ifndef AML_MAP_ALLOC_WRAP
    test_limit();
#else
    /* Full-capacity ownership runs in normal/ASan mode; fault accounting keeps
       its fixed pointer registry bounded while sweeping every allocation site. */
    test_faults(); test_persistent_faults(); CHECK(live_count == 0);
#endif
    printf("AML_MAP_API_OK %d checks\n", checks);
    return 0;
}
