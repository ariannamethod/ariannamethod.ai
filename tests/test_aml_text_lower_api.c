/* Unicode 15 lowercase, original-input sigma context, immutable ownership,
 * byte growth and allocation failure. Optional GNU-compatible linker gate:
 * -DAML_LOWER_ALLOC_WRAP -Wl,--wrap=malloc,--wrap=free
 */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixtures/text_lower_fixture.h"

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL lowercase line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#ifdef AML_LOWER_ALLOC_WRAP
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
        CHECK(live_blocks > 0);
        live_blocks--;
    }
    __real_free(ptr);
}
#endif

static int codepoints(const char* utf8) {
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)utf8; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

static void exact_case(const char* source, const char* expected) {
    AM_String* text = am_string_new(source);
    CHECK(text != NULL);
    am_string_ref(text);
    AM_String* result = am_string_lower(text);
    CHECK(result != NULL);
    CHECK(result != text && result->data != text->data);
    CHECK(result->refcount == 1 && text->refcount == 2);
    CHECK(result->byte_len == (int)strlen(expected));
    CHECK(result->len == codepoints(expected));
    CHECK(strcmp(result->data, expected) == 0);
    CHECK(strcmp(text->data, source) == 0);
    am_string_free(text);
    CHECK(text->refcount == 1 && strcmp(text->data, source) == 0);
    am_string_free(text);
    CHECK(strcmp(result->data, expected) == 0);
    AM_String* twice = am_string_lower(result);
    CHECK(twice != NULL && twice != result);
    CHECK(strcmp(twice->data, expected) == 0);
    am_string_free(twice);
    am_string_free(result);
}

static AM_String* repeated(const char* unit, int n, int suffix) {
    size_t unit_len = strlen(unit);
    size_t size = unit_len * (size_t)n + (size_t)suffix;
    char* data = malloc(size + 1);
    CHECK(data != NULL);
    for (int i = 0; i < n; i++) memcpy(data + unit_len * (size_t)i, unit, unit_len);
    memset(data + unit_len * (size_t)n, 'A', (size_t)suffix);
    data[size] = 0;
    AM_String* text = am_string_new(data);
    free(data);
    CHECK(text != NULL);
    return text;
}

static void test_limits(void) {
    AM_String* text = repeated("A", AM_MAX_STRING_BYTES, 0);
    AM_String* out = am_string_lower(text);
    CHECK(out != NULL && out->byte_len == AM_MAX_STRING_BYTES);
    CHECK(out->len == AM_MAX_STRING_BYTES);
    CHECK(out->data[0] == 'a' && out->data[out->byte_len - 1] == 'a');
    CHECK(out->data[out->byte_len] == 0);
    CHECK(text->data[0] == 'A' && text->data[text->byte_len - 1] == 'A');
    am_string_free(out);
    am_string_free(text);

    const int count = AM_MAX_STRING_BYTES / 3;
    const int suffix = AM_MAX_STRING_BYTES % 3;
    /* U+0130 adds a codepoint and a byte: 2 UTF-8 bytes -> 3. */
    text = repeated("\304\260", count, suffix);
    out = am_string_lower(text);
    CHECK(out != NULL && out->byte_len == AM_MAX_STRING_BYTES);
    CHECK(out->len == 2 * count + suffix);
    CHECK(memcmp(out->data, "i\314\207", 3) == 0);
    CHECK(memcmp(out->data + 3 * (count - 1), "i\314\207", 3) == 0);
    CHECK(out->data[AM_MAX_STRING_BYTES - 1] == 'a');
    am_string_free(out);
    am_string_free(text);
    text = repeated("\304\260", count, suffix + 1);
    CHECK(am_string_lower(text) == NULL);
    CHECK(text->refcount == 1 && text->byte_len == 2 * count + suffix + 1);
    am_string_free(text);

    /* U+023A expands UTF-8 bytes without changing the codepoint count. */
    text = repeated("\310\272", count, suffix);
    out = am_string_lower(text);
    CHECK(out != NULL && out->byte_len == AM_MAX_STRING_BYTES);
    CHECK(out->len == count + suffix);
    CHECK(memcmp(out->data, "\342\261\245", 3) == 0);
    am_string_free(out);
    am_string_free(text);
    text = repeated("\310\272", count, suffix + 1);
    CHECK(am_string_lower(text) == NULL);
    am_string_free(text);

    /* Kelvin sign shrinks to ASCII; the input may fill the whole byte budget. */
    text = repeated("\342\204\252", count, suffix);
    out = am_string_lower(text);
    CHECK(out != NULL && out->byte_len == count + suffix);
    CHECK(out->len == count + suffix && out->data[0] == 'k');
    CHECK(out->data[count - 1] == 'k' && out->data[count] == 'a');
    am_string_free(out);
    am_string_free(text);
}

static void test_invalid(void) {
    CHECK(am_string_lower(NULL) == NULL);
    AM_String invalid = {NULL, 0, 0, 1};
    CHECK(am_string_lower(&invalid) == NULL);
    invalid.data = "a";
    invalid.byte_len = -1;
    CHECK(am_string_lower(&invalid) == NULL);
    invalid.byte_len = AM_MAX_STRING_BYTES + 1;
    CHECK(am_string_lower(&invalid) == NULL);
    invalid.data = "\300\257";
    invalid.byte_len = 2;
    CHECK(am_string_lower(&invalid) == NULL);
    invalid.data = "\355\240\200";
    invalid.byte_len = 3;
    CHECK(am_string_lower(&invalid) == NULL);
    invalid.data = "\360\237";
    invalid.byte_len = 2;
    CHECK(am_string_lower(&invalid) == NULL);
    invalid.data = "a\0b";
    invalid.byte_len = 3;
    CHECK(am_string_lower(&invalid) == NULL);
}

static void test_allocation(void) {
#ifdef AML_LOWER_ALLOC_WRAP
    CHECK(live_blocks == 0);
    AM_String* source = am_string_new("A\316\243\304\260");
    CHECK(source != NULL && live_blocks == 2);
    for (int budget = 0; budget <= 2; budget++) {
        allocation_budget = budget;
        AM_String* out = am_string_lower(source);
        allocation_budget = -1;
        if (budget < 2) {
            CHECK(out == NULL && live_blocks == 2);
        } else {
            CHECK(out != NULL && live_blocks == 4);
            CHECK(strcmp(out->data, "a\317\203i\314\207") == 0);
            am_string_free(out);
        }
        CHECK(source->refcount == 1);
        CHECK(strcmp(source->data, "A\316\243\304\260") == 0);
        CHECK(live_blocks == 2);
    }
    am_string_free(source);
    CHECK(live_blocks == 0);
#endif
}

int main(void) {
    for (size_t i = 0; i < sizeof(lower_cases) / sizeof(lower_cases[0]); i++)
        exact_case(lower_cases[i].source, lower_cases[i].expected);
    test_invalid();
    test_limits();
    test_allocation();
#ifdef AML_LOWER_ALLOC_WRAP
    CHECK(live_blocks == 0);
#endif
    printf("PASS Unicode lowercase: %d API checks (%zu Python fixtures)\n", checks,
           sizeof(lower_cases) / sizeof(lower_cases[0]));
    return 0;
}
