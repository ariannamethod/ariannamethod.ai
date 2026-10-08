/* Immutable tokenizer ownership, backend replacement, bad output, and faults. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks;
#define CHECK(c) do { __atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED); if (!(c)) { \
    fprintf(stderr, "FAIL tokenizer API line %d: %s (%s)\n", __LINE__, #c, am_get_error()); \
    exit(1); } } while (0)

#ifdef AML_TOKENIZER_ALLOC_WRAP
static int allocation_budget = -1, tracked_count;
static void* tracked[200000];
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
static void track(void* p) {
    if (p && allocation_budget >= 0) {
        CHECK(tracked_count < 200000); tracked[tracked_count++] = p;
    }
}
static void untrack(void* p) {
    for (int i = 0; p && i < tracked_count; i++)
        if (tracked[i] == p) { tracked[i] = tracked[--tracked_count]; return; }
}
static int refuse(void) {
    if (allocation_budget == 0) return 1;
    if (allocation_budget > 0) allocation_budget--;
    return 0;
}
void* __wrap_malloc(size_t n) {
    if (refuse()) return NULL;
    void* p = __real_malloc(n); track(p); return p;
}
void* __wrap_calloc(size_t n, size_t s) {
    if (refuse()) return NULL;
    void* p = __real_calloc(n, s); track(p); return p;
}
void* __wrap_realloc(void* old, size_t n) {
    if (refuse()) return NULL;
    void* p = __real_realloc(old, n);
    if (p) { untrack(old); track(p); }
    return p;
}
void __wrap_free(void* p) { untrack(p); __real_free(p); }
#endif

typedef struct { int kind, guard; } Model;
static int loaded[3], destroyed[3], piece_calls;
static char last_path[4096];
static void* load_kind(const char* path, char* error, size_t cap, int kind) {
    CHECK(path && strlen(path) < sizeof(last_path));
    strcpy(last_path, path);
    if (strstr(path, "load-fails")) {
        if (error && cap) snprintf(error, cap, "mock refused model");
        return NULL;
    }
    Model* model = malloc(sizeof(*model));
    if (!model) { if (error && cap) snprintf(error, cap, "mock allocation"); return NULL; }
    model->kind = kind; model->guard = 0x575;
    __atomic_add_fetch(&loaded[kind], 1, __ATOMIC_RELAXED); return model;
}
static void* load_a(const char* p, char* e, size_t n) { return load_kind(p, e, n, 1); }
static void* load_b(const char* p, char* e, size_t n) { return load_kind(p, e, n, 2); }
static void destroy(void* data) {
    Model* model = data; CHECK(model && model->guard == 0x575);
    __atomic_add_fetch(&destroyed[model->kind], 1, __ATOMIC_RELAXED);
    model->guard = 0; free(model);
}
static int pieces(const void* data, const char* text, size_t bytes,
                  AM_TokenizerEmit emit, void* context, char* error, size_t cap) {
    const Model* model = data; CHECK(model && model->guard == 0x575);
    CHECK(text && bytes == strlen(text)); __atomic_add_fetch(&piece_calls, 1, __ATOMIC_RELAXED);
    if (!strcmp(text, "empty")) return 0;
    if (!strcmp(text, "invalid-utf8")) return emit(context, "\xC0\xAF", 2);
    if (!strcmp(text, "embedded-nul")) return emit(context, "a\0b", 3);
    if (!strcmp(text, "null-piece")) return emit(context, NULL, 1);
    if (!strcmp(text, "oversized-piece")) return emit(context, "x", AM_MAX_STRING_BYTES + (size_t)1);
    if (!strcmp(text, "overflow-piece")) return emit(context, "x", (size_t)-1);
    if (!strcmp(text, "exact-byte-cap") || !strcmp(text, "sum-over-cap")) {
        char* huge = malloc(AM_MAX_STRING_BYTES); if (!huge) return -1;
        memset(huge, 'x', AM_MAX_STRING_BYTES);
        int rc = emit(context, huge, AM_MAX_STRING_BYTES); free(huge);
        if (!rc && !strcmp(text, "sum-over-cap")) rc = emit(context, "x", 1);
        return rc;
    }
    if (!strcmp(text, "exact-item-cap")) {
        for (int i = 0; i < AM_MAX_LIST_ITEMS; i++) if (emit(context, "", 0)) return -1;
        return 0;
    }
    if (!strcmp(text, "too-many-pieces")) {
        for (int i = 0; i <= AM_MAX_LIST_ITEMS; i++) if (emit(context, "", 0)) return -1;
        return 0;
    }
    const char* first = model->kind == 1 ? "▁one" : "▁two";
    if (emit(context, first, strlen(first))) return -1;
    if (emit(context, text, bytes)) return -1;
    if (!strcmp(text, "fail-after-output")) {
        if (error && cap) snprintf(error, cap, "mock stopped after output");
        return -1;
    }
    return emit(context, "🌧", strlen("🌧"));
}
static AM_TokenizerBackend table(int kind) {
    AM_TokenizerBackend result = {
        .load = kind == 1 ? load_a : load_b, .destroy = destroy, .pieces = pieces,
        .identity = NULL
    }; return result;
}
static void install(int kind) {
    AM_TokenizerBackend backend = table(kind); am_set_tokenizer_backend(&backend);
    memset(&backend, 0, sizeof(backend)); /* Registration must copy the table. */
}
static void expect(const AM_List* list, int kind, const char* text) {
    CHECK(list && list->len == 3);
    CHECK(!strcmp(list->items[0]->data, kind == 1 ? "▁one" : "▁two"));
    CHECK(!strcmp(list->items[1]->data, text)); CHECK(!strcmp(list->items[2]->data, "🌧"));
}
static void test_lifetime(void) {
    char error[128]; am_set_tokenizer_backend(NULL);
    CHECK(am_tokenizer_load("mock", error, sizeof(error)) == NULL && error[0]);
    for (int missing = 0; missing < 3; missing++) {
        AM_TokenizerBackend partial = table(1);
        if (missing == 0) partial.load = NULL;
        if (missing == 1) partial.destroy = NULL;
        if (missing == 2) partial.pieces = NULL;
        am_set_tokenizer_backend(&partial);
        CHECK(am_tokenizer_load("mock", error, sizeof(error)) == NULL && error[0]);
    }
    install(1); am_init();
    int old_a = destroyed[1], old_b = destroyed[2];
    AM_Tokenizer* a = am_tokenizer_load("literal/model path", error, sizeof(error)); CHECK(a);
    CHECK(!strcmp(last_path, "literal/model path"));
    AM_String* text = am_string_new("café"); CHECK(text);
    am_tokenizer_ref(a); am_tokenizer_free(a); CHECK(destroyed[1] == old_a);
    install(2);
    AM_Tokenizer* b = am_tokenizer_load("second", NULL, 0); CHECK(b);
    am_set_tokenizer_backend(NULL);
    AM_List* result = am_tokenizer_pieces(a, text, error, sizeof(error)); expect(result, 1, "café");
    am_list_free(result);
    result = am_tokenizer_pieces(b, text, error, sizeof(error)); expect(result, 2, "café");
    am_list_free(result);
    CHECK(am_set_var_tokenizer("saved", a) == 0);
    CHECK(am_get_var_tokenizer("saved") == a);
    am_tokenizer_free(a); CHECK(destroyed[1] == old_a);
    am_tokenizer_free(b); CHECK(destroyed[2] == old_b + 1);
    CHECK(am_exec("answer = tokenizer_pieces(saved, 'retained')\nsaved = 0\n") == 0);
    expect(am_get_var_list("answer"), 1, "retained");
    CHECK(destroyed[1] == old_a + 1);
    am_string_free(text); am_persistent_clear();
    am_tokenizer_ref(NULL); am_tokenizer_free(NULL);
}
static void test_failed_output(void) {
    install(1); char error[128];
    CHECK(am_tokenizer_load(NULL, error, sizeof(error)) == NULL && error[0]);
    CHECK(am_tokenizer_load("load-fails", error, sizeof(error)) == NULL && strstr(error, "mock"));
    AM_Tokenizer* model = am_tokenizer_load("mock", error, sizeof(error)); CHECK(model);
    AM_String* good = am_string_new("ordinary"); CHECK(good);
    CHECK(am_tokenizer_pieces(NULL, good, error, sizeof(error)) == NULL && error[0]);
    CHECK(am_tokenizer_pieces(model, NULL, error, sizeof(error)) == NULL && error[0]);
    const char* invalid[] = {"invalid-utf8", "embedded-nul", "null-piece", "oversized-piece",
                             "overflow-piece", "too-many-pieces", "sum-over-cap", "fail-after-output"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        AM_String* text = am_string_new(invalid[i]); CHECK(text); error[0] = 0;
        CHECK(am_tokenizer_pieces(model, text, error, sizeof(error)) == NULL && error[0]);
        am_string_free(text);
        AM_List* result = am_tokenizer_pieces(model, good, error, sizeof(error));
        expect(result, 1, "ordinary"); am_list_free(result);
    }
    AM_String* empty = am_string_new("empty");
    AM_List* result = am_tokenizer_pieces(model, empty, NULL, 0); CHECK(result && result->len == 0);
    am_list_free(result); am_string_free(empty);
    AM_String* boundary = am_string_new("exact-byte-cap"); CHECK(boundary);
    result = am_tokenizer_pieces(model, boundary, error, sizeof(error));
    CHECK(result && result->len == 1 && result->items[0]->byte_len == AM_MAX_STRING_BYTES);
    CHECK(result->items[0]->data[0] == 'x' && result->items[0]->data[AM_MAX_STRING_BYTES - 1] == 'x');
    am_list_free(result); am_string_free(boundary);
    boundary = am_string_new("exact-item-cap"); CHECK(boundary);
    result = am_tokenizer_pieces(model, boundary, error, sizeof(error));
    CHECK(result && result->len == AM_MAX_LIST_ITEMS && result->items[AM_MAX_LIST_ITEMS - 1]->byte_len == 0);
    am_list_free(result); am_string_free(boundary); am_string_free(good); am_tokenizer_free(model);
}
static int identity_mode;
static const char* identity(const void* data) {
    const Model* model = data; CHECK(model && model->guard == 0x575);
    static const char* values[] = {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "0123456789abcdeg0123456789abcdef0123456789abcdef0123456789abcdef",
        "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef",
        "123", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0", NULL
    };
    return values[identity_mode];
}
static void test_identity(void) {
    install(1); char error[128];
    AM_Tokenizer* absent = am_tokenizer_load("without-identity", error, sizeof(error)); CHECK(absent);
    CHECK(am_tokenizer_identity(absent, error, sizeof(error)) == NULL && error[0]);
    AM_TokenizerBackend backend = table(1); backend.identity = identity;
    am_set_tokenizer_backend(&backend); memset(&backend, 0, sizeof(backend));
    AM_Tokenizer* model = am_tokenizer_load("with-identity", error, sizeof(error)); CHECK(model);
    install(2); /* The old model retains its copied identity callback. */
    identity_mode = 0;
    AM_String* value = am_tokenizer_identity(model, error, sizeof(error));
    CHECK(value && !strcmp(value->data, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    am_string_free(value);
    CHECK(am_tokenizer_identity(absent, error, sizeof(error)) == NULL && error[0]);
    for (identity_mode = 1; identity_mode <= 5; identity_mode++)
        CHECK(am_tokenizer_identity(model, error, sizeof(error)) == NULL && error[0]);
    CHECK(am_tokenizer_identity(NULL, error, sizeof(error)) == NULL && error[0]);
    identity_mode = 0;
#ifdef AML_TOKENIZER_ALLOC_WRAP
    int succeeded = 0;
    for (int budget = 0; budget < 8; budget++) {
        CHECK(tracked_count == 0); allocation_budget = budget;
        value = am_tokenizer_identity(model, error, sizeof(error)); allocation_budget = -1;
        if (value) { CHECK(value->byte_len == 64); succeeded = 1; } else CHECK(error[0]);
        am_string_free(value); CHECK(tracked_count == 0);
        AM_String* text = am_string_new("still usable"); CHECK(text);
        AM_List* result = am_tokenizer_pieces(model, text, error, sizeof(error));
        expect(result, 1, "still usable"); am_list_free(result); am_string_free(text);
        if (succeeded) break;
    }
    CHECK(succeeded);
#endif
    am_tokenizer_free(model); am_tokenizer_free(absent);
}
static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* p = am_program_open(source); if (!p) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(p, 1)) CHECK(--budget > 0);
        return am_program_close(p);
    }
    void* p = am_compile(source); if (!p) return 1;
    int rc = am_exec_compiled(p); am_free_compiled(p); return rc;
}
static void test_runtime(int mode) {
    install(1); am_init(); am_persistent_mode(1);
    int old = destroyed[1];
    CHECK(run(mode, "def keep(model):\n    return model\n"
        "model = tokenizer_load('mock')\nalias = model\nparen = (alias)\n"
        "returned = keep(keep(paren))\nresult = tokenizer_pieces(returned, 'nested')\n"
        "model = 0\nalias = 'released'\nparen = [1]\n"
        "empty = TOKENIZER_PIECES(returned, 'empty')\n") == 0);
    expect(am_get_var_list("result"), 1, "nested"); CHECK(destroyed[1] == old);
    CHECK(am_get_var_list("empty") && am_get_var_list("empty")->len == 0);
    CHECK(run(mode, "returned = map_new()\n") == 0); CHECK(destroyed[1] == old + 1);
    CHECK(run(mode, "model = tokenizer_load('mock')\nkept = list_new()\nlist_push(kept, 'old')\n") == 0);
    const char* bad[] = {
        "tokenizer_load()", "tokenizer_load(1)", "tokenizer_load([1])", "tokenizer_load(model)",
        "tokenizer_load('mock', 'extra')", "tokenizer_load('load-fails')",
        "tokenizer_pieces()", "tokenizer_pieces(model)", "tokenizer_pieces(model, 'x', 'y')",
        "tokenizer_pieces('mock', 'x')", "tokenizer_pieces(1, 'x')",
        "tokenizer_pieces([1], 'x')", "tokenizer_pieces(list_new(), 'x')",
        "tokenizer_pieces(map_new(), 'x')", "tokenizer_pieces(model, model)",
        "tokenizer_pieces(model, 1)", "tokenizer_pieces(model, [1])",
        "tokenizer_pieces(model, list_new())", "tokenizer_pieces(model, map_new())",
        "tokenizer_pieces(model, 'invalid-utf8')", "tokenizer_pieces(model, 'fail-after-output')",
        "model + 1", "1 + model", "len(model)", "sum(model)", "text_len(model)",
        "list_len(model)", "map_len(model)", "floor(model)", "isfinite(model)",
        "nt_tanh(model)", "codepoint_isalnum(model)", "list_push(kept, model)",
        "text_concat('x', model)", "tokenizer_identity(model)",
        "tokenizer_identity()", "tokenizer_identity(1)", "tokenizer_identity(model, 1)"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char source[512]; snprintf(source, sizeof(source), "sentinel = 0\nkept = %s\nsentinel = 1\n", bad[i]);
        if (!run(mode, source)) { fprintf(stderr, "accepted bad mode=%d: %s\n", mode, bad[i]); exit(1); }
        CHECK(am_get_error()[0]); CHECK(am_get_var_float("sentinel") == 0);
        const AM_List* kept = am_get_var_list("kept"); CHECK(kept && kept->len == 1);
        CHECK(!strcmp(kept->items[0]->data, "old")); CHECK(am_get_var_tokenizer("model"));
    }
    CHECK(run(mode, "tokenizer_pieces(model, 'discarded')\n") == 0);
    am_persistent_clear(); CHECK(destroyed[1] == old + 2);
}
static void test_workers(void) {
#ifndef AM_ASYNC_DISABLED
    for (int mode = 0; mode < 3; mode++) {
        install(1); am_init(); am_persistent_mode(1); int old = destroyed[1];
        CHECK(run(mode, "model = tokenizer_load('parent')\n"
            "CHANNEL CREATE tok_gate 2\nCHANNEL CREATE tok_result 2\n"
            "SPAWN tok_first:\n    CHANNEL READ tok_gate ready\n"
            "    result = tokenizer_pieces(model, 'worker')\n"
            "    CHANNEL WRITE tok_result list_len(result)\n"
            "SPAWN tok_second:\n    CHANNEL READ tok_gate ready\n"
            "    result = tokenizer_pieces(model, 'worker')\n"
            "    CHANNEL WRITE tok_result list_len(result)\n"
            "model = 0\nCHANNEL WRITE tok_gate 1\nCHANNEL WRITE tok_gate 1\nAWAIT\n") == 0);
        CHECK(am_spawn_count() == 0 && am_channel_depth("tok_result") == 2);
        float first, second;
        CHECK(am_channel_try_read("tok_result", &first) == 0 && first == 3);
        CHECK(am_channel_try_read("tok_result", &second) == 0 && second == 3);
        CHECK(destroyed[1] == old + 1); am_persistent_clear(); am_channel_close_all();
    }
#endif
}
static void test_origins(void) {
    install(1); am_init(); am_persistent_mode(1);
    char directory[] = "/tmp/aml tokenizer-XXXXXX"; CHECK(mkdtemp(directory));
    char source[256], expected[256];
    snprintf(source, sizeof(source), "%s/main.aml", directory);
    snprintf(expected, sizeof(expected), "%s/models/words.model", directory);
    FILE* file = fopen(source, "wb"); CHECK(file && fclose(file) == 0);
    CHECK(am_exec_source("model = tokenizer_load('models/words.model')\n",
                         source) == 0);
    CHECK(!strcmp(last_path, expected));
    CHECK(unlink(source) == 0 && rmdir(directory) == 0);
    am_persistent_clear();
}
static void test_allocations(void) {
#ifdef AML_TOKENIZER_ALLOC_WRAP
    install(1); char error[128]; int success = 0;
    for (int budget = 0; budget < 20; budget++) {
        CHECK(tracked_count == 0); int old_loaded = loaded[1], old_destroyed = destroyed[1];
        allocation_budget = budget;
        AM_Tokenizer* model = am_tokenizer_load("mock", error, sizeof(error)); allocation_budget = -1;
        if (model) success = 1; else CHECK(error[0]);
        am_tokenizer_free(model); CHECK(tracked_count == 0);
        CHECK(loaded[1] - old_loaded == destroyed[1] - old_destroyed);
        if (success) break;
    }
    CHECK(success);
    AM_Tokenizer* model = am_tokenizer_load("mock", error, sizeof(error)); CHECK(model);
    AM_String* text = am_string_new("repeated café"); CHECK(text); success = 0;
    for (int budget = 0; budget < 40; budget++) {
        CHECK(tracked_count == 0); allocation_budget = budget;
        AM_List* result = am_tokenizer_pieces(model, text, error, sizeof(error)); allocation_budget = -1;
        if (result) { expect(result, 1, "repeated café"); success = 1; } else CHECK(error[0]);
        am_list_free(result); CHECK(tracked_count == 0);
        if (success) break;
    }
    CHECK(success); am_string_free(text); am_tokenizer_free(model);
#endif
}
int main(void) {
    am_init(); am_persistent_mode(1);
    test_lifetime(); test_failed_output(); test_identity(); test_allocations();
    for (int mode = 0; mode < 3; mode++) test_runtime(mode);
    test_workers(); test_origins();
    am_persistent_mode(0); am_set_tokenizer_backend(NULL);
    CHECK(loaded[1] == destroyed[1] && loaded[2] == destroyed[2]);
    printf("AML_TOKENIZER_API_OK %d checks; %d models released\n", checks, destroyed[1] + destroyed[2]);
    return 0;
}
