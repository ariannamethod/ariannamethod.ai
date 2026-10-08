/* Canonical NoTorch model/piece integration and reusable three-host runner. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/tokenizer_tiny.h"

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL tokenizer native line %d: %s (%s)\n", __LINE__, #c, am_get_error()); \
    exit(1); } } while (0)
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
static char* read_source(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f); if (n < 0 || n > 1048576 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    char* source = malloc((size_t)n + 1); if (!source) { fclose(f); return NULL; }
    size_t got = fread(source, 1, (size_t)n, f); fclose(f);
    if (got != (size_t)n) { free(source); return NULL; }
    source[n] = 0; return source;
}
static void test_vectors(AM_Tokenizer* model) {
    static const struct { const char* text; int count; const char* pieces[4]; } cases[] = {
        {"", 0, {NULL}}, {"a", 1, {"a"}}, {"aa", 1, {"aa"}},
        {"aaa", 2, {"a", "aa"}}, {"aaaa", 2, {"aa", "aa"}},
        {"⚡👀abéà_", 3, {"⚡👀", "a", "béà_"}},
        {"hello", 1, {"hello"}}, {"  a  ", 3, {"  ", "a", "  "}},
        {"שלום", 1, {"שלום"}}, {"Привет", 1, {"Привет"}},
        {"a\nb", 2, {"a", "\nb"}}, {"éa\t", 3, {"é", "a", "\t"}}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        AM_String* text = am_string_new(cases[i].text); CHECK(text); char error[256];
        AM_List* pieces = am_tokenizer_pieces(model, text, error, sizeof(error));
        if (!pieces) fprintf(stderr, "native encode: %s\n", error);
        CHECK(pieces && pieces->len == cases[i].count);
        for (int j = 0; j < pieces->len; j++) CHECK(!strcmp(pieces->items[j]->data, cases[i].pieces[j]));
        CHECK(!strcmp(text->data, cases[i].text)); am_list_free(pieces); am_string_free(text);
    }
}
typedef struct { AM_Tokenizer* model; int ok; } Worker;
static void* worker(void* data) {
    Worker* state = data; AM_String* text = am_string_new("⚡👀abéà_");
    if (!text) return NULL;
    state->ok = 1;
    for (int i = 0; i < 250; i++) {
        char error[256]; AM_List* result = am_tokenizer_pieces(state->model, text, error, sizeof(error));
        if (!result || result->len != 3 || strcmp(result->items[0]->data, "⚡👀") ||
            strcmp(result->items[1]->data, "a") || strcmp(result->items[2]->data, "béà_")) state->ok = 0;
        am_list_free(result);
    }
    am_string_free(text); return NULL;
}
static void test_native(const char* path) {
    char error[256]; AM_Tokenizer* model = am_tokenizer_load(path, error, sizeof(error));
    if (!model) fprintf(stderr, "native load: %s\n", error);
    CHECK(model);
    test_vectors(model);
    pthread_t threads[4]; Worker states[4];
    for (int i = 0; i < 4; i++) { states[i] = (Worker){model, 0}; CHECK(pthread_create(&threads[i], NULL, worker, &states[i]) == 0); }
    for (int i = 0; i < 4; i++) { CHECK(pthread_join(threads[i], NULL) == 0); CHECK(states[i].ok); }
    am_tokenizer_free(model);
    printf("AML_TOKENIZER_NATIVE_OK %d checks; 12 vectors and 1,000 concurrent encodings\n", checks);
}
int main(int argc, char** argv) {
    if (argc == 3 && (!strcmp(argv[1], "--write-model") || !strcmp(argv[1], "--write-unsupported"))) {
        unsigned char bytes[sizeof(tokenizer_tiny)]; memcpy(bytes, tokenizer_tiny, sizeof(bytes));
        if (!strcmp(argv[1], "--write-unsupported")) bytes[73] = 2; /* TrainerSpec model_type BPE. */
        FILE* f = fopen(argv[2], "wb"); if (!f) return 2;
        size_t n = fwrite(bytes, 1, sizeof(bytes), f); return fclose(f) || n != sizeof(bytes);
    }
    am_use_notorch(); am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]); char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source); if (rc) fprintf(stderr, "tokenizer: %s\n", am_get_error());
        free(source); return rc;
    }
    if (argc == 3 && !strcmp(argv[1], "--test-model")) { test_native(argv[2]); return 0; }
    return 2;
}
