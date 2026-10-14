/* A UTC civil day as a value: deterministic time, ownership and failure gates. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef AML_UTC_WRAP
#include "fixtures/utc_day_clock.h"
#endif

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL UTC day line %d: %s; AML: %s\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* fixture =
    "def carry(value):\n    return value\n"
    "captured = carry(UtC_DaY())\n"
    "assert(isfinite(captured) and captured == floor(captured), 'integral civil day')\n"
    "assert(captured >= -16777216 and captured <= 16777216, 'exact day range')\n"
    "request = record_new()\n"
    "record_set(request, 'day', captured)\n"
    "SPAWN frozen_day:\n    return request\n"
    "record_set(request, 'day', -50)\n"
    "AWAIT frozen_day INTO received\n"
    "assert(record_get(received, 'day') == captured, 'captured worker day')\n"
    "PRINT 'UTC_DAY_OK'\n";

static const struct { const char* source; const char* error; } invalid[] = {
    {"day = utc_day(1)\n", "utc_day requires no arguments"},
    {"day = UTC_DAY('clock')\n", "utc_day requires no arguments"},
    {"day = utc_day(1, 2)\n", "utc_day requires no arguments"},
    {"def utc_day():\n    return 1\n", "function name collides with an intrinsic"},
    {"def UTC_DAY():\n    return 1\n", "function name collides with an intrinsic"},
};

static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program = am_program_open(source);
        if (!program) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(program, 1)) CHECK(--budget > 0);
        return am_program_close(program);
    }
    void* compiled = am_compile(source);
    if (!compiled) return 1;
    int rc = am_exec_compiled(compiled);
    am_free_compiled(compiled);
    return rc;
}

static void prepare(void) {
    am_init();
    am_persistent_mode(1);
    CHECK(am_exec("TENSION 0.375\nBIRTH 731\nSELF_NOW_DAYS 911\nJANUS_KEY 1\n") == 0);
}

static void field_unchanged(const AM_State* before) {
    CHECK(memcmp(before, am_get_state(), sizeof(*before)) == 0);
    CHECK(am_calendar_epoch_seconds() == 1727956800L);
    CHECK(am_birth_set() && am_birth_epoch_days() == 731);
    CHECK(am_janus_key_armed() == 1);
}

#ifdef AML_UTC_WRAP
static void set_clock(int64_t seconds) {
    char value[32];
    snprintf(value, sizeof(value), "%" PRId64, seconds);
    CHECK(setenv("AML_TEST_UTC_SECONDS", value, 1) == 0);
    atomic_store(&utc_test_time_calls, 0);
}

static int clock_supported(int64_t seconds) {
    if (seconds < 0 && (time_t)-1 > (time_t)0) return 0;
    return (long double)(time_t)seconds == (long double)seconds;
}

static void test_boundaries(int mode) {
    static const struct { int64_t seconds; float day; } cases[] = {
        {0, 0}, {1, 0}, {86399, 0}, {86400, 1}, {86401, 1},
        {1709078400, 19781}, {1709164799, 19781},
        {1709164800, 19782}, {1709251199, 19782}, {1709251200, 19783},
        {1727913599, 19998}, {1727913600, 19999},
        {1727956799, 19999}, {1727956800, 19999},
        {1727999999, 19999}, {1728000000, 20000},
        {2147483647, 24855},
        {-2, -1}, {-86399, -1}, {-86400, -1}, {-86401, -2},
        {-1449551462400LL, -16777216},
        {1449551462400LL, 16777216}, {1449551548799LL, 16777216},
    };
    static const char* zones[] = {
        "UTC0", "PST8PDT,M3.2.0/2,M11.1.0/2", "JST-9"
    };
    for (size_t zone = 0; zone < sizeof(zones) / sizeof(zones[0]); zone++) {
        CHECK(setenv("TZ", zones[zone], 1) == 0);
        tzset();
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if (!clock_supported(cases[i].seconds)) continue;
            prepare();
            AM_State before = *am_get_state();
            set_clock(cases[i].seconds);
            CHECK(run(mode, "day = utc_day()\n") == 0);
            CHECK(am_get_var_float("day") == cases[i].day);
            CHECK(atomic_load(&utc_test_time_calls) == 1);
            field_unchanged(&before);
            am_persistent_clear();
        }
    }
    const int64_t failures[] = {-1, -1449551462401LL, 1449551548800LL};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        if (i != 0 && !clock_supported(failures[i])) continue;
        prepare();
        AM_State before = *am_get_state();
        set_clock(failures[i]);
        CHECK(run(mode, "day = 123\nsentinel = 0\nday = utc_day()\nsentinel = 1\n") != 0);
        CHECK(strstr(am_get_error(), i == 0 ? "could not read the system clock" : "exact scalar day range"));
        CHECK(am_get_var_float("day") == 123);
        CHECK(am_get_var_float("sentinel") == 0);
        CHECK(atomic_load(&utc_test_time_calls) == 1);
        field_unchanged(&before);
        am_persistent_clear();
    }
}

static void test_preparation(void) {
    prepare();
    set_clock(1709164799);
    void* program = am_program_open("day = utc_day()\n");
    CHECK(program != NULL && atomic_load(&utc_test_time_calls) == 0);
    set_clock(1709164800);
    CHECK(am_program_step(program, 1) == 1);
    CHECK(am_program_close(program) == 0);
    CHECK(am_get_var_float("day") == 19782 && atomic_load(&utc_test_time_calls) == 1);
    set_clock(1709164799);
    void* compiled = am_compile("day = utc_day()\n");
    CHECK(compiled != NULL && atomic_load(&utc_test_time_calls) == 0);
    set_clock(1709164800);
    CHECK(am_exec_compiled(compiled) == 0);
    CHECK(am_get_var_float("day") == 19782 && atomic_load(&utc_test_time_calls) == 1);
    set_clock(1709251200);
    CHECK(am_exec_compiled(compiled) == 0);
    CHECK(am_get_var_float("day") == 19783 && atomic_load(&utc_test_time_calls) == 1);
    am_free_compiled(compiled);
    am_persistent_clear();
}
#endif

static void test_functions(int mode) {
    /* utc_day does not consume any of the 495 available user definitions. */
    char source[32768];
    size_t used = 0;
    for (int i = 0; i < 495; i++) {
        int n = snprintf(source + used, sizeof(source) - used,
                         "def user_%d():\n    return %d\n", i, i);
        CHECK(n > 0 && (size_t)n < sizeof(source) - used);
        used += (size_t)n;
    }
    snprintf(source + used, sizeof(source) - used,
             "captured = utc_day()\n"
             "SPAWN day_receipt:\n    return captured + user_494()\n"
             "AWAIT day_receipt INTO result\n"
             "assert(result == captured + 494, 'full function table')\n");
    prepare();
