/* Owned NoTorch streams through AML values, resumable hosts and workers. */
#include "ariannamethod.h"
#include "notorch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL sampling line %d: %s (%s)\n", __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program = am_program_open(source); if (!program) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(program, 1)) CHECK(--budget > 0);
        return am_program_close(program);
    }
    void* program = am_compile(source); if (!program) return 1;
    int rc = am_exec_compiled(program); am_free_compiled(program); return rc;
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb"); if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char* source = malloc((size_t)length + 1); if (!source) { fclose(file); return NULL; }
    size_t count = fread(source, 1, (size_t)length, file); fclose(file);
    if (count != (size_t)length) { free(source); return NULL; }
    source[length] = 0; return source;
}

static void equal_maps(const AM_Map* a, const AM_Map* b) {
    CHECK(a && b && a->len == b->len);
    for (int i = 0; i < a->len; i++) {
        float value = -1;
        CHECK(am_map_get(b, a->entries[i].key, &value) == 1);
        CHECK(value == a->entries[i].value);
    }
}

static const char* invalid[] = {
    "rng_new()", "rng_new(1, 2)", "rng_new('seed')", "rng_new(-1)",
    "rng_new(0.5)", "rng_new(16777216)", "rng_new(1e39)",
    "rng_uniform()", "rng_uniform(rng, 1)", "rng_uniform(1)",
    "rng_uniform([1, 2])", "rng_uniform(map_new())", "rng_uniform(list_new())",
    "rng_index(rng)", "rng_index(rng, 3, 1)", "rng_index(rng, 0)",
    "rng_index(rng, -1)", "rng_index(rng, 1.5)", "rng_index(rng, 16777218)",
    "rng_index(rng, 'bound')", "rng_index(rng, 1e39)",
    "rng_categorical(rng, [1], 0)", "rng_categorical(rng, [1], -1)",
    "rng_categorical(rng, [1], 1e39)", "rng_categorical(rng, [1], (1e39 - 1e39))",
    "rng_categorical(rng, [0, 0], 1)", "rng_categorical(rng, [-1, 2], 1)",
    "rng_categorical(rng, [1, 1e39], 1)", "rng_categorical(rng, [1, (1e39 - 1e39)], 1)",
    "rng_categorical(rng, 'weights', 1)", "rng_categorical(rng, [1], 'temperature')",
    "rng_categorical(rng, [1])", "rng_categorical(rng, [1], 1, 0)",
    "categorical_at([1], 1, -0.1)", "categorical_at([1], 1, 1)",
    "categorical_at([1], 1, 1e39)", "categorical_at([1], 1, (1e39 - 1e39))",
    "categorical_at([1], 1, 'draw')", "categorical_at([0, 0], 1, 0)",
    "categorical_at([-1, 2], 1, 0)", "categorical_at([1], 0, 0)",
    "categorical_at([1], 1)", "categorical_at([1], 1, 0, 0)",
};

static const char* malformed[] = {
    "map_set(rng, 'algorithm', 2)", "map_set(rng, 'algorithm', 0.5)",
    "map_delete(rng, 'algorithm')", "map_set(rng, 'extra', 1)",
    "map_set(rng, 'state0', -1)", "map_set(rng, 'state1', 65536)",
    "map_set(rng, 'state2', 0.5)", "map_delete(rng, 'state3')",
};

static void test_errors(int mode) {
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char source[1024];
        snprintf(source, sizeof(source),
                 "rng = rng_new(42)\nbefore = rng\nsentinel = 0\n%s\nsentinel = 1\n", invalid[i]);
        am_persistent_clear(); am_persistent_mode(1);
        if (!run(mode, source)) {
            fprintf(stderr, "accepted invalid sampling fixture %zu in mode %d: %s\n", i, mode, invalid[i]);
            exit(1);
        }
        CHECK(am_get_error()[0]);
        CHECK(am_get_var_float("sentinel") == 0);
        equal_maps(am_get_var_map("rng"), am_get_var_map("before"));
    }
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        char source[1024];
        snprintf(source, sizeof(source), "rng = rng_new(42)\n%s\nbefore = rng\n"
                 "sentinel = 0\nrng_uniform(rng)\nsentinel = 1\n", malformed[i]);
        am_persistent_clear(); am_persistent_mode(1);
        CHECK(run(mode, source) != 0 && am_get_error()[0]);
        CHECK(am_get_var_float("sentinel") == 0);
        equal_maps(am_get_var_map("rng"), am_get_var_map("before"));
    }
    am_persistent_clear();
    printf("PASS sampling invalids mode=%d: %zu argument/state cases preserve streams\n",
           mode, sizeof(invalid) / sizeof(invalid[0]) + sizeof(malformed) / sizeof(malformed[0]));
}

