/* Sampling callback lifetime, typed validation, and ordinary map ownership. */
#include "ariannamethod.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, seed_calls, draw_calls, at_calls, bad_result;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL sampling API line %d: %s (%s)\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

#ifdef AML_SAMPLING_ALLOC_WRAP
/* Arm failure from seed(), immediately before the new RNG map allocates. Only
   allocations made in this narrow interval are tracked; parser setup is outside
   this gate, and cleanup may release earlier untracked allocations normally. */
static int seed_allocation_budget = -1, allocation_budget = -1, owned_blocks;
static void* owned[64];
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void __real_free(void*);

static void* track_owned(void* ptr) {
    if (ptr) {
        if (owned_blocks == (int)(sizeof(owned) / sizeof(owned[0]))) abort();
        owned[owned_blocks++] = ptr;
    }
    return ptr;
}

void* __wrap_malloc(size_t bytes) {
    if (allocation_budget < 0) return __real_malloc(bytes);
    if (allocation_budget == 0) return NULL;
    allocation_budget--;
    return track_owned(__real_malloc(bytes));
}

void* __wrap_calloc(size_t count, size_t bytes) {
    if (allocation_budget < 0) return __real_calloc(count, bytes);
    if (allocation_budget == 0) return NULL;
    allocation_budget--;
    return track_owned(__real_calloc(count, bytes));
}

void __wrap_free(void* ptr) {
    if (ptr) {
        for (int i = 0; i < owned_blocks; i++) {
            if (owned[i] == ptr) { owned[i] = owned[--owned_blocks]; break; }
        }
    }
    __real_free(ptr);
}
#endif

/* These callbacks expose dispatch/state behavior, not a second sampler. Real
   PCG32 and categorical arithmetic are exercised against NoTorch separately. */
static void probe_seed(uint64_t* state, uint64_t seed) {
    seed_calls++;
    *state = UINT64_C(0x123456789abc0000) + seed;
#ifdef AML_SAMPLING_ALLOC_WRAP
    allocation_budget = seed_allocation_budget;
#endif
}

static uint32_t probe_u32(uint64_t* state) {
    draw_calls++;
    *state += 1;
    return 0;
}

static float probe_uniform(uint64_t* state) {
    probe_u32(state);
    return bad_result ? NAN : 0.125f;
}

static int probe_index(uint64_t* state, uint32_t bound, uint32_t* out) {
    probe_u32(state);
    *out = bad_result == 2 ? bound : bound - 1;
    return bad_result == 1 ? -1 : 0;
}

static int probe_at(const float* weights, int n, float temperature,
                     double draw, int* out) {
    at_calls++;
    CHECK(n == 3 && weights[0] == 0 && weights[1] == 2 && weights[2] == 0);
    CHECK(temperature == 0.5f && draw == 0.25);
    *out = bad_result == 2 ? n : 1;
    return bad_result == 1 ? -1 : 0;
}

static int probe_categorical(uint64_t* state, const float* weights, int n,
                             float temperature, int* out) {
    probe_u32(state);
    return probe_at(weights, n, temperature, 0.25, out);
}

static AM_SamplingBackend probe_backend(void) {
    AM_SamplingBackend result = {
        probe_seed, probe_u32, probe_uniform, probe_index,
        probe_at, probe_categorical
    };
    return result;
}

static uint64_t state_of(const char* name) {
    const AM_Map* map = am_get_var_map(name);
    CHECK(map && map->len == 5);
    uint64_t state = 0;
    for (int i = 0; i < 4; i++) {
        char text[] = "state0";
        text[5] = (char)('0' + i);
        AM_String* key = am_string_new(text);
        float value = -1;
        CHECK(key && am_map_get(map, key, &value) == 1);
        CHECK(value >= 0 && value <= 65535 && truncf(value) == value);
        state |= (uint64_t)value << (16 * i);
        am_string_free(key);
    }
    return state;
}

static void setup(void) {
    am_init();
    am_persistent_mode(1);
    AM_SamplingBackend backend = probe_backend();
    am_set_sampling_backend(&backend);
    memset(&backend, 0, sizeof(backend)); /* Registration must copy the table. */
    seed_calls = draw_calls = at_calls = bad_result = 0;
    CHECK(am_exec("rng = rng_new(42)\nweights = [0, 2, 0]\n") == 0);
    CHECK(seed_calls == 1 && draw_calls == 0 && at_calls == 0);
}

