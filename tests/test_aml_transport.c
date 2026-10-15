/* Transport organs: SHA-256 oracles, line journal faults, process capture. */
#undef _FORTIFY_SOURCE
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(AML_TRANSPORT_FAULTS)
#define _GNU_SOURCE
#endif
#include "ariannamethod.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef AML_TRANSPORT_FAULTS
#include <dlfcn.h>
#endif

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL transport %s line %d: %s (errno=%d; AML: %s)\n", \
            current_test, __LINE__, #c, errno, am_get_error()); exit(1); } } while (0)
static const char* current_test = "";
static char self[PATH_MAX];
static char work[] = "/tmp/aml-transport-XXXXXX";

/* SHA-256 oracle digests from `shasum -a 256` (Perl Digest::SHA). */
static const struct { size_t bytes; const char* digest; } repeated_a[] = {
    {1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
    {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
    {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    {1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
};

#ifdef AML_TRANSPORT_FAULTS
/* Definitions in this executable outrank libc for libaml.a's references; the
 * real functions are resolved once, before any fork. */
enum { FAULT_NONE, FAULT_FSYNC, FAULT_PARTIAL_FAIL, FAULT_PARTIAL_CONTINUE,
       FAULT_PARTIAL_NO_RESTORE, FAULT_SHORT_READ };
static int fault, fault_hits;
static ssize_t (*real_write)(int, const void*, size_t);
static ssize_t (*real_read)(int, void*, size_t);
static int (*real_fsync)(int);
static int (*real_ftruncate)(int, off_t);
ssize_t write(int fd, const void* data, size_t n) {
    if (fd > 2 && fault >= FAULT_PARTIAL_FAIL && fault <= FAULT_PARTIAL_NO_RESTORE) {
        if (fault_hits++ == 0) return real_write(fd, data, n / 2);
        if (fault != FAULT_PARTIAL_CONTINUE) { errno = ENOSPC; return -1; }
    }
    return real_write(fd, data, n);
}
ssize_t read(int fd, void* data, size_t n) {
    if (fd > 2 && fault == FAULT_SHORT_READ) { fault_hits++; return real_read(fd, data, n < 7 ? n : 7); }
    return real_read(fd, data, n);
}
int fsync(int fd) {
    if (fault == FAULT_FSYNC) { fault_hits++; errno = EIO; return -1; }
    return real_fsync(fd);
}
int ftruncate(int fd, off_t length) {
    if (fault == FAULT_PARTIAL_NO_RESTORE) { fault_hits++; errno = EIO; return -1; }
    return real_ftruncate(fd, length);
}
static void resolve_real(void) {
    real_write = (ssize_t (*)(int, const void*, size_t))dlsym(RTLD_NEXT, "write");
    real_read = (ssize_t (*)(int, void*, size_t))dlsym(RTLD_NEXT, "read");
    real_fsync = (int (*)(int))dlsym(RTLD_NEXT, "fsync");
    real_ftruncate = (int (*)(int, off_t))dlsym(RTLD_NEXT, "ftruncate");
    if (!real_write || !real_read || !real_fsync || !real_ftruncate) {
        fprintf(stderr, "FAIL transport: real I/O symbols unavailable\n"); exit(1);
    }
}
#endif

/* ── Child behaviours: this binary is the program process_run executes. ── */
static void write_all(int fd, const char* data, size_t n) {
    while (n) {
        ssize_t count = write(fd, data, n);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) _exit(90);
        data += count; n -= (size_t)count;
    }
}
static int child_main(int argc, char** argv) {
    const char* what = argv[2];
    if (!strcmp(what, "args")) {
        printf("%d", argc - 3);
        for (int i = 3; i < argc; i++) printf("[%s]", argv[i]);
        return 0;
    }
    if (!strcmp(what, "exit") && argc == 4) return atoi(argv[3]);
    if (!strcmp(what, "raise") && argc == 4) {
        int number = atoi(argv[3]);
        signal(number, SIG_DFL);
        raise(number);
        return 99;
    }
    if (!strcmp(what, "env") && argc == 4) {
        const char* value = getenv(argv[3]);
        printf("%s", value ? value : "<unset>");
        return 0;
    }
    if (!strcmp(what, "emit") && argc == 5) {
        size_t out = strtoul(argv[3], NULL, 10), err = strtoul(argv[4], NULL, 10);
        char o[4096], e[4096];
        memset(o, 'o', sizeof(o)); memset(e, 'e', sizeof(e));
        while (out || err) {
            size_t n = out < sizeof(o) ? out : sizeof(o);
            if (n) { write_all(1, o, n); out -= n; }
            n = err < sizeof(e) ? err : sizeof(e);
            if (n) { write_all(2, e, n); err -= n; }
        }
        return 0;
    }
    if (!strcmp(what, "bytes")) { write_all(1, "\xff\n", 2); return 0; }
    if (!strcmp(what, "nul")) { write_all(1, "a\0b", 3); return 0; }
    if (!strcmp(what, "stdin")) {
        char buffer[256]; size_t total = 0; ssize_t count;
        while ((count = read(0, buffer, sizeof(buffer))) > 0) total += (size_t)count;
        printf("%zu", total);
        return count < 0 ? 91 : 0;
    }
    if (!strcmp(what, "print-sleep")) {
        printf("partial\n"); fflush(stdout); sleep(30); return 0;
    }
    if (!strcmp(what, "ignore-term")) {
        signal(SIGTERM, SIG_IGN); sleep(30); return 0;
    }
    if (!strcmp(what, "touch") && argc == 4) {
        int fd = open(argv[3], O_CREAT | O_WRONLY, 0644);
        return fd < 0 ? 92 : close(fd);
    }
    return 93;
}

/* ── Host helpers ── */
static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program = am_program_open(source);
        if (!program) return 1;
        int budget = AML_MAX_LINES + 1;
        while (!am_program_step(program, 1)) CHECK(--budget > 0);
        return am_program_close(program);
    }
    void* compiled = am_compile(source);
    if (!compiled) return 1;
    int rc = am_exec_compiled(compiled);
    am_free_compiled(compiled);
    return rc;
}
static void prepare(const char* name) {
    current_test = name;
    am_init();
    am_persistent_mode(1);
}
static double now_seconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static int open_descriptors(void) {
    int count = 0;
    for (int fd = 0; fd < 1024; fd++) count += fcntl(fd, F_GETFD) != -1;
    return count;
}
static void no_children(void) {
    errno = 0;
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
}
static void path_in_work(char* out, size_t cap, const char* name) {
    CHECK(snprintf(out, cap, "%s/%s", work, name) < (int)cap);
}
static void write_file(const char* path, const char* data, size_t n) {
    FILE* f = fopen(path, "wb"); CHECK(f);
    CHECK(fwrite(data, 1, n, f) == n && fclose(f) == 0);
}
static char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb"); CHECK(f);
    size_t cap = 1 << 20, len = 0; char* data = malloc(cap); CHECK(data);
    size_t got;
    while ((got = fread(data + len, 1, cap - len, f)) > 0) {
        len += got;
        if (len == cap) { cap *= 2; data = realloc(data, cap); CHECK(data); }
    }
    CHECK(!ferror(f) && fclose(f) == 0);
    *n = len; return data;
}
static void expect_file(const char* path, const char* expected, size_t n) {
    size_t len; char* data = read_file(path, &len);
    CHECK(len == n && !memcmp(data, expected, n));
    free(data);
}
static void absent(const char* path) {
    struct stat st; CHECK(stat(path, &st) != 0 && errno == ENOENT);
}
static void set_command(const char* var, ...) {
    AM_List* list = am_list_new(); CHECK(list);
    va_list ap; va_start(ap, var);
    for (const char* item; (item = va_arg(ap, const char*));) {
        AM_String* text = am_string_new(item); CHECK(text);
        CHECK(am_list_push(list, text) > 0); am_string_free(text);
    }
    va_end(ap);
    CHECK(am_set_var_list(var, list) == 0); am_list_free(list);
}
static const AML_Var* field(const char* var, const char* key) {
    const AM_Record* record = am_get_var_record(var); CHECK(record);
    AM_String* name = am_string_new(key); CHECK(name);
    const AML_Var* value = am_record_get(record, name);
    am_string_free(name); CHECK(value); return value;
}
static void expect_result(const char* var, float status, const char* out, const char* err) {
    const AML_Var* s = field(var, "status");
    CHECK(s->type == AML_TYPE_FLOAT && s->value == status);
    const AML_Var* o = field(var, "stdout");
    CHECK(o->type == AML_TYPE_STRING && !strcmp(o->string->data, out));
    const AML_Var* e = field(var, "stderr");
    CHECK(e->type == AML_TYPE_STRING && !strcmp(e->string->data, err));
}
/* One call through the given mode; the destination stays 7 on failure. */
static int call(int mode, const char* expression) {
    char source[1024];
    snprintf(source, sizeof(source), "r = 7\nsentinel = 0\nr = %s\nsentinel = 1\n", expression);
    int before = open_descriptors();
    int rc = run(mode, source);
    no_children();
    CHECK(open_descriptors() == before);
    if (rc) CHECK(am_get_var_float("sentinel") == 0 && am_get_var_float("r") == 7);
    else CHECK(am_get_var_float("sentinel") == 1);
    return rc;
}
static void refused(int mode, const char* expression, const char* diagnostic) {
    CHECK(call(mode, expression) != 0);
    if (!strstr(am_get_error(), diagnostic)) {
        fprintf(stderr, "FAIL transport %s: '%s' gave '%s', expected '%s'\n",
                current_test, expression, am_get_error(), diagnostic);
        exit(1);
    }
    checks++;
}