static void test_resume(void) {
    static const uint32_t vector[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u};
    am_persistent_clear(); am_persistent_mode(1);
    CHECK(am_exec("rng = rng_new(42)\nfirst = rng_uniform(rng)\n") == 0);
    CHECK(am_get_var_float("first") == (float)(vector[0] >> 8) / 16777216.0f);
    AM_Map* saved = am_map_clone(am_get_var_map("rng")); CHECK(saved != NULL);
    CHECK(am_exec("next = rng_uniform(rng)\n") == 0);
    CHECK(am_get_var_float("next") == (float)(vector[1] >> 8) / 16777216.0f);
    CHECK(am_exec("next = rng_uniform(rng)\n") == 0);
    CHECK(am_get_var_float("next") == (float)(vector[2] >> 8) / 16777216.0f);
    am_persistent_clear();
    CHECK(am_set_var_map("rng", saved) == 0); am_map_free(saved);
    for (int i = 1; i < 4; i++) {
        srand(400 + i); (void)rand(); nt_seed((uint64_t)i);
        nt_tensor* noise = nt_tensor_new(16); CHECK(noise != NULL);
        nt_tensor_rand(noise, 1); nt_tensor_free(noise);
        CHECK(am_exec("noise = randn(7, 1)\nnext = rng_uniform(rng)\n") == 0);
        CHECK(am_get_var_float("next") == (float)(vector[i] >> 8) / 16777216.0f);
    }
    am_persistent_clear();
    puts("PASS sampling resume: host map snapshot, repeated exec and unrelated random streams");
}

static void test_workers(void) {
#ifndef AM_ASYNC_DISABLED
    const char* source =
        "rng = rng_new(42)\nCHANNEL CREATE rng_gate 2\nCHANNEL CREATE rng_values 4\n"
        "SPAWN rng_first:\n    CHANNEL READ rng_gate ready\n"
        "    CHANNEL WRITE rng_values rng_uniform(rng)\n"
        "    CHANNEL WRITE rng_values rng_uniform(rng)\n"
        "parent_first = rng_uniform(rng)\n"
        "SPAWN rng_second:\n    CHANNEL READ rng_gate ready\n"
        "    CHANNEL WRITE rng_values rng_uniform(rng)\n"
        "parent_second = rng_uniform(rng)\n"
        "CHANNEL WRITE rng_gate 1\nCHANNEL WRITE rng_gate 1\nAWAIT\n"
        "parent_third = rng_uniform(rng)\n";
    const float first = (float)(0xa15c02b7u >> 8) / 16777216.0f;
    const float second = (float)(0x7b47f409u >> 8) / 16777216.0f;
    const float third = (float)(0xba1d3330u >> 8) / 16777216.0f;
    for (int mode = 0; mode < 3; mode++) {
        am_init(); am_persistent_mode(1);
        CHECK(run(mode, source) == 0);
        CHECK(am_get_var_float("parent_first") == first);
        CHECK(am_get_var_float("parent_second") == second);
        CHECK(am_get_var_float("parent_third") == third);
        CHECK(am_spawn_count() == 0 && am_channel_depth("rng_values") == 3);
        int first_count = 0, second_count = 0;
        for (int i = 0; i < 3; i++) {
            float value = -1; CHECK(am_channel_try_read("rng_values", &value) == 0);
            first_count += value == first; second_count += value == second;
        }
        CHECK(first_count == 1 && second_count == 2);
        am_persistent_clear(); am_channel_close_all();
    }
    puts("PASS sampling workers: independent streams at different launch positions through all runtime modes");
#endif
}

int main(int argc, char** argv) {
    am_use_notorch_sampling(); am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]); char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "sampling: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) test_errors(mode);
    test_resume(); test_workers();
    printf("AML_SAMPLING_RUNTIME_OK %d checks\n", checks);
    return 0;
}
