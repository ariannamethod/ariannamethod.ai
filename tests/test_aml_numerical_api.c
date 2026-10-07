/* Backend table lifetime, typed dispatch, output ownership and failed publication. */
#include "ariannamethod.h"
#include "numerical_invalids.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, calls, bad_result, last_op;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL numerical API line %d: %s (%s)\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

#ifdef AML_NUMERICAL_ALLOC_WRAP
static int arm_budget = -1, allocation_budget = -1, tracked_count;
static void* tracked[64];
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void __real_free(void*);
static void* track(void* p) {
    if (p) {
        if (tracked_count == 64) abort();
        tracked[tracked_count++] = p;
    }
    return p;
}
void* __wrap_malloc(size_t n) {
    if (allocation_budget < 0) return __real_malloc(n);
    if (allocation_budget == 0) return NULL;
    --allocation_budget; return track(__real_malloc(n));
}
void* __wrap_calloc(size_t n, size_t size) {
    if (allocation_budget < 0) return __real_calloc(n, size);
    if (allocation_budget == 0) return NULL;
    --allocation_budget; return track(__real_calloc(n, size));
}
void __wrap_free(void* p) {
    for (int i = 0; p && i < tracked_count; i++) {
        if (tracked[i] == p) { tracked[i] = tracked[--tracked_count]; break; }
    }
    __real_free(p);
}
#endif

static void seed(uint64_t* state, uint64_t value) { *state = value + 100; }
static uint32_t u32(uint64_t* state) { *state += 1; return 0; }
static float uniform(uint64_t* state) {
    u32(state);
#ifdef AML_NUMERICAL_ALLOC_WRAP
    allocation_budget = arm_budget;
#endif
    return 0.125f;
}
static int index_of(uint64_t* state, uint32_t n, uint32_t* out) {
    u32(state); *out = n - 1;
#ifdef AML_NUMERICAL_ALLOC_WRAP
    allocation_budget = arm_budget;
#endif
    return 0;
}
static int at(const float* w, int n, float t, double d, int* out) {
    (void)w; (void)n; (void)t; (void)d; *out = 0; return 0;
}
static int categorical(uint64_t* state, const float* w, int n, float t, int* out) {
    u32(state); return at(w, n, t, 0, out);
}
static AM_SamplingBackend sampling = { seed, u32, uniform, index_of, at, categorical };

static int fill(int op, int n, float* out) {
#ifdef AML_NUMERICAL_ALLOC_WRAP
    allocation_budget = -1; /* Both result allocations have already succeeded. */
#endif
    calls++; last_op = op;
    for (int i = 0; i < n; i++) out[i] = (float)(op * 10 + i);
    if (bad_result == 2) out[n - 1] = NAN;
    if (bad_result == 3) out[n - 1] = INFINITY;
    return bad_result == 1 ? -1 : 0;
}
static int linear(const float* w, const float* b, const float* x, int r, int c, float* out) {
    CHECK(r == 2 && c == 2 && w[0] == 1 && w[3] == 4 && b[1] == 0.5f && x[0] == 0.25f);
    CHECK(out != w && out != b && out != x); return fill(1, r, out);
}
static int linear_vjp(const float* w, const float* x, const float* dy, int r, int c, float* out) {
    CHECK(r == 2 && c == 2 && w[2] == 3 && x[1] == -0.75f && dy[1] == 0.5f);
    CHECK(out != w && out != x && out != dy); return fill(2, r * c + r + c, out);
}
static int tanh_value(const float* x, int n, float* out) {
    CHECK(n == 2 && x[0] == 0.25f && x[1] == -0.75f && out != x); return fill(3, n, out);
}
static int tanh_vjp(const float* y, const float* dy, int n, float* out) {
    CHECK(n == 2 && y[0] == 0.25f && dy[1] == 0.5f && out != y && out != dy); return fill(4, n, out);
}
static int mse_grad(const float* p, const float* t, int n, float* out) {
    CHECK(n == 2 && p[0] == 0.25f && t[1] == 0.5f && out != p && out != t); return fill(5, n + 1, out);
}
static int sgd(const float* p, const float* g, int n, float lr, float* out) {
    CHECK(n == 2 && p[0] == 0.25f && g[1] == 0.5f && lr == 0.125f);
    CHECK(out != p && out != g); return fill(6, n, out);
}
static int normal(uint64_t* state, int n, float* out) {
    CHECK(n == 3); *state += 6; return fill(7, n, out);
}
static AM_NumericalBackend backend(void) {
    AM_NumericalBackend result = {linear, linear_vjp, tanh_value, tanh_vjp, mse_grad, sgd, normal};
    return result;
}

