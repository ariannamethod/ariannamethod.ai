/* The field organ and exp/log: WOLFE golden vectors, double-inside contracts, refusals. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL field line %d: %s; AML: %s\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

/* Lexical projections project(v, word, 1.) taken from WOLFE (vendor/wolfe/wolfe.c,
 * sha256 dea0df2c...) over the LALO selector field: nonzero units and values of 96. */
static const struct { const char* word; int unit[3]; float value[3]; } golden[] = {
    {"read", {55, 76, 93}, {1, 1, -1}},
    {"journal", {64, 66, 91}, {1, 1, -1}},
    {"status", {11, 50, 53}, {1, -1, 1}},
    {"inspect", {10, 13, 67}, {1, 1, -1}},
    {"limits", {29, 76, 87}, {1, 1, -1}},
    {"receipts", {16, 43, 82}, {1, 1, 1}},
    {"known", {3, 8, 74}, {1, -1, 1}},
    {"subjects", {20, 63, 86}, {1, 1, 1}},
    {"active", {3, 13, 74}, {-1, 1, 1}},
    {"facts", {8, 10, 35}, {1, 1, -1}},
    {"assertions", {35, 40, 42}, {-1, -1, -1}},
    {"observations", {49, 62, 80}, {-1, -1, 1}},
    {"history", {83, 90, 93}, {-1, 1, -1}},
    {"revision", {11, 48, 82}, {-1, 1, -1}},
    {"lineage", {25, 70, 88}, {1, 1, 1}},
    {"correction", {0, 1, 59}, {1, 1, -1}},
    {"predecessor", {40, 73, 86}, {1, 1, 1}},
    {"hypothesis", {3, 10, 13}, {-1, 1, -1}},
    {"conflicting", {44, 55, 93}, {-1, -1, -1}},
    {"evidence", {23, 44, 78}, {-1, 1, -1}},
    {"not", {34, 37, 84}, {-1, 1, -1}},
    {"lalo", {27, 37, 66}, {-1, -1, -1}},
    {"metadata", {30, 39, 92}, {-1, 1, 1}},
    {"ids", {13, 71, 92}, {-1, 1, 1}},
    {"knownat", {5, 20, 63}, {-1, -1, 1}},
    {"3", {16, 81, 94}, {-1, -1, 1}},
};

static const char* words[] = {"read", "receipts", "сова", "🦉", "日本語", "", "a", "lineage"};

static const char* fixture =
    "def lift(vec, word):\n    return field_project(vec, word, 1)\n"
    "base = zeros(96)\n"
    "v = lift(base, 'read')\n"
    "assert(v[55] == 1 and v[76] == 1 and v[93] == -1 and base[55] == 0, 'golden read')\n"
    "u = field_normalize(v)\n"
    "assert(abs(field_dot(u, u) - 1) < 0.000001, 'unit norm')\n"
    "assert(field_dot(v, lift(base, 'read')) == 3, 'self overlap')\n"
    "assert(clamp(field_dot(u, field_normalize(lift(base, 'journal'))), 0, 1) == 0, 'disjoint words')\n"
    "assert(exp(0) == 1 and log(1) == 0 and abs(log(exp(2)) - 2) < 0.000001, 'exp log')\n"
    "field_project(base, 'standalone', 1)\n"
    "SPAWN field_worker:\n    return field_dot(lift(zeros(96), 'lineage'), lift(zeros(96), 'lineage'))\n"
    "AWAIT field_worker INTO received\n"
    "assert(received == 3, 'worker field')\n"
    "PRINT 'FIELD_OK'\n";

