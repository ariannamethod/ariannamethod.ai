/* Text-list values through interpreter, resumable, bytecode and CLI fixtures. */
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
        fprintf(stderr, "FAIL list runtime line %d: %s; AML: %s\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static const char* program =
    "def identity(value):\n    return value\n"
    "def fresh(word):\n    value = list_new()\n    list_push(value, word)\n    return value\n"
    "def visit(words, count):\n"
    "    if count <= 0:\n        return words\n"
    "    list_push(words, text_from_codepoint(96 + count))\n"
    "    return visit(words, count - 1)\n"
    "def local_copy(words):\n"
    "    local = words\n    list_set(local, 0, 'local')\n    return local\n"
    "def change(words, word):\n    list_push(words, word)\n    return words\n"
    "def gamma_once(events):\n    list_push(events, 'g')\n    return 0\n"
    "def array_once(events):\n    list_push(events, 's')\n    return [7, 8]\n"
    "empty = list_new()\n"
    "words = fresh('owl')\n"
    "added = list_push(words, 'Привет')\n"
    "list_push(words, 'שלום')\n"
    "copy = words\n"
    "list_set(copy, 0, 'changed')\n"
    "list_push(copy, '🦉')\n"
    "saved = list_get(words, 0)\n"
    "stored = list_set(words, -1, 'Café')\n"
    "alias = words\n"
    "alias = alias\n"
    "list_set(alias, 1, 'rebuilt')\n"
    "recursive = visit(words, 3)\n"
    "list_set(recursive, -1, 'tail')\n"
    "local = local_copy(words)\n"
    "returned = change(words, 'tail')\n"
    "list_set(returned, 0, 'return')\n"
    "part = list_slice(words, -3, list_len(words))\n"
    "bounded = list_slice(words, -2147483648, 2147483520)\n"
    "backwards = list_slice(words, 3, 1)\n"
    "cloned = list_clone(words)\n"
    "list_set(cloned, 0, 'clone')\n"
    "nested = fresh(text_concat(identity('a, ) ] # = \"'), identity(\"b, [ '\")))\n"
    "list_push(nested, 'quoted#') # outside comment\n"
    "nested_word = list_get(identity(list_clone(words)), text_len(']') - 1)\n"
    "replaced = fresh('old')\n"
    "retained = list_get(replaced, 0)\n"
    "list_set(replaced, 0, 'new')\n"
    "replaced = 7\n"
    "replaced = [7, 8]\n"
    "replaced = 'text'\n"
    "replaced = fresh('last')\n"
    "special = list_new()\n"
    "list_push(special, '')\n"
    "list_push(special, 'é')\n"
    "list_push(special, 'é')\n"
    "list_push(special, '🦉')\n"
    "list_push(special, 'one\\n\\r\\t\\\\\\\"\\\'')\n"
    "list_push(special, text_from_codepoint(1))\n"
    "list_push(special, text_from_codepoint(31))\n"
    "list_push(special, text_from_codepoint(127))\n"
    "sorted_special = list_sorted(identity(special))\n"
    "list_sorted(special) # discarded sorted result owns its references\n"
    "sorted_words = list_sorted(words)\n"
    "sorted_copy = list_sorted(words)\n"
    "list_set(sorted_copy, 0, 'changed_sorted')\n"
    "sorted_empty = list_sorted(empty)\n"
    "sorted_one = list_sorted(fresh('only'))\n"
    "duplicates = list_new()\n"
    "list_push(duplicates, 'é')\nlist_push(duplicates, '')\nlist_push(duplicates, 'é')\n"
    "list_push(duplicates, 'é')\n"
    "sorted_duplicates = list_sorted(duplicates)\n"
    "length = list_len(words)\n"
    "first = list_find(words, 'owl')\n"
    "missing = list_find(words, 'missing')\n"
    "composed = list_find(special, 'é')\n"
    "combining = list_find(special, 'é')\n"
    "same = text_equal(list_get(words, -1), 'tail')\n"
    "numeric = [7, 8]\n"
    "default_norm = layernorm(numeric)\n"
    "optional_norm = layernorm(numeric, 0, optional_unknown)\n"
    "default_seqnorm = seq_layernorm(numeric, 0, 0, 1, 2)\n"
    "optional_seqnorm = seq_layernorm(numeric, 0, optional_unknown, 1, 2)\n"
    "default_bias = spa_connectedness(numeric, 1, 2)\n"
    "optional_bias = spa_connectedness(numeric, 1, 2, optional_unknown)\n"
    "events = list_new()\n"
    "once_norm = layernorm(numeric, gamma_once(events), 0)\n"
    "once_sum = sum(array_once(events))\n"
    "flow = list_new()\n"
    "if 1:\n    if 0:\n        list_push(flow, 'wrong_inner')\n"
    "else:\n    list_push(flow, 'wrong_outer')\n"
    "list_push(flow, 'after')\n"
    "i = 0\nwhile i < 3:\n"
    "    if i < 2:\n"
    "        if i == 0:\n            list_push(flow, 'zero')\n"
    "        else:\n            list_push(flow, 'one')\n"
    "    else:\n        list_push(flow, 'two')\n"
    "    i = i + 1\n"
    "if 0:\n    if 1:\n        list_push(flow, 'wrong_skipped')\n"
    "    else:\n        list_push(flow, 'wrong_skipped_else')\n"
    "else:\n    list_push(flow, 'outer')\n"
    "done = 1\n";

static const char* printed =
    "PRINT empty\nPRINT words\nPRINT copy\nPRINT alias\n"
    "PRINT recursive\nPRINT local\nPRINT returned\nPRINT part\n"
    "PRINT bounded\nPRINT backwards\nPRINT cloned\nPRINT nested\n"
    "PRINT replaced\nPRINT special\nPRINT saved\nPRINT stored\n"
    "PRINT retained\nPRINT nested_word\nPRINT added\nPRINT length\n"
    "PRINT first\nPRINT missing\nPRINT composed\nPRINT combining\nPRINT same\n"
    "PRINT 'x=add(words,copy)'\nPRINT flow\n"
    "if 1:\n    if 0:\n        ECHO WRONG_INNER\n"
    "else:\n    ECHO WRONG_OUTER\nECHO GOOD\nPRINT events\nPRINT once_sum\n"
    "PRINT sorted_special\nPRINT sorted_words\nPRINT sorted_copy\n"
    "PRINT sorted_empty\nPRINT sorted_one\nPRINT sorted_duplicates\n";

static const char* expected_output =
    "[]\n"
    "[\"owl\", \"Привет\", \"Café\", \"c\", \"b\", \"a\", \"tail\"]\n"
    "[\"changed\", \"Привет\", \"שלום\", \"🦉\"]\n"
    "[\"owl\", \"rebuilt\", \"Café\"]\n"
    "[\"owl\", \"Привет\", \"Café\", \"c\", \"b\", \"tail\"]\n"
    "[\"local\", \"Привет\", \"Café\", \"c\", \"b\", \"a\"]\n"
    "[\"return\", \"Привет\", \"Café\", \"c\", \"b\", \"a\", \"tail\"]\n"
    "[\"b\", \"a\", \"tail\"]\n"
    "[\"owl\", \"Привет\", \"Café\", \"c\", \"b\", \"a\", \"tail\"]\n"
    "[]\n"
    "[\"clone\", \"Привет\", \"Café\", \"c\", \"b\", \"a\", \"tail\"]\n"
    "[\"a, ) ] # = \\\"b, [ '\", \"quoted#\"]\n"
    "[\"last\"]\n"
    "[\"\", \"é\", \"é\", \"🦉\", \"one\\n\\r\\t\\\\\\\"'\", \"\\u0001\", \"\\u001f\", \"\x7f\"]\n"
    "owl\nCafé\nold\nowl\n2\n7\n0\n-1\n1\n2\n1\nx=add(words,copy)\n"
    "[\"after\", \"zero\", \"one\", \"two\", \"outer\"]\n[AML] GOOD\n"
    "[\"g\", \"s\"]\n15\n"
    "[\"\", \"\\u0001\", \"\\u001f\", \"é\", \"one\\n\\r\\t\\\\\\\"'\", \"\x7f\", \"é\", \"🦉\"]\n"
    "[\"Café\", \"a\", \"b\", \"c\", \"owl\", \"tail\", \"Привет\"]\n"
    "[\"changed_sorted\", \"a\", \"b\", \"c\", \"owl\", \"tail\", \"Привет\"]\n"
    "[]\n[\"only\"]\n[\"\", \"é\", \"é\", \"é\"]\n";

static const char* bad[] = {
    "x = list_new(1)", "x = list_len()", "x = list_get(words, 0, 1)",
    "x = list_push(words)", "x = list_set(words, 0)",
    "x = list_find(words, 'x', 1)", "x = list_slice(words, 0)",
    "x = list_clone(words, 1)", "x = list_len(words,)",
    "x = list_sorted()", "x = list_sorted(words, 1)", "x = list_sorted(1)",
    "x = list_sorted('owl')", "x = list_sorted(a)", "list_sorted(1)",
    "x = list_len('owl')", "x = list_get(a, 0)", "x = list_push(words, 1)",
    "x = list_push(words, list_new())", "x = list_set(words, 0, a)",
    "x = list_find(words, 1)", "x = list_slice(words, '0', 1)", "x = list_clone(1)",
    "x = list_get(words, 0.5)", "x = list_set(words, 1e39, 'x')",
    "x = list_slice(words, 0, 2147483648)", "x = list_slice(words, -2147483904, 1)",
    "x = list_get(empty, 0)", "x = list_get(words, -3)",
    "x = list_set(words, 2, 'x')", "x = list_set(words, -3, 'x')",
    "x = list_set(words, 0.5, 'x')", "x = text_len(words)",
    "x = words + 1", "x = len(words)", "x = sum(words)",
    "x = min(words, 1)", "x = max(1, words)", "x = zeros(words)",
    "x = dot(words, a)", "x = rows(words)", "x = cols(words)",
    "x = add(words, a)", "x = add(a, words)", "x = mul(words, a)",
    "x = silu(words)", "x = matvec(w, words)", "x = softmax(words)",
    "x = seq_matvec(words, a, 1)", "x = seq_matvec(w, a, words)",
    "x = seq_rmsnorm(words, 1, 2)", "x = seq_rmsnorm(a, words, 2)",
    "a[words] = 99", "a[0] = words", "x = a[words]", "TENSION words",
    "if words:\n    sentinel = 1", "while words:\n    sentinel = 1",
    "x = list_push(words, text_from_codepoint(0))",
    "x = layernorm(a, (words), 0)", "x = layernorm(a, 0, (words))",
    "x = layernorm(a, list_new(), 0)", "x = layernorm(a, 0, list_value())",
    "x = seq_layernorm(a, list_value(), 0, 1, 2)",
    "x = seq_layernorm(a, 0, list_new(), 1, 2)",
    "x = spa_connectedness(a, 1, 2, list_new())",
    "x = spa_connectedness(a, 1, 2, list_value())",
    "PRINT len(list_new())", "PRINT sum((words))", "PRINT rows(list_new())",
    "PRINT dot(a, list_new())", "PRINT cols((words))",
};

static const char* bad_prefix =
    "def list_value():\n    return list_new()\n"
    "sentinel = 0\nwords = list_new()\nlist_push(words, 'owl')\n"
    "list_push(words, 'tail')\nempty = list_new()\na = [7, 8]\n"
    "w = matrix_zeros(1, 2)\nTENSION 0.75\nTAPE START\nTAPE PARAM a\n";

static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program_handle = am_program_open(source);
        if (!program_handle) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(program_handle, 1)) CHECK(--budget > 0);
        CHECK(am_program_remaining(program_handle) == 0);
        return am_program_close(program_handle);
    }
    void* compiled = am_compile(source);
    if (!compiled) return 1;
    int rc = am_exec_compiled(compiled);
    am_free_compiled(compiled);
    return rc;
}

