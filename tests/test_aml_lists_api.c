/* Homogeneous text-list ownership, copy semantics, limits and allocation faults. */
#include "ariannamethod.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL list API line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#ifdef AML_LIST_ALLOC_WRAP
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
    fprintf(stderr, "FAIL list API: freeing/reallocating an untracked allocation\n");
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

static void item_equals(const AM_List* list, int index, const char* expected) {
    AM_String* item = am_list_get(list, index);
    CHECK(item != NULL);
    CHECK(strcmp(item->data, expected) == 0);
    am_string_free(item);
}

static void test_lifetimes(void) {
    AM_List* list = am_list_new();
    CHECK(list && list->len == 0 && list->refcount == 1);
    CHECK(list->capacity >= 0 && list->capacity <= AM_MAX_LIST_ITEMS);
    CHECK(am_list_get(list, 0) == NULL);
    AM_String* owl = am_string_new("🦉");
    AM_String* word = am_string_new("שלום");
    CHECK(owl && word && owl->refcount == 1 && word->refcount == 1);
    CHECK(am_list_push(list, owl) == 1);
    CHECK(am_list_push(list, word) == 2);
    CHECK(owl->refcount == 2 && word->refcount == 2);
    AM_String* held = am_list_get(list, 0);
    CHECK(held == owl && owl->refcount == 3);
    CHECK(am_list_set(list, 0, held) == 0);
    CHECK(owl->refcount == 3); /* Self replacement retains before releasing. */
    CHECK(am_list_set(list, -1, owl) == 0);
    CHECK(owl->refcount == 4 && word->refcount == 1);
    am_string_free(word);
    am_string_free(owl);
    am_list_ref(list);
    CHECK(list->refcount == 2);
    am_list_free(list);
    CHECK(list->refcount == 1);
    item_equals(list, -1, "🦉");
    am_list_free(list);
    CHECK(held->refcount == 1 && !strcmp(held->data, "🦉"));
    am_string_free(held);
    am_list_free(NULL);
    am_list_ref(NULL);
    puts("PASS list lifetime: retained inputs/get results, self replacement, shared list owner");
}

static void test_values_and_copies(void) {
    const char* values[] = {"", "Привет", "שלום", "Café", "🦉", "é", "é", "🦉"};
    AM_List* list = am_list_new();
    CHECK(list != NULL);
    for (int i = 0; i < 8; i++) {
        AM_String* item = am_string_new(values[i]);
        CHECK(item && am_list_push(list, item) == i + 1);
        am_string_free(item);
    }
    for (int i = 0; i < 8; i++) {
        item_equals(list, i, values[i]);
        item_equals(list, i - 8, values[i]);
    }
    AM_String* needle = am_string_new("🦉");
    CHECK(am_list_find(list, needle) == 4);
    am_string_free(needle);
    needle = am_string_new("é");
    CHECK(am_list_find(list, needle) == 5);
    am_string_free(needle);
    needle = am_string_new("é");
    CHECK(am_list_find(list, needle) == 6);
    am_string_free(needle);
    needle = am_string_new("missing");
    CHECK(am_list_find(list, needle) == -1);
    am_string_free(needle);
    needle = am_string_new("");
    CHECK(am_list_find(list, needle) == 0);
    am_string_free(needle);

    AM_List* copy = am_list_clone(list);
    CHECK(copy && copy != list && copy->items != list->items && copy->len == 8);
    for (int i = 0; i < 8; i++) CHECK(copy->items[i] == list->items[i]);
    AM_String* replacement = am_string_new("changed");
    CHECK(replacement && am_list_set(copy, 1, replacement) == 0);
    CHECK(am_list_push(copy, replacement) == 9);
    am_string_free(replacement);
    item_equals(list, 1, "Привет");
    item_equals(copy, 1, "changed");
    CHECK(list->len == 8);
    am_list_free(copy);

    const struct { int start, end, first, length; } slices[] = {
        {0, 8, 0, 8}, {1, 4, 1, 3}, {-3, -1, 5, 2},
        {INT_MIN, INT_MAX, 0, 8}, {-100, 2, 0, 2},
        {3, 1, 3, 0}, {8, 99, 8, 0}, {INT_MAX, INT_MIN, 8, 0},
    };
    for (size_t i = 0; i < sizeof(slices) / sizeof(slices[0]); i++) {
        AM_List* part = am_list_slice(list, slices[i].start, slices[i].end);
        CHECK(part && part != list && part->len == slices[i].length);
        for (int j = 0; j < part->len; j++)
            item_equals(part, j, values[slices[i].first + j]);
        am_list_free(part);
    }
    AM_List* survivor = am_list_slice(list, -2, 8);
    CHECK(survivor && survivor->len == 2);
    am_list_free(list);
    item_equals(survivor, 0, "é");
    item_equals(survivor, 1, "🦉");
    am_list_free(survivor);
    puts("PASS list values: Unicode equality, negative indices, slices, independent containers");
}

