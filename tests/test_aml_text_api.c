/* Native UTF-8 ownership, codepoint operations, and the 1 MiB byte limit.
 * Optional allocation-failure gate (GNU-compatible linker):
 *   -DAML_TEXT_ALLOC_WRAP -Wl,--wrap=malloc,--wrap=free
 */
#include "ariannamethod.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL text API line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#ifdef AML_TEXT_ALLOC_WRAP
static int allocation_budget = -1;
static size_t live_blocks;
void* __real_malloc(size_t bytes);
void __real_free(void* ptr);

void* __wrap_malloc(size_t bytes) {
    if (allocation_budget == 0) return NULL;
    if (allocation_budget > 0) allocation_budget--;
    void* ptr = __real_malloc(bytes);
    if (ptr) live_blocks++;
    return ptr;
}

void __wrap_free(void* ptr) {
    if (ptr) {
        if (!live_blocks) {
            fprintf(stderr, "FAIL text API: unmatched free\n");
            exit(1);
        }
        live_blocks--;
    }
    __real_free(ptr);
}
#endif

static void expect_text(const AM_String* text, const char* bytes, int len) {
    CHECK(text != NULL);
    CHECK(text->data != NULL);
    CHECK(text->len == len);
    CHECK(text->byte_len == (int)strlen(bytes));
    CHECK(memcmp(text->data, bytes, (size_t)text->byte_len + 1) == 0);
    CHECK(text->refcount > 0);
}

static void test_languages(void) {
    static const struct { const char* text; int len; } cases[] = {
        {"", 0}, {"Haiku", 5}, {"Café", 4}, {"Привет", 6}, {"שלום", 4},
        {"🦉", 1}, {"e\xCC\x81", 2}, {"👩‍💻", 3}, {"line\n\tend", 9},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        AM_String* text = am_string_new(cases[i].text);
        expect_text(text, cases[i].text, cases[i].len);
        CHECK(text->refcount == 1);
        am_string_free(text);
    }
    CHECK(am_string_new(NULL) == NULL);
    CHECK(am_string_concat(NULL, NULL) == NULL);
    CHECK(am_string_slice(NULL, 0, 1) == NULL);
    CHECK(am_string_find(NULL, NULL) == -1);
    CHECK(am_string_codepoint(NULL, 0) == -1);
    am_string_ref(NULL);
    am_string_free(NULL);
    puts("PASS languages: Cyrillic, Hebrew, French, emoji, combining marks, empty text");
}