#ifdef AML_UTC_WRAP
    set_clock(1709164800);
#endif
    CHECK(run(mode, source) == 0);
#ifdef AML_UTC_WRAP
    CHECK(atomic_load(&utc_test_time_calls) == 1);
#endif
    am_persistent_clear();
}

static void test_values_and_errors(int mode) {
    prepare();
#ifdef AML_UTC_WRAP
    set_clock(1709164800);
#endif
    CHECK(run(mode, fixture) == 0);
#ifdef AML_UTC_WRAP
    CHECK(am_get_var_float("captured") == 19782);
    CHECK(atomic_load(&utc_test_time_calls) == 1);
#endif
    am_persistent_clear();
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char source[1024];
        snprintf(source, sizeof(source), "day = 123\nsentinel = 0\n%ssentinel = 1\n", invalid[i].source);
        prepare();
        AM_State before = *am_get_state();
#ifdef AML_UTC_WRAP
        set_clock(1709164800);
#endif
        CHECK(run(mode, source) != 0);
        CHECK(strstr(am_get_error(), invalid[i].error));
        CHECK(am_get_var_float("sentinel") == 0);
        if (i < 3) CHECK(am_get_var_float("day") == 123);
#ifdef AML_UTC_WRAP
        CHECK(atomic_load(&utc_test_time_calls) == 0);
#endif
        field_unchanged(&before);
        am_persistent_clear();
    }
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size < 0 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char* source = malloc((size_t)size + 1);
    if (!source) { fclose(file); return NULL; }
    size_t count = fread(source, 1, (size_t)size, file);
    int failed = ferror(file) || count != (size_t)size;
    fclose(file);
    if (failed) { free(source); return NULL; }
    source[count] = 0;
    return source;
}

int main(int argc, char** argv) {
#ifdef AML_UTC_WRAP
    if (argc == 3 && !strcmp(argv[1], "--supports-seconds"))
        return clock_supported(strtoll(argv[2], NULL, 10)) ? 0 : 1;
#endif
    if (argc == 2 && !strcmp(argv[1], "--fixture")) { fputs(fixture, stdout); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(invalid) / sizeof(invalid[0])); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int which = atoi(argv[2]);
        if (which < 0 || (size_t)which >= sizeof(invalid) / sizeof(invalid[0])) return 2;
        fputs(invalid[which].source, stdout);
        puts("PRINT 'WRONG_CONTINUATION'"); return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]);
        if (mode < 0 || mode > 2) return 2;
        char* source = read_source(argv[3]);
        if (!source) return 2;
        am_init();
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "UTC day runtime: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) {
        test_values_and_errors(mode);
        test_functions(mode);
#ifdef AML_UTC_WRAP
        test_boundaries(mode);
#endif
    }
#ifdef AML_UTC_WRAP
    test_preparation();
#endif
    printf("PASS UTC day: %d C/runtime checks\n", checks);
    return 0;
}
