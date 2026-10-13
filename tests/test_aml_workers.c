/* Owned worker lifecycle across interpreter, bytecode, resumable and C hosts. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#ifdef AML_WORKER_ALLOC_WRAP
static _Thread_local int allocation_armed, allocation_index, allocation_fail_at, allocation_failed;
static _Thread_local int creation_fail;
void* __real_malloc(size_t n);
void* __real_calloc(size_t n, size_t size);
void* __real_realloc(void* p, size_t n);
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
static int allocation_refused(void) {
    if (allocation_armed && allocation_index++ == allocation_fail_at) { allocation_failed = 1; return 1; }
    return 0;
}
void* __wrap_malloc(size_t n) { return allocation_refused() ? NULL : __real_malloc(n); }
void* __wrap_calloc(size_t n, size_t size) { return allocation_refused() ? NULL : __real_calloc(n, size); }
void* __wrap_realloc(void* p, size_t n) { return allocation_refused() ? NULL : __real_realloc(p, n); }
int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    return creation_fail ? EAGAIN : __real_pthread_create(thread, attr, fn, arg);
}
#endif
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL workers line %d: %s (%s)\n", __LINE__, #x, am_get_error()); exit(1); } } while (0)
static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* p = am_program_open(source); if (!p) return 1;
        while (!am_program_step(p, 1)) {}
        return am_program_close(p);
    }
    void* p = am_compile(source); if (!p) return 1;
    int rc = am_exec_compiled(p); am_free_compiled(p); return rc;
}
static void fail(int mode, const char* source, const char* diagnostic) {
    CHECK(run(mode, source) != 0);
    CHECK(strstr(am_get_error(), diagnostic));
    CHECK(am_spawn_count() == 0);
}
static void test_errors(int mode) {
    am_init(); am_persistent_mode(1);
    CHECK(run(mode, "kept = 'unchanged'\n") == 0);
    fail(mode, "SPAWN bad:\n    assert(0, 'worker failed')\nAWAIT bad INTO kept\n", "worker failed");
    CHECK(!strcmp(am_get_var_text("kept"), "unchanged"));
    fail(mode, "SPAWN no_value:\n    x = 7\nAWAIT no_value INTO kept\n", "did not return");
    CHECK(!strcmp(am_get_var_text("kept"), "unchanged"));
    fail(mode, "AWAIT absent INTO kept\n", "no unjoined");
    fail(mode, "SPAWN once:\n    return 7\nAWAIT once INTO kept\nAWAIT once INTO kept\n", "no unjoined");
    CHECK(am_get_var_float("kept") == 7);
    fail(mode, "SPAWN bad:\n    assert(0, 'teardown failure')\n", "teardown failure");
    fail(mode, "SPAWN bad:\n    assert(0, 'child failure')\nassert(0, 'parent failure')\n", "parent failure");
    fail(mode, "SPAWN parent:\n    SPAWN child:\n        assert(0, 'nested failure')\nAWAIT parent\n", "nested failure");
    fail(mode, "SPAWN repeat:\n    return 1\nSPAWN repeat:\n    return 2\n", "already pending");
    fail(mode, "SPAWN broken\n    return 1\n", "expected SPAWN");
    fail(mode, "SPAWN broken: trailing\n    return 1\n", "unexpected text");
    fail(mode, "SPAWN test:\n    return 1\nAWAIT test INTO\n", "expected AWAIT");
    fail(mode, "CHANNEL WRITE absent 1\n", "channel missing");
    fail(mode, "CHANNEL READ absent kept\n", "channel missing");
    fail(mode, "CHANNEL TRY absent kept\n", "channel missing");
    fail(mode, "CHANNEL CLOSE absent\n", "channel missing");
    fail(mode, "CHANNEL CREATE bad 0\n", "invalid channel");
    fail(mode, "CHANNEL CREATE bad 1.5\n", "invalid channel");
    fail(mode, "CHANNEL CREATE bad text\n", "invalid channel");
    fail(mode, "CHANNEL READ absent bad-name\n", "expected CHANNEL");
    fail(mode, "CHANNEL READ absent output trailing\n", "expected CHANNEL");
    fail(mode, "CHANNEL CLOSE absent trailing\n", "expected CHANNEL");
    CHECK(run(mode, "CHANNEL CREATE queue 1\nkept = 9\nCHANNEL TRY queue kept\nassert(kept == 9, 'empty keeps value')\n") == 0);
    fail(mode, "CHANNEL CREATE queue 1\n", "already exists");
    fail(mode, "SPAWN blocked:\n    CHANNEL READ queue ready\nCHANNEL CLOSE queue\nAWAIT blocked\n", "channel");
    // A program joins its final child without an explicit AWAIT.
    CHECK(run(mode, "CHANNEL CREATE done 1\nSPAWN normal:\n    CHANNEL WRITE done 7\n") == 0);
    float value = 0; CHECK(am_channel_read("done", &value) == 0 && value == 7);
    am_channel_close_all(); am_persistent_mode(0);
}
static void test_hosts(void) {
    am_init(); am_persistent_mode(1);
    float input[2] = {3, 4}; CHECK(am_set_var_array("input", input, 2) == 0);
    AML_Var value = {0};
    for (int i = 0; i < 96; i++) {
        char* source = strdup("input[0] = input[0] + 1\nreturn input\n"); CHECK(source);
        CHECK(am_spawn_launch("same-name", source) >= 0); free(source);
        CHECK(am_spawn_await_value("same-name", &value) == 0);
        CHECK(value.type == AML_TYPE_ARRAY && value.array->data[0] == 4 && value.array->data[1] == 4);
        int length; CHECK(am_get_var_array("input", &length)[0] == 3 && length == 2);
    }
    CHECK(am_spawn_await_value("same-name", &value) != 0 && value.array->data[0] == 4);
    am_value_clear(&value); CHECK(!value.array);
    CHECK(am_spawn_launch("discard", "return 'discard me'\n") >= 0);
    CHECK(am_spawn_drain() == 0 && am_spawn_count() == 0);
    CHECK(am_spawn_launch("error", "assert(0, 'host drain failed')\n") >= 0);
    CHECK(am_spawn_drain() != 0 && strstr(am_get_error(), "host drain failed"));
    // Each resumable program owns its own identically named pending worker.
    CHECK(am_channel_create("first_gate", 1) >= 0);
    CHECK(am_channel_create("second_gate", 1) >= 0);
    void* a = am_program_open("SPAWN same:\n    CHANNEL READ first_gate x\n    return x\nAWAIT same INTO answer\nassert(answer == 3, 'first')\n");
    void* b = am_program_open("SPAWN same:\n    CHANNEL READ second_gate x\n    return x\nAWAIT same INTO answer\nassert(answer == 7, 'second')\n");
    CHECK(a && b); CHECK(am_program_step(a, 1) == 0); CHECK(am_program_step(b, 1) == 0);
    CHECK(am_channel_write("second_gate", 7) == 0);
    CHECK(am_program_step(b, 0) == 1 && am_program_close(b) == 0);
    CHECK(am_channel_write("first_gate", 3) == 0);
    CHECK(am_program_step(a, 0) == 1 && am_program_close(a) == 0);
    // Early program close joins its child even when later statements never run.
    a = am_program_open("SPAWN early:\n    assert(0, 'close reports child')\nx = 0\n");
    CHECK(a && am_program_step(a, 1) == 0);
    CHECK(am_program_close(a) != 0 && strstr(am_get_error(), "close reports child"));
    CHECK(am_spawn_count() == 0);
    // The 16-slot bound is concurrent, and failures do not lose pending jobs.
    CHECK(am_channel_create("capacity", 16) >= 0);
    for (int i = 0; i < 16; i++) {
        char name[32]; snprintf(name, sizeof(name), "job%d", i);
        CHECK(am_spawn_launch(name, "CHANNEL READ capacity go\nreturn go\n") >= 0);
    }
    CHECK(am_spawn_launch("overflow", "return 0\n") < 0 && strstr(am_get_error(), "limit"));
    for (int i = 0; i < 16; i++) CHECK(am_channel_write("capacity", 1) == 0);
    CHECK(am_spawn_drain() == 0 && am_spawn_count() == 0);
    am_channel_close_all(); am_persistent_mode(0);
}
static atomic_int model_freed;
static void* model_load(const char* path, char* error, size_t cap) { (void)path; (void)error; (void)cap; return malloc(1); }
static void model_destroy(void* model) { free(model); atomic_fetch_add(&model_freed, 1); }
static int model_pieces(const void* model, const char* text, size_t n, AM_TokenizerEmit emit, void* context, char* error, size_t cap) {
    (void)model; (void)error; (void)cap; return emit(context, text, n);
}
static void test_tokenizer(void) {
    am_init();
    AM_TokenizerBackend backend = {model_load, model_destroy, model_pieces, NULL};
    am_set_tokenizer_backend(&backend);
    AM_Tokenizer* model = am_tokenizer_load("test", NULL, 0); CHECK(model);
    CHECK(am_set_var_tokenizer("model", model) == 0); am_tokenizer_free(model);
    CHECK(am_spawn_launch("tokenizer", "return model\n") >= 0);
    am_persistent_mode(0);
    AML_Var result = {0}; CHECK(am_spawn_await_value("tokenizer", &result) == 0);
    CHECK(result.type == AML_TYPE_TOKENIZER && atomic_load(&model_freed) == 0);
    AM_String* word = am_string_new("held");
    AM_List* pieces = am_tokenizer_pieces(result.tokenizer, word, NULL, 0);
    CHECK(pieces && pieces->len == 1 && !strcmp(pieces->items[0]->data, "held"));
    am_list_free(pieces); am_string_free(word); am_value_clear(&result);
    CHECK(atomic_load(&model_freed) == 1); am_set_tokenizer_backend(NULL);
}
static void* own_host_thread(void* data) {
    int* ok = data; AML_Var result = {0};
    *ok = am_spawn_launch("private", "return 7\n") >= 0 &&
        am_spawn_await_value("private", &result) == 0 && result.value == 7;
    am_value_clear(&result); return NULL;
}
static void test_concurrent_hosts(void) {
    am_init(); pthread_t threads[4]; int ok[4] = {0};
    for (int i = 0; i < 4; i++) CHECK(pthread_create(&threads[i], NULL, own_host_thread, &ok[i]) == 0);
    for (int i = 0; i < 4; i++) CHECK(pthread_join(threads[i], NULL) == 0 && ok[i]);
    CHECK(am_spawn_count() == 0);
    for (int i = 0; i < 128; i++) {
        CHECK(am_channel_create("reused", 1) >= 0);
        CHECK(am_channel_write("reused", 3) == 0);
        CHECK(am_channel_close("reused") == 0);
    }
    CHECK(am_channel_count() == 0);
}
typedef struct { int write, rc; float value; atomic_int started; } ChannelWait;
static void* channel_waiter(void* data) {
    ChannelWait* wait = data;
    atomic_store(&wait->started, 1);
    wait->rc = wait->write ? am_channel_write("closing", wait->value)
                          : am_channel_read("closing", &wait->value);
    return NULL;
}
static void test_channel_close(void) {
    am_init();
    for (int write = 0; write < 2; write++) {
        for (int repeat = 0; repeat < 8; repeat++) {
            CHECK(am_channel_create("closing", 1) >= 0);
            if (write) CHECK(am_channel_write("closing", 3) == 0);
            ChannelWait wait = {.write = write, .value = 99};
            pthread_t thread; CHECK(pthread_create(&thread, NULL, channel_waiter, &wait) == 0);
            while (!atomic_load(&wait.started)) {}
            // Let the actual wait begin before closing and reusing its slot.
            struct timespec pause = {0, 10000000}; nanosleep(&pause, NULL);
            CHECK(am_channel_close("closing") == 0);
            CHECK(am_channel_create("closing", 1) >= 0);
            CHECK(am_channel_write("closing", 7) == 0);
            CHECK(pthread_join(thread, NULL) == 0);
            CHECK(wait.rc == -2 && wait.value == 99);
            float value = 0;
            CHECK(am_channel_read("closing", &value) == 0 && value == 7);
            CHECK(am_channel_close("closing") == 0);
        }
    }
    CHECK(am_channel_create("reset", 1) >= 0);
    CHECK(am_spawn_launch("reset", "CHANNEL READ reset x\n") >= 0);
    // Full runtime reset first closes queues and joins workers, then resets G.
    am_init(); CHECK(am_spawn_count() == 0 && am_channel_count() == 0);
}
static void test_launch_failures(void) {
#ifdef AML_WORKER_ALLOC_WRAP
    am_init();
    float original[3] = {2, 3, 5};
    CHECK(am_set_var_array("original", original, 3) == 0);
    int reached_success = 0;
    for (int index = 0; index < 128; index++) {
        allocation_index = allocation_failed = 0; allocation_fail_at = index; allocation_armed = 1;
        int rc = am_spawn_launch("allocated", "return original\n");
        allocation_armed = 0;
        if (allocation_failed) {
            CHECK(rc < 0 && am_get_error()[0] && am_spawn_count() == 0);
        } else {
            AML_Var value = {0}; CHECK(rc >= 0 && am_spawn_await_value("allocated", &value) == 0);
            CHECK(value.type == AML_TYPE_ARRAY && value.array->data[2] == 5);
            am_value_clear(&value); reached_success = 1;
        }
        int n; const float* kept = am_get_var_array("original", &n);
        CHECK(kept && n == 3 && kept[0] == 2 && kept[1] == 3 && kept[2] == 5);
        if (reached_success) break;
    }
    CHECK(reached_success);
    creation_fail = 1; CHECK(am_spawn_launch("refused", "return original\n") < 0); creation_fail = 0;
    CHECK(am_spawn_count() == 0 && strstr(am_get_error(), "cannot launch"));
    CHECK(am_spawn_launch("refused", "return original\n") >= 0 && am_spawn_await("refused") == 0);
    am_persistent_mode(0);
#endif
}
int main(int argc, char** argv) {
    am_init();
    if (argc == 3) {
        FILE* f = fopen(argv[2], "rb"); CHECK(f); CHECK(!fseek(f, 0, SEEK_END)); long n = ftell(f); rewind(f);
        CHECK(n >= 0); char* source = calloc((size_t)n + 1, 1); CHECK(source);
        CHECK(fread(source, 1, (size_t)n, f) == (size_t)n); fclose(f);
        int rc = run(atoi(argv[1]), source); if (rc) fprintf(stderr, "%s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) test_errors(mode);
    test_hosts(); test_tokenizer(); test_concurrent_hosts(); test_channel_close(); test_launch_failures();
    printf("AML_WORKER_RUNTIME_OK %d checks\n", checks); return 0;
}
