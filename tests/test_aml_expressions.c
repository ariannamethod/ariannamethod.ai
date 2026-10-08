/* Complete expression boundaries across interpreter, prepared and bytecode AML. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(value) do { \
    checks++; \
    if (!(value)) { \
        fprintf(stderr, "FAIL expressions line %d: %s; AML: %s\n", \
                __LINE__, #value, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* valid =
    "def identity(value):\n"
    "    return value\n"
    "def bump(array):\n"
    "    array[0] = array[0] + 1\n"
    "    return array[0]\n"
    "cursor = 1\n"
    "numerator = cursor * 5 + 3\n"
    "remainder = numerator - floor(numerator / 16) * 16\n"
    "value = remainder / 16 # explicit remainder\n"
    "assert(value == 0.5, 'remainder')\n"
    "assert(abs(-3) + min(8, 4) * max(2, 1) == 11, 'scalar calls')\n"
    "assert(sqrt(9) == 3 and clamp(4, 0, 2) == 2, 'scalar nesting')\n"
    "a = [7, 8]\n"
    "assert(a[(1 - 1)] + a[1] == 15, 'indices')\n"
    "a[identity(0)] = (2 + 3) # writable index\n"
    "assert(a[0] == 5, 'index write')\n"
    "sized = zeros(text_len('],#=(')) # quoted punctuation\n"
    "assert(len(sized) == 5, 'quoted array argument')\n"
    "nested = zeros(identity(2) + max(1, 2))\n"
    "assert(len(nested) == 4, 'array scalar expression')\n"
    "scaled = scale(a, identity(2))\n"
    "fast = add(a, a) # same bytecode array boundary\n"
    "assert(sum(scaled) == 26 and sum(fast) == 26, 'array calls')\n"
    "j = 0\n"
    "if text_len('#:') == 2: # colon before a comment\n"
    "    j = 1\n"
    "while j < 3: # keep the same boundary each iteration\n"
    "    j = j + 1\n"
    "assert(j == 3, 'control comments')\n"
    "TENSION (0.25 + 0.5) # whole-expression command argument\n"
    "assert(TENSION == 0.75, 'field argument')\n"
    "TAPE ADAMW 0.01 0.1 0.9 0.95 # legacy separate arguments\n"
    "TAPE LR_COSINE 0.01 2 8 0.001\n"
    "TAPE APPLY_ACCUM (1 + 1) # whole argument, including bytecode\n"
    "TAPE CLIP_GRADS (0.5 + 0.5) # whole argument\n"
    "CHANNEL CREATE expression_gate 1\n"
    "CHANNEL WRITE expression_gate (3 + 4) # command name precedes expression\n"
    "CHANNEL READ expression_gate message\n"
    "assert(message == 7, 'channel expression')\n"
    "counter = [0]\n"
    "total = bump(counter) + bump(counter)\n"
    "assert(total == 3 and counter[0] == 2, 'one evaluation per call')\n"
    "legacy = undefined_name + unknown_scalar(3)\n"
    "assert(legacy == 0, 'legacy undefined values')\n"
    "PRINT value\n"
    "PRINT 'BOUNDARIES_OK'\n";

static const char* invalid[] = {
    "value = ((cursor * 5 + 3) % 16) / 16\n",
    "value = 8 % 3\n",
    "value = 8 // 3\n",
    "value = 2 ** 3\n",
    "value = 2 ^ 3\n",
    "value = 1 && 1\n",
    "value = 1 || 1\n",
    "value = 1 2\n",
    "value = 1 trailing\n",
    "value = (1 + 2\n",
    "value = 1 + 2)\n",
    "value = (1 + 2] / 3\n",
    "value = abs(1\n",
    "value = abs(1,)\n",
    "value = min(1 2)\n",
    "value = abs(1) junk\n",
    "value = floor(1 % 2)\n",
    "value = floor((1 + 2)\n",
    "value = floor(1) 2\n",
    "value = identity(1 % 2)\n",
    "value = identity((1 + 2)\n",
    "value = identity(1) 2\n",
    "value = a[0\n",
    "value = a[0)\n",
    "value = a[0 1]\n",
    "value = a[0 % 2]\n",
    "a[0 = 1\n",
    "a[0 % 2] = 1\n",
    "a[0] = 1 garbage\n",
    "a[0] = (1 + 2\n",
    "value = zeros(2) garbage\n",
    "value = zeros(1 % 2)\n",
    "value = zeros(2\n",
    "value = zeros(,2)\n",
    "value = zeros(2,)\n",
    "value = add(a,,a)\n",
    "value = add(a,a) garbage\n",
    "value = [1, 2] garbage\n",
    "if cursor % 2:\n    TENSION 0.1\n",
    "if (cursor > 0: # missing close\n    TENSION 0.1\n",
    "while cursor % 2:\n    TENSION 0.1\n",
    "TENSION (0.1 + 0.2\n",
    "TENSION 0.1 garbage\n",
    "TAPE APPLY_ACCUM 1 % 2\n",
    "TAPE CLIP_GRADS 1 % 2\n",
    "def broken():\n    return 1 % 2\nvalue = broken()\n",
    "assert(1 % 2, 'invalid argument')\n",
    "PRINT 1 % 2\n",
};

static void bad_program(size_t which, char* out, size_t size) {
    snprintf(out, size,
             "def identity(x):\n    return x\n"
             "cursor = 1\nvalue = 17\na = [7, 8]\nsentinel = 0\nTENSION 0.75\n"
             "%ssentinel = 1\nPRINT 'WRONG_CONTINUATION'\n", invalid[which]);
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
    if (argc == 2 && !strcmp(argv[1], "--expected")) { puts("0.5\nBOUNDARIES_OK"); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(invalid) / sizeof(invalid[0])); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int which = atoi(argv[2]);
        if (which < 0 || (size_t)which >= sizeof(invalid) / sizeof(invalid[0])) return 2;
        char source[2048]; bad_program((size_t)which, source, sizeof(source));
        fputs(source, stdout); return 0;
    }
    am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]);
        if (mode < 0 || mode > 2) return 2;
        char* source = read_source(argv[3]);
        if (!source) return 2;
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "expression runtime: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) {
        am_persistent_clear(); am_persistent_mode(1);
        CHECK(run(mode, valid) == 0);
        CHECK(am_get_var_float("value") == 0.5f);
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char source[2048]; bad_program(i, source, sizeof(source));
            am_persistent_clear(); am_persistent_mode(1);
            int rc = run(mode, source);
            if (!rc) fprintf(stderr, "accepted invalid fixture %zu in mode %d: %s", i, mode, invalid[i]);
            CHECK(rc != 0);
            CHECK(am_get_error()[0] != 0);
            CHECK(am_get_var_float("sentinel") == 0);
            CHECK(am_get_var_float("value") == 17);
            CHECK(am_get_state()->tension == 0.75f);
            int len = 0;
            const float* a = am_get_var_array("a", &len);
            CHECK(a && len == 2 && a[0] == 7 && a[1] == 8);
        }
    }
    am_persistent_mode(0);
    printf("AML_EXPRESSIONS_OK %d checks, %zu invalid fixtures\n", checks, sizeof(invalid) / sizeof(invalid[0]));
    return 0;
}