static void item_equals(const AM_List* list, int index, const char* expected) {
    AM_String* item = am_list_get(list, index);
    CHECK(item && !strcmp(item->data, expected));
    am_string_free(item);
}

static void list_equals(const char* name, int count, const char* const* expected) {
    const AM_List* list = am_get_var_list(name);
    CHECK(list && list->len == count);
    for (int i = 0; i < count; i++) item_equals(list, i, expected[i]);
}

static void text_equals(const char* name, const char* expected) {
    const char* text = am_get_var_text(name);
    CHECK(text && !strcmp(text, expected));
}

static void test_program(int mode) {
    am_persistent_clear();
    am_persistent_mode(1);
    CHECK(run(mode, program) == 0);
    const char* words[] = {"owl", "Привет", "Café", "c", "b", "a", "tail"};
    const char* copy[] = {"changed", "Привет", "שלום", "🦉"};
    const char* alias[] = {"owl", "rebuilt", "Café"};
    const char* recursive[] = {"owl", "Привет", "Café", "c", "b", "tail"};
    const char* local[] = {"local", "Привет", "Café", "c", "b", "a"};
    const char* returned[] = {"return", "Привет", "Café", "c", "b", "a", "tail"};
    const char* nested[] = {"a, ) ] # = \"b, [ '", "quoted#"};
    const char* last[] = {"last"};
    const char* flow[] = {"after", "zero", "one", "two", "outer"};
    const char* events[] = {"g", "s"};
    const char* sorted_words[] = {"Café", "a", "b", "c", "owl", "tail", "Привет"};
    const char* sorted_copy[] = {"changed_sorted", "a", "b", "c", "owl", "tail", "Привет"};
    const char* sorted_duplicates[] = {"", "é", "é", "é"};
    const char* duplicates[] = {"é", "", "é", "é"};
    const char* sorted_one[] = {"only"};
    list_equals("words", 7, words);
    list_equals("copy", 4, copy);
    list_equals("alias", 3, alias);
    list_equals("recursive", 6, recursive);
    list_equals("local", 6, local);
    list_equals("returned", 7, returned);
    list_equals("part", 3, words + 4);
    list_equals("bounded", 7, words);
    list_equals("empty", 0, NULL);
    list_equals("backwards", 0, NULL);
    list_equals("nested", 2, nested);
    list_equals("replaced", 1, last);
    list_equals("flow", 5, flow);
    list_equals("events", 2, events);
    list_equals("sorted_words", 7, sorted_words);
    list_equals("sorted_copy", 7, sorted_copy);
    list_equals("sorted_duplicates", 4, sorted_duplicates);
    list_equals("duplicates", 4, duplicates);
    list_equals("sorted_empty", 0, NULL);
    list_equals("sorted_one", 1, sorted_one);
    text_equals("saved", "owl");
    text_equals("stored", "Café");
    text_equals("retained", "old");
    text_equals("nested_word", "owl");
    CHECK(am_get_var_float("added") == 2 && am_get_var_float("length") == 7);
    CHECK(am_get_var_float("first") == 0 && am_get_var_float("missing") == -1);
    CHECK(am_get_var_float("composed") == 1 && am_get_var_float("combining") == 2);
    CHECK(am_get_var_float("same") == 1 && am_get_var_float("done") == 1);
    CHECK(am_get_var_float("once_sum") == 15);
    const char* base_names[] = {"default_norm", "default_seqnorm", "default_bias", "default_norm"};
    const char* optional_names[] = {"optional_norm", "optional_seqnorm", "optional_bias", "once_norm"};
    for (int i = 0; i < 4; i++) {
        int base_len = 0, optional_len = 0;
        const float* base = am_get_var_array(base_names[i], &base_len);
        const float* optional = am_get_var_array(optional_names[i], &optional_len);
        CHECK(base && optional && base_len > 0 && optional_len == base_len);
        for (int j = 0; j < base_len; j++) CHECK(base[j] == optional[j]);
    }
    CHECK(am_get_var_array("words", NULL) == NULL && am_get_var_text("words") == NULL);
    CHECK(am_get_var_list("saved") == NULL && am_get_var_list("length") == NULL);
    am_persistent_clear();
    CHECK(am_get_var_list("words") == NULL);
    printf("PASS list values mode=%d: typed recursion, shared params, copied assignments/returns, immutable items\n", mode);
}

