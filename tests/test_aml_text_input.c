/* Unicode properties and line input: direct API, failures, and three hosts. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "fixtures/text_input_fixture.h"

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL text input line %d: %s (%s)\n", __LINE__, #c, am_get_error()); \
    exit(1); } } while (0)

#ifdef AML_INPUT_ALLOC_WRAP
static int allocation_budget = -1, tracked_count;
static void* tracked[128];
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
static void track(void* p) {
    if (p && allocation_budget >= 0) {
        CHECK(tracked_count < 128); tracked[tracked_count++] = p;
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

static FILE* input(const void* bytes, size_t n) {
    FILE* f = tmpfile(); CHECK(f);
    CHECK(fwrite(bytes, 1, n, f) == n && fseek(f, 0, SEEK_SET) == 0);
    return f;
}
static void expect_line(const AM_List* list, const char* bytes, size_t n) {
    CHECK(list && list->len == 1 && list->items[0]);
    CHECK(list->items[0]->byte_len == (int)n);
    CHECK(memcmp(list->items[0]->data, bytes, n) == 0);
    CHECK(list->items[0]->data[n] == 0);
}
static void test_property(void) {
    uint64_t digest = UINT64_C(14695981039346656037);
    int count = 0;
    for (int cp = 0; cp <= 0x10FFFF; cp++) {
        int result = am_codepoint_isalnum(cp);
        CHECK(result >= -1 && result <= 1);
        CHECK((result == -1) == (cp >= 0xD800 && cp <= 0xDFFF));
        count += result == 1;
        digest = (digest ^ (uint64_t)(result + 1)) * UINT64_C(1099511628211);
    }
    CHECK(count == INPUT_ALNUM_COUNT && digest == INPUT_ALNUM_FNV);
    for (size_t i = 0; i < sizeof(input_alnum_cases) / sizeof(input_alnum_cases[0]); i++)
        CHECK(am_codepoint_isalnum(input_alnum_cases[i].cp) == input_alnum_cases[i].expected);
    CHECK(am_codepoint_isalnum(-1) == -1 && am_codepoint_isalnum(INT_MIN) == -1);
    CHECK(am_codepoint_isalnum(0x110000) == -1 && am_codepoint_isalnum(INT_MAX) == -1);
}
static void test_lines(void) {
    char error[256];
    const char bytes[] = "\nПривет, שלום\r\nlast 🌧";
    FILE* f = input(bytes, sizeof(bytes) - 1);
    AM_List* line = am_read_line(f, error, sizeof(error));
    expect_line(line, "", 0); am_list_free(line);
    line = am_read_line(f, error, sizeof(error));
    expect_line(line, "Привет, שלום\r", strlen("Привет, שלום\r")); am_list_free(line);
    line = am_read_line(f, error, sizeof(error));
    expect_line(line, "last 🌧", strlen("last 🌧")); am_list_free(line);
    for (int i = 0; i < 3; i++) {
        line = am_read_line(f, error, sizeof(error)); CHECK(line && line->len == 0); am_list_free(line);
    }
    fclose(f);
    CHECK(am_read_line(NULL, error, sizeof(error)) == NULL && error[0]);
    /* A failed underlying descriptor must be distinct from clean EOF. */
    f = input("x", 1); CHECK(close(fileno(f)) == 0);
    CHECK(am_read_line(f, error, sizeof(error)) == NULL && error[0]); fclose(f);
    f = input("\r\n", 2); line = am_read_line(f, NULL, 0);
    expect_line(line, "\r", 1); am_list_free(line); fclose(f);
}
static void test_invalid_input(void) {
    const struct { const char* bytes; size_t n; } bad[] = {
        {"a\0b\n", 4}, {"\x80\n", 2}, {"\xC0\xAF\n", 3},
        {"\xE0\x80\x80\n", 4}, {"\xED\xA0\x80\n", 4},
        {"\xF4\x90\x80\x80\n", 5}, {"\xF0\x9F\n", 3},
        {"valid\xFF\n", 7}, {"\xF0\x9F", 2}
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        FILE* f = input(bad[i].bytes, bad[i].n); char error[256] = {0};
        CHECK(am_read_line(f, error, sizeof(error)) == NULL && error[0]); fclose(f);
    }
    char* huge = malloc(AM_MAX_STRING_BYTES + 2); CHECK(huge);
    memset(huge, 'A', AM_MAX_STRING_BYTES + 2);
    for (int newline = 0; newline <= 1; newline++) {
        huge[AM_MAX_STRING_BYTES] = '\n';
        FILE* f = input(huge, AM_MAX_STRING_BYTES + newline); char error[256];
        AM_List* line = am_read_line(f, error, sizeof(error));
        expect_line(line, huge, AM_MAX_STRING_BYTES); am_list_free(line); fclose(f);
        huge[AM_MAX_STRING_BYTES] = 'A'; huge[AM_MAX_STRING_BYTES + 1] = '\n';
        f = input(huge, AM_MAX_STRING_BYTES + 1 + newline);
        CHECK(am_read_line(f, error, sizeof(error)) == NULL && error[0]); fclose(f);
    }
    free(huge);
}
typedef struct { FILE* file; AM_List* result; char error[256]; } LineWorker;
static void* read_worker(void* data) {
    LineWorker* worker = data;
    worker->result = am_read_line(worker->file, worker->error, sizeof(worker->error));
    return NULL;
}
static void test_whole_lines(void) {
    const size_t length = 8193;
    char* bytes = malloc(2 * length + 2); CHECK(bytes);
    memset(bytes, 'a', length); bytes[length] = '\n';
    memset(bytes + length + 1, 'b', length); bytes[2 * length + 1] = '\n';
    FILE* f = input(bytes, 2 * length + 2);
    pthread_t threads[2]; LineWorker workers[2] = {{f, NULL, {0}}, {f, NULL, {0}}};
    for (int i = 0; i < 2; i++) CHECK(pthread_create(&threads[i], NULL, read_worker, &workers[i]) == 0);
    for (int i = 0; i < 2; i++) CHECK(pthread_join(threads[i], NULL) == 0);
    int seen_a = 0, seen_b = 0;
    for (int i = 0; i < 2; i++) {
        AM_List* result = workers[i].result; CHECK(result && result->len == 1);
        char first = result->items[0]->data[0]; CHECK(first == 'a' || first == 'b');
        seen_a += first == 'a'; seen_b += first == 'b';
        expect_line(result, bytes + (first == 'b' ? length + 1 : 0), length);
        am_list_free(result);
    }
    CHECK(seen_a == 1 && seen_b == 1); fclose(f); free(bytes);
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
static int redirect_stdin(FILE* f) {
    int saved = dup(STDIN_FILENO); CHECK(saved >= 0);
    CHECK(dup2(fileno(f), STDIN_FILENO) >= 0); clearerr(stdin); return saved;
}
static void restore_stdin(int saved) {
    CHECK(dup2(saved, STDIN_FILENO) >= 0); close(saved); clearerr(stdin);
}
static void test_runtime(int mode) {
    const char* source =
        "def next_line():\n    return read_line()\n"
        "first = next_line()\nsecond = READ_LINE()\nthird = read_line()\n"
        "ended = read_line()\n"
        "assert(list_len(first) == 1 and text_len(list_get(first, 0)) == 0, 'blank')\n"
        "assert(text_len(list_get(second, 0)) == 5, 'retained CR length')\n"
        "assert(text_find(list_get(second, 0), 'rain\\r') == 0, 'retained CR')\n"
        "assert(text_len(list_get(third, 0)) == 1, 'unterminated UTF8 length')\n"
        "assert(text_find(list_get(third, 0), '🌧') == 0, 'unterminated UTF8 line')\n"
        "assert(list_len(ended) == 0, 'EOF')\n"
        "assert(codepoint_isalnum(0) == 0, 'scalar zero')\n"
        "assert(CODEPOINT_ISALNUM(65) == 1, 'Latin letter')\n";
    char program[2048]; strcpy(program, source);
    FILE* f = input("\nrain\r\n🌧", strlen("\nrain\r\n🌧"));
    int saved = redirect_stdin(f); am_init(); am_persistent_mode(1);
    CHECK(run(mode, program) == 0);
    expect_line(am_get_var_list("third"), "🌧", strlen("🌧"));
    restore_stdin(saved); fclose(f); am_persistent_clear();
    static const char* bad[] = {
        "read_line(1)", "read_line('x')", "read_line(list_new())",
        "codepoint_isalnum()", "codepoint_isalnum(65, 66)",
        "codepoint_isalnum('A')", "codepoint_isalnum([65])",
        "codepoint_isalnum(list_new())", "codepoint_isalnum(map_new())",
        "codepoint_isalnum(-1)", "codepoint_isalnum(0.5)",
        "codepoint_isalnum(55296)", "codepoint_isalnum(57343)",
        "codepoint_isalnum(1114112)", "codepoint_isalnum(1e39)",
        "codepoint_isalnum(1e39 - 1e39)",
        "def read_line():\n    return 1", "def codepoint_isalnum(x):\n    return x"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        am_init(); am_persistent_mode(1); CHECK(am_exec("kept = list_new()\nlist_push(kept, 'old')\n") == 0);
        snprintf(program, sizeof(program), "sentinel = 0\nkept = %s\nsentinel = 1\n", bad[i]);
        if (!strncmp(bad[i], "def ", 4)) snprintf(program, sizeof(program), "%s\nsentinel = 1\n", bad[i]);
        CHECK(run(mode, program) != 0 && am_get_error()[0]);
        CHECK(am_get_var_float("sentinel") == 0);
        expect_line(am_get_var_list("kept"), "old", 3); am_persistent_clear();
    }
    f = input("a\0b\n", 4); saved = redirect_stdin(f);
    am_init(); am_persistent_mode(1); CHECK(am_exec("kept = list_new()\nlist_push(kept, 'old')\n") == 0);
    CHECK(run(mode, "kept = read_line()\nsentinel = 1\n") != 0);
    expect_line(am_get_var_list("kept"), "old", 3); CHECK(am_get_var_float("sentinel") == 0);
    restore_stdin(saved); fclose(f); am_persistent_clear();
}
static void test_allocations(void) {
#ifdef AML_INPUT_ALLOC_WRAP
    const int lengths[] = {0, 1, 1024, 8193};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        char* bytes = malloc((size_t)lengths[i] + 1); CHECK(bytes);
        memset(bytes, 'x', (size_t)lengths[i]); bytes[lengths[i]] = '\n';
        int succeeded = 0;
        for (int budget = 0; budget < 40; budget++) {
            FILE* f = input(bytes, (size_t)lengths[i] + 1); char error[256] = {0};
            CHECK(tracked_count == 0); allocation_budget = budget;
            AM_List* line = am_read_line(f, error, sizeof(error)); allocation_budget = -1;
            if (line) { expect_line(line, bytes, (size_t)lengths[i]); succeeded = 1; }
            else CHECK(error[0]);
            am_list_free(line); CHECK(tracked_count == 0); fclose(f);
            if (succeeded) break;
        }
        CHECK(succeeded); free(bytes);
    }
#endif
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
static int test_prompt(const char* binary, const char* source) {
    int incoming[2], outgoing[2]; CHECK(pipe(incoming) == 0 && pipe(outgoing) == 0);
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) {
        if (dup2(incoming[0], STDIN_FILENO) < 0 || dup2(outgoing[1], STDOUT_FILENO) < 0) _exit(126);
        close(incoming[0]); close(incoming[1]); close(outgoing[0]); close(outgoing[1]);
        if (source) execl(binary, binary, source, (char*)NULL);
        else execl(binary, binary, (char*)NULL);
        _exit(127);
    }
    close(incoming[0]); close(outgoing[1]);
    struct pollfd ready = {outgoing[0], POLLIN, 0};
    int available = poll(&ready, 1, 3000);
    if (available <= 0) {
        kill(pid, SIGKILL); waitpid(pid, NULL, 0);
        close(incoming[1]); close(outgoing[0]);
        fprintf(stderr, "FAIL prompt remained buffered before read_line\n"); return 1;
    }
    char first[64] = {0}; ssize_t n = read(outgoing[0], first, sizeof(first) - 1);
    int valid = n == (ssize_t)strlen("PROMPT_READY\n") && !strcmp(first, "PROMPT_READY\n");
    if (write(incoming[1], "answer\n", 7) != 7) valid = 0;
    close(incoming[1]);
    char rest[64] = {0}; n = read(outgoing[0], rest, sizeof(rest) - 1);
    if (n != 7 || strcmp(rest, "answer\n")) valid = 0;
    close(outgoing[0]); int status = 0; CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(valid && WIFEXITED(status) && WEXITSTATUS(status) == 0); return 0;
}
int main(int argc, char** argv) {
    setvbuf(stdin, NULL, _IONBF, 0); am_init();
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "--prompt"))
        return test_prompt(argv[2], argc == 4 ? argv[3] : NULL);
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]); char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source); if (rc) fprintf(stderr, "text input: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    test_property(); test_lines(); test_invalid_input(); test_whole_lines(); test_allocations();
    for (int mode = 0; mode < 3; mode++) test_runtime(mode);
    am_persistent_mode(0);
    printf("AML_TEXT_INPUT_OK %d checks; 1,114,112 Unicode positions\n", checks); return 0;
}