static const struct { const char* source; const char* error; } invalid[] = {
    {"v = field_project(zeros(4), 'a')\n", "wrong number of field arguments"},
    {"v = field_project(zeros(4), 1, 1)\n", "field_project word must be a string"},
    {"v = field_project(zeros(4), 'a', 'b')\n", "field_project scale must be a finite scalar"},
    {"v = field_project(5, 'a', 1)\n", "field operation requires a nonempty numeric array"},
    {"v = field_project(list_new(), 'a', 1)\n", "field operation requires a nonempty numeric array"},
    {"v = zeros(2)\nv[1] = 3e38 * 10\nw = field_project(v, 'a', 1)\n", "field operation requires finite array values"},
    {"v = zeros(1)\nv[0] = 3e38\nw = field_project(v, 'receipts', 3e38)\n", "field_project exceeds the float32 range"},
    {"x = field_dot(zeros(3))\n", "wrong number of field arguments"},
    {"x = field_dot(zeros(3), zeros(4))\n", "field_dot requires arrays of equal length"},
    {"x = field_dot(zeros(3), 'abc')\n", "field operation requires a nonempty numeric array"},
    {"v = zeros(2)\nv[0] = 3e38\nv[1] = 3e38\nx = field_dot(v, v)\n", "field_dot exceeds the float32 range"},
    {"v = field_normalize(zeros(3), zeros(3))\n", "wrong number of field arguments"},
    {"v = field_normalize('abc')\n", "field operation requires a nonempty numeric array"},
    {"x = exp(89)\n", "exp result exceeds the float32 range"},
    {"x = exp()\n", "exp requires exactly one argument"},
    {"x = EXP(1, 2)\n", "exp requires exactly one argument"},
    {"x = exp('1')\n", "exp requires a finite scalar"},
    {"x = log(0)\n", "log requires a finite positive scalar"},
    {"x = log(-2)\n", "log requires a finite positive scalar"},
    {"x = log(zeros(2))\n", "log requires a finite positive scalar"},
    {"def exp(x):\n    return x\n", "function name collides with an intrinsic"},
    {"def LOG(x):\n    return x\n", "function name collides with an intrinsic"},
    {"def field_project(a, b, c):\n    return a\n", "function name collides with an intrinsic"},
    {"def Field_Dot(a, b):\n    return a\n", "function name collides with an intrinsic"},
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
}

/* Independent statement of the contract: double sums per touched unit, one rounding. */
static void reference_project(float* v, int n, const char* word, float scale) {
    uint64_t h = UINT64_C(1469598103934665603);
    for (const unsigned char* p = (const unsigned char*)word; *p; p++) {
        h ^= *p;
        h *= UINT64_C(1099511628211);
    }
    double sum[3];
    int unit[3], taps = 0;
    for (int t = 0; t < 3; t++) {
        int k = (int)(h % (uint64_t)n), j = 0;
        while (j < taps && unit[j] != k) j++;
        if (j == taps) { unit[taps] = k; sum[taps++] = v[k]; }
        sum[j] += ((h >> 20) & 1) ? (double)scale : -(double)scale;
        h = h * UINT64_C(6364136223846793005) + 1;
    }
    for (int j = 0; j < taps; j++) v[unit[j]] = (float)sum[j];
}

static uint32_t lcg_state = 12345;
static float draw(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (float)((int)(lcg_state >> 8) % 20001 - 10000) / 4096.0f;
}

static int same_bits(const float* a, const float* b, int n) {
    return !memcmp(a, b, (size_t)n * sizeof(float));
}

static void test_golden(int mode) {
    for (size_t i = 0; i < sizeof(golden) / sizeof(golden[0]); i++) {
        char source[256];
        float expected[96] = {0};
        int len = 0;
        for (int k = 0; k < 3; k++) expected[golden[i].unit[k]] = golden[i].value[k];
        snprintf(source, sizeof(source), "v = field_project(zeros(96), '%s', 1)\n", golden[i].word);
        prepare();
        CHECK(run(mode, source) == 0);
        const float* v = am_get_var_array("v", &len);
        CHECK(v && len == 96 && same_bits(v, expected, 96));
        am_persistent_clear();
    }
}

static void test_projection_contract(int mode) {
    static const int lengths[] = {1, 2, 3, 7, 96};
    static const float scales[] = {1, 0.3f, -1.7f, 0.001f, 12345.678f};
    for (size_t li = 0; li < sizeof(lengths) / sizeof(lengths[0]); li++) {
        int n = lengths[li];
        for (size_t wi = 0; wi < sizeof(words) / sizeof(words[0]); wi++) {
            for (size_t si = 0; si < sizeof(scales) / sizeof(scales[0]); si++) {
                float base[96], expected[96];
                char source[256];
                int len = 0;
                for (int d = 0; d < n; d++) base[d] = expected[d] = draw();
                reference_project(expected, n, words[wi], scales[si]);
                prepare();
                CHECK(am_set_var_array("base", base, n) == 0);
                snprintf(source, sizeof(source), "v = field_project(base, '%s', %.9g)\n", words[wi], (double)scales[si]);
                CHECK(run(mode, source) == 0);
                const float* v = am_get_var_array("v", &len);
                CHECK(v && len == n && same_bits(v, expected, n));
                const float* kept = am_get_var_array("base", &len);
                CHECK(kept && len == n && same_bits(kept, base, n));
                am_persistent_clear();
            }
        }
    }
}

