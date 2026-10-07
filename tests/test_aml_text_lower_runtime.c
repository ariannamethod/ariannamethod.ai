/* text_lower through interpreter, resumable, bytecode, runner and amlc paths. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/text_lower_fixture.h"

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL lowercase runtime line %d: %s; AML: %s\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* program =
    "def lower_owned(word):\n"
    "    inside = text_lower(word)\n"
    "    return inside\n"
    "source = 'İ ΑΣ'\n"
    "alias = source\n"
    "folded = lower_owned(source)\n"
    "text_lower(source)\n"
    "source = lower_owned(source)\n"
    "parts = list_new()\n"
    "list_push(parts, text_lower(\"AΣ'A\"))\n"
    "from_parts = list_get(parts, 0)\n"
    "mixed = TEXT_LOWER('Ⱥ K')\n"
    "char_count = text_len(folded)\n"
    "byte_count = text_bytes(folded)\n";

static const char* printed =
    "PRINT alias\nPRINT folded\nPRINT source\nPRINT from_parts\n"
    "PRINT mixed\nPRINT char_count\nPRINT byte_count\n";

static const char* expected = "İ ΑΣ\ni̇ ας\ni̇ ας\naσ'a\nⱥ k\n5\n8\n";

static const struct { const char* source; const char* error; } bad[] = {
    {"x = text_lower()\n", "arguments"},
    {"x = text_lower('A', 'B')\n", "arguments"},
    {"x = text_lower(1)\n", "requires a string"},
    {"x = text_lower([1])\n", "requires a string"},
    {"x = text_lower(list_new())\n", "requires a string"},
    {"x = text_lower(map_new())\n", "requires a string"},
    {"x = text_lower(text_len('A'))\n", "requires a string"},
    {"def text_lower(word):\n    return word\nx = 1\n", "intrinsic"},
    {"def TEXT_LOWER(word):\n    return word\nx = 1\n", "intrinsic"},
    {"word = 'İ'\ni = 0\nwhile i < 19:\n    word = text_concat(word, word)\n"
     "    i = i + 1\nx = text_lower(word)\n", "string limit"},
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

static void text_equals(const char* name, const char* expected_text) {
    const char* value = am_get_var_text(name);
    CHECK(value != NULL && strcmp(value, expected_text) == 0);
}

static void test_program(int mode) {
    am_init();
    am_persistent_mode(1);
    CHECK(run(mode, program) == 0);
    text_equals("alias", "İ ΑΣ");
    text_equals("folded", "i̇ ας");
    text_equals("source", "i̇ ας");
    text_equals("from_parts", "aσ'a");
    text_equals("mixed", "ⱥ k");
    CHECK(am_get_var_float("char_count") == 5);
    CHECK(am_get_var_float("byte_count") == 8);
    CHECK(run(mode, "again = text_lower(folded)\nfolded = 9\n") == 0);
    text_equals("again", "i̇ ας");
    CHECK(am_get_var_text("folded") == NULL);
    text_equals("alias", "İ ΑΣ");
    am_persistent_clear();
}

static void test_failures(int mode) {
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char script[1024];
        snprintf(script, sizeof(script), "sentinel = 0\n%s\nsentinel = 1\n", bad[i].source);
        am_init();
        CHECK(am_set_var_text("kept", "İ ΑΣ") == 0);
        CHECK(run(mode, script) != 0);
        if (!strstr(am_get_error(), bad[i].error))
            fprintf(stderr, "fixture=%zu mode=%d expected=%s actual=%s\n",
                    i, mode, bad[i].error, am_get_error());
        CHECK(strstr(am_get_error(), bad[i].error) != NULL);
        CHECK(am_get_var_float("sentinel") == 0);
        text_equals("kept", "İ ΑΣ");
        am_persistent_clear();
    }
}

static void quoted(FILE* file, const char* value) {
    fputc('\'', file);
    for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
        switch (*p) {
            case '\'': fputs("\\'", file); break;
            case '\\': fputs("\\\\", file); break;
            case '\n': fputs("\\n", file); break;
            case '\r': fputs("\\r", file); break;
            case '\t': fputs("\\t", file); break;
            default: fputc(*p, file); break;
        }
    }
    fputc('\'', file);
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long len = ftell(file);
    if (len < 0 || len > 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char* source = malloc((size_t)len + 1);
    if (!source) { fclose(file); return NULL; }
    size_t count = fread(source, 1, (size_t)len, file);
    fclose(file);
    if (count != (size_t)len) { free(source); return NULL; }
    source[len] = 0;
    return source;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--fixture")) {
        fputs(program, stdout);
        fputs(printed, stdout);
        for (size_t i = 0; i < sizeof(lower_cases) / sizeof(lower_cases[0]); i++) {
            fputs("PRINT text_lower(", stdout);
            quoted(stdout, lower_cases[i].source);
            fputs(")\n", stdout);
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--expected")) {
        fputs(expected, stdout);
        for (size_t i = 0; i < sizeof(lower_cases) / sizeof(lower_cases[0]); i++) {
            fputs(lower_cases[i].expected, stdout);
            fputc('\n', stdout);
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(bad) / sizeof(bad[0]));
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int n = atoi(argv[2]);
        if (n < 0 || (size_t)n >= sizeof(bad) / sizeof(bad[0])) return 2;
        fputs(bad[n].source, stdout);
        fputs("PRINT 'WRONG_CONTINUATION'\n", stdout);
        return 0;
    }
    am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]);
        if (mode < 0 || mode > 2) return 2;
        char* source = read_source(argv[3]);
        if (!source) return 2;
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "lower runtime: %s\n", am_get_error());
        free(source);
        am_persistent_clear();
        return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) {
        test_program(mode);
        test_failures(mode);
    }
    printf("PASS Unicode lowercase runtime: %d checks; %zu invalid fixtures\n", checks,
           sizeof(bad) / sizeof(bad[0]));
    return 0;
}
