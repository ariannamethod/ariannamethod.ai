/* Canonical NoTorch values through AML: three hosts, snapshots and isolation. */
#include "ariannamethod.h"
#include "notorch.h"
#include "numerical_invalids.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL numerical runtime line %d: %s (%s)\n", \
                __LINE__, #condition, am_get_error()); exit(1); \
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
        CHECK(am_map_get(b, a->entries[i].key, &value) == 1 && value == a->entries[i].value);
    }
}
static void array_equals(const char* name, const float* want, int count) {
    int n = 0; const float* value = am_get_var_array(name, &n);
    CHECK(value && n == count);
    for (int i = 0; i < n; i++) CHECK(fabsf(value[i] - want[i]) <= 3e-6f * fmaxf(1, fabsf(want[i])));
}
static void test_errors(int mode) {
    for (size_t i = 0; i < sizeof(numerical_invalids) / sizeof(numerical_invalids[0]); i++) {
        am_persistent_clear(); am_persistent_mode(1);
        CHECK(run(mode, "w = [1, 2, 3, 4]\nb = [-0.5, 0.5]\nx = [0.25, -0.75]\n"
                  "rng = rng_new(42)\nbefore = rng\nresult = [99]\nsentinel = 0\n"
                  "nan_array = zeros(2)\nnan_array[0] = 1e39 - 1e39\n") == 0);
        char source[1024];
        snprintf(source, sizeof(source), "%s\nsentinel = 1\n", numerical_invalids[i]);
        if (!run(mode, source)) {
            fprintf(stderr, "accepted invalid numerical mode=%d: %s\n", mode, numerical_invalids[i]); exit(1);
        }
        CHECK(am_get_error()[0]); CHECK(am_get_var_float("sentinel") == 0);
        equal_maps(am_get_var_map("rng"), am_get_var_map("before"));
        const float x[] = {0.25f, -0.75f}, previous[] = {99};
        array_equals("x", x, 2); array_equals("result", previous, 1);
    }
    const char* overflow[] = {
        "nt_linear([3e38], [0], [3e38], 1, 1)",
        "nt_linear_vjp([3e38], [3e38], [3e38], 1, 1)",
        "nt_mse_grad([3e38], [-3e38])", "nt_sgd([3e38], [-3e38], 3e38)"
    };
    for (size_t i = 0; i < sizeof(overflow) / sizeof(overflow[0]); i++) {
        char source[256]; snprintf(source, sizeof(source), "result = %s\n", overflow[i]);
        CHECK(run(mode, source) != 0);
        CHECK(strstr(am_get_error(), "numerical backend"));
        const float previous[] = {99}; array_equals("result", previous, 1);
    }
    am_persistent_clear();
    printf("PASS numerical invalids mode=%d: %zu argument/domain cases and %zu overflows preserve state\n",
           mode, sizeof(numerical_invalids) / sizeof(numerical_invalids[0]), sizeof(overflow) / sizeof(overflow[0]));
}
static void test_resume(void) {
    am_init(); am_persistent_mode(1);
    CHECK(am_exec("p = [1, -2]\ng = [0.5, -0.25]\np = nt_sgd(p, g, 0.125)\n"
                  "rng = rng_new(42)\nfirst = rng_normal(rng, 3)\n") == 0);
    int n; const float* current = am_get_var_array("p", &n); CHECK(current && n == 2);
    float saved_p[] = {current[0], current[1]};
    AM_Map* saved_rng = am_map_clone(am_get_var_map("rng")); CHECK(saved_rng);
    CHECK(am_exec("p = nt_sgd(p, g, 0.125)\nnext = rng_normal(rng, 4)\n") == 0);
    current = am_get_var_array("p", &n); float expected_p[] = {current[0], current[1]};
    current = am_get_var_array("next", &n); CHECK(current && n == 4);
    float expected_next[4]; memcpy(expected_next, current, sizeof(expected_next));
    am_persistent_clear();
    CHECK(am_set_var_array("p", saved_p, 2) == 0);
    CHECK(am_set_var_map("rng", saved_rng) == 0); am_map_free(saved_rng);
    saved_p[0] = 999; /* Host injection owns a copy. */
    srand(9); nt_seed(71); nt_tensor* noise = nt_tensor_new(11); CHECK(noise);
    nt_tensor_rand(noise, 1); nt_tensor_free(noise);
    CHECK(am_exec("noise = randn(5, 1)\ng = [0.5, -0.25]\n"
                  "p = nt_sgd(p, g, 0.125)\nnext = rng_normal(rng, 4)\n") == 0);
    array_equals("p", expected_p, 2); array_equals("next", expected_next, 4);
    am_persistent_clear();
    puts("PASS numerical resume: learner arrays and owned normal stream restore independently");
}
static void test_matrix_bounds(void) {
    am_init(); am_persistent_mode(1);
    const float old[] = {1, 2, 3, 4}, single = 7;
    CHECK(am_set_var_matrix("shape", old, 2, 2) == 0);
    CHECK(am_set_var_matrix("shape", &single, 65536, 65537) != 0);
    CHECK(am_set_var_matrix("shape", &single, INT_MAX, INT_MAX) != 0);
    CHECK(am_set_var_matrix("shape", &single, 1, AM_MAX_ARRAY_SIZE + 1) != 0);
    array_equals("shape", old, 4);
    for (int mode = 0; mode < 3; mode++) {
        CHECK(run(mode, "activated = nt_tanh(shape)\nback = nt_tanh_vjp(activated, shape)\n"
                        "updated = nt_sgd(shape, shape, 0.125)\n"
                        "assert(rows(activated) == 2 and cols(activated) == 2, 'tanh shape')\n"
                        "assert(rows(back) == 2 and cols(back) == 2, 'vjp shape')\n"
                        "assert(rows(updated) == 2 and cols(updated) == 2, 'sgd shape')\n"
                        "flat = nt_mse_grad(shape, shape)\n"
                        "assert(rows(flat) == 0 and cols(flat) == 0, 'packed shape')\n") == 0);
        CHECK(run(mode, "oversized = matrix_zeros(65536, 65537)\n") == 0);
        int n = -1; CHECK(am_get_var_array("oversized", &n) == NULL);
    }
    float* large = calloc(AM_MAX_ARRAY_SIZE, sizeof(float)); CHECK(large);
    large[0] = 3; large[AM_MAX_ARRAY_SIZE - 1] = -4;
    CHECK(am_set_var_matrix("shape", large, 1, AM_MAX_ARRAY_SIZE) == 0);
    CHECK(am_set_var_matrix("shape", large, AM_MAX_ARRAY_SIZE, 1) == 0);
    free(large);
    int n; const float* value = am_get_var_array("shape", &n);
    CHECK(value && n == AM_MAX_ARRAY_SIZE && value[0] == 3 && value[n - 1] == -4);
    am_persistent_clear();
    puts("PASS matrix bounds: overflow dimensions reject before copying; both exact-cap shapes survive");
}
static void test_tapes(void) {
    for (int mode = 0; mode < 3; mode++) {
        am_init(); am_persistent_mode(1);
        nt_tape_start();
        nt_tensor* np = nt_tensor_new(2); CHECK(np); np->data[0] = 0.25f; np->data[1] = -0.75f;
        int ni = nt_tape_param(np), ny = nt_scale(ni, 3); CHECK(ni >= 0 && ny >= 0);
        am_tape_start();
        AM_Array* ap = am_array_new(2); AM_Array* ay = am_array_new(2); CHECK(ap && ay);
        ap->data[0] = 2; ap->data[1] = -1; ay->data[0] = 4; ay->data[1] = -2;
        int ai = am_tape_record_param(ap), al = am_tape_record(ay, AM_OP_SCALE, ai, -1, 2);
        CHECK(ai >= 0 && al >= 0);
        nt_tape* nt = nt_tape_get(); AM_Tape* at = am_tape_get();
        int nc = nt->count, ac = at->count, nparams = nt->n_params, aparams = at->n_params;
        nt_tape_entry ne[2]; AM_TapeEntry ae[2];
        memcpy(ne, nt->entries, sizeof(ne)); memcpy(ae, at->entries, sizeof(ae));
        CHECK(run(mode, "w = [1, 2, 3, 4]\nb = [0.1, -0.2]\nx = [0.25, -0.5]\n"
            "y = nt_linear(w, b, x, 2, 2)\nh = nt_tanh(y)\n"
            "loss = nt_mse_grad(h, b)\ndy = zeros(2)\ndy[0] = loss[1]\ndy[1] = loss[2]\n"
            "dz = nt_tanh_vjp(h, dy)\ng = nt_linear_vjp(w, x, dz, 2, 2)\n"
            "step = nt_sgd(x, b, 0.1)\nrng = rng_new(42)\nnoise = rng_normal(rng, 3)\n") == 0);
        CHECK(nt->count == nc && nt->active && nt->n_params == nparams);
        CHECK(at->count == ac && at->active && at->n_params == aparams);
        CHECK(memcmp(ne, nt->entries, sizeof(ne)) == 0 && memcmp(ae, at->entries, sizeof(ae)) == 0);
        CHECK(np->data[0] == 0.25f && np->data[1] == -0.75f && ap->data[0] == 2 && ap->data[1] == -1);
        nt_tape_backward(ny); am_tape_backward(al);
        CHECK(nt->entries[ni].grad && at->entries[ai].grad);
        CHECK(nt->entries[ni].grad->data[0] == 3 && nt->entries[ni].grad->data[1] == 3);
        CHECK(at->entries[ai].grad->data[0] == 2 && at->entries[ai].grad->data[1] == 2);
        am_array_free(ap); am_array_free(ay); am_tape_clear(); nt_tape_clear(); nt_tensor_free(np);
        am_persistent_clear();
    }
    nt_tape_destroy();
    puts("PASS numerical isolation: both live legacy tapes retain graph, parameters and backward results");
}
static void test_workers(void) {
#ifndef AM_ASYNC_DISABLED
    const char* source =
        "p = [1, -2]\ng = [0.5, -0.25]\nrng = rng_new(42)\n"
        "CHANNEL CREATE learner_gate 2\nCHANNEL CREATE learner_values 8\n"
        "SPAWN learner_first:\n    CHANNEL READ learner_gate ready\n"
        "    result = nt_sgd(p, g, 0.125)\n    p[0] = 99\n"
        "    noise = rng_normal(rng, 1)\n    CHANNEL WRITE learner_values result[0]\n"
        "    CHANNEL WRITE learner_values noise[0]\n"
        "SPAWN learner_second:\n    CHANNEL READ learner_gate ready\n"
        "    result = nt_sgd(p, g, 0.5)\n    p[0] = 199\n"
        "    noise = rng_normal(rng, 1)\n    CHANNEL WRITE learner_values result[0]\n"
        "    CHANNEL WRITE learner_values noise[0]\n"
        "p[0] = 7\nparent_noise = rng_normal(rng, 1)\n"
        "CHANNEL WRITE learner_gate 1\nCHANNEL WRITE learner_gate 1\nAWAIT\n"
        "assert(p[0] == 7, 'worker learner arrays remain private')\n";
    for (int mode = 0; mode < 3; mode++) {
        am_init(); am_persistent_mode(1); CHECK(run(mode, source) == 0);
        int n; const float* noise = am_get_var_array("parent_noise", &n); CHECK(noise && n == 1);
        int first = 0, second = 0, normal_count = 0;
        CHECK(am_spawn_count() == 0 && am_channel_depth("learner_values") == 4);
        for (int i = 0; i < 4; i++) {
            float value; CHECK(am_channel_try_read("learner_values", &value) == 0);
            first += value == 0.9375f; second += value == 0.75f; normal_count += value == noise[0];
        }
        CHECK(first == 1 && second == 1 && normal_count == 2);
        am_persistent_clear(); am_channel_close_all();
    }
    puts("PASS numerical workers: independent learner arrays and normal streams in three hosts");
#endif
}
int main(int argc, char** argv) {
    am_use_notorch(); am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]); char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source); if (rc) fprintf(stderr, "numerical: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) test_errors(mode);
    test_resume(); test_matrix_bounds(); test_tapes(); test_workers();
    am_persistent_mode(0);
    printf("AML_NUMERICAL_RUNTIME_OK %d checks\n", checks); return 0;
}