/* ── SHA-256 ── */
static void test_sha256_vectors(int mode) {
    prepare("sha256_vectors");
    CHECK(run(mode, "a = text_hash_sha256('abc')\nb = TEXT_HASH_SHA256('')\n"
        "c = text_hash_sha256('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq')\n"
        "d = text_hash_sha256('שלום 🌧')\n") == 0);
    CHECK(!strcmp(am_get_var_text("a"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(!strcmp(am_get_var_text("b"), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(!strcmp(am_get_var_text("c"), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK(!strcmp(am_get_var_text("d"), "bcc707f6258549a51b71f52a42763ea38d8ab3151a1a8ad114a58da75697cb97"));
    char* payload = malloc(1000001); CHECK(payload);
    for (size_t i = 0; i < sizeof(repeated_a) / sizeof(repeated_a[0]); i++) {
        memset(payload, 'a', repeated_a[i].bytes); payload[repeated_a[i].bytes] = 0;
        CHECK(am_set_var_text("payload", payload) == 0);
        CHECK(run(mode, "h = text_hash_sha256(payload)\n") == 0);
        CHECK(!strcmp(am_get_var_text("h"), repeated_a[i].digest));
    }
    free(payload);
    refused(mode, "text_hash_sha256(1)", "text_hash_sha256 requires text");
    refused(mode, "text_hash_sha256(list_new())", "text_hash_sha256 requires text");
    refused(mode, "text_hash_sha256('a', 'b')", "wrong number of hash/journal arguments");
    refused(mode, "text_hash_sha256()", "wrong number of hash/journal arguments");
    refused(mode, "text_hash_sha256('abc') + 1", "used as a scalar");
    am_persistent_clear();
}

static void test_file_hash_equals_text_hash(int mode) {
    prepare("file_hash_equals_text_hash");
    char path[512], fifo[512]; path_in_work(path, sizeof(path), "payload.bin");
    char* payload = malloc(1000001); CHECK(payload);
    CHECK(am_set_var_text("path", path) == 0);
    for (size_t i = 0; i < sizeof(repeated_a) / sizeof(repeated_a[0]); i++) {
        memset(payload, 'a', repeated_a[i].bytes); payload[repeated_a[i].bytes] = 0;
        write_file(path, payload, repeated_a[i].bytes);
        CHECK(am_set_var_text("payload", payload) == 0);
        CHECK(run(mode, "f = file_hash_sha256(path)\nt = text_hash_sha256(payload)\n"
                        "assert(text_equal(f, t), 'file hash equals text hash')\n") == 0);
        CHECK(!strcmp(am_get_var_text("f"), repeated_a[i].digest));
    }
    write_file(path, "", 0);
    CHECK(run(mode, "f = file_hash_sha256(path)\n") == 0);
    CHECK(!strcmp(am_get_var_text("f"), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    free(payload);
    path_in_work(fifo, sizeof(fifo), "hash.fifo"); CHECK(mkfifo(fifo, 0600) == 0);
    CHECK(am_set_var_text("fifo", fifo) == 0);
    CHECK(am_set_var_text("directory", work) == 0);
    CHECK(am_set_var_text("missing", "/nonexistent-aml-transport/file") == 0);
    refused(mode, "file_hash_sha256(missing)", "file_hash_sha256 open failed");
    refused(mode, "file_hash_sha256(directory)", "file_hash_sha256 requires a regular file");
    refused(mode, "file_hash_sha256(fifo)", "file_hash_sha256 requires a regular file");
    refused(mode, "file_hash_sha256('')", "file path must be a nonempty string");
    refused(mode, "file_hash_sha256(3)", "file path must be a nonempty string");
    refused(mode, "file_hash_sha256(path, path)", "wrong number of hash/journal arguments");
    CHECK(unlink(fifo) == 0 && unlink(path) == 0);
    am_persistent_clear();
}

/* ── Line journal ── */
static void test_append_ordering(int mode) {
    prepare("append_ordering");
    char path[512]; path_in_work(path, sizeof(path), "journal.log");
    CHECK(am_set_var_text("journal", path) == 0);
    absent(path);
    CHECK(call(mode, "file_append_line(journal, 'first')") == 0 && am_get_var_float("r") == 1);
    CHECK(call(mode, "FILE_APPEND_LINE(journal, 'second')") == 0 && am_get_var_float("r") == 1);
    expect_file(path, "first\nsecond\n", 13);
    struct stat st; CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == (0644 & ~(mode_t)022));
    CHECK(call(mode, "file_append_line(journal, '')") == 0 && am_get_var_float("r") == 1);
    CHECK(call(mode, "file_append_line(journal, 'שלום\\r 🌧')") == 0 && am_get_var_float("r") == 1);
    expect_file(path, "first\nsecond\n\nשלום\r 🌧\n", strlen("first\nsecond\n\nשלום\r 🌧\n"));
    CHECK(run(mode, "assert(text_equal(file_hash_sha256(journal), text_hash_sha256('first\\nsecond\\n\\nשלום\\r 🌧\\n')), 'journal bytes')\n") == 0);
    CHECK(unlink(path) == 0);
    am_persistent_clear();
}

static void test_append_newline_rejected(int mode) {
    prepare("append_newline_rejected");
    char path[512], fresh[512];
    path_in_work(path, sizeof(path), "kept.log"); path_in_work(fresh, sizeof(fresh), "never.log");
    write_file(path, "old\n", 4);
    CHECK(am_set_var_text("journal", path) == 0 && am_set_var_text("fresh", fresh) == 0);
    refused(mode, "file_append_line(journal, 'a\\nb')", "must not contain a newline");
    refused(mode, "file_append_line(journal, '\\n')", "must not contain a newline");
    refused(mode, "file_append_line(journal, 'tail\\n')", "must not contain a newline");
    expect_file(path, "old\n", 4);
    refused(mode, "file_append_line(fresh, 'x\\ny')", "must not contain a newline");
    absent(fresh);
    CHECK(unlink(path) == 0);
    am_persistent_clear();
}

static void test_append_refusals(int mode) {
    prepare("append_refusals");
    char fifo[512], blocked[512], path[512];
    path_in_work(fifo, sizeof(fifo), "journal.fifo"); CHECK(mkfifo(fifo, 0600) == 0);
    path_in_work(path, sizeof(path), "plain.log"); write_file(path, "keep\n", 5);
    snprintf(blocked, sizeof(blocked), "%s/child.log", path);
    CHECK(am_set_var_text("fifo", fifo) == 0 && am_set_var_text("directory", work) == 0);
    CHECK(am_set_var_text("blocked", blocked) == 0 && am_set_var_text("journal", path) == 0);
    CHECK(am_set_var_text("missing", "/nonexistent-aml-transport/journal.log") == 0);
    refused(mode, "file_append_line(fifo, 'x')", "file_append_line open failed");
    refused(mode, "file_append_line(directory, 'x')", "file_append_line open failed");
    refused(mode, "file_append_line(missing, 'x')", "file_append_line open failed");
    refused(mode, "file_append_line(blocked, 'x')", "file_append_line open failed");
    refused(mode, "file_append_line(journal, 1)", "requires a path and text");
    refused(mode, "file_append_line(1, 'x')", "file path must be a nonempty string");
    refused(mode, "file_append_line(journal)", "wrong number of hash/journal arguments");
    refused(mode, "file_append_line(journal, 'a', 'b')", "wrong number of hash/journal arguments");
    expect_file(path, "keep\n", 5);
    CHECK(unlink(fifo) == 0 && unlink(path) == 0);
    am_persistent_clear();
}

#ifdef AML_TRANSPORT_FAULTS
static void test_append_faults(int mode) {
    prepare("append_faults");
    char path[512]; path_in_work(path, sizeof(path), "fault.log");
    CHECK(am_set_var_text("journal", path) == 0);
    const char* full = "old\npartial line\n";
    write_file(path, "old\n", 4);
    fault = FAULT_FSYNC; fault_hits = 0;
    CHECK(call(mode, "file_append_line(journal, 'partial line')") == 0);
    fault = FAULT_NONE;
    CHECK(fault_hits == 1 && am_get_var_float("r") == 2);
    expect_file(path, full, strlen(full));

    write_file(path, "old\n", 4);
    fault = FAULT_PARTIAL_FAIL; fault_hits = 0;
    refused(mode, "file_append_line(journal, 'partial line')", "file_append_line write failed");
    fault = FAULT_NONE;
    CHECK(fault_hits == 2);
    expect_file(path, "old\n", 4);

    fault = FAULT_PARTIAL_CONTINUE; fault_hits = 0;
    CHECK(call(mode, "file_append_line(journal, 'partial line')") == 0);
    fault = FAULT_NONE;
    CHECK(fault_hits == 2 && am_get_var_float("r") == 1);
    expect_file(path, full, strlen(full));

    write_file(path, "old\n", 4);
    fault = FAULT_PARTIAL_NO_RESTORE; fault_hits = 0;
    refused(mode, "file_append_line(journal, 'partial line')", "could not restore the previous length");
    fault = FAULT_NONE;
    CHECK(fault_hits == 3);
    expect_file(path, "old\npartia", 10);
    CHECK(unlink(path) == 0);

    char* payload = malloc(1000001); CHECK(payload);
    memset(payload, 'a', 1000000);
    write_file(path, payload, 1000000);
    free(payload);
    fault = FAULT_SHORT_READ; fault_hits = 0;
    CHECK(call(mode, "file_hash_sha256(journal)") == 0);
    fault = FAULT_NONE;
    CHECK(fault_hits > 1000000 / 7);
    CHECK(!strcmp(am_get_var_text("r"), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(unlink(path) == 0);
    am_persistent_clear();
}
#endif

/* ── External processes ── */
static void test_process_echo_and_status(int mode) {
    prepare("process_echo_and_status");
    set_command("cmd", "echo", "hi", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0);
    expect_result("r", 0, "hi\n", "");
    CHECK(run(mode, "assert(text_equal(list_get(record_keys(r), 0), 'status'), 'status first')\n"
                    "assert(text_equal(list_get(record_keys(r), 2), 'stderr'), 'stderr last')\n"
                    "assert(list_len(record_keys(r)) == 3, 'three fields')\n") == 0);
    set_command("cmd", self, "--child", "exit", "3", NULL);
    CHECK(call(mode, "PROCESS_RUN(cmd)") == 0); expect_result("r", 3, "", "");
    set_command("cmd", self, "--child", "exit", "137", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0); expect_result("r", 137, "", "");
    set_command("cmd", self, "--child", "raise", "9", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0); expect_result("r", -9, "", "");
    set_command("cmd", self, "--child", "raise", "15", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0); expect_result("r", -15, "", "");
    set_command("cmd", self, "--child", "stdin", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0); expect_result("r", 0, "0", "");
    set_command("cmd", "false", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0); expect_result("r", 1, "", "");
    am_persistent_clear();
}

static void test_process_argv_one_entry(int mode) {
    prepare("process_argv_one_entry");
    set_command("cmd", self, "--child", "args", "a;b c", "", " spaced ", "$HOME", "*", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0);
    expect_result("r", 0, "5[a;b c][][ spaced ][$HOME][*]", "");
    set_command("cmd", "printf", "[%s]", "a;b c", NULL);
    CHECK(call(mode, "process_run(cmd)") == 0);
    expect_result("r", 0, "[a;b c]", "");
    am_persistent_clear();
}

static void test_process_environment(int mode) {
    prepare("process_environment");
    CHECK(run(mode, "env = record_new()\nrecord_set(env, 'AML_TRANSPORT_INHERITED', 'record')\n"
                    "record_set(env, 'AML_TRANSPORT_ADDED', 'x y;z=1 שלום')\n") == 0);
    set_command("inherited", self, "--child", "env", "AML_TRANSPORT_INHERITED", NULL);
    set_command("added", self, "--child", "env", "AML_TRANSPORT_ADDED", NULL);
    CHECK(call(mode, "process_run(inherited)") == 0); expect_result("r", 0, "parent", "");
    CHECK(call(mode, "process_run(added)") == 0); expect_result("r", 0, "<unset>", "");
    CHECK(call(mode, "process_run(inherited, env)") == 0); expect_result("r", 0, "record", "");
    CHECK(call(mode, "process_run(added, env)") == 0); expect_result("r", 0, "x y;z=1 שלום", "");
    CHECK(call(mode, "process_run(added, record_new())") == 0); expect_result("r", 0, "<unset>", "");
    CHECK(!strcmp(getenv("AML_TRANSPORT_INHERITED"), "parent") && !getenv("AML_TRANSPORT_ADDED"));
    /* The program lookup uses the child's PATH. */
    CHECK(run(mode, "nopath = record_new()\nrecord_set(nopath, 'PATH', '/nonexistent-aml-transport')\n") == 0);
    set_command("cmd", "echo", "hi", NULL);
    refused(mode, "process_run(cmd, nopath)", "could not execute 'echo'");
    am_persistent_clear();
}

static void test_process_refusals(int mode) {
    prepare("process_refusals");
    char marker[512]; path_in_work(marker, sizeof(marker), "touched");
    set_command("touch", self, "--child", "touch", marker, NULL);
    CHECK(run(mode, "bad = record_new()\nrecord_set(bad, 'NUMBER', 1)\n"
                    "eq = record_new()\nrecord_set(eq, 'A=B', 'x')\n"
                    "blank = record_new()\nrecord_set(blank, '', 'x')\n"
                    "empty = list_new()\nnoname = list_new()\nlist_push(noname, '')\n") == 0);
    refused(mode, "process_run(touch, bad)", "environment values must be text");
    refused(mode, "process_run(touch, eq)", "contain no '='");
    refused(mode, "process_run(touch, blank)", "environment names must be nonempty");
    refused(mode, "process_run(touch, 1)", "environment must be a record");
    refused(mode, "process_run(touch, record_new(), 0)", "timeout must be a positive finite");
    refused(mode, "process_run(touch, record_new(), -1)", "timeout must be a positive finite");
    refused(mode, "process_run(touch, record_new(), 'soon')", "timeout must be a positive finite");
    refused(mode, "process_run(touch, record_new(), 1, 2)", "requires argv");
    refused(mode, "process_run()", "requires argv");
    refused(mode, "process_run(empty)", "nonempty argv string list");
    refused(mode, "process_run('echo')", "nonempty argv string list");
    refused(mode, "process_run(noname)", "program name must be nonempty");
    absent(marker);
    set_command("cmd", "aml-transport-no-such-binary", NULL);
    refused(mode, "process_run(cmd)", "could not execute 'aml-transport-no-such-binary': No such file");
    set_command("cmd", "/nonexistent-aml-transport/bin", NULL);
    refused(mode, "process_run(cmd)", "No such file or directory");
    set_command("cmd", work, NULL);
    refused(mode, "process_run(cmd)", "Permission denied");
    /* No hidden /bin/sh fallback for an executable without #!. */
    char script[512]; path_in_work(script, sizeof(script), "no-shebang");
    write_file(script, "echo hi\n", 8); CHECK(chmod(script, 0644) == 0);
    set_command("cmd", script, NULL);
    refused(mode, "process_run(cmd)", "Permission denied");
    CHECK(chmod(script, 0755) == 0);
    refused(mode, "process_run(cmd)", "Exec format error");
    CHECK(unlink(script) == 0);
    set_command("cmd", self, "--child", "bytes", NULL);
    refused(mode, "process_run(cmd)", "stdout is not UTF-8 text without NUL");
    set_command("cmd", self, "--child", "nul", NULL);
    refused(mode, "process_run(cmd)", "stdout is not UTF-8 text without NUL");
    CHECK(call(mode, "process_run(touch)") == 0); expect_result("r", 0, "", "");
    CHECK(unlink(marker) == 0);
    am_persistent_clear();
}

static void test_process_timeout(int mode) {
    prepare("process_timeout");
    set_command("cmd", "sleep", "5", NULL);
    double start = now_seconds();
    CHECK(call(mode, "process_run(cmd, record_new(), 0.25)") == 0);
    double elapsed = now_seconds() - start;
    expect_result("r", -256, "", "");
    CHECK(elapsed >= 0.25 && elapsed < 3.0);
    set_command("cmd", self, "--child", "print-sleep", NULL);
    CHECK(call(mode, "process_run(cmd, record_new(), 0.3)") == 0);
    expect_result("r", -256, "partial\n", "");
    set_command("cmd", self, "--child", "ignore-term", NULL);
    start = now_seconds();
    CHECK(call(mode, "process_run(cmd, record_new(), 0.2)") == 0);
    elapsed = now_seconds() - start;
    expect_result("r", -256, "", "");
    CHECK(elapsed >= 1.2 && elapsed < 4.0);
    set_command("cmd", "echo", "quick", NULL);
    CHECK(call(mode, "process_run(cmd, record_new(), 30)") == 0);
    expect_result("r", 0, "quick\n", "");
    am_persistent_clear();
}

static void test_process_large_output(int mode) {
    prepare("process_large_output");
    set_command("cmd", self, "--child", "emit", "1048576", "1048576", NULL);
    double start = now_seconds();
    CHECK(call(mode, "process_run(cmd, record_new(), 60)") == 0);
    CHECK(now_seconds() - start < 30);
    const AML_Var* out = field("r", "stdout"), * err = field("r", "stderr");
    CHECK(field("r", "status")->value == 0);
    CHECK(out->string->byte_len == 1048576 && err->string->byte_len == 1048576);
    size_t mismatched = 0;
    for (int i = 0; i < 1048576; i++)
        mismatched += out->string->data[i] != 'o' || err->string->data[i] != 'e';
    CHECK(mismatched == 0);
    set_command("cmd", self, "--child", "emit", "1048577", "0", NULL);
    refused(mode, "process_run(cmd)", "stdout exceeds 1 MiB");
    set_command("cmd", self, "--child", "emit", "0", "1048577", NULL);
    refused(mode, "process_run(cmd)", "stderr exceeds 1 MiB");
    set_command("cmd", self, "--child", "emit", "4000000", "4000000", NULL);
    refused(mode, "process_run(cmd)", "exceeds 1 MiB");
    am_persistent_clear();
}

/* A host with closed stdin/stdout: the runtime's descriptors land on 0/1 and
 * must move above the standard streams before the child's dup2. */
static void test_process_closed_stdio(int mode) {
    prepare("process_closed_stdio");
    set_command("cmd", self, "--child", "stdin", NULL);
    fflush(stdout);
    int saved_in = dup(0), saved_out = dup(1);
    CHECK(saved_in > 2 && saved_out > 2 && close(0) == 0 && close(1) == 0);
    int rc = run(mode, "r = process_run(cmd)\n");
    CHECK(dup2(saved_in, 0) == 0 && dup2(saved_out, 1) == 1 && close(saved_in) == 0 && close(saved_out) == 0);
    CHECK(rc == 0);
    expect_result("r", 0, "0", "");
    no_children();
    am_persistent_clear();
}

static void test_process_workers(int mode) {
#ifndef AM_ASYNC_DISABLED
    prepare("process_workers");
    char source[2048]; size_t used = 0;
    for (int i = 0; i < 4; i++) {
        char var[8], tag[8]; snprintf(var, sizeof(var), "c%d", i); snprintf(tag, sizeof(tag), "w%d", i);
        set_command(var, self, "--child", "args", tag, NULL);
        used += (size_t)snprintf(source + used, sizeof(source) - used,
            "SPAWN w%d:\n    acc = ''\n    k = 0\n    while k < 5:\n"
            "        acc = text_concat(acc, record_get(process_run(c%d), 'stdout'))\n"
            "        k = k + 1\n    return acc\n", i, i);
    }
    for (int i = 0; i < 4; i++)
        used += (size_t)snprintf(source + used, sizeof(source) - used,
            "AWAIT w%d INTO o%d\nassert(text_equal(o%d, '1[w%d]1[w%d]1[w%d]1[w%d]1[w%d]'), 'worker %d')\n",
            i, i, i, i, i, i, i, i, i);
    CHECK(used < sizeof(source));
    CHECK(run(mode, source) == 0);
    no_children();
    am_persistent_clear();
#else
    (void)mode;
#endif
}

static void test_reserved_names(int mode) {
    prepare("reserved_names");
    const char* names[] = {"text_hash_sha256", "FILE_HASH_SHA256", "file_append_line", "Process_Run"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char source[128]; snprintf(source, sizeof(source), "def %s(x):\n    return x\n", names[i]);
        CHECK(run(mode, source) != 0 && strstr(am_get_error(), "collides with an intrinsic"));
    }
    am_persistent_clear();
}

/* ── Fixtures for the five-path shell gate ── */
static const char* const invalid[] = {
    "h = text_hash_sha256(1)\n",
    "h = text_hash_sha256('a', 'b')\n",
    "h = file_hash_sha256('transport-missing-file')\n",
    "s = file_append_line('journal.log', 'a\\nb')\n",
    "s = file_append_line('journal.log')\n",
    "xs = list_new()\nr = process_run(xs)\n",
    "r = process_run('echo')\n",
    "xs = list_new()\nlist_push(xs, 'aml-transport-no-such-binary')\nr = process_run(xs)\n",
    "xs = list_new()\nlist_push(xs, 'echo')\nr = process_run(xs, 1)\n",
    "xs = list_new()\nlist_push(xs, 'echo')\nr = process_run(xs, record_new(), 0)\n",
    "def file_append_line(a, b):\n    return 1\n",
};

static char* read_source(const char* path) {
    size_t n; char* data = read_file(path, &n);
    data = realloc(data, n + 1); CHECK(data); data[n] = 0; return data;
}

int main(int argc, char** argv) {
#ifdef AML_TRANSPORT_FAULTS
    resolve_real();
#endif
    if (argc >= 3 && !strcmp(argv[1], "--child")) return child_main(argc, argv);
    if (argc == 2 && !strcmp(argv[1], "--bad-count")) {
        printf("%zu\n", sizeof(invalid) / sizeof(invalid[0])); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bad-fixture")) {
        int i = atoi(argv[2]);
        if (i < 0 || (size_t)i >= sizeof(invalid) / sizeof(invalid[0])) return 2;
        printf("%sPRINT 'WRONG_CONTINUATION'\n", invalid[i]); return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--run")) {
        int mode = atoi(argv[2]); char* source = read_source(argv[3]);
        if (mode < 0 || mode > 2 || !source) { free(source); return 2; }
        int rc = run(mode, source);
        if (rc) fprintf(stderr, "transport: %s\n", am_get_error());
        free(source); return rc;
    }
    CHECK(argc == 1);
    umask(022);
    CHECK(realpath(argv[0], self) != NULL);
    CHECK(mkdtemp(work) != NULL);
    CHECK(setenv("AML_TRANSPORT_INHERITED", "parent", 1) == 0);
    unsetenv("AML_TRANSPORT_ADDED");
    for (int mode = 0; mode < 3; mode++) {
#ifdef AML_TRANSPORT_FAULTS
        test_append_faults(mode);
#endif
        test_sha256_vectors(mode);
        test_file_hash_equals_text_hash(mode);
        test_append_ordering(mode);
        test_append_newline_rejected(mode);
        test_append_refusals(mode);
        test_process_echo_and_status(mode);
        test_process_argv_one_entry(mode);
        test_process_environment(mode);
        test_process_refusals(mode);
        test_process_timeout(mode);
        test_process_large_output(mode);
        test_process_closed_stdio(mode);
        test_process_workers(mode);
        test_reserved_names(mode);
    }
    CHECK(rmdir(work) == 0);
    am_persistent_mode(0);
#ifdef AML_TRANSPORT_FAULTS
    printf("AML_TRANSPORT_FAULTS_OK %d checks\n", checks);
#else
    printf("AML_TRANSPORT_OK %d checks\n", checks);
#endif
    return 0;
}