static void test_failures_and_limit(void) {
    AM_List* list = am_list_new();
    AM_String* item = am_string_new("owl");
    CHECK(list && item);
    CHECK(am_list_clone(NULL) == NULL && am_list_slice(NULL, 0, 1) == NULL);
    CHECK(am_list_sorted(NULL) == NULL);
    CHECK(am_list_get(NULL, 0) == NULL && am_list_find(NULL, item) == -1);
    CHECK(am_list_find(list, NULL) == -1);
    CHECK(am_list_push(NULL, item) == -1 && am_list_push(list, NULL) == -1);
    CHECK(am_list_set(NULL, 0, item) == -1);
    CHECK(list->len == 0 && item->refcount == 1);
    for (int i = 0; i < AM_MAX_LIST_ITEMS; i++) {
        if (am_list_push(list, item) != i + 1) {
            fprintf(stderr, "FAIL list capacity at item %d\n", i);
            exit(1);
        }
    }
    CHECK(list->len == AM_MAX_LIST_ITEMS && list->capacity == AM_MAX_LIST_ITEMS);
    CHECK(item->refcount == AM_MAX_LIST_ITEMS + 1);
    CHECK(am_list_push(list, item) == -1);
    CHECK(list->len == AM_MAX_LIST_ITEMS && item->refcount == AM_MAX_LIST_ITEMS + 1);
    const int invalid[] = {INT_MIN, -AM_MAX_LIST_ITEMS - 1, AM_MAX_LIST_ITEMS, INT_MAX};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(am_list_get(list, invalid[i]) == NULL);
        CHECK(am_list_set(list, invalid[i], item) == -1);
        CHECK(list->len == AM_MAX_LIST_ITEMS && item->refcount == AM_MAX_LIST_ITEMS + 1);
    }
    CHECK(am_list_set(list, 0, NULL) == -1);
    item_equals(list, 0, "owl");
    item_equals(list, -AM_MAX_LIST_ITEMS, "owl");
    item_equals(list, -1, "owl");
    AM_List* copy = am_list_sorted(list);
    CHECK(copy && copy->len == AM_MAX_LIST_ITEMS);
    am_list_free(list);
    CHECK(item->refcount == AM_MAX_LIST_ITEMS + 1);
    item_equals(copy, AM_MAX_LIST_ITEMS - 1, "owl");
    am_list_free(copy);
    CHECK(item->refcount == 1);
    am_string_free(item);
    puts("PASS list failures: NULL/index rejection, exact 65536 capacity, refused mutation is atomic");
}