static void test_errors(int mode) {
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char source[1024];
        snprintf(source, sizeof(source), "%s%s\nsentinel = 1\n", bad_prefix, bad[i]);
        am_persistent_clear();
        am_persistent_mode(1);
        int rc = run(mode, source);
        if (!rc || !am_get_error()[0])
            fprintf(stderr, "bad list fixture=%zu mode=%d accepted: %s\n", i, mode, bad[i]);
        CHECK(rc != 0 && am_get_error()[0] != 0);
        CHECK(am_get_var_float("sentinel") == 0);
        const char* old[] = {"owl", "tail"};
        list_equals("words", 2, old);
        int len = 0;
        const float* array = am_get_var_array("a", &len);
        CHECK(array && len == 2 && array[0] == 7 && array[1] == 8);
        CHECK(am_get_state()->tension == 0.75f);
        CHECK(am_tape_get()->count == 1 && am_tape_get()->n_params == 1);
        am_tape_clear();
    }
    am_persistent_clear();
    printf("PASS list errors mode=%d: %zu invalid calls/types/indices preserve prior values and stop execution\n",
           mode, sizeof(bad) / sizeof(bad[0]));
}

static void test_persistent(void) {
    am_persistent_mode(0);
    AM_List* input = am_list_new();
    AM_String* old = am_string_new("before");
    AM_String* replacement = am_string_new("after");
    CHECK(input && old && replacement && am_list_push(input, old) == 1);
    CHECK(am_set_var_list("words", input) == 0);
    CHECK(am_get_var_list("words") != input);
    CHECK(am_list_set(input, 0, replacement) == 0);
    const char* before[] = {"before"};
    list_equals("words", 1, before);
    CHECK(am_exec("saved = list_get(words, 0)\ncopy = words\nlist_set(words, 0, 'current')\n") == 0);
    list_equals("copy", 1, before);
    text_equals("saved", "before");
    CHECK(am_set_var_list(NULL, input) != 0 && am_set_var_list("", input) != 0);
    CHECK(am_set_var_list("9words", input) != 0 && am_set_var_list("two words", input) != 0);
    CHECK(am_set_var_list("words", NULL) != 0);
    char name[AML_MAX_NAME + 1];
    memset(name, 'a', sizeof(name) - 1); name[sizeof(name) - 1] = 0;
    CHECK(am_set_var_list(name, input) != 0);
    const char* current[] = {"current"};
    list_equals("words", 1, current);
    CHECK(am_get_var_list(NULL) == NULL && am_get_var_list("absent") == NULL);
    CHECK(am_exec("words = [1, 2]\n") == 0 && am_get_var_list("words") == NULL);
    CHECK(am_set_var_list("words", input) == 0 && am_get_var_array("words", NULL) == NULL);
    CHECK(am_set_var_text("words", "text") == 0 && am_get_var_list("words") == NULL);
    CHECK(am_set_var_list("words", input) == 0 && am_get_var_text("words") == NULL);
    CHECK(am_exec("words = 7\n") == 0 && am_get_var_list("words") == NULL);
    CHECK(am_set_var_list("words", input) == 0);
    am_list_free(input);
    am_string_free(old);
    am_string_free(replacement);
    const char* after[] = {"after"};
    list_equals("words", 1, after);
    am_persistent_mode(0);
    CHECK(am_get_var_list("words") == NULL);
    CHECK(am_exec("temporary = list_new()\nlist_push(temporary, 'discard')\n") == 0);
    CHECK(am_get_var_list("temporary") == NULL);
    puts("PASS list C store: cloned inputs, replacement across all types, identifier checks and cleanup");
}