static void test_validation(void) {
    static const char* invalid[] = {
        "\x80", "\xBF",                         /* isolated continuation */
        "\xC0\xAF", "\xC1\xBF",                 /* overlong 2-byte */
        "\xE0\x80\x80", "\xE0\x9F\xBF",         /* overlong 3-byte */
        "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF", /* overlong 4-byte */
        "\xED\xA0\x80", "\xED\xBF\xBF",         /* surrogate range */
        "\xF4\x90\x80\x80", "\xF4\xBF\xBF\xBF", /* > U+10FFFF */
        "\xF5\x80\x80\x80", "\xF7\xBF\xBF\xBF", /* invalid leading byte */
        "\xF8\x88\x80\x80\x80", "\xFC\x84\x80\x80\x80\x80",
        "\xFE", "\xFF",                         /* obsolete / invalid encodings */
        "\xC2", "\xE2", "\xE2\x82",             /* truncated sequences */
        "\xF0", "\xF0\x9F", "\xF0\x9F\xA6",
        "\xC2\x7F", "\xC2\xC0",                /* invalid continuation */
        "\xE2\x28\xA1", "\xE2\x82\x41",
        "\xF0\x9F\x28\x80", "\xF0\x9F\xA6\x41",
        "a\x80", "é\xED\xA0\x80",             /* invalid after a valid prefix */
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        CHECK(am_string_new(invalid[i]) == NULL);

    static const struct { int cp; const char* utf8; } boundary[] = {
        {1, "\x01"}, {0x7F, "\x7F"},
        {0x80, "\xC2\x80"}, {0x7FF, "\xDF\xBF"},
        {0x800, "\xE0\xA0\x80"}, {0xD7FF, "\xED\x9F\xBF"},
        {0xE000, "\xEE\x80\x80"}, {0xFFFF, "\xEF\xBF\xBF"},
        {0x10000, "\xF0\x90\x80\x80"}, {0x10FFFF, "\xF4\x8F\xBF\xBF"},
        {0x1F989, "🦉"},
    };
    for (size_t i = 0; i < sizeof(boundary) / sizeof(boundary[0]); i++) {
        AM_String* text = am_string_from_codepoint(boundary[i].cp);
        expect_text(text, boundary[i].utf8, 1);
        CHECK(am_string_codepoint(text, 0) == boundary[i].cp);
        CHECK(am_string_codepoint(text, -1) == boundary[i].cp);
        AM_String* decoded = am_string_new(boundary[i].utf8);
        expect_text(decoded, boundary[i].utf8, 1);
        am_string_free(text);
        am_string_free(decoded);
    }
    const int invalid_cp[] = {INT_MIN, -1, 0, 0xD800, 0xDBFF, 0xDC00,
                              0xDFFF, 0x110000, INT_MAX};
    for (size_t i = 0; i < sizeof(invalid_cp) / sizeof(invalid_cp[0]); i++)
        CHECK(am_string_from_codepoint(invalid_cp[i]) == NULL);
    puts("PASS UTF-8 validation: overlong, surrogate, truncated, continuation and range errors");
}

static void test_ownership(void) {
    char original[] = "Café";
    AM_String* text = am_string_new(original);
    CHECK(text != NULL);
    original[0] = 'X';
    expect_text(text, "Café", 4);
    AM_String* alias = text;
    am_string_ref(alias);
    CHECK(text->refcount == 2);
    am_string_ref(alias);
    CHECK(text->refcount == 3);
    am_string_free(text);
    CHECK(alias->refcount == 2);
    am_string_free(text);
    CHECK(alias->refcount == 1);

    AM_String* doubled = am_string_concat(alias, alias);
    AM_String* whole = am_string_slice(alias, 0, alias->len);
    expect_text(doubled, "CaféCafé", 8);
    expect_text(whole, "Café", 4);
    CHECK(doubled != alias && doubled->data != alias->data);
    CHECK(whole != alias && whole->data != alias->data);
    CHECK(alias->refcount == 1);
    am_string_free(alias);
    expect_text(doubled, "CaféCafé", 8);
    expect_text(whole, "Café", 4);

    AM_String* empty = am_string_new("");
    AM_String* left = am_string_concat(empty, whole);
    AM_String* right = am_string_concat(whole, empty);
    expect_text(left, "Café", 4);
    expect_text(right, "Café", 4);
    CHECK(am_string_concat(whole, NULL) == NULL);
    CHECK(am_string_concat(NULL, whole) == NULL);
    am_string_free(doubled); am_string_free(whole); am_string_free(empty);
    expect_text(left, "Café", 4);
    expect_text(right, "Café", 4);
    am_string_free(left); am_string_free(right);
    puts("PASS ownership: copied input, reference lifetime, concat/slice independence");
}

static void expect_slice(const AM_String* text, int start, int end,
                         const char* expected, int len) {
    AM_String* slice = am_string_slice(text, start, end);
    expect_text(slice, expected, len);
    CHECK(slice->refcount == 1);
    am_string_free(slice);
}

static void test_index_slice_find(void) {
    AM_String* text = am_string_new("AéЖש🦉e\xCC\x81");
    expect_text(text, "AéЖש🦉e\xCC\x81", 7);
    const int cp[] = {'A', 0xE9, 0x416, 0x5E9, 0x1F989, 'e', 0x301};
    for (int i = 0; i < text->len; i++) {
        CHECK(am_string_codepoint(text, i) == cp[i]);
        CHECK(am_string_codepoint(text, i - text->len) == cp[i]);
    }
    CHECK(am_string_codepoint(text, 7) == -1);
    CHECK(am_string_codepoint(text, -8) == -1);
    CHECK(am_string_codepoint(text, INT_MIN) == -1);
    CHECK(am_string_codepoint(text, INT_MAX) == -1);
    expect_slice(text, 2, 5, "Жש🦉", 3);
    expect_slice(text, -3, -1, "🦉e", 2);
    expect_slice(text, -1, 7, "\xCC\x81", 1);
    expect_slice(text, 4, 2, "", 0);
    expect_slice(text, 7, 7, "", 0);
    expect_slice(text, 99, 100, "", 0);
    expect_slice(text, -100, -99, "", 0);
    expect_slice(text, 0, -1, "AéЖש🦉e", 6);
    expect_slice(text, INT_MIN, INT_MAX, text->data, 7);
    expect_slice(text, -7, 7, text->data, 7);

    static const struct { const char* needle; int index; } needles[] = {
        {"", 0}, {"A", 0}, {"éЖ", 1}, {"ש🦉", 3}, {"e\xCC\x81", 5},
        {"\xCC\x81", 6}, {"café", -1}, {"🦉e\xCC\x81!", -1},
    };
    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        AM_String* needle = am_string_new(needles[i].needle);
        CHECK(needle != NULL);
        CHECK(am_string_find(text, needle) == needles[i].index);
        am_string_free(needle);
    }
    CHECK(am_string_find(text, text) == 0);
    CHECK(am_string_find(text, NULL) == -1);
    CHECK(am_string_find(NULL, text) == -1);
    AM_String* empty = am_string_new("");
    CHECK(am_string_find(empty, empty) == 0);
    CHECK(am_string_find(empty, text) == -1);
    CHECK(am_string_codepoint(empty, 0) == -1);
    CHECK(am_string_codepoint(empty, -1) == -1);
    expect_slice(empty, INT_MIN, INT_MAX, "", 0);
    am_string_free(empty);
    am_string_free(text);

    text = am_string_new("cafe\xCC\x81 café cafe\xCC\x81");
    AM_String* decomposed = am_string_new("e\xCC\x81");
    AM_String* composed = am_string_new("é");
    CHECK(text && decomposed && composed);
    CHECK(am_string_find(text, decomposed) == 3);
    CHECK(am_string_find(text, composed) == 9);
    am_string_free(text); am_string_free(decomposed); am_string_free(composed);
    puts("PASS indexing: negative/clamped slices, codepoint search, combining sequence identity");
}

