/* Scalar math values use independent decimal reference constants, never AML math
 * on the expected side. Exercise interpreter, resumable and bytecode hosts;
 * the shell harness repeats the same programs through runner and amlc. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(value) do { \
    checks++; \
    if (!(value)) { \
        fprintf(stderr, "FAIL scalar math line %d: %s; AML: %s\n", \
                __LINE__, #value, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* valid =
    "def identity(x):\n    return x\n"
    "def touch(a):\n    a[0] = a[0] + 1\n    return 0\n"
    "assert(exp(0) == 1, 'exp zero')\n"
    "assert(abs(exp(1) - 2.718281828459045) < 0.000001, 'exp positive')\n"
    "assert(abs(exp(-1) - 0.3678794411714423) < 0.0000001, 'exp negative')\n"
    "assert(abs(log(2) - 0.6931471805599453) < 0.0000001, 'log two')\n"
    "assert(abs(log(0.5) + 0.6931471805599453) < 0.0000001, 'log fraction')\n"
    "assert(log(1) == 0, 'log one')\n"
    "assert(abs(tanh(0.3) - 0.2913126124515909) < 0.0000001, 'tanh positive')\n"
    "assert(abs(tanh(-0.3) + 0.2913126124515909) < 0.0000001, 'tanh negative')\n"
    "assert(tanh(0) == 0 and tanh(100) == 1 and tanh(-100) == -1, 'tanh endpoints')\n"
    "assert(text_equal(text_fixed(tanh(-0), 1), '-0.0'), 'tanh signed zero')\n"
    "assert(abs(log(1 + exp(0)) - 0.6931471805599453) < 0.0000001, 'parliament softplus zero')\n"
    "assert(abs(log(1 + exp(10)) - 10.000045398899218) < 0.000002, 'parliament positive credit')\n"
    "assert(abs(log(1 + exp(-10)) - 0.00004539889921686465) < 0.00000003, 'parliament negative credit')\n"
    "assert(exp(-1000) == 0, 'representable underflow')\n"
    "assert(abs(exp(80) / 5.54062238439351e34 - 1) < 0.000001, 'finite large exp')\n"
    "assert(abs(log(1e-30) + 69.07755278982137) < 0.00001, 'small positive log')\n"
    "assert(EXP(identity(0)) == 1 and LOG(1) == 0 and TANH(0) == 0, 'case and typed argument')\n"
    "a = [0]\n"
    "result = exp(touch(a))\n"
    "assert(result == 1 and a[0] == 1, 'argument evaluated once')\n"
    "exp(0)\nlog(1)\ntanh(0)\n"
    "abs(1)\nmin(1, 2)\nmax(1, 2)\nsqrt(9)\nclamp(1, 0, 2)\n"
    "abs (1) # standalone legacy scalar\nmin (1, 2)\nmax (1, 2)\nsqrt (9)\nclamp (1, 0, 2)\n"
    "PRINT 'SCALAR_MATH_OK'\n";

struct Invalid { const char* source; const char* error; int preparation; };
static const struct Invalid invalid[] = {
    {"value = exp()\n", "exactly one argument", 0},
    {"value = exp(0, 1)\n", "exactly one argument", 0},
    {"value = log()\n", "exactly one argument", 0},
    {"value = log(1, 2)\n", "exactly one argument", 0},
    {"value = tanh()\n", "exactly one argument", 0},
    {"value = tanh(0, 1)\n", "exactly one argument", 0},
    {"value = exp('0')\n", "finite scalar", 0},
    {"value = log(a)\n", "finite scalar", 0},
    {"value = tanh(list_new())\n", "finite scalar", 0},
    {"value = exp(map_new())\n", "finite scalar", 0},
    {"value = log(record_new())\n", "finite scalar", 0},
    {"value = tanh(model_try_load('missing.gguf'))\n", "finite scalar", 0},
    {"value = exp(1e39)\n", "finite scalar", 0},
    {"value = log(-1e39)\n", "finite scalar", 0},
    {"value = tanh(1e39 - 1e39)\n", "finite scalar", 0},
    {"value = log(0)\n", "positive scalar", 0},
    {"value = log(-1)\n", "positive scalar", 0},
    {"value = exp(100)\n", "nonfinite result", 0},
    {"value = unknown_scalar(3)\n", "unknown function", 0},
    {"value = 1 + unknown_scalar(3)\n", "unknown function", 0},
    {"value = exp(unknown_scalar(3))\n", "unknown function", 0},
    {"unknown_scalar(3)\n", "unknown function", 0},
    {"unknown_scalar (3) # call, not a flat command\n", "unknown function", 0},
    {"if unknown_scalar(3):\n    TENSION 0.1\n", "unknown function", 0},
    {"while unknown_scalar(3):\n    TENSION 0.1\n", "unknown function", 0},
    {"def missing():\n    return unknown_scalar(3)\nvalue = missing()\n", "unknown function", 0},
    {"PRINT unknown_scalar(3)\n", "unknown function", 0},
    {"TENSION unknown_scalar(3)\n", "unknown function", 0},
    {"a[0] = unknown_scalar(3)\n", "unknown function", 0},
    {"value = zeros(-1)\n", "array function could not produce a result", 0},
    {"value = scale(missing_array, 1)\n", "array function could not produce a result", 0},
    {"value = seq_cross_entropy(a, a, 2, 4)\n", "array function could not produce a result", 0},
    {"value = add(a, scale(a, -1))\n", "array function could not produce a result", 0},
    {"value = add(a, missing_array)\n", "array function could not produce a result", 0},
    {"value = mul(a, missing_array)\n", "array function could not produce a result", 0},
    {"value = silu(missing_array)\n", "array function could not produce a result", 0},
    {"def exp(x):\n    return x\n", "collides with an intrinsic", 1},
    {"def LOG(x):\n    return x\n", "collides with an intrinsic", 1},
    {"def tanh(x):\n    return x\n", "collides with an intrinsic", 1},
};

static void bad_program(size_t i, char* out, size_t size) {
    snprintf(out, size, "value = 17\na = [7, 8]\nsentinel = 0\nTENSION 0.75\n"
             "%ssentinel = 1\nPRINT 'WRONG_CONTINUATION'\n", invalid[i].source);
}

static int run(int mode, const char* script) {
    if (mode == 0) return am_exec(script);
    if (mode == 1) {
        void* handle = am_program_open(script);
        if (!handle) return 1;
        int remaining = AML_MAX_LINES + 1;
        while (!am_program_step(handle, 1)) CHECK(--remaining > 0);
        return am_program_close(handle);
    }
    void* handle = am_compile(script);
    if (!handle) return 1;
    int rc = am_exec_compiled(handle);
    am_free_compiled(handle);
    return rc;
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size < 0 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file); return NULL;
    }
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
    if (argc == 2 && !strcmp(argv[1], "--fixture")) { fputs(valid, stdout); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--expected")) { puts("SCALAR_MATH_OK"); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(invalid) / sizeof(invalid[0])); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int i = atoi(argv[2]);
        if (i < 0 || (size_t)i >= sizeof(invalid) / sizeof(invalid[0])) return 2;
        char source[2048]; bad_program((size_t)i, source, sizeof(source));
        fputs(source, stdout); return 0;
    }
    am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]);
        if (mode < 0 || mode > 2) return 2;
        char* source = read_source(argv[3]);
        if (!source) return 2;
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "scalar math runtime: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) {
        am_persistent_clear(); am_persistent_mode(1);
        CHECK(run(mode, valid) == 0);
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char source[2048]; bad_program(i, source, sizeof(source));
            am_persistent_clear(); am_persistent_mode(1);
            int rc = run(mode, source);
            if (!rc) fprintf(stderr, "accepted invalid fixture %zu in mode %d: %s", i, mode, invalid[i].source);
            CHECK(rc != 0);
            if (!strstr(am_get_error(), invalid[i].error))
                fprintf(stderr, "wrong error for fixture %zu in mode %d\n", i, mode);
            CHECK(strstr(am_get_error(), invalid[i].error));
            CHECK(am_get_var_float("sentinel") == 0);
            CHECK(am_get_var_float("value") == (invalid[i].preparation ? 0 : 17));
            CHECK(am_get_state()->tension == 0.75f);
            if (!invalid[i].preparation) {
                int len = 0;
                const float* a = am_get_var_array("a", &len);
                CHECK(a && len == 2 && a[0] == 7 && a[1] == 8);
            }
        }
    }
    am_persistent_mode(0);
    printf("AML_SCALAR_MATH_OK %d checks, %zu invalid fixtures\n", checks, sizeof(invalid) / sizeof(invalid[0]));
    return 0;
}