static void test_async_snapshots(void) {
#ifndef AM_ASYNC_DISABLED
    const char* source =
        "CHANNEL CREATE list_gate 1\nCHANNEL CREATE list_values 4\n"
        "words = list_new()\nlist_push(words, 'before')\n"
        "SPAWN list_snapshot:\n"
        "    CHANNEL READ list_gate released\n"
        "    CHANNEL WRITE list_values text_equal(list_get(words, 0), 'before')\n"
        "    list_set(words, 0, 'child')\n"
        "    list_push(words, 'private')\n"
        "    CHANNEL WRITE list_values list_len(words)\n"
        "list_set(words, 0, 'parent')\n"
        "CHANNEL WRITE list_gate 1\nAWAIT list_snapshot\n"
        "CHANNEL WRITE list_values text_equal(list_get(words, 0), 'parent')\n"
        "CHANNEL WRITE list_values list_len(words)\n";
    const float expected[] = {1, 2, 1, 1};
    for (int mode = 0; mode < 3; mode++) {
        am_init();
        CHECK(run(mode, source) == 0);
        CHECK(am_spawn_count() == 0 && am_channel_depth("list_values") == 4);
        for (int i = 0; i < 4; i++) {
            float value = -1;
            CHECK(am_channel_try_read("list_values", &value) == 0 && value == expected[i]);
        }
    }
    am_init();
    AM_List* input = am_list_new();
    AM_String* item = am_string_new("before");
    CHECK(input && item && am_list_push(input, item) == 1);
    CHECK(am_set_var_list("words", input) == 0);
    am_list_free(input); am_string_free(item);
    CHECK(am_channel_create("list_gate", 1) >= 0 && am_channel_create("list_values", 1) >= 0);
    CHECK(am_spawn_launch("c_list_snapshot",
        "CHANNEL READ list_gate released\n"
        "CHANNEL WRITE list_values text_equal(list_get(words, 0), 'before')\n"
        "list_set(words, 0, 'child')\n") >= 0);
    CHECK(am_exec("list_set(words, 0, 'parent')\n") == 0);
    CHECK(am_channel_write("list_gate", 1) == 0 && am_spawn_await("c_list_snapshot") == 0);
    float value = -1;
    CHECK(am_channel_try_read("list_values", &value) == 0 && value == 1);
    const char* parent[] = {"parent"};
    list_equals("words", 1, parent);
    am_persistent_clear();
    am_channel_close_all();
    puts("PASS list snapshots: channel barriers prove launch-time copies and isolated mutation");
#else
    puts("SKIP list snapshots: AM_ASYNC_DISABLED");
#endif
}

