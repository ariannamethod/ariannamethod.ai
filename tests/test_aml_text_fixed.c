/* Fixed text values and function capacity through five execution paths. */
#include "ariannamethod.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/text_fixed_fixture.h"

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL fixed runtime line %d: %s (%s)\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* program =
    "def own_fixed(number, places):\n"
    "    result = text_fixed(number, places)\n"
    "    return result\n"
    "source = -0.0\n"
    "formatted = own_fixed(source, 3)\n"
    "alias = formatted\n"
    "text_fixed(1.25, 1)\n"
    "formatted = TEXT_FIXED(1.25, 1)\n"
    "parts = list_new()\n"
    "list_push(parts, text_fixed(1.75, 1))\n"
    "record = record_new()\n"
    "record_set(record, 'number', 2.5)\n"
    "from_record = text_fixed(record_get(record, 'number'), 0)\n"
    "joined = text_concat(formatted, text_fixed(0.45, 1))\n";
static const char* printed =
    "PRINT alias\nPRINT formatted\nPRINT list_get(parts, 0)\n"
    "PRINT from_record\nPRINT joined\n";
static const char* expected = "-0.000\n1.2\n1.8\n2\n1.20.4\n";

static const struct { const char* source; const char* error; } bad[] = {
    {"target = text_fixed()\n", "arguments"},
    {"target = text_fixed(1)\n", "arguments"},
    {"target = text_fixed(1, 2, 3)\n", "arguments"},
    {"target = text_fixed('1', 1)\n", "finite scalar"},
    {"target = text_fixed([1], 1)\n", "finite scalar"},
    {"target = text_fixed(list_new(), 1)\n", "finite scalar"},
    {"target = text_fixed(map_new(), 1)\n", "finite scalar"},
    {"target = text_fixed(record_new(), 1)\n", "finite scalar"},
    {"target = text_fixed(1e39, 1)\n", "finite scalar"},
    {"target = text_fixed(-1e39, 1)\n", "finite scalar"},
    {"target = text_fixed(1e39 - 1e39, 1)\n", "finite scalar"},
    {"target = text_fixed(1, '1')\n", "digits"},
    {"target = text_fixed(1, [1])\n", "digits"},
    {"target = text_fixed(1, list_new())\n", "digits"},
    {"target = text_fixed(1, map_new())\n", "digits"},
    {"target = text_fixed(1, record_new())\n", "digits"},
    {"target = text_fixed(1, -1)\n", "digits"},
    {"target = text_fixed(1, 10)\n", "digits"},
    {"target = text_fixed(1, 1.5)\n", "digits"},
    {"target = text_fixed(1, 1e39)\n", "digits"},
    {"target = text_fixed(1, 1e39 - 1e39)\n", "digits"},
    {"target = text_fixed(1, text_fixed(1, 0))\n", "digits"},
    {"target = text_concat('before', text_fixed(1, -1))\n", "digits"},
    {"target = text_fixed(1, 2) + 1\n", "scalar expression"},
    {"def text_fixed(value, digits):\n    return value\n", "intrinsic"},
    {"def TEXT_FIXED(value, digits):\n    return value\n", "intrinsic"},
};

static int run(int mode, const char* script) {
    if (mode == 0) return am_exec(script);
    if (mode == 1) {
        void* p = am_program_open(script);
        if (!p) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(p, 1)) CHECK(--budget > 0);
        CHECK(am_program_remaining(p) == 0);
        return am_program_close(p);
    }
    void* p = am_compile(script);
    if (!p) return 1;
    int rc = am_exec_compiled(p);
    am_free_compiled(p);
    return rc;
}

static void equal(const char* name, const char* text) {
    const char* value = am_get_var_text(name);
    CHECK(value && !strcmp(value, text));
}

static void test_values(int mode) {
    am_init();
    am_persistent_mode(1);
    CHECK(run(mode, program) == 0);
    equal("alias", "-0.000");
    equal("formatted", "1.2");
    equal("from_record", "2");
    equal("joined", "1.20.4");
    CHECK(run(mode, "formatted = 9\nalias_copy = alias\nalias = [1, 2]\n") == 0);
    equal("alias_copy", "-0.000");
    CHECK(am_get_var_text("alias") == NULL);
    am_persistent_clear();
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char script[512];
        snprintf(script, sizeof(script), "sentinel = 0\n%s\nsentinel = 1\n", bad[i].source);
        CHECK(am_set_var_text("target", "before") == 0);
        CHECK(am_set_var_text("alias", "before") == 0);
        CHECK(run(mode, script) != 0);
        CHECK(strstr(am_get_error(), bad[i].error) != NULL);
        equal("target", "before");
        equal("alias", "before");
        CHECK(am_get_var_float("sentinel") == 0);
        am_persistent_clear();
    }
}

/* Every admitted helper is called, so registration without usable entries
 * cannot pass. Definitions and sequential calls fit the unchanged line limit. */
static void append_source(char* script, size_t* used, const char* format, ...) {
    if (!script || *used >= 32768) abort();
    va_list args;
    va_start(args, format);
    int count = vsnprintf(script + *used, 32768 - *used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= 32768 - *used) abort();
    *used += (size_t)count;
}