static uint64_t state_of(const char* name) {
    const AM_Map* map = am_get_var_map(name); CHECK(map && map->len == 5);
    uint64_t result = 0;
    for (int i = 0; i < 4; i++) {
        char key_name[] = "state0"; key_name[5] += i;
        AM_String* key = am_string_new(key_name); float value = -1;
        CHECK(key && am_map_get(map, key, &value) == 1);
        result |= (uint64_t)value << (16 * i); am_string_free(key);
    }
    return result;
}
static void setup(void) {
    am_init(); am_persistent_mode(1);
    AM_NumericalBackend table = backend();
    am_set_numerical_backend(&table); memset(&table, 0, sizeof(table));
    am_set_sampling_backend(&sampling);
    calls = bad_result = last_op = 0;
    CHECK(am_exec("w = [1, 2, 3, 4]\nb = [-0.5, 0.5]\nx = [0.25, -0.75]\n"
                  "rng = rng_new(42)\nresult = [99]\n"
                  "nan_array = zeros(2)\nnan_array[0] = 1e39 - 1e39\n") == 0);
}
static const char* operations[] = {
    "nt_linear(w, b, x, 2, 2)", "nt_linear_vjp(w, x, b, 2, 2)",
    "nt_tanh(x)", "nt_tanh_vjp(x, b)", "nt_mse_grad(x, b)",
    "nt_sgd(x, b, 0.125)", "rng_normal(rng, 3)"
};
static void test_lifetime(void) {
    setup(); am_set_numerical_backend(NULL);
    for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
        CHECK(am_exec(operations[i]) != 0);
        CHECK(strstr(am_get_error(), "numerical backend unavailable"));
    }
    AM_NumericalBackend partial = backend(); partial.normal = NULL;
    am_set_numerical_backend(&partial);
    CHECK(am_exec("nt_tanh(x)") != 0);
    CHECK(strstr(am_get_error(), "numerical backend unavailable"));
    setup(); am_init(); am_persistent_mode(1);
    CHECK(am_exec("x = [0.25, -0.75]\ny = nt_tanh(x)\n") == 0);
    CHECK(calls == 1 && last_op == 3);
    am_set_numerical_backend(NULL);
    CHECK(am_exec("nt_tanh(x)") != 0);
    /* isfinite belongs to the standalone language and needs no backend. */
    CHECK(am_exec("assert(isfinite(1) == 1, 'finite')\nassert(isfinite(1e39) == 0, 'inf')\n") == 0);
}
static void test_dispatch(void) {
    setup();
    const int lengths[] = {2, 8, 2, 2, 3, 2, 3};
    for (int i = 0; i < 7; i++) {
        char source[256]; snprintf(source, sizeof(source), "result = %s\n", operations[i]);
        CHECK(am_exec(source) == 0); CHECK(last_op == i + 1);
        int n = 0; const float* result = am_get_var_array("result", &n);
        CHECK(result && n == lengths[i]);
        for (int j = 0; j < n; j++) CHECK(result[j] == (i + 1) * 10 + j);
        CHECK(am_exec("result[0] = -99\nassert(x[0] == 0.25, 'input survives result edit')\n") == 0);
    }
    CHECK(calls == 7 && state_of("rng") == 148);
    CHECK(am_exec("def transform(value):\n    return nt_tanh(value)\n"
                  "fresh = transform(x)\nfresh[0] = 99\n"
                  "assert(x[0] == 0.25, 'function result is fresh')\n"
                  "NT_TANH(x)\n") == 0);
    CHECK(calls == 9);
}
static void test_validation(void) {
    setup();
    for (size_t i = 0; i < sizeof(numerical_invalids) / sizeof(numerical_invalids[0]); i++) {
        if (!am_exec(numerical_invalids[i])) {
            fprintf(stderr, "accepted invalid numerical API: %s\n", numerical_invalids[i]); exit(1);
        }
        CHECK(am_get_error()[0]); CHECK(calls == 0); CHECK(state_of("rng") == 142);
    }
    const char* corrupt[] = {"map_set(rng, 'algorithm', 2)", "map_set(rng, 'state0', -1)",
                            "map_delete(rng, 'state1')", "map_set(rng, 'extra', 0)"};
    for (size_t i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); i++) {
        setup(); CHECK(am_exec(corrupt[i]) == 0);
        CHECK(am_exec("rng_normal(rng, 3)") != 0 && calls == 0);
    }
}
static void test_failed_results(void) {
    for (int failure = 1; failure <= 3; failure++) {
        for (int i = 0; i < 7; i++) {
            setup(); bad_result = failure;
            char source[256]; snprintf(source, sizeof(source), "result = %s\n", operations[i]);
            CHECK(am_exec(source) != 0);
            CHECK(strstr(am_get_error(), "numerical backend"));
            CHECK(calls == 1 && state_of("rng") == 142);
            int n; const float* result = am_get_var_array("result", &n);
            CHECK(result && n == 1 && result[0] == 99);
            bad_result = 0; CHECK(am_exec(source) == 0 && calls == 2);
        }
    }
}
#ifdef AML_NUMERICAL_ALLOC_WRAP
static void test_allocation_failures(void) {
    for (int op = 0; op < 2; op++) {
        for (int budget = 0; budget <= 2; budget++) {
            setup();
            /* The nested scalar callback arms exactly the result's allocation
               window, after parser setup and all array arguments exist. */
            arm_budget = budget;
            int rc = am_exec(op == 0 ? "nt_sgd(x, b, rng_uniform(rng))\n"
                                    : "rng_normal(rng, rng_index(rng, 4))\n");
            allocation_budget = arm_budget = -1;
            CHECK(tracked_count == 0);
            CHECK((rc == 0) == (budget == 2));
            CHECK(calls == (budget == 2));
            CHECK(state_of("rng") == (uint64_t)(budget == 2 ? 143 + (op == 1 ? 6 : 0) : 142));
            if (rc) CHECK(strstr(am_get_error(), "numerical output allocation failed"));
        }
    }
    puts("PASS numerical allocations: both output sites refuse cleanly; RNG commits after success");
}
#endif
int main(void) {
    test_lifetime(); test_dispatch(); test_validation(); test_failed_results();
#ifdef AML_NUMERICAL_ALLOC_WRAP
    test_allocation_failures();
#endif
    am_persistent_mode(0); am_set_numerical_backend(NULL); am_set_sampling_backend(NULL);
    printf("AML_NUMERICAL_API_OK %d checks\n", checks); return 0;
}