static void test_byte_limit(void) {
    char* bytes = (char*)malloc((size_t)AM_MAX_STRING_BYTES + 2);
    CHECK(bytes != NULL);
    memset(bytes, 'x', AM_MAX_STRING_BYTES);
    bytes[AM_MAX_STRING_BYTES] = 0;
    AM_String* exact = am_string_new(bytes);
    CHECK(exact != NULL);
    CHECK(exact->byte_len == AM_MAX_STRING_BYTES && exact->len == AM_MAX_STRING_BYTES);
    CHECK(exact->data[AM_MAX_STRING_BYTES] == 0);
    AM_String* empty = am_string_new("");
    AM_String* one = am_string_new("x");
    CHECK(empty && one);
    AM_String* full_copy = am_string_concat(exact, empty);
    CHECK(full_copy != NULL && full_copy->byte_len == AM_MAX_STRING_BYTES);
    CHECK(memcmp(full_copy->data, bytes, (size_t)AM_MAX_STRING_BYTES + 1) == 0);
    CHECK(am_string_concat(exact, one) == NULL);
    CHECK(am_string_concat(one, exact) == NULL);
    CHECK(exact->refcount == 1 && one->refcount == 1);
    expect_slice(exact, AM_MAX_STRING_BYTES - 1, INT_MAX, "x", 1);
    am_string_free(full_copy); am_string_free(exact);
    am_string_free(empty); am_string_free(one);

    bytes[AM_MAX_STRING_BYTES] = 'x';
    bytes[AM_MAX_STRING_BYTES + 1] = 0;
    CHECK(am_string_new(bytes) == NULL);
    bytes[AM_MAX_STRING_BYTES - 1] = (char)0xC3;
    bytes[AM_MAX_STRING_BYTES] = 0;
    CHECK(am_string_new(bytes) == NULL);

    for (int i = 0; i < AM_MAX_STRING_BYTES; i += 2) {
        bytes[i] = (char)0xC3;
        bytes[i + 1] = (char)0xA9;
    }
    exact = am_string_new(bytes);
    CHECK(exact != NULL);
    CHECK(exact->byte_len == AM_MAX_STRING_BYTES && exact->len == AM_MAX_STRING_BYTES / 2);
    CHECK(am_string_codepoint(exact, -1) == 0xE9);
    expect_slice(exact, -2, INT_MAX, "éé", 2);
    am_string_free(exact);
    free(bytes);
    puts("PASS limit: exact 1 MiB ASCII/multibyte text, overflow, truncated final codepoint");
}

#ifdef AML_TEXT_ALLOC_WRAP
static void test_allocation_failures(void) {
    CHECK(live_blocks == 0);
    AM_String* a = am_string_new("é🦉");
    AM_String* b = am_string_new("שלום");
    CHECK(a && b);
    size_t baseline = live_blocks;
    for (int budget = 0; budget <= 1; budget++) {
        allocation_budget = budget;
        AM_String* out = am_string_new("valid");
        allocation_budget = -1;
        CHECK(out == NULL && live_blocks == baseline);
        allocation_budget = budget;
        out = am_string_concat(a, b);
        allocation_budget = -1;
        CHECK(out == NULL && live_blocks == baseline);
        allocation_budget = budget;
        out = am_string_slice(a, 1, 2);
        allocation_budget = -1;
        CHECK(out == NULL && live_blocks == baseline);
        allocation_budget = budget;
        out = am_string_slice(a, 1, 1);
        allocation_budget = -1;
        CHECK(out == NULL && live_blocks == baseline);
        allocation_budget = budget;
        out = am_string_from_codepoint(0x1F989);
        allocation_budget = -1;
        CHECK(out == NULL && live_blocks == baseline);
        CHECK(a->refcount == 1 && b->refcount == 1);
    }
    allocation_budget = 0;
    CHECK(am_string_find(a, a) == 0);
    CHECK(am_string_codepoint(a, -1) == 0x1F989);
    am_string_ref(a);
    CHECK(a->refcount == 2);
    am_string_free(a);
    allocation_budget = -1;
    CHECK(live_blocks == baseline);
    expect_text(a, "é🦉", 2);
    expect_text(b, "שלום", 4);
    am_string_free(a); am_string_free(b);
    CHECK(live_blocks == 0);
    puts("PASS allocation failures: both allocations, all constructors, no leaked owners/buffers");
}
#endif

int main(void) {
    test_languages();
    test_validation();
    test_ownership();
    test_index_slice_find();
    test_byte_limit();
#ifdef AML_TEXT_ALLOC_WRAP
    test_allocation_failures();
    CHECK(live_blocks == 0);
#endif
    printf("AML_TEXT_API_OK %d checks\n", checks);
    return 0;
}
