/* Typed text through actual interpreter, resumable, and bytecode execution.
 * --fixture / --bad-fixture feed the same programs to runner and amlc gates. */
#include "ariannamethod.h"
#include <limits.h>
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
        fprintf(stderr, "FAIL text runtime line %d: %s; AML: %s\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* program =
    "def identity(value):\n"
    "    return value\n"
    "def grow(word, count):\n"
    "    if count <= 0:\n"
    "        return word\n"
    "    return grow(text_concat(word, '🦉'), count - 1)\n"
    "def keep(word):\n"
    "    saved = word\n"
    "    word = 17\n"
    "    return saved\n"
    "def choose(flag, word, numbers):\n"
    "    if flag == 1:\n"
    "        return word\n"
    "    return numbers\n"
    "seed = 'Привет'\n"
    "alias = seed\n"
    "alias = alias\n"
    "seed = 3\n"
    "seed = [7, 8]\n"
    "seed = 'שלום'\n"
    "kept = keep(alias)\n"
    "grown = grow(identity('é'), 3)\n"
    "chosen = choose(1, identity('Café'), [1, 2, 3])\n"
    "numbers = choose(0, 'unused', identity([1, 2, 3]))\n"
    "copy = numbers\n"
    "numbers[0] = 9\n"
    "indexed = [7, 8]\n"
    "indexed[text_len(']')] = 9\n"
    "quoted_size = zeros(text_len(')'))\n"
    "punct_size = zeros(text_len('],#=['))\n"
    "total = sum(copy)\n"
    "number = identity(7)\n"
    "punct = text_concat(identity('a, ) ] # = \"'), identity(\"b, [ '\"))\n"
    "paren = (identity('🦉'))\n"
    "controls = 'one\\n\\t\\r\\\\\\\"\\\''\n"
    "commented = text_concat('quoted#', 'tail') # outside comment\n"
    "length = text_len(grown)\n"
    "bytes = text_bytes(grown)\n"
    "same = text_equal(chosen, 'Café')\n"
    "different = text_equal('é', 'é')\n"
    "found = text_find(grown, text_concat('🦉', '🦉'))\n"
    "missing = text_find(grown, 'אין')\n"
    "part = text_slice(grown, -2, text_len(grown))\n"
    "bounded = text_slice(grown, -2147483648, 2147483520)\n"
    "point = text_codepoint(grown, -1)\n"
    "roundtrip = text_from_codepoint(point)\n"
    "i = 0\n"
    "while i < 10000:\n"
    "    i = i + 1\n"
    "done = 1\n";

static const char* printed =
    "PRINT alias\nPRINT seed\nPRINT grown\nPRINT chosen\nPRINT copy\n"
    "PRINT numbers\nPRINT punct\nPRINT controls\nPRINT commented\n"
    "PRINT length\nPRINT bytes\nPRINT same\nPRINT different\n"
    "PRINT found\nPRINT missing\nPRINT part\nPRINT point\nPRINT roundtrip\n"
    "PRINT text_concat('print:', identity('שלום'))\n"
    "PRINT (text_len('🦉') + 2)\n"
    "PRINT 'x=add(copy,numbers)'\n";

static const char* expected_output =
    "Привет\nשלום\né🦉🦉🦉\nCafé\n[1, 2, 3]\n[9, 2, 3]\n"
    "a, ) ] # = \"b, [ '\none\n\t\r\\\"'\nquoted#tail\n"
    "4\n14\n1\n0\n1\n-1\n🦉🦉\n129417\n🦉\nprint:שלום\n3\n"
    "x=add(copy,numbers)\n";

static const struct { const char* source; const char* error; } bad[] = {
    {"x = '\x80'\n", "UTF-8"},
    {"x = '\xC0\xAF'\n", "UTF-8"},
    {"x = '\xED\xA0\x80'\n", "UTF-8"},
    {"x = '\xF4\x90\x80\x80'\n", "UTF-8"},
    {"x = '\xE2\x82'\n", "UTF-8"},
    {"x = '\\0'\n", "escape"},
    {"x = '\\q'\n", "escape"},
    {"x = '\\x41'\n", "escape"},
    {"x = 'unfinished\n", "unterminated"},
    {"x = text_len(1)\n", "requires a string"},
    {"x = text_len([1, 2])\n", "requires a string"},
    {"x = ['owl']\n", NULL},
    {"x = text_concat('a', 1)\n", "requires a string"},
    {"x = text_equal('a', [1])\n", "requires a string"},
    {"x = text_find(1, 'a')\n", "requires a string"},
    {"x = text_slice('a', '0', 1)\n", "integer"},
    {"x = text_codepoint('a', 0.5)\n", "integer"},
    {"x = text_slice('a', 0, 2147483648)\n", "integer"},
    {"x = text_slice('a', -2147483904, 1)\n", "integer"},
    {"x = text_codepoint('a', 1e39)\n", "integer"},
    {"x = text_codepoint('a', 1)\n", "out of range"},
    {"x = text_codepoint('', -1)\n", "out of range"},
    {"x = text_from_codepoint(0)\n", "Unicode codepoint"},
    {"x = text_from_codepoint(-1)\n", "Unicode codepoint"},
    {"x = text_from_codepoint(55296)\n", "Unicode codepoint"},
    {"x = text_from_codepoint(1114112)\n", "Unicode codepoint"},
    {"x = text_len()\n", "number of text arguments"},
    {"x = text_concat('a')\n", "number of text arguments"},
    {"x = text_len('a', 'b')\n", "number of text arguments"},
    {"x = text_len('a',)\n", "empty function argument"},
    {"x = text_concat('a', text_from_codepoint(0))\n", "Unicode codepoint"},
    {"x = 'a' + 1\n", "after string literal"},
    {"s = 'a'\nx = s + 1\n", "scalar expression"},
    {"a = [7, 8]\ns = 'owl'\na[s] = 99\n", "scalar expression"},
    {"a = [7, 8]\ns = 'owl'\na[0] = s\n", "scalar expression"},
    {"s = 'owl'\nTENSION s\n", "scalar expression"},
    {"x = text_concat('a', 'b') + 1\n", "scalar expression"},
    {"def f(x):\n    return x\ny = f()\n", "argument"},
    {"def f(x):\n    return x\ny = f('a', 'b')\n", "argument"},
    {"def forever(word):\n    return forever(text_concat(word, 'é'))\ny = forever('a')\n", "max call depth"},
    {"i = 0\nwhile i <= 10000:\n    i = i + 1\n", "iteration limit"},
};

static int run(int mode, const char* script) {
    if (mode == 0) return am_exec(script);
    if (mode == 1) {
        void* p = am_program_open(script);
        if (!p) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(p, 1)) CHECK(--budget > 0);
        CHECK(am_program_remaining(p) == 0);
        CHECK(am_program_step(p, 1) == 1);
        return am_program_close(p);
    }
    void* p = am_compile(script);
    if (!p) return 1;
    int rc = am_exec_compiled(p);
    am_free_compiled(p);
    return rc;
}