#ifndef AM_ASYNC_DISABLED
static void* isolated_list_thread(void* opaque) {
    int* result = (int*)opaque;
    AM_List* list = am_list_new();
    AM_String* item = am_string_new("worker");
    *result = am_get_var_list("words") == NULL && list && item &&
              am_list_push(list, item) == 1 && am_set_var_list("words", list) == 0;
    am_list_free(list); am_string_free(item);
    const AM_List* stored = am_get_var_list("words");
    *result = *result && stored && stored->len == 1 && !strcmp(stored->items[0]->data, "worker");
    am_persistent_clear();
    return NULL;
}
#endif

static void test_async_stress(void) {
#ifndef AM_ASYNC_DISABLED
    am_init();
    CHECK(am_exec("words = list_new()\n") == 0);
    AM_List* input = am_list_new();
    AM_String* item = am_string_new("🦉שלום");
    CHECK(input && item && am_list_push(input, item) == 1);
    CHECK(am_set_var_list("words", input) == 0);
    am_list_free(input); am_string_free(item);
    pthread_t thread;
    int result = 0;
    CHECK(pthread_create(&thread, NULL, isolated_list_thread, &result) == 0);
    CHECK(pthread_join(thread, NULL) == 0 && result == 1);
    const char* expected[] = {"🦉שלום"};
    list_equals("words", 1, expected);
    const char* source =
        "SPAWN list_a:\n"
        "    i = 0\n    while i < 10000:\n"
        "        held = list_get(words, 0)\n        list_set(words, 0, held)\n"
        "        copy = words\n        i = i + 1\n"
        "SPAWN list_b:\n"
        "    i = 0\n    while i < 10000:\n"
        "        held = list_get(words, 0)\n        list_set(words, 0, held)\n"
        "        copy = list_slice(words, 0, 1)\n        i = i + 1\n"
        "AWAIT\n";
    for (int round = 0; round < 6; round++) {
        CHECK(am_exec(source) == 0 && am_spawn_count() == 0);
        list_equals("words", 1, expected);
        CHECK(am_get_var_list("copy") == NULL && am_get_var_text("held") == NULL);
    }
    am_persistent_clear();
    puts("PASS list concurrency: TLS host store, 12 workers and 120000 get/set/container-copy cycles");
#else
    puts("SKIP list concurrency: AM_ASYNC_DISABLED");
#endif
}

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char* source = (char*)malloc((size_t)length + 1);
    if (!source) { fclose(file); return NULL; }
    size_t count = fread(source, 1, (size_t)length, file);
    fclose(file);
    if (count != (size_t)length) { free(source); return NULL; }
    source[length] = 0;
    return source;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--fixture")) {
        fputs(program, stdout); fputs(printed, stdout); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--expected")) { fputs(expected_output, stdout); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(bad) / sizeof(bad[0])); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int index = atoi(argv[2]);
        if (index < 0 || (size_t)index >= sizeof(bad) / sizeof(bad[0])) return 2;
        fputs(bad_prefix, stdout); puts(bad[index]); puts("PRINT 'WRONG_CONTINUATION'"); return 0;
    }
    am_init();
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]);
        char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "list runtime: %s\n", am_get_error());
        free(source);
        return rc;
    }
    CHECK(argc == 1);
    for (int mode = 0; mode < 3; mode++) { test_program(mode); test_errors(mode); }
    test_persistent();
    test_async_snapshots();
    test_async_stress();
    printf("AML_LIST_RUNTIME_OK %d checks\n", checks);
    return 0;
}