static void capacity_script(char* script, int definitions, int print) {
    size_t used = 0;
    append_source(script, &used, "TENSION 0.875\n");
    for (int i = 0; i < definitions; i++)
        append_source(script, &used, "def capacity_%d():\n    return %d\n", i, i);
    append_source(script, &used, "total = 0\n");
    for (int i = 0; i < definitions; i++)
        append_source(script, &used, "total = total + capacity_%d()\n", i);
    append_source(script, &used, print ? "PRINT total\n" : "CHANNEL WRITE capacity_result total\n");
}

static void test_capacity(void) {
    CHECK(AML_MAX_FUNCS == 256);
    CHECK(AML_MAX_LINES == 8192);
    char script[32768];
    int counts[] = {112, 239, 240};
    for (int mode = 0; mode < 3; mode++) {
        for (size_t j = 0; j < sizeof(counts) / sizeof(counts[0]); j++) {
            int n = counts[j];
            capacity_script(script, n, 0);
            /* Avoid an output/channel effect for these API-only executions. */
            *strstr(script, "CHANNEL WRITE") = 0;
            am_init();
            CHECK(am_exec("TENSION 0.125\n") == 0);
            am_persistent_mode(1);
            int rc = run(mode, script);
            if (n < 240) {
                CHECK(rc == 0);
                CHECK(am_get_var_float("total") == (float)(n * (n - 1) / 2));
                CHECK(am_get_state()->tension == 0.875f);
            } else {
                CHECK(rc != 0 && strstr(am_get_error(), "function limit exceeded"));
                CHECK(am_get_state()->tension == 0.125f);
            }
            am_persistent_clear();
        }
    }
    am_init();
    CHECK(am_channel_create("capacity_result", 1) >= 0);
    capacity_script(script, 239, 0);
    CHECK(am_spawn_launch("capacity_worker", script) == 0);
    CHECK(am_spawn_await("capacity_worker") == 0);
    float total = -1;
    CHECK(am_channel_read("capacity_result", &total) == 0);
    CHECK(total == 28441);
    am_channel_close_all();
    am_persistent_clear();
    printf("PASS function slots: 112 and 239 user functions; 240 rejects before effects; worker239; context=%zu bytes\n",
           sizeof(AML_ExecCtx));
}

static void test_worker(void) {
    am_init();
    CHECK(am_set_var_text("original", "held by worker") == 0);
    CHECK(am_channel_create("fixed_gate", 1) >= 0);
    CHECK(am_channel_create("fixed_result", 2) >= 0);
    CHECK(am_spawn_launch("fixed_worker",
        "CHANNEL READ fixed_gate released\n"
        "CHANNEL WRITE fixed_result text_equal(original, 'held by worker')\n"
        "local = text_fixed(-0.0, 9)\n"
        "CHANNEL WRITE fixed_result text_equal(local, '-0.000000000')\n") == 0);
    CHECK(am_set_var_text("original", "rebound by parent") == 0);
    CHECK(am_channel_write("fixed_gate", 1) == 0);
    CHECK(am_spawn_await("fixed_worker") == 0);
    float result;
    CHECK(am_channel_read("fixed_result", &result) == 0 && result == 1);
    CHECK(am_channel_read("fixed_result", &result) == 0 && result == 1);
    equal("original", "rebound by parent");
    CHECK(am_get_var_text("local") == NULL);
    am_channel_close_all();
    am_persistent_clear();
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    CHECK(length >= 0 && length < 1024 * 1024);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    char* source = malloc((size_t)length + 1);
    CHECK(source != NULL);
    CHECK(fread(source, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    source[length] = 0;
    return source;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--fixture")) {
        fputs(program, stdout);
        fputs(printed, stdout);
        for (size_t i = 0; i < sizeof(fixed_cases) / sizeof(fixed_cases[0]); i++)
            printf("PRINT text_fixed(%s, %d)\n", fixed_cases[i].literal, fixed_cases[i].digits);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--expected")) {
        fputs(expected, stdout);
        for (size_t i = 0; i < sizeof(fixed_cases) / sizeof(fixed_cases[0]); i++)
            puts(fixed_cases[i].expected);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(bad) / sizeof(bad[0]));
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int n = atoi(argv[2]);
        CHECK(n >= 0 && (size_t)n < sizeof(bad) / sizeof(bad[0]));
        fputs(bad[n].source, stdout);
        fputs("PRINT 'WRONG_CONTINUATION'\n", stdout);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--capacity")) {
        int n = atoi(argv[2]);
        CHECK(n == 112 || n == 239 || n == 240);
        char script[32768];
        capacity_script(script, n, 1);
        fputs(script, stdout);
        return 0;
    }
    am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]);
        CHECK(mode >= 0 && mode <= 2);
        char* source = read_source(argv[3]);
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "fixed runtime: %s\n", am_get_error());
        free(source);
        am_persistent_clear();
        return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) test_values(mode);
    test_capacity();
    test_worker();
    printf("PASS fixed text runtime: %d checks; %zu invalid programs\n", checks,
           sizeof(bad) / sizeof(bad[0]));
    return 0;
}