static void text_equals(const char* name, const char* expected) {
    const char* actual = am_get_var_text(name);
    CHECK(actual != NULL);
    CHECK(strcmp(actual, expected) == 0);
}

static void test_program(int mode) {
    am_persistent_clear();
    am_persistent_mode(1);
    CHECK(run(mode, program) == 0);
    text_equals("alias", "Привет");
    text_equals("kept", "Привет");
    text_equals("seed", "שלום");
    text_equals("grown", "é🦉🦉🦉");
    text_equals("chosen", "Café");
    text_equals("punct", "a, ) ] # = \"b, [ '");
    text_equals("paren", "🦉");
    text_equals("controls", "one\n\t\r\\\"'");
    text_equals("commented", "quoted#tail");
    text_equals("part", "🦉🦉");
    text_equals("bounded", "é🦉🦉🦉");
    text_equals("roundtrip", "🦉");
    CHECK(am_get_var_float("length") == 4);
    CHECK(am_get_var_float("bytes") == 14);
    CHECK(am_get_var_float("same") == 1);
    CHECK(am_get_var_float("different") == 0);
    CHECK(am_get_var_float("found") == 1);
    CHECK(am_get_var_float("missing") == -1);
    CHECK(am_get_var_float("point") == 129417);
    CHECK(am_get_var_float("total") == 6);
    CHECK(am_get_var_float("number") == 7);
    CHECK(am_get_var_float("i") == 10000);
    CHECK(am_get_var_float("done") == 1);
    int len = 0;
    const float* numbers = am_get_var_array("numbers", &len);
    CHECK(numbers && len == 3 && numbers[0] == 9 && numbers[1] == 2 && numbers[2] == 3);
    numbers = am_get_var_array("copy", &len);
    CHECK(numbers && len == 3 && numbers[0] == 1 && numbers[1] == 2 && numbers[2] == 3);
    numbers = am_get_var_array("indexed", &len);
    CHECK(numbers && len == 2 && numbers[0] == 7 && numbers[1] == 9);
    numbers = am_get_var_array("quoted_size", &len);
    CHECK(numbers && len == 1 && numbers[0] == 0);
    numbers = am_get_var_array("punct_size", &len);
    CHECK(numbers && len == 5 && numbers[4] == 0);
    CHECK(am_get_var_text("numbers") == NULL);
    CHECK(am_get_var_array("alias", NULL) == NULL);
    am_persistent_clear();
    CHECK(am_get_var_text("alias") == NULL);
    printf("PASS typed program mode=%d: recursion, aliases, values, punctuation, limits\n", mode);
}