static void test_backend_lifetime(void) {
    const char* calls[] = {
        "rng_new(1)", "rng_uniform(map_new())", "rng_index(map_new(), 1)",
        "rng_categorical(map_new(), [1], 1)", "categorical_at([1], 1, 0)"
    };
    am_set_sampling_backend(NULL);
    am_init();
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
        CHECK(am_exec(calls[i]) != 0);
        CHECK(strstr(am_get_error(), "sampling backend unavailable") != NULL);
    }
    AM_SamplingBackend partial = probe_backend();
    partial.u32 = NULL;
    am_set_sampling_backend(&partial);
    CHECK(am_exec("rng_new(1)") != 0);
    CHECK(strstr(am_get_error(), "sampling backend unavailable") != NULL);
    setup();
    am_init(); /* Full backend survives am_init. */
    am_persistent_mode(1);
    CHECK(am_exec("rng = rng_new(42)\nx = rng_uniform(rng)\n") == 0);
    CHECK(seed_calls == 2 && draw_calls == 1 && am_get_var_float("x") == 0.125f);
    CHECK(state_of("rng") == UINT64_C(0x123456789abc002b));
    am_set_sampling_backend(NULL);
    CHECK(am_exec("rng_uniform(rng)") != 0);
    CHECK(strstr(am_get_error(), "sampling backend unavailable") != NULL);
}

static void test_dispatch_ownership(void) {
    setup();
    CHECK(am_exec(
        "def identity(value):\n"
        "    return value\n"
        "def advance(value):\n"
        "    return rng_uniform(value)\n"
        "bare = rng\n"
        "parenthesized = (rng)\n"
        "returned = identity(rng)\n"
        "x = advance(rng)\n"
        "y = 1 + rng_index(rng, 7)\n"
        "z = rng_categorical(rng, weights, 0.5)\n"
        "at = categorical_at(weights, 0.5, 0.25)\n"
        "rng_uniform(rng)\n") == 0);
    CHECK(draw_calls == 4 && at_calls == 2);
    CHECK(am_get_var_float("x") == 0.125f && am_get_var_float("y") == 7);
    CHECK(am_get_var_float("z") == 1 && am_get_var_float("at") == 1);
    CHECK(state_of("rng") == UINT64_C(0x123456789abc002e));
    CHECK(state_of("bare") == UINT64_C(0x123456789abc002a));
    CHECK(state_of("parenthesized") == UINT64_C(0x123456789abc002a));
    CHECK(state_of("returned") == UINT64_C(0x123456789abc002a));
    CHECK(am_exec("rng_uniform(returned)\n") == 0);
    CHECK(state_of("returned") == UINT64_C(0x123456789abc002b));
    CHECK(state_of("rng") == UINT64_C(0x123456789abc002e));
    /* Ordinary map order is irrelevant; named 16-bit limbs define the state. */
    CHECK(am_exec(
        "last = map_get(rng, 'state1')\n"
        "map_delete(rng, 'state1')\n"
        "map_set(rng, 'state1', last)\n"
        "rng_uniform(rng)\n") == 0);
    CHECK(state_of("rng") == UINT64_C(0x123456789abc002f));
    CHECK(am_exec(
        "map_set(rng, 'state0', 65535)\n"
        "rng_uniform(rng)\n") == 0);
    CHECK(state_of("rng") == UINT64_C(0x123456789abd0000));
    CHECK(am_exec("a = rng_new(0)\nb = rng_new(16777215)\nx = rng_index(rng, 16777216)\n") == 0);
    CHECK(am_get_var_float("x") == 16777215);
}

