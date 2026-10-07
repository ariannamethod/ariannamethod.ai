/* Ordered maps and structural keys through every AML execution path. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef AM_ASYNC_DISABLED
#include <pthread.h>
#endif

static int checks;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL map runtime line %d: %s; AML: %s\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* program =
    "def identity(value):\n    return value\n"
    "def insert(counts, name, value):\n    map_set(counts, name, value)\n    return counts\n"
    "def recur(counts, count):\n"
    "    if count <= 0:\n        return counts\n"
    "    map_set(counts, text_from_codepoint(96 + count), count)\n"
    "    return recur(counts, count - 1)\n"
    "def local_copy(counts):\n"
    "    local = counts\n    map_set(local, 'owl', 90)\n    return local\n"
    "def touch(events, name, value):\n"
    "    map_set(events, name, map_get(events, name) + 1)\n    return value\n"
    "def message(events):\n"
    "    map_set(events, 'message', map_get(events, 'message') + 1)\n    return 'сова 🦉'\n"
    "empty = map_new()\ncounts = map_new()\n"
    "map_set(counts, 'owl', 1)\nmap_set(counts, 'é', 2)\n"
    "map_set(counts, 'é', 3)\nmap_set(counts, 'שלום', 4)\nmap_set(counts, '', 0)\n"
    "old_keys = map_keys(counts)\n"
    "copy = counts\nmap_set(copy, 'owl', 99)\nmap_set(copy, 'extra', 7)\n"
    "returned = insert(counts, 'new', 5)\nmap_set(returned, 'owl', 8)\n"
    "local = local_copy(counts)\n"
    "recursive = recur(counts, 3)\nmap_delete(recursive, 'a')\n"
    "removed = map_delete(counts, 'é')\nabsent = map_delete(counts, 'absent')\n"
    "map_set(counts, 'é', 12)\nassigned = map_set(counts, 'owl', 1.5)\n"
    "alias = counts\nalias = alias\nmap_set(alias, 'owl', -1)\n"
    "cloned = map_clone(counts)\nmap_delete(cloned, 'owl')\n"
    "nested = map_get(identity(counts), text_concat('o', 'wl'))\n"
    "assert(map_get(copy, 'owl') == 99, 'assignment must copy')\n"
    "assert(map_get(returned, 'owl') == 8, 'returned map must copy on assignment')\n"
    "assert(map_get(local, 'owl') == 90, 'local copy')\n"
    "assert(map_get(alias, 'owl') == -1, 'self assignment')\n"
    "assert(map_len(recursive) == 8, 'recursive parameters mutate caller')\n"
    "assert(map_has(counts, 'owl'), 'caller survives copies')\n"
    "rebound = counts\nrebound = 'text'\nrebound = [7, 8]\n"
    "rebound = list_new()\nrebound = 7\nrebound = map_new()\nmap_set(rebound, 'fresh', 6)\n"
    "parts = list_new()\nlist_push(parts, 'a')\nlist_push(parts, 'bc')\n"
    "packed = list_key(parts)\n"
    "other = list_new()\nlist_push(other, 'ab')\nlist_push(other, 'c')\n"
    "tuple_counts = map_new()\nmap_set(tuple_counts, packed, 2)\n"
    "map_set(tuple_counts, list_key(other), 3)\n"
    "assert(map_len(tuple_counts) == 2, 'tuple boundaries are part of identity')\n"
    "unicode = list_new()\nlist_push(unicode, 'é')\nlist_push(unicode, '🦉')\n"
    "unicode_key = list_key(unicode)\n"
    "events = map_new()\nmap_set(events, 'condition', 0)\n"
    "map_set(events, 'message', 0)\nmap_set(events, 'floor', 0)\n"
    "checked = assert(touch(events, 'condition', 1), message(events))\n"
    "rounded = floor(touch(events, 'floor', -1.25))\n"
    "positive = floor(2.75)\nexact = floor(-3)\n"
    "assert(-1, 'negative finite condition is true')\n"
    "escaped = map_new()\nmap_set(escaped, 'k\\n\\t\\r\\\\\\\"', -0.125)\n"
    "done = 1\n";

static const char* printed =
    "PRINT counts\nPRINT copy\nPRINT returned\nPRINT old_keys\nPRINT empty\n"
    "PRINT rebound\nPRINT tuple_counts\nPRINT packed\nPRINT unicode_key\n"
    "PRINT events\nPRINT escaped\nPRINT removed\nPRINT absent\nPRINT assigned\n"
    "PRINT nested\nPRINT checked\nPRINT rounded\nPRINT positive\nPRINT exact\n"
    "PRINT 'x=add(counts,copy)'\n";

static const char* expected_output =
    "{\"owl\": 1.5, \"é\": 3, \"שלום\": 4, \"\": 0, \"new\": 5, \"c\": 3, \"b\": 2, \"a\": 1, \"é\": 12}\n"
    "{\"owl\": 99, \"é\": 2, \"é\": 3, \"שלום\": 4, \"\": 0, \"extra\": 7}\n"
    "{\"owl\": 8, \"é\": 2, \"é\": 3, \"שלום\": 4, \"\": 0, \"new\": 5}\n"
    "[\"owl\", \"é\", \"é\", \"שלום\", \"\"]\n{}\n{\"fresh\": 6}\n"
    "{\"2:1:a2:bc\": 2, \"2:2:ab1:c\": 3}\n2:1:a2:bc\n2:2:é4:🦉\n"
    "{\"condition\": 1, \"message\": 1, \"floor\": 1}\n"
    "{\"k\\n\\t\\r\\\\\\\"\": -0.125}\n"
    "1\n0\n1.5\n1.5\n1\n-2\n2\n-3\nx=add(counts,copy)\n";

static const char* bad[] = {
    "x = map_new(1)", "x = map_len()", "x = map_has(counts)",
    "x = map_get(counts, 'owl', 0)", "x = map_set(counts, 'owl')",
    "x = map_delete(counts, 'owl', 0)", "x = map_keys()", "x = map_clone(counts, 1)",
    "x = map_len(counts,)", "x = map_len(words)", "x = map_get(a, 'owl')",
    "x = map_has(counts, 1)", "x = map_get(counts, words)",
    "x = map_set(counts, 'owl', 'text')", "x = map_set(counts, 'owl', a)",
    "x = map_set(counts, 'owl', words)", "x = map_set(counts, 'owl', map_new())",
    "x = map_set(counts, 'owl', 1e39)", "x = map_set(counts, 'new', -1e39)",
    "x = map_delete(counts, 1)", "x = map_keys('text')", "x = map_clone(1)",
    "x = map_get(counts, 'missing')", "x = list_key()", "x = list_key(words, 0)",
    "x = list_key(counts)", "x = list_key('owl')", "x = text_len(counts)",
    "x = list_push(words, counts)", "x = list_len(counts)", "x = counts + 1",
    "PRINT len(map_new())", "PRINT sum((counts))", "PRINT rows(map_new())",
    "PRINT dot(a, map_new())", "PRINT cols((counts))", "x = zeros(counts)",
    "x = add(counts, a)", "x = add(a, counts)", "x = mul(counts, a)", "x = silu(counts)",
    "x = layernorm(a, (counts), 0)", "x = layernorm(a, 0, map_new())",
    "x = seq_layernorm(a, returned_map(), 0, 1, 2)",
    "x = spa_connectedness(a, 1, 2, returned_map())",
    "x = seq_matvec(w, a, counts)", "x = seq_rmsnorm(a, counts, 2)",
    "a[counts] = 99", "a[0] = counts", "x = a[counts]", "counts[0] = 9",
    "TENSION counts", "if counts:\n    sentinel = 1", "while counts:\n    sentinel = 1",
    "TAPE PARAM counts", "TAPE BACKWARD counts",
    "x = assert(0, 'сова остановилась')", "x = assert()", "x = assert(1)",
    "x = assert(1, 'ok', 2)", "x = assert(counts, 'bad')", "x = assert(a, 'bad')",
    "x = assert(1, counts)", "x = assert(1, 7)", "x = assert(1e39, 'bad')",
    "x = floor()", "x = floor(1, 2)", "x = floor('owl')", "x = floor(counts)",
    "x = floor(a)", "x = floor(1e39)", "x = floor(-1e39)",
    "def AsSeRt(condition, message):\n    return 1",
    "def FlOoR(value):\n    return value",
    "def MaP_SeT(value):\n    return value",
    "def LiSt_KeY(value):\n    return value",
};

static const char* bad_prefix =
    "def returned_map():\n    return map_new()\n"
    "sentinel = 0\ncounts = map_new()\nmap_set(counts, 'owl', 7)\n"
    "map_set(counts, 'tail', 8)\nwords = list_new()\nlist_push(words, 'owl')\n"
    "a = [7, 8]\nw = matrix_zeros(1, 2)\nTENSION 0.75\nTAPE START\nTAPE PARAM a\n";

static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program_handle = am_program_open(source); if (!program_handle) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(program_handle, 1)) CHECK(--budget > 0);
        CHECK(am_program_remaining(program_handle) == 0);
        return am_program_close(program_handle);
    }
    void* compiled = am_compile(source); if (!compiled) return 1;
    int rc = am_exec_compiled(compiled); am_free_compiled(compiled); return rc;
}

static void value_is(const AM_Map* map, const char* name, float expected) {
    AM_String* key = am_string_new(name); CHECK(key != NULL);
    float value = -999;
    CHECK(am_map_get(map, key, &value) == 1 && value == expected);
    am_string_free(key);
}

static void order_is(const AM_Map* map, int length, const char* const* names) {
    AM_List* keys = am_map_keys(map); CHECK(keys && keys->len == length);
    for (int i = 0; i < length; i++) CHECK(!strcmp(keys->items[i]->data, names[i]));
    am_list_free(keys);
}

static void test_program(int mode) {
    am_persistent_clear(); am_persistent_mode(1);
    CHECK(run(mode, program) == 0);
    const AM_Map* counts = am_get_var_map("counts"); CHECK(counts && counts->len == 9);
    const char* names[] = {"owl", "é", "שלום", "", "new", "c", "b", "a", "é"};
    const float values[] = {1.5f, 3, 4, 0, 5, 3, 2, 1, 12};
    order_is(counts, 9, names);
    for (int i = 0; i < 9; i++) value_is(counts, names[i], values[i]);
    value_is(am_get_var_map("copy"), "owl", 99);
    value_is(am_get_var_map("returned"), "owl", 8);
    value_is(am_get_var_map("local"), "owl", 90);
    value_is(am_get_var_map("alias"), "owl", -1);
    CHECK(am_get_var_map("recursive")->len == 8 && am_get_var_map("cloned")->len == 8);
    CHECK(am_get_var_map("empty")->len == 0);
    value_is(am_get_var_map("rebound"), "fresh", 6);
    value_is(am_get_var_map("tuple_counts"), "2:1:a2:bc", 2);
    value_is(am_get_var_map("tuple_counts"), "2:2:ab1:c", 3);
    const AM_List* old = am_get_var_list("old_keys");
    CHECK(old && old->len == 5 && !strcmp(old->items[1]->data, "é"));
    CHECK(!strcmp(am_get_var_text("packed"), "2:1:a2:bc"));
    CHECK(!strcmp(am_get_var_text("unicode_key"), "2:2:é4:🦉"));
    value_is(am_get_var_map("events"), "condition", 1);
    value_is(am_get_var_map("events"), "message", 1);
    value_is(am_get_var_map("events"), "floor", 1);
    CHECK(am_get_var_float("checked") == 1 && am_get_var_float("rounded") == -2);
    CHECK(am_get_var_float("positive") == 2 && am_get_var_float("exact") == -3);
    CHECK(am_get_var_float("removed") == 1 && am_get_var_float("absent") == 0);
    CHECK(am_get_var_float("assigned") == 1.5f && am_get_var_float("nested") == 1.5f);
    CHECK(am_get_var_float("done") == 1);
    CHECK(am_get_var_array("counts", NULL) == NULL && am_get_var_text("counts") == NULL);
    CHECK(am_get_var_list("counts") == NULL && am_get_var_map("packed") == NULL);
    am_persistent_clear(); CHECK(am_get_var_map("counts") == NULL);
    printf("PASS map values mode=%d: ordering, copied assignments, shared parameters, tuple keys, assert/floor once-only evaluation\n", mode);
}

static void test_errors(int mode) {
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char source[1536]; snprintf(source, sizeof(source), "%s%s\nsentinel = 1\n", bad_prefix, bad[i]);
        am_persistent_clear(); am_persistent_mode(1); am_tape_clear();
        CHECK(am_exec("TENSION 0.25\n") == 0);
        int rc = run(mode, source);
        if (!rc || !am_get_error()[0]) fprintf(stderr, "bad map fixture=%zu mode=%d accepted: %s\n", i, mode, bad[i]);
        CHECK(rc != 0 && am_get_error()[0] != 0);
        CHECK(am_get_var_float("sentinel") == 0);
        if (!strncmp(bad[i], "def ", 4)) {
            CHECK(am_get_var_map("counts") == NULL && am_get_state()->tension == 0.25f);
            CHECK(am_tape_get()->count == 0);
        } else {
            const AM_Map* counts = am_get_var_map("counts"); CHECK(counts && counts->len == 2);
            value_is(counts, "owl", 7); value_is(counts, "tail", 8);
            int len = 0; const float* a = am_get_var_array("a", &len);
            CHECK(a && len == 2 && a[0] == 7 && a[1] == 8);
            CHECK(am_get_state()->tension == 0.75f);
            CHECK(am_tape_get()->count == 1 && am_tape_get()->n_params == 1);
        }
        if (strstr(bad[i], "сова остановилась")) CHECK(strstr(am_get_error(), "сова остановилась") != NULL);
        am_tape_clear();
    }
    am_persistent_clear();
    printf("PASS map errors mode=%d: %zu missing/type/arity/finite/reserved failures before later effects\n",
           mode, sizeof(bad) / sizeof(bad[0]));
}

static void test_persistent(void) {
    am_persistent_mode(0);
    AM_Map* input = am_map_new(); AM_String* name = am_string_new("owl");
    CHECK(input && name && am_map_set(input, name, 1) == 0);
    CHECK(am_set_var_map("counts", input) == 0 && am_get_var_map("counts") != input);
    CHECK(am_map_set(input, name, 9) == 0); value_is(am_get_var_map("counts"), "owl", 1);
    CHECK(am_exec("copy = counts\nmap_set(counts, 'owl', 3)\n") == 0);
    value_is(am_get_var_map("copy"), "owl", 1); value_is(am_get_var_map("counts"), "owl", 3);
    CHECK(am_set_var_map(NULL, input) != 0 && am_set_var_map("", input) != 0);
    CHECK(am_set_var_map("9counts", input) != 0 && am_set_var_map("two names", input) != 0);
    CHECK(am_set_var_map("counts", NULL) != 0);
    char long_name[AML_MAX_NAME + 1]; memset(long_name, 'a', sizeof(long_name) - 1); long_name[sizeof(long_name) - 1] = 0;
    CHECK(am_set_var_map(long_name, input) != 0 && am_get_var_map(NULL) == NULL);
    value_is(am_get_var_map("counts"), "owl", 3);
    CHECK(am_exec("counts = [1, 2]\n") == 0 && am_get_var_map("counts") == NULL);
    CHECK(am_set_var_map("counts", input) == 0 && am_get_var_array("counts", NULL) == NULL);
    CHECK(am_set_var_text("counts", "text") == 0 && am_get_var_map("counts") == NULL);
    CHECK(am_set_var_map("counts", input) == 0 && am_get_var_text("counts") == NULL);
    CHECK(am_exec("counts = list_new()\n") == 0 && am_get_var_map("counts") == NULL);
    CHECK(am_set_var_map("counts", input) == 0 && am_get_var_list("counts") == NULL);
    CHECK(am_exec("counts = 7\n") == 0 && am_get_var_map("counts") == NULL);
    CHECK(am_set_var_map("counts", input) == 0);
    am_map_free(input); am_string_free(name); value_is(am_get_var_map("counts"), "owl", 9);
    am_persistent_mode(0); CHECK(am_get_var_map("counts") == NULL);
    puts("PASS map host store: cloned inputs, all typed replacements, identifiers and lifetime cleanup");
}

static void test_snapshots(void) {
#ifndef AM_ASYNC_DISABLED
    const char* source =
        "CHANNEL CREATE map_gate 1\nCHANNEL CREATE map_values 4\n"
        "counts = map_new()\nmap_set(counts, 'owl', 1)\n"
        "SPAWN map_snapshot:\n"
        "    CHANNEL READ map_gate released\n"
        "    CHANNEL WRITE map_values map_get(counts, 'owl')\n"
        "    map_delete(counts, 'owl')\n    map_set(counts, 'worker', 7)\n"
        "    CHANNEL WRITE map_values map_len(counts)\n"
        "map_set(counts, 'owl', 9)\nCHANNEL WRITE map_gate 1\nAWAIT map_snapshot\n"
        "CHANNEL WRITE map_values map_get(counts, 'owl')\nCHANNEL WRITE map_values map_len(counts)\n";
    const float expected[] = {1, 1, 9, 1};
    for (int mode = 0; mode < 3; mode++) {
        am_init(); CHECK(run(mode, source) == 0);
        CHECK(am_spawn_count() == 0 && am_channel_depth("map_values") == 4);
        for (int i = 0; i < 4; i++) { float v = -1; CHECK(am_channel_try_read("map_values", &v) == 0 && v == expected[i]); }
    }
    am_init();
    AM_Map* input = am_map_new(); AM_String* key = am_string_new("owl");
    CHECK(input && key && am_map_set(input, key, 1) == 0 && am_set_var_map("counts", input) == 0);
    am_map_free(input); am_string_free(key);
    CHECK(am_channel_create("map_gate", 1) >= 0 && am_channel_create("map_values", 1) >= 0);
    CHECK(am_spawn_launch("c_map_snapshot", "CHANNEL READ map_gate released\nCHANNEL WRITE map_values map_get(counts, 'owl')\nmap_delete(counts, 'owl')\n") >= 0);
    CHECK(am_exec("map_set(counts, 'owl', 9)\n") == 0);
    CHECK(am_channel_write("map_gate", 1) == 0 && am_spawn_await("c_map_snapshot") == 0);
    float v = -1; CHECK(am_channel_try_read("map_values", &v) == 0 && v == 1);
    value_is(am_get_var_map("counts"), "owl", 9);
    am_persistent_clear(); am_channel_close_all();
    puts("PASS map snapshots: AML/C launch-time snapshots and parent/worker mutation isolation");
#else
    puts("SKIP map snapshots: AM_ASYNC_DISABLED");
#endif
}

#ifndef AM_ASYNC_DISABLED
static void* isolated_thread(void* opaque) {
    int* result = (int*)opaque;
    AM_Map* map = am_map_new(); AM_String* key = am_string_new("worker");
    *result = !am_get_var_map("counts") && map && key && !am_map_set(map, key, 1) && !am_set_var_map("counts", map);
    am_map_free(map); am_string_free(key);
    const AM_Map* stored = am_get_var_map("counts");
    *result = *result && stored && stored->len == 1;
    am_persistent_clear(); return NULL;
}
#endif

static void test_concurrency(void) {
#ifndef AM_ASYNC_DISABLED
    am_init(); am_persistent_mode(1);
    CHECK(am_exec("counts = map_new()\nmap_set(counts, '🦉שלום', 1)\n") == 0);
    pthread_t thread; int result = 0;
    CHECK(pthread_create(&thread, NULL, isolated_thread, &result) == 0);
    CHECK(pthread_join(thread, NULL) == 0 && result == 1);
    const char* source =
        "SPAWN map_a:\n    i = 0\n    while i < 10000:\n"
        "        copy = counts\n        map_set(copy, '🦉שלום', i)\n"
        "        keys = map_keys(copy)\n        i = i + 1\n"
        "SPAWN map_b:\n    i = 0\n    while i < 10000:\n"
        "        copy = map_clone(counts)\n        map_set(copy, '🦉שלום', i)\n"
        "        keys = map_keys(copy)\n        i = i + 1\nAWAIT\n";
    for (int round = 0; round < 6; round++) {
        CHECK(am_exec(source) == 0 && am_spawn_count() == 0);
        value_is(am_get_var_map("counts"), "🦉שלום", 1);
        CHECK(am_get_var_map("copy") == NULL && am_get_var_list("keys") == NULL);
    }
    am_persistent_clear();
    puts("PASS map concurrency: TLS host state and 12 workers / 120000 clone/set/keys cycles");
#else
    puts("SKIP map concurrency: AM_ASYNC_DISABLED");
#endif
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb"); if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char* source = (char*)malloc((size_t)length + 1); if (!source) { fclose(file); return NULL; }
    size_t count = fread(source, 1, (size_t)length, file); fclose(file);
    if (count != (size_t)length) { free(source); return NULL; }
    source[length] = 0; return source;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--fixture")) { fputs(program, stdout); fputs(printed, stdout); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--expected")) { fputs(expected_output, stdout); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) { printf("%zu\n", sizeof(bad) / sizeof(bad[0])); return 0; }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int index = atoi(argv[2]); if (index < 0 || (size_t)index >= sizeof(bad) / sizeof(bad[0])) return 2;
        fputs(bad_prefix, stdout); puts(bad[index]); puts("PRINT 'WRONG_CONTINUATION'"); return 0;
    }
    am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]); char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source); if (rc) fprintf(stderr, "map runtime: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) { test_program(mode); test_errors(mode); }
    test_persistent(); test_snapshots(); test_concurrency();
    printf("AML_MAP_RUNTIME_OK %d checks\n", checks); return 0;
}