static void test_failures(int mode) {
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char script[2048];
        snprintf(script, sizeof(script), "sentinel = 0\n%s\nsentinel = 1\n", bad[i].source);
        am_persistent_clear();
        am_persistent_mode(1);
        int rc = run(mode, script);
        if (rc == 0 || !am_get_error()[0] ||
            (bad[i].error && !strstr(am_get_error(), bad[i].error)))
            fprintf(stderr, "bad fixture=%zu mode=%d rc=%d wanted=%s actual=%s\n",
                    i, mode, rc, bad[i].error ? bad[i].error : "nonempty diagnostic", am_get_error());
        CHECK(rc != 0);
        CHECK(am_get_error()[0] != 0);
        if (bad[i].error) CHECK(strstr(am_get_error(), bad[i].error) != NULL);
        CHECK(am_get_var_float("sentinel") == 0);
    }
    am_persistent_clear();
    printf("PASS errors mode=%d: %zu typed/UTF-8/escape/integer/recursion/loop cases\n",
           mode, sizeof(bad) / sizeof(bad[0]));
}

static void test_refused_effects(int mode) {
    static const char* invalid[] = {"a[s] = 99", "a[0] = s", "TENSION s"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char script[256];
        snprintf(script, sizeof(script),
                 "sentinel = 0\na = [7, 8]\ns = 'owl'\nTENSION 0.75\n%s\nsentinel = 1\n",
                 invalid[i]);
        am_persistent_clear();
        am_persistent_mode(1);
        CHECK(run(mode, script) != 0);
        CHECK(am_get_error()[0] != 0);
        CHECK(am_get_var_float("sentinel") == 0);
        int len = 0;
        const float* array = am_get_var_array("a", &len);
        CHECK(array && len == 2 && array[0] == 7 && array[1] == 8);
        CHECK(am_get_state()->tension == 0.75f);
    }
    am_persistent_clear();
    printf("PASS refused effects mode=%d: invalid string scalar preserves arrays and field state\n", mode);
}

static void test_persistent(void) {
    am_persistent_mode(0);
    char source[] = "Привет";
    CHECK(am_set_var_text("greeting", source) == 0);
    source[0] = 'X';
    text_equals("greeting", "Привет");
    CHECK(am_exec("alias = greeting\ngreeting = text_concat(greeting, '🦉')\n") == 0);
    text_equals("greeting", "Привет🦉");
    text_equals("alias", "Привет");
    CHECK(am_exec("greeting = 3\n") == 0);
    CHECK(am_get_var_text("greeting") == NULL && am_get_var_float("greeting") == 3);
    CHECK(am_set_var_text("greeting", "שלום") == 0);
    float array[] = {4, 5};
    CHECK(am_set_var_array("greeting", array, 2) == 0);
    CHECK(am_get_var_text("greeting") == NULL);
    CHECK(am_set_var_text("greeting", "Café") == 0);
    CHECK(am_get_var_array("greeting", NULL) == NULL);
    text_equals("greeting", "Café");
    CHECK(am_set_var_text("greeting", "\x80") != 0);
    text_equals("greeting", "Café");
    CHECK(am_set_var_text(NULL, "a") != 0);
    CHECK(am_set_var_text("", "a") != 0);
    CHECK(am_set_var_text("greeting", NULL) != 0);
    CHECK(am_set_var_text("9name", "a") != 0);
    CHECK(am_set_var_text("two names", "a") != 0);
    char long_name[AML_MAX_NAME + 1];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    CHECK(am_set_var_text(long_name, "a") != 0);
    CHECK(am_get_var_text(NULL) == NULL);
    CHECK(am_get_var_text("absent") == NULL);
    for (int i = 0; i < 32; i++) {
        CHECK(am_exec("temporary = text_concat(alias, 'é')\ncopy = temporary\ntemporary = [1, 2]\ntemporary = 1\n") == 0);
        text_equals("copy", "Приветé");
    }
    am_persistent_clear();
    CHECK(am_get_var_text("alias") == NULL && am_get_var_text("copy") == NULL);
    CHECK(am_set_var_text("reset", "🦉") == 0);
    am_persistent_mode(0);
    CHECK(am_get_var_text("reset") == NULL);
    CHECK(am_set_var_text("reset", "שלום") == 0);
    am_init();
    CHECK(am_get_var_text("reset") == NULL);
    puts("PASS persistent text: copied C inputs, typed replacement, aliases, clear/mode/reset cleanup");
}