static void test_sorted(void) {
    static const char* values[] = {
        "é", "owl", "", "ow", "é", "a", "é", "א", "Ж", "Ω", "A", "𐀀", "\x7f", "\x1f"
    };
    static const int order[] = {2, 13, 10, 5, 4, 3, 1, 12, 0, 6, 9, 8, 7, 11};
    AM_List* source = am_list_new();
    CHECK(source != NULL);
    AM_List* empty = am_list_sorted(source);
    CHECK(empty && empty != source && empty->len == 0);
    am_list_free(empty);
    AM_String* owners[14];
    for (int i = 0; i < 14; i++) {
        owners[i] = am_string_new(values[i]);
        CHECK(owners[i] && am_list_push(source, owners[i]) == i + 1);
        // Every prefix exercises a separate run/merge boundary, including odd sizes.
        AM_List* prefix = am_list_sorted(source);
        CHECK(prefix && prefix != source && prefix->items != source->items && prefix->len == i + 1);
        for (int j = 0; j <= i; j++) {
            int rank = 0;
            // Independent rank oracle, retaining original index for equal values.
            for (int k = 0; k <= i; k++) {
                int cmp = strcmp(values[k], values[j]);
                if (cmp < 0 || (cmp == 0 && k < j)) rank++;
            }
            CHECK(prefix->items[rank] == owners[j]);
            CHECK(source->items[j] == owners[j] && owners[j]->refcount == 3);
        }
        am_list_free(prefix);
        for (int j = 0; j <= i; j++) CHECK(owners[j]->refcount == 2);
    }
    AM_List* sorted = am_list_sorted(source);
    CHECK(sorted && sorted->len == 14);
    for (int i = 0; i < 14; i++) {
        CHECK(sorted->items[i] == owners[order[i]]);
        item_equals(sorted, i, values[order[i]]);
        item_equals(source, i, values[i]);
    }
    CHECK(owners[0] != owners[6]); // Equal text retains the order of distinct owners.
    AM_String* replacement = am_string_new("changed");
    CHECK(replacement && am_list_set(sorted, 0, replacement) == 0);
    CHECK(am_list_push(sorted, replacement) == 15);
    CHECK(source->len == 14 && source->items[2] == owners[2]);
    am_string_free(replacement);
    am_list_free(source);
    item_equals(sorted, 1, "\x1f");
    item_equals(sorted, 8, "é");
    am_list_free(sorted);
    for (int i = 0; i < 14; i++) {
        CHECK(owners[i]->refcount == 1);
        am_string_free(owners[i]);
    }
    puts("PASS list sorting: UTF-8 byte order, stable duplicate owners, empty/odd prefixes, independent retained output");
}

#ifdef AML_LIST_ALLOC_WRAP
static void test_allocation_failures(void) {
    CHECK(live_count == 0);
    int succeeded = 0;
    for (int budget = 0; budget < 8; budget++) {
        allocation_budget = budget;
        AM_List* list = am_list_new();
        allocation_budget = -1;
        if (list) { am_list_free(list); succeeded = 1; }
        CHECK(live_count == 0);
        if (succeeded) break;
    }
    CHECK(succeeded);
    AM_List* list = am_list_new();
    AM_String* item = am_string_new("🦉שלום");
    CHECK(list && item);
    CHECK(am_list_push(list, item) == 1);
    while (list->len < list->capacity) CHECK(am_list_push(list, item) > 0);
    int len = list->len, capacity = list->capacity, refs = item->refcount;
    AM_String** entries = list->items;
    int baseline = live_count;
    allocation_budget = 0;
    int rc = am_list_push(list, item);
    allocation_budget = -1;
    CHECK(rc == -1);
    CHECK(list->len == len && list->capacity == capacity && list->items == entries);
    CHECK(item->refcount == refs && live_count == baseline);
    CHECK(am_list_push(list, item) == len + 1);
    baseline = live_count;
    refs = item->refcount;
    for (int operation = 0; operation < 3; operation++) {
        succeeded = 0;
        for (int budget = 0; budget < 8; budget++) {
            allocation_budget = budget;
            AM_List* copy = operation == 2 ? am_list_sorted(list)
                : operation == 1 ? am_list_slice(list, 0, list->len) : am_list_clone(list);
            allocation_budget = -1;
            if (copy) { am_list_free(copy); succeeded = 1; }
            CHECK(live_count == baseline && item->refcount == refs);
            CHECK(list->len == len + 1);
            if (succeeded) break;
        }
        CHECK(succeeded);
    }
    allocation_budget = 0;
    AM_String* held = am_list_get(list, 0);
    rc = am_list_set(list, 0, held);
    allocation_budget = -1;
    CHECK(held == item && rc == 0);
    am_string_free(held);
    CHECK(live_count == baseline && item->refcount == refs);
    am_list_free(list);
    am_string_free(item);
    CHECK(live_count == 0);
    puts("PASS allocation faults: constructor/clone/slice/sort cleanup, atomic growth, allocation-free get/set");
}