static void test_norm_and_dot(int mode) {
    for (int trial = 0; trial < 40; trial++) {
        int n = 1 + trial * 7 % 96, len = 0;
        float a[96], b[96], expected[96];
        double z = 0, dot = 0;
        for (int d = 0; d < n; d++) {
            a[d] = draw();
            b[d] = draw();
            z += (double)a[d] * a[d];
            dot += (double)a[d] * b[d];
        }
        z = sqrt(z);
        for (int d = 0; d < n; d++) expected[d] = z > 1e-12 ? (float)(a[d] / z) : a[d];
        prepare();
        CHECK(am_set_var_array("a", a, n) == 0 && am_set_var_array("b", b, n) == 0);
        CHECK(run(mode, "u = field_normalize(a)\nd = field_dot(a, b)\n") == 0);
        const float* u = am_get_var_array("u", &len);
        CHECK(u && len == n && same_bits(u, expected, n));
        float got = am_get_var_float("d"), want = (float)dot;
        CHECK(!memcmp(&got, &want, sizeof got));
        am_persistent_clear();
    }
    float tiny[4] = {1e-14f, -1e-14f, 1e-14f, 0}, zero[3] = {0};
    int len = 0;
    prepare();
    CHECK(am_set_var_array("tiny", tiny, 4) == 0 && am_set_var_array("zero", zero, 3) == 0);
    CHECK(run(mode, "t = field_normalize(tiny)\nz = field_normalize(zero)\n") == 0);
    const float* t = am_get_var_array("t", &len);
    CHECK(t && len == 4 && same_bits(t, tiny, 4));
    const float* zz = am_get_var_array("z", &len);
    CHECK(zz && len == 3 && same_bits(zz, zero, 3));
    am_persistent_clear();
}

static void test_exp_log(int mode) {
    /* The trailing inputs are where float-only expf/logf differ from one double evaluation. */
    static const float inputs[] = {-100, -40, -1, 0, 0.5f, 1, 2.5f, 10, 88,
        -50.0004807f, -3.00021005f, 0.750004828f, 7.00004816f, 42.0013504f, 80.0000763f};
    static const float positives[] = {1e-45f, 1e-20f, 0.5f, 1, 2, 10, 1e30f,
        1.05675456e-30f, 0.00100162032f, 0.600014567f, 3.02069426f, 1002.59387f, 1.05304099e+20f};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        char source[128];
        snprintf(source, sizeof(source), "y = exp(%.9g)\n", (double)inputs[i]);
        prepare();
        CHECK(run(mode, source) == 0);
        float got = am_get_var_float("y"), want = (float)exp((double)inputs[i]);
        CHECK(!memcmp(&got, &want, sizeof got));
        am_persistent_clear();
    }
    for (size_t i = 0; i < sizeof(positives) / sizeof(positives[0]); i++) {
        char source[128];
        snprintf(source, sizeof(source), "y = log(%.9g)\n", (double)positives[i]);
        prepare();
        CHECK(run(mode, source) == 0);
        float got = am_get_var_float("y"), want = (float)log((double)positives[i]);
        CHECK(!memcmp(&got, &want, sizeof got));
        am_persistent_clear();
    }
}

static void test_errors(int mode) {
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char source[1024];
        snprintf(source, sizeof(source), "sentinel = 0\n%ssentinel = 1\n", invalid[i].source);
        prepare();
        CHECK(run(mode, source) != 0);
        CHECK(strstr(am_get_error(), invalid[i].error));
        CHECK(am_get_var_float("sentinel") == 0);
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
        if (rc) fprintf(stderr, "field runtime: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) {
        test_golden(mode);
        test_projection_contract(mode);
        test_norm_and_dot(mode);
        test_exp_log(mode);
        test_errors(mode);
    }
    printf("PASS field: %d C/runtime checks\n", checks);
    return 0;
}
