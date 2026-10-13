/* Fixed decimal conversion: exact bits, fenv/locale independence and ownership. */
#include "ariannamethod.h"
#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/text_fixed_fixture.h"

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL fixed API line %d: %s (%s)\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

#ifdef AML_FIXED_ALLOC_WRAP
static void* live[4096];
static int live_count, allocation_budget = -1;
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
char* __real_realpath(const char*, char*);
static int allowed(void) {
    if (allocation_budget == 0) return 0;
    if (allocation_budget > 0) allocation_budget--;
    return 1;
}
static int index_of(void* ptr) {
    for (int i = 0; i < live_count; i++) if (live[i] == ptr) return i;
    fputs("FAIL fixed allocation tracker: untracked pointer\n", stderr);
    exit(1);
}
static void* track(void* ptr) {
    if (ptr) { CHECK(live_count < 4096); live[live_count++] = ptr; }
    return ptr;
}
void* __wrap_malloc(size_t bytes) { return allowed() ? track(__real_malloc(bytes)) : NULL; }
void* __wrap_calloc(size_t n, size_t bytes) { return allowed() ? track(__real_calloc(n, bytes)) : NULL; }
void* __wrap_realloc(void* ptr, size_t bytes) {
    if (!allowed()) return NULL;
    if (!ptr) return track(__real_realloc(NULL, bytes));
    int index = index_of(ptr);
    void* replacement = __real_realloc(ptr, bytes);
    if (replacement) live[index] = replacement;
    else if (!bytes) live[index] = live[--live_count];
    return replacement;
}
void __wrap_free(void* ptr) {
    if (ptr) { int index = index_of(ptr); live[index] = live[--live_count]; }
    __real_free(ptr);
}
char* __wrap_realpath(const char* path, char* resolved) {
    if (resolved) return __real_realpath(path, resolved);
    if (!allowed()) { errno = ENOMEM; return NULL; }
    return track(__real_realpath(path, NULL));
}
#endif

static float from_bits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void test_cases(void) {
    const char* locales[] = {"C", "en_US.UTF-8", "de_DE.UTF-8"};
    int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    int available = 0, comma_locale = 0;
    for (size_t loc = 0; loc < sizeof(locales) / sizeof(locales[0]); loc++) {
        if (!setlocale(LC_NUMERIC, locales[loc])) continue;
        available++;
        if (!strcmp(localeconv()->decimal_point, ",")) comma_locale++;
        for (size_t mode = 0; mode < sizeof(modes) / sizeof(modes[0]); mode++) {
            CHECK(fesetround(modes[mode]) == 0);
            for (size_t i = 0; i < sizeof(fixed_cases) / sizeof(fixed_cases[0]); i++) {
                float input = from_bits(fixed_cases[i].bits);
                CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
                CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
                AM_String* out = am_string_fixed(input, fixed_cases[i].digits);
                CHECK(out && !strcmp(out->data, fixed_cases[i].expected));
                CHECK(out->len == (int)strlen(fixed_cases[i].expected));
                CHECK(out->byte_len == out->len && out->refcount == 1);
                CHECK(fegetround() == modes[mode]);
                CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
                am_string_ref(out);
                CHECK(out->refcount == 2);
                am_string_free(out);
                CHECK(!strcmp(out->data, fixed_cases[i].expected));
                am_string_free(out);
            }
        }
    }
    CHECK(available >= 1);
    if (getenv("AML_FIXED_TEST_LOCALE")) CHECK(comma_locale > 0);
    CHECK(fesetround(FE_TONEAREST) == 0);
    CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    CHECK(setlocale(LC_NUMERIC, "C") != NULL);
    printf("PASS fixed decimal: %zu Python fixtures x 4 rounding modes x %d locales\n",
           sizeof(fixed_cases) / sizeof(fixed_cases[0]), available);
}

static void test_invalid(void) {
    uint32_t rejected[] = {0x7f800000u, 0xff800000u, 0x7fc00000u,
                           0xffc00000u, 0x7f800001u, 0xff800001u};
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
        for (int digits = 0; digits <= 9; digits++)
            CHECK(am_string_fixed(from_bits(rejected[i]), digits) == NULL);
    CHECK(am_string_fixed(1, -1) == NULL);
    CHECK(am_string_fixed(1, 10) == NULL);
    CHECK(am_string_fixed(1, INT_MIN) == NULL);
    CHECK(am_string_fixed(1, INT_MAX) == NULL);
}

#ifdef AML_FIXED_ALLOC_WRAP
static int run(int mode, const char* script) {
    if (mode == 0) return am_exec(script);
    if (mode == 1) {
        void* p = am_program_open(script);
        if (!p) return 1;
        while (!am_program_step(p, 1)) {}
        return am_program_close(p);
    }
    void* p = am_compile(script);
    if (!p) return 1;
    int rc = am_exec_compiled(p);
    am_free_compiled(p);
    return rc;
}

static void test_allocations(void) {
    CHECK(live_count == 0);
    for (int budget = 0; budget <= 2; budget++) {
        allocation_budget = budget;
        AM_String* out = am_string_fixed(from_bits(0xff7fffff), 9);
        allocation_budget = -1;
        CHECK((out != NULL) == (budget == 2));
        am_string_free(out);
        CHECK(live_count == 0);
    }
    am_init();
    for (int mode = 0; mode < 3; mode++) {
        int completed = 0;
        for (int budget = 0; budget < 64; budget++) {
            CHECK(am_set_var_text("target", "still here") == 0);
            CHECK(am_set_var_text("alias", "still here") == 0);
            allocation_budget = budget;
            int rc = run(mode, "target = text_fixed(0.15, 1)\n");
            allocation_budget = -1;
            const char* target = am_get_var_text("target");
            const char* alias = am_get_var_text("alias");
            CHECK(target && alias && !strcmp(alias, "still here"));
            if (!rc) {
                CHECK(!strcmp(target, "0.2"));
                completed = 1;
            } else {
                CHECK(am_get_error()[0] != 0);
                CHECK(!strcmp(target, "still here"));
            }
            am_persistent_clear();
            CHECK(live_count == 0);
            if (completed) {
                printf("PASS fixed allocation sweep mode=%d: %d refused budgets, no retained blocks\n", mode, budget);
                break;
            }
        }
        CHECK(completed);
    }
}
#endif

int main(void) {
    test_cases();
    test_invalid();
#ifdef AML_FIXED_ALLOC_WRAP
    test_allocations();
    CHECK(live_count == 0);
#endif
    printf("PASS fixed text API: %d checks\n", checks);
    return 0;
}