static void test_validation_before_dispatch(void) {
    const char* invalid[] = {
        "rng_new()", "rng_new(1, 2)", "rng_new('x')", "rng_new([1])",
        "rng_new(map_new())", "rng_new(-1)", "rng_new(0.5)",
        "rng_new(16777216)", "rng_new(1e100)",
        "rng_uniform()", "rng_uniform(rng, 1)", "rng_uniform(1)",
        "rng_uniform('state')", "rng_uniform([1])", "rng_uniform(map_new())",
        "rng_index(rng)", "rng_index(rng, 1, 2)", "rng_index(rng, 'x')",
        "rng_index(rng, [1])", "rng_index(rng, 0)", "rng_index(rng, -1)",
        "rng_index(rng, 1.5)", "rng_index(rng, 16777218)", "rng_index(rng, 1e100)",
        "rng_categorical(rng, weights)", "rng_categorical(rng, 1, 0.5)",
        "rng_categorical(rng, 'weights', 0.5)", "rng_categorical(rng, weights, 'x')",
        "rng_categorical(rng, weights, 0)", "rng_categorical(rng, weights, -1)",
        "rng_categorical(rng, weights, 1e100)", "categorical_at(weights, 0.5)",
        "categorical_at(1, 0.5, 0.25)", "categorical_at(weights, 0, 0.25)",
        "categorical_at(weights, 0.5, 'x')", "categorical_at(weights, 0.5, -0.1)",
        "categorical_at(weights, 0.5, 1)", "categorical_at(weights, 0.5, 1e100)",
        "def rng_new(seed):\n    return seed\n",
        "def RNG_INDEX(state, n):\n    return n\n",
        "def rng_uniform(state):\n    return state\n",
        "def rng_categorical(state, weights, temp):\n    return 0\n",
        "def categorical_at(weights, temp, draw):\n    return 0\n"
    };
    setup();
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(am_exec(invalid[i]) != 0);
        CHECK(am_get_error()[0] != 0);
        CHECK(seed_calls == 1 && draw_calls == 0 && at_calls == 0);
        CHECK(state_of("rng") == UINT64_C(0x123456789abc002a));
    }
    const char* corruptions[] = {
        "map_delete(rng, 'state0')", "map_set(rng, 'other', 1)",
        "map_set(rng, 'algorithm', 2)", "map_set(rng, 'state0', -1)",
        "map_set(rng, 'state1', 65536)", "map_set(rng, 'state2', 0.5)",
        "map_delete(rng, 'state3')\nmap_set(rng, 'else', 0)"
    };
    for (size_t i = 0; i < sizeof(corruptions) / sizeof(corruptions[0]); i++) {
        setup();
        CHECK(am_exec(corruptions[i]) == 0);
        CHECK(am_exec("rng_uniform(rng)") != 0);
        CHECK(draw_calls == 0);
    }
}

static void test_rejected_backend_result(void) {
    const char* operations[] = {
        "rng_uniform(rng)", "rng_index(rng, 7)",
        "rng_categorical(rng, weights, 0.5)", "categorical_at(weights, 0.5, 0.25)"
    };
    for (int failure = 1; failure <= 2; failure++) {
        for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
            setup();
            bad_result = failure;
            CHECK(am_exec(operations[i]) != 0);
            CHECK(strstr(am_get_error(), "sampling backend") != NULL);
            CHECK(state_of("rng") == UINT64_C(0x123456789abc002a));
            bad_result = 0;
            CHECK(am_exec("rng_uniform(rng)") == 0);
            CHECK(state_of("rng") == UINT64_C(0x123456789abc002b));
        }
    }
}

#ifdef AML_SAMPLING_ALLOC_WRAP
static void test_rng_new_allocation_failures(void) {
    am_init();
    am_persistent_mode(0);
    AM_SamplingBackend backend = probe_backend();
    am_set_sampling_backend(&backend);
    int succeeded = 0;
    for (int budget = 0; budget < 32; budget++) {
        seed_allocation_budget = budget;
        int rc = am_exec("rng_new(7)\n");
        allocation_budget = seed_allocation_budget = -1;
        CHECK(owned_blocks == 0);
        if (rc == 0) {
            CHECK(budget == 13); /* map + capacity/buckets + five text pairs */
            printf("PASS RNG construction: %d refused allocation sites, no retained blocks\n", budget);
            succeeded = 1;
            break;
        }
        CHECK(strstr(am_get_error(), "RNG map allocation failed") != NULL);
    }
    CHECK(succeeded);
}
#endif

int main(void) {
    test_backend_lifetime();
    test_dispatch_ownership();
    test_validation_before_dispatch();
    test_rejected_backend_result();
#ifdef AML_SAMPLING_ALLOC_WRAP
    test_rng_new_allocation_failures();
#endif
    am_persistent_mode(0);
    am_set_sampling_backend(NULL);
    printf("AML_SAMPLING_API_OK %d checks\n", checks);
    return 0;
}