static void test_async_ownership(void) {
#ifndef AM_ASYNC_DISABLED
    am_init();
    CHECK(am_set_var_text("seed", "🦉שלום") == 0);
    const char* script =
        "SPAWN text_a:\n"
        "    i = 0\n"
        "    while i < 10000:\n"
        "        q = seed\n"
        "        i = i + 1\n"
        "SPAWN text_b:\n"
        "    i = 0\n"
        "    while i < 10000:\n"
        "        q = seed\n"
        "        i = i + 1\n"
        "AWAIT\n";
    /* Stay within the fixed 16-slot spawn table; every worker really runs. */
    for (int round = 0; round < 6; round++) {
        CHECK(am_exec(script) == 0);
        CHECK(am_spawn_count() == 0);
        text_equals("seed", "🦉שלום");
        CHECK(am_get_var_text("q") == NULL);
    }
    am_persistent_clear();
    CHECK(am_get_var_text("seed") == NULL && am_get_var_text("q") == NULL);
    puts("PASS async text ownership: 12 workers, 120000 alias rebinds, persistent cleanup");
#else
    puts("SKIP async text ownership: AM_ASYNC_DISABLED");
#endif
}

static void test_async_snapshots(void) {
#ifndef AM_ASYNC_DISABLED
    const char* script =
        "CHANNEL CREATE snapshot_gate 1\n"
        "CHANNEL CREATE snapshot_values 8\n"
        "word = '🦉שלום'\n"
        "numbers = [3, 4]\n"
        "number = 7\n"
        "SPAWN snapshot:\n"
        "    CHANNEL READ snapshot_gate released\n"
        "    CHANNEL WRITE snapshot_values text_equal(word, '🦉שלום')\n"
        "    CHANNEL WRITE snapshot_values numbers[0]\n"
        "    CHANNEL WRITE snapshot_values number\n"
        "    word = 'child'\n"
        "    numbers[1] = 99\n"
        "    number = -1\n"
        "    CHANNEL WRITE snapshot_values text_equal(word, 'child')\n"
        "word = 'parent'\n"
        "numbers[0] = 20\n"
        "number = 80\n"
        "CHANNEL WRITE snapshot_gate 1\n"
        "AWAIT snapshot\n"
        "CHANNEL WRITE snapshot_values text_equal(word, 'parent')\n"
        "CHANNEL WRITE snapshot_values numbers[0]\n"
        "CHANNEL WRITE snapshot_values numbers[1]\n"
        "CHANNEL WRITE snapshot_values number\n";
    const float expected[] = {1, 3, 7, 1, 1, 20, 4, 80};
    for (int mode = 0; mode < 3; mode++) {
        am_init(); /* No persistent setters: snapshot ordinary AML globals. */
        CHECK(run(mode, script) == 0);
        CHECK(am_spawn_count() == 0);
        CHECK(am_channel_depth("snapshot_values") == 8);
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            float value = -999;
            CHECK(am_channel_try_read("snapshot_values", &value) == 0);
            CHECK(value == expected[i]);
        }
        CHECK(am_get_var_text("word") == NULL);
    }

    am_init();
    CHECK(am_channel_create("c_snapshot_gate", 1) >= 0);
    CHECK(am_channel_create("c_snapshot_values", 3) >= 0);
    CHECK(am_set_var_text("word", "🦉שלום") == 0);
    float initial[] = {3, 4};
    CHECK(am_set_var_array("numbers", initial, 2) == 0);
    CHECK(am_exec("number = 7\n") == 0);
    CHECK(am_spawn_launch("c_snapshot",
        "CHANNEL READ c_snapshot_gate released\n"
        "CHANNEL WRITE c_snapshot_values text_equal(word, '🦉שלום')\n"
        "CHANNEL WRITE c_snapshot_values numbers[0]\n"
        "CHANNEL WRITE c_snapshot_values number\n"
        "word = 'child'\nnumbers[1] = 99\nnumber = -1\n") == 0);
    CHECK(am_set_var_text("word", "parent") == 0);
    float replacement[] = {20, 4};
    CHECK(am_set_var_array("numbers", replacement, 2) == 0);
    CHECK(am_exec("number = 80\n") == 0);
    CHECK(am_channel_write("c_snapshot_gate", 1) == 0);
    CHECK(am_spawn_await("c_snapshot") == 0);
    const float c_expected[] = {1, 3, 7};
    for (size_t i = 0; i < sizeof(c_expected) / sizeof(c_expected[0]); i++) {
        float value = -999;
        CHECK(am_channel_try_read("c_snapshot_values", &value) == 0);
        CHECK(value == c_expected[i]);
    }
    text_equals("word", "parent");
    int len = 0;
    const float* numbers = am_get_var_array("numbers", &len);
    CHECK(numbers && len == 2 && numbers[0] == 20 && numbers[1] == 4);
    CHECK(am_get_var_float("number") == 80);
    am_persistent_clear();
    am_channel_close_all();
    puts("PASS async snapshots: AML globals in all modes and C persistent globals survive parent/child rebinds");