static int run_fault_script(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program = am_program_open(source);
        if (!program) return 1;
        while (!am_program_step(program, 1)) {}
        return am_program_close(program);
    }
    void* compiled = am_compile(source);
    if (!compiled) return 1;
    int rc = am_exec_compiled(compiled);
    am_free_compiled(compiled);
    return rc;
}

static void test_persistent_faults(void) {
    am_init();
    CHECK(live_count == 0);
    AM_List* source = am_list_new();
    AM_String* item = am_string_new("before");
    CHECK(source && item && am_list_push(source, item) == 1);
    int owner_blocks = live_count;
    for (int mode = 0; mode < 3; mode++) {
        int succeeded = 0;
        for (int budget = 0; budget < 128; budget++) {
            am_persistent_clear();
            CHECK(live_count == owner_blocks);
            /* A retained string precedes both allocated container copies. */
            CHECK(am_set_var_text("word", "before") == 0);
            CHECK(am_set_var_list("words", source) == 0);
            float numbers[] = {1, 2};
            CHECK(am_set_var_array("numbers", numbers, 2) == 0);
            int baseline = live_count;
            int item_refs = item->refcount;
            allocation_budget = budget;
            int rc = run_fault_script(mode,
                "list_push(words, 'after')\n"
                "words = list_sorted(words)\n"
                "numbers[0] = 9\n"
                "word = text_concat(word, 'after')\n");
            allocation_budget = -1;
            const AM_List* stored = am_get_var_list("words");
            int length = 0;
            const float* data = am_get_var_array("numbers", &length);
            const char* text = am_get_var_text("word");
            if (!stored || !data || !text || length != 2)
                fprintf(stderr, "persistent fault mode=%d budget=%d rc=%d error=%s\n",
                        mode, budget, rc, am_get_error());
            CHECK(stored && data && text && length == 2);
            if (rc == 0) {
                CHECK(stored->len == 2 && data[0] == 9 && data[1] == 2);
                item_equals(stored, 0, "after");
                item_equals(stored, 1, "before");
                CHECK(!strcmp(text, "beforeafter"));
                succeeded = 1;
            } else {
                if (!am_get_error()[0])
                    fprintf(stderr, "missing allocation diagnostic mode=%d budget=%d rc=%d\n",
                            mode, budget, rc);
                CHECK(am_get_error()[0] != 0);
                CHECK(stored->len == 1 && data[0] == 1 && data[1] == 2);
                item_equals(stored, 0, "before");
                CHECK(!strcmp(text, "before"));
                CHECK(live_count == baseline && item->refcount == item_refs);
            }
            am_persistent_clear();
            CHECK(live_count == owner_blocks && item->refcount == 2);
            if (succeeded) {
                printf("PASS persistent allocation sweep mode=%d: %d refused budgets, complete save succeeds\n",
                       mode, budget);
                break;
            }
        }
        CHECK(succeeded);
    }
    CHECK(am_set_var_list("words", source) == 0);
    const AM_List* old = am_get_var_list("words");
    CHECK(old && old != source);
    int baseline = live_count, refs = item->refcount;
    for (int budget = 0; budget < 2; budget++) {
        allocation_budget = budget;
        int rc = am_set_var_list("words", source);
        allocation_budget = -1;
        CHECK(rc != 0);
        CHECK(am_get_var_list("words") == old);
        CHECK(live_count == baseline && item->refcount == refs);
    }
    am_persistent_mode(0);
    am_list_free(source);
    am_string_free(item);
    CHECK(live_count == 0);
    puts("PASS persistent allocation faults: atomic host replacement and partial table cleanup");
}
#endif

int main(void) {
    test_lifetimes();
    test_values_and_copies();
    test_failures_and_limit();
    test_sorted();
#ifdef AML_LIST_ALLOC_WRAP
    test_allocation_failures();
    test_persistent_faults();
    CHECK(live_count == 0);
#endif
    printf("AML_LIST_API_OK %d checks\n", checks);
    return 0;
}
