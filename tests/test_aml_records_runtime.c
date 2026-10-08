/* Records through interpreted, stepped and bytecode execution. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record_invalids.h"
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL record runtime line %d: %s (%s)\n", __LINE__, #c, am_get_error()); \
    exit(1); } } while (0)
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
    long n = ftell(file); if (n < 0 || n > 1048576 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char* source = malloc((size_t)n + 1); if (!source) { fclose(file); return NULL; }
    size_t count = fread(source, 1, (size_t)n, file); fclose(file);
    if (count != (size_t)n) { free(source); return NULL; }
    source[n] = 0; return source;
}
static void test_failures(int mode) {
    am_init(); am_persistent_mode(1);
    CHECK(run(mode, "state = record_new()\nrecord_set(state, 'number', 7)\n"
                    "kept = list_new()\nlist_push(kept, 'old')\n") == 0);
    for (size_t i = 0; i < sizeof(record_invalids) / sizeof(record_invalids[0]); i++) {
        char source[512];
        snprintf(source, sizeof(source), "sentinel = 0\nkept = %s\nsentinel = 1\n", record_invalids[i]);
        if (!run(mode, source)) { fprintf(stderr, "accepted record invalid mode%d: %s\n", mode, record_invalids[i]); exit(1); }
        CHECK(am_get_error()[0] && am_get_var_float("sentinel") == 0);
        const AM_List* kept = am_get_var_list("kept"); CHECK(kept && kept->len == 1);
        CHECK(!strcmp(kept->items[0]->data, "old"));
        CHECK(run(mode, "assert(record_get(state, 'number') == 7, 'bad call changed state')\n"
                        "assert(list_len(record_keys(state)) == 1, 'bad call added a field')\n") == 0);
    }
    const char* reserved[] = {"record_new", "record_set", "record_get", "record_has", "record_keys",
                              "record_kind", "record_clone", "record_replace", "record_swap", "checkpoint_save",
                              "checkpoint_load", "file_exists", "tokenizer_identity"};
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        char source[256]; snprintf(source, sizeof(source), "def %s():\n    return 0\n", reserved[i]);
        CHECK(run(mode, source) != 0 && strstr(am_get_error(), "intrinsic"));
    }
    am_persistent_clear();
}
static void test_workers(int mode) {
#ifndef AM_ASYNC_DISABLED
    am_init(); am_persistent_mode(1);
    CHECK(run(mode, "state = record_new()\nparts = list_new()\nlist_push(parts, 'initial')\n"
        "record_set(state, 'parts', parts)\nrecord_set(state, 'version', 1)\n"
        "CHANNEL CREATE record_gate 2\nCHANNEL CREATE record_results 4\n"
        "SPAWN first:\n    CHANNEL READ record_gate ready\n"
        "    CHANNEL WRITE record_results record_get(state, 'version')\n"
        "    list_push(record_get(state, 'parts'), 'worker')\n"
        "    CHANNEL WRITE record_results list_len(record_get(state, 'parts'))\n"
        "SPAWN second:\n    CHANNEL READ record_gate ready\n"
        "    CHANNEL WRITE record_results record_get(state, 'version')\n"
        "    CHANNEL WRITE record_results list_len(record_get(state, 'parts'))\n"
        "record_set(state, 'version', 2)\nlist_push(record_get(state, 'parts'), 'parent')\n"
        "CHANNEL WRITE record_gate 1\nCHANNEL WRITE record_gate 1\nAWAIT\n"
        "assert(record_get(state, 'version') == 2, 'parent version')\n"
        "assert(list_len(record_get(state, 'parts')) == 2, 'private worker leaf')\n") == 0);
    CHECK(am_spawn_count() == 0 && am_channel_depth("record_results") == 4);
    int ones = 0, twos = 0;
    for (int i = 0; i < 4; i++) { float value; CHECK(am_channel_try_read("record_results", &value) == 0); ones += value == 1; twos += value == 2; }
    CHECK(ones == 3 && twos == 1); am_persistent_clear(); am_channel_close_all();
#else
    (void)mode;
#endif
}
int main(int argc, char** argv) {
    am_init();
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(record_invalids) / sizeof(record_invalids[0])); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int i = atoi(argv[2]); if (i < 0 || (size_t)i >= sizeof(record_invalids) / sizeof(record_invalids[0])) return 2;
        printf("state = record_new()\nrecord_set(state, 'number', 7)\n%s\nPRINT 'WRONG_CONTINUATION'\n", record_invalids[i]); return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]); char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source); if (rc) fprintf(stderr, "records: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) { test_failures(mode); test_workers(mode); }
    am_persistent_mode(0);
    printf("AML_RECORD_RUNTIME_OK %d checks; %zu invalid calls\n", checks,
           sizeof(record_invalids) / sizeof(record_invalids[0])); return 0;
}