#else
    puts("SKIP async snapshots: AM_ASYNC_DISABLED");
#endif
}

#ifndef AM_ASYNC_DISABLED
static void* isolated_text_thread(void* opaque) {
    int* result = (int*)opaque;
    *result = am_get_var_text("thread_word") == NULL &&
              am_set_var_text("thread_word", "worker") == 0;
    const char* value = am_get_var_text("thread_word");
    *result = *result && value && !strcmp(value, "worker");
    am_persistent_clear();
    return NULL;
}
#endif

static void test_thread_local_persistence(void) {
#ifndef AM_ASYNC_DISABLED
    am_init();
    CHECK(am_set_var_text("thread_word", "parent") == 0);
    pthread_t worker;
    int result = 0;
    CHECK(pthread_create(&worker, NULL, isolated_text_thread, &result) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(result == 1);
    text_equals("thread_word", "parent");
    am_persistent_clear();
    puts("PASS persistent C API: independent host threads own independent text stores");
#else
    puts("SKIP thread-local persistence: AM_ASYNC_DISABLED");
#endif
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long len = ftell(file);
    if (len < 0 || len > 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char* source = (char*)malloc((size_t)len + 1);
    if (!source) { fclose(file); return NULL; }
    size_t read = fread(source, 1, (size_t)len, file);
    fclose(file);
    if (read != (size_t)len) { free(source); return NULL; }
    source[len] = 0;
    return source;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--fixture")) {
        fputs(program, stdout); fputs(printed, stdout); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--expected")) {
        fputs(expected_output, stdout); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(bad) / sizeof(bad[0])); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int n = atoi(argv[2]);
        if (n < 0 || (size_t)n >= sizeof(bad) / sizeof(bad[0])) return 2;
        fputs(bad[n].source, stdout);
        fputs("PRINT 'WRONG_CONTINUATION'\n", stdout);
        return 0;
    }
    am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]);
        if (mode < 0 || mode > 2) return 2;
        char* source = read_source(argv[3]);
        if (!source) return 2;
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "text runtime: %s\n", am_get_error());
        free(source);
        return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) {
        test_program(mode);
        test_failures(mode);
        test_refused_effects(mode);
    }
    test_persistent();
    test_async_snapshots();
    test_async_ownership();
    test_thread_local_persistence();
    printf("AML_TEXT_RUNTIME_OK %d checks\n", checks);
    return 0;
}
