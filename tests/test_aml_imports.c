/* IMPORT preparation, shared scope, source origins, and all C execution APIs.
 * Run with a fresh writable fixture directory as argv[1]. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static char root[512], elsewhere[1024], source_path[1024];

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL imports line %d: %s (%s)\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

static void put_file(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    size_t size = strlen(text);
    CHECK(fwrite(text, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void tension_is(float expected) {
    CHECK(fabsf(am_get_state()->tension - expected) < 1e-6f);
}

static void run_paths(const char* script, float expected) {
    CHECK(chdir(root) == 0);
    am_init();
    CHECK(am_exec_source(script, source_path) == 0);
    tension_is(expected);

    am_init();
    void* program = am_program_open(script);
    CHECK(program != NULL);
    CHECK(chdir(elsewhere) == 0);
    int steps = 0;
    while (!am_program_step(program, 1)) CHECK(++steps <= AML_MAX_LINES);
    CHECK(am_program_remaining(program) == 0);
    CHECK(am_program_step(program, 1) == 1);
    CHECK(am_program_close(program) == 0);
    tension_is(expected);

    CHECK(chdir(root) == 0);
    void* compiled = am_compile(script);
    CHECK(compiled != NULL);
    CHECK(chdir(elsewhere) == 0);
    for (int repetition = 0; repetition < 2; repetition++) {
        am_init();
        CHECK(am_exec_compiled(compiled) == 0);
        tension_is(expected);
    }
    am_free_compiled(compiled);
    CHECK(chdir(root) == 0);
}

static void reject_paths(const char* script, const char* diagnostic) {
    CHECK(chdir(root) == 0);
    am_init();
    CHECK(am_exec("TENSION 0.125\n") == 0);
    CHECK(am_exec_source(script, source_path) != 0);
    CHECK(strstr(am_get_error(), diagnostic) != NULL);
    tension_is(0.125f);
    CHECK(am_program_open(script) == NULL);
    CHECK(strstr(am_get_error(), diagnostic) != NULL);
    tension_is(0.125f);
    CHECK(am_compile(script) == NULL);
    CHECK(strstr(am_get_error(), diagnostic) != NULL);
    tension_is(0.125f);
}

static void test_shared_graph(void) {
    CHECK(mkdir("tree", 0700) == 0);
    run_paths("IMPORT CODES_RIC\nTENSION 0.125\n", 0.125f);
    CHECK(am_pack_enabled(AM_PACK_CODES_RIC));
    run_paths("if 1:\n    IMPORT CODES/RIC\nIMPORT DARKMATTER\n"
              "IMPORT NOTORCH\nTENSION 0.125\n", 0.125f);
    CHECK(am_pack_enabled(AM_PACK_CODES_RIC));
    put_file("tree/empty.aml", "");
    run_paths("IMPORT \"tree/empty.aml\"\nIMPORT \"tree/empty.aml\"\n"
              "TENSION 0.125\n", 0.125f);
    put_file("tree/common.aml",
        "import_runs = import_runs + 1\n"
        "visit_order = visit_order * 10 + 1\n"
        "shared_gain = 0.1\n"
        "def leaf_gain(x):\n    return x * shared_gain\n");
    CHECK(symlink("common.aml", "tree/common-link.aml") == 0);
    put_file("tree/left.aml",
        "IMPORT \"common.aml\"\n"
        "visit_order = visit_order * 10 + 2\n"
        "def left_gain(x):\n    return leaf_gain(x) + 0.2\n");
    put_file("tree/right.aml",
        "IMPORT \"./common-link.aml\" # canonical duplicate\n"
        "visit_order = visit_order * 10 + 3\n"
        "def right_gain(x):\n    return left_gain(x) + 0.3\n");
    const char* script =
        "import_runs = 0\nvisit_order = 0\n"
        "IMPORT \"tree/left.aml\"\nIMPORT \"tree/right.aml\"\n"
        "IMPORT \"tree/../tree/common.aml\"\n"
        "result = right_gain(2)\n"
        "if import_runs == 1 and visit_order == 123:\n    TENSION result\n"
        "else:\n    TENSION 0.99\n";
    run_paths(script, 0.7f);
    put_file("graph.aml", script);
    puts("PASS imports: diamond/symlink deduplication, ordered globals, shared functions");
}

static void test_origins(void) {
    put_file("tree/origins.aml",
        "def import_include():\n    INCLUDE \"child.aml\"\n"
        "def import_worker():\n"
        "    SPAWN from_import:\n        INCLUDE \"worker-child.aml\"\n"
        "    AWAIT from_import\n");
    put_file("tree/child.aml", "TENSION 0.625\n");
    put_file("tree/worker-child.aml", "CHANNEL WRITE import_events 0.75\n");
    put_file("parent-child.aml", "TENSION 0.875\n");
    run_paths("IMPORT \"tree/origins.aml\"\nimport_include()\n", 0.625f);
    run_paths("IMPORT \"tree/origins.aml\"\nimport_include()\n"
              "INCLUDE \"parent-child.aml\"\n", 0.875f);
    run_paths("IMPORT \"tree/origins.aml\"\nCHANNEL CREATE import_events 2\n"
              "import_worker()\nCHANNEL READ import_events observed\n"
              "TENSION observed\n", 0.75f);

    put_file("tree/broken.aml", "def broken_import():\n    INCLUDE absent.aml\n");
    am_init();
    CHECK(am_exec_source("IMPORT \"tree/broken.aml\"\nbroken_import()\n", source_path) != 0);
    CHECK(strstr(am_get_error(), "tree/broken.aml:2:") != NULL);
    CHECK(strstr(am_get_error(), "absent.aml") != NULL);
    puts("PASS imports: function/worker INCLUDE origins, caller restoration, source diagnostics");
}

static void test_snapshot_and_control_flow(void) {
    put_file("tree/snapshot.aml",
        "def snapshot_gain():\n    return 0.25\n"
        "def dormant():\n    TENSION 0.99\n"
        "if 0:\n    TENSION 0.99\n"
        "rounds = 0\nwhile rounds < 3:\n    rounds = rounds + 1\n");
    const char* script = "IMPORT \"tree/snapshot.aml\"\n"
                         "TENSION snapshot_gain() + rounds * 0.1\n";
    am_init();
    void* program = am_program_open(script);
    CHECK(program != NULL);
    void* compiled = am_compile(script);
    CHECK(compiled != NULL);
    put_file("tree/snapshot.aml", "TENSION 0.99\n");
    CHECK(unlink("tree/snapshot.aml") == 0);
    CHECK(chdir(elsewhere) == 0);
    CHECK(am_program_step(program, 0) == 1);
    CHECK(am_program_close(program) == 0);
    tension_is(0.55f);
    for (int repetition = 0; repetition < 2; repetition++) {
        am_init();
        CHECK(am_exec_compiled(compiled) == 0);
        tension_is(0.55f);
    }
    am_free_compiled(compiled);
    CHECK(chdir(root) == 0);
    puts("PASS imports: prepared snapshot survives removal; bytecode def/if/while parity");
}

static void test_rejections(void) {
    reject_paths("TENSION 0.875\nIMPORT \"missing.aml\"\n", "cannot resolve source");
    reject_paths("TENSION 0.875\nIMPORT missing.aml\n", "quoted path");
    reject_paths("TENSION 0.875\nIMPORT \"\"\n", "invalid IMPORT path");
    reject_paths("TENSION 0.875\nIMPORT \"tree/common.aml\" junk\n", "unexpected text");
    reject_paths("TENSION 0.875\nif 1:\n    IMPORT \"tree/common.aml\"\n", "top-level");
    reject_paths("TENSION 0.875\nIMPORT \"tree\"\n", "not a regular file");
    put_file("tree/cycle-a.aml", "IMPORT \"cycle-b.aml\"\n");
    put_file("tree/cycle-b.aml", "IMPORT \"cycle-a.aml\"\n");
    reject_paths("TENSION 0.875\nIMPORT \"tree/cycle-a.aml\"\n", "IMPORT cycle");
    put_file("root-cycle.aml", "IMPORT \"root-cycle.aml\"\n");
    am_init();
    CHECK(am_exec_file("root-cycle.aml") != 0);
    CHECK(strstr(am_get_error(), "IMPORT cycle") != NULL);

    put_file("tree/duplicate.aml", "def duplicate():\n    return 1\n");
    reject_paths("TENSION 0.875\nIMPORT \"tree/duplicate.aml\"\n"
                 "def duplicate():\n    return 2\n", "duplicate function");
    reject_paths("TENSION 0.875\ndef TEXT_LEN(x):\n    return x\n", "intrinsic");
    reject_paths("TENSION 0.875\ndef GALVANIZE():\n    return 1\n", "duplicate function");
    reject_paths("TENSION 0.875\ndef bad(a, a):\n    return 1\n", "duplicate function parameter");
    reject_paths("TENSION 0.875\ndef bad(a b):\n    return 1\n", "parameter list");
    reject_paths("TENSION 0.875\ndef bad(a,b,c,d,e,f,g,h,i):\n    return 1\n", "parameter limit");
    reject_paths("TENSION 0.875\ndef bad()\n    return 1\n", "needs ':'");
    reject_paths("TENSION 0.875\ndef bad\n", "invalid function declaration");
    reject_paths("TENSION 0.875\ndef function_name_exceeding_31_bytes_x():\n    return 1\n", "overlong function name");

    FILE* file = fopen("tree/nul.aml", "wb");
    CHECK(file != NULL);
    CHECK(fwrite("a\0b", 1, 3, file) == 3);
    CHECK(fclose(file) == 0);
    reject_paths("TENSION 0.875\nIMPORT \"tree/nul.aml\"\n", "invalid IMPORT source read");
    puts("PASS imports: malformed paths/declarations, missing files, NUL, cycles and collisions reject before effects");
}

static void test_budgets(void) {
    char script[65536];
    strcpy(script, "TENSION 0.875\n");
    memset(script + strlen(script), 'x', AML_MAX_LINE_LEN);
    script[strlen("TENSION 0.875\n") + AML_MAX_LINE_LEN] = 0;
    reject_paths(script, "source line exceeds");

    size_t used = 0;
    for (int i = 0; i <= AML_MAX_LINES; i++)
        used += (size_t)snprintf(script + used, sizeof(script) - used, "TENSION 0.875\n");
    put_file("tree/too-many-lines.aml", script);
    reject_paths("TENSION 0.875\nIMPORT \"tree/too-many-lines.aml\"\n", "line limit");

    FILE* large = fopen("tree/too-large.aml", "wb");
    CHECK(large != NULL);
    CHECK(fseek(large, 1024 * 1024, SEEK_SET) == 0);
    CHECK(fputc('x', large) == 'x');
    CHECK(fclose(large) == 0);
    reject_paths("TENSION 0.875\nIMPORT \"tree/too-large.aml\"\n", "source exceeds 1 MiB");

    used = (size_t)snprintf(script, sizeof(script), "TENSION 0.875\n");
    for (int i = 0; i < AML_MAX_FUNCS; i++)
        used += (size_t)snprintf(script + used, sizeof(script) - used,
                                "def budget_%d():\n    return %d\n", i, i);
    put_file("tree/too-many-funcs.aml", script);
    reject_paths("TENSION 0.875\nIMPORT \"tree/too-many-funcs.aml\"\n", "function limit");

    for (int i = 0; i <= AML_MAX_IMPORT_DEPTH; i++) {
        char filename[80], content[80];
        snprintf(filename, sizeof(filename), "tree/depth-%d.aml", i);
        snprintf(content, sizeof(content), "IMPORT \"depth-%d.aml\"\n", i + 1);
        put_file(filename, content);
    }
    reject_paths("TENSION 0.875\nIMPORT \"tree/depth-0.aml\"\n", "depth limit");

    used = (size_t)snprintf(script, sizeof(script), "TENSION 0.875\n");
    for (int i = 0; i < AML_MAX_IMPORTS; i++) {
        char filename[80];
        snprintf(filename, sizeof(filename), "tree/source-%d.aml", i);
        put_file(filename, "");
        used += (size_t)snprintf(script + used, sizeof(script) - used,
                                "IMPORT \"%s\"\n", filename);
    }
    reject_paths(script, "source limit");

    CHECK(mkdir("long", 0700) == 0);
    char long_path[4096] = "long";
    const char* part = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    for (int i = 0; i < 4; i++) {
        char module[4096];
        CHECK(snprintf(module, sizeof(module), "%s/next.aml", long_path) < (int)sizeof(module));
        char content[100];
        snprintf(content, sizeof(content), "IMPORT \"%s/next.aml\"\n", part);
        put_file(module, content);
        strcat(long_path, "/"); strcat(long_path, part);
        CHECK(mkdir(long_path, 0700) == 0);
    }
    strcat(long_path, "/next.aml");
    put_file(long_path, "TENSION 0.875\n");
    reject_paths("TENSION 0.875\nIMPORT \"long/next.aml\"\n", "source path exceeds");
    puts("PASS imports: physical/expanded lines, function slots, depth, source count and canonical path bounds");
}

static void test_bytecode_types(void) {
    static const char* invalid[] = {
        "result = add(a, word)", "result = mul(word, a)", "result = silu(word)",
        "result = seq_embed(w, w, word, 1)", "result = seq_embed(w, w, a, word)",
        "result = seq_matvec(word, a, 1)", "result = seq_matvec(w, a, word)",
        "result = seq_rmsnorm(word, 1, 1)", "result = seq_rmsnorm(a, word, 1)",
        "result = multi_head_attention(word, a, a, 1, 1, 1)",
        "result = multi_head_attention(a, a, a, 1, 1, word)",
        "result = seq_cross_entropy(a, word, 1, 1)",
        "result = seq_cross_entropy(a, a, 1, word)",
        "TAPE APPLY_ACCUM word", "TAPE CLIP_GRADS word", "TAPE ADAMW_STEP word",
    };
    for (size_t n = 0; n < sizeof(invalid) / sizeof(invalid[0]); n++) {
        char script[512];
        snprintf(script, sizeof(script),
            "word = 'owl'\na = [1]\nw = matrix_zeros(1, 1)\nTENSION 0.125\n%s\nTENSION 0.99\n",
            invalid[n]);
        for (int mode = 0; mode < 3; mode++) {
            am_init();
            int rc;
            if (mode == 0) rc = am_exec(script);
            else if (mode == 1) {
                void* program = am_program_open(script);
                CHECK(program != NULL);
                CHECK(am_program_step(program, 0) == 1);
                rc = am_program_close(program);
            } else {
                void* compiled = am_compile(script);
                CHECK(compiled != NULL);
                rc = am_exec_compiled(compiled);
                am_free_compiled(compiled);
            }
            CHECK(rc != 0);
            CHECK(strstr(am_get_error(), "scalar expression") != NULL);
            tension_is(0.125f);
        }
    }
    run_paths(
        "def dimension():\n    TENSION tension + 0.1\n    return 1\n"
        "TENSION 0\na = [1, 2]\nb = [3, 4]\nc = add(a, b)\nd = mul(c, b)\n"
        "e = silu(a)\nw = matrix_zeros(2, 2)\nw[0] = 2\nw[3] = 3\n"
        "g = seq_matvec(w, a, 1)\nf = seq_rmsnorm(a, dimension(), 2)\n"
        "h = seq_rmsnorm(a, text_len(text_slice('abcdefghijklmnop', 0, 1)), 2)\n"
        "if g[0] != 2 or g[1] != 6 or d[0] != 12 or d[1] != 24:\n    TENSION 0.99\n",
        0.1f);
    run_paths(
        "a = [2]\nTAPE START\nTAPE PARAM a\nb = mul(a, a)\nTAPE BACKWARD b\n"
        "TAPE ADAMW_STEP 0.1 0 0.9 0.95\nTAPE CLEAR\nTENSION a[0] / 10\n",
        0.19f);
    puts("PASS bytecode: typed array/numeric argument errors, nested argument evaluation, literal optimizer operands");
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    CHECK(strlen(argv[1]) + 20 < sizeof(root));
    strcpy(root, argv[1]);
    snprintf(elsewhere, sizeof(elsewhere), "%s/elsewhere", root);
    snprintf(source_path, sizeof(source_path), "%s/main.aml", root);
    CHECK(chdir(root) == 0);
    CHECK(mkdir(elsewhere, 0700) == 0);
    test_shared_graph();
    test_origins();
    test_snapshot_and_control_flow();
    test_rejections();
    test_budgets();
    test_bytecode_types();
    printf("PASS IMPORT C API: %d checks\n", checks);
    return 0;
}
