/* An IMPORT checks and reads one opened file, even when its pathname changes. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, replace_after_check, replacements, checked_fd = -1, fail_metadata;
static char imported_path[1024], replacement_path[1024];
static struct stat expected_file;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL IMPORT file line %d: %s; AML: %s\n", \
                __LINE__, #condition, am_get_error()); \
        exit(1); \
    } \
} while (0)

int __real_stat(const char* path, struct stat* metadata);
int __real_fstat(int fd, struct stat* metadata);

static void replace_path(void) {
    if (!replace_after_check) return;
    replace_after_check = 0;
    CHECK(rename(replacement_path, imported_path) == 0);
    replacements++;
}

/* The old path-based check reads the replacement. The descriptor-based check
 * must keep reading the file it inspected. Both hooks perform the same rename. */
int __wrap_stat(const char* path, struct stat* metadata) {
    int rc = __real_stat(path, metadata);
    if (!rc && !strcmp(path, imported_path)) replace_path();
    return rc;
}

int __wrap_fstat(int fd, struct stat* metadata) {
    int rc = __real_fstat(fd, metadata);
    if (!rc && metadata->st_dev == expected_file.st_dev && metadata->st_ino == expected_file.st_ino) {
        checked_fd = fd;
        if (fail_metadata) { errno = EIO; return -1; }
        replace_path();
    }
    return rc;
}

static void put_file(const char* path, const char* source) {
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    size_t size = strlen(source);
    CHECK(fwrite(source, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void observe_file(void) {
    CHECK(__real_stat(imported_path, &expected_file) == 0);
    checked_fd = -1;
}

static void descriptor_closed(void) {
    CHECK(checked_fd >= 0);
    errno = 0;
    CHECK(fcntl(checked_fd, F_GETFD) == -1 && errno == EBADF);
}

static int run(int mode) {
    const char* source = "IMPORT \"opened.aml\"\n";
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* program = am_program_open(source);
        if (!program) return 1;
        CHECK(am_program_step(program, 0) == 1);
        return am_program_close(program);
    }
    void* compiled = am_compile(source);
    if (!compiled) return 1;
    int rc = am_exec_compiled(compiled);
    am_free_compiled(compiled);
    return rc;
}

static void test_replacement(int mode, int fifo) {
    /* Equal-size regular files defeat the old stale-size validation. */
    put_file(imported_path, "TENSION 0.625\n");
    if (fifo) CHECK(mkfifo(replacement_path, 0600) == 0);
    else put_file(replacement_path, "TENSION 0.875\n");
    observe_file();
    replacements = 0;
    replace_after_check = 1;
    am_init();
    alarm(5);
    CHECK(run(mode) == 0);
    alarm(0);
    CHECK(replacements == 1 && !replace_after_check);
    CHECK(am_get_state()->tension == 0.625f);
    descriptor_closed();
    CHECK(unlink(imported_path) == 0);
}

static void test_refusal(int mode, int kind) {
    if (kind == 0) CHECK(mkfifo(imported_path, 0600) == 0);
    else if (kind == 1) CHECK(mkdir(imported_path, 0700) == 0);
    else {
        put_file(imported_path, "TENSION 0.875\n");
        if (kind == 2) CHECK(truncate(imported_path, 1024 * 1024 + 1) == 0);
    }
    observe_file();
    fail_metadata = kind == 3;
    am_init();
    CHECK(am_exec("TENSION 0.125\n") == 0);
    alarm(5); // A FIFO with no writer must be refused without a blocking open.
    CHECK(run(mode) != 0);
    alarm(0);
    CHECK(strstr(am_get_error(), kind == 2 ? "exceeds 1 MiB" : "not a regular file") != NULL);
    CHECK(am_get_state()->tension == 0.125f);
    descriptor_closed();
    fail_metadata = 0;
    if (kind == 1) CHECK(rmdir(imported_path) == 0);
    else CHECK(unlink(imported_path) == 0);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    CHECK(chdir(argv[1]) == 0);
    char cwd[900];
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
    int n = snprintf(imported_path, sizeof(imported_path), "%s/opened.aml", cwd);
    CHECK(n >= 0 && (size_t)n < sizeof(imported_path));
    n = snprintf(replacement_path, sizeof(replacement_path), "%s/replacement.aml", cwd);
    CHECK(n >= 0 && (size_t)n < sizeof(replacement_path));
    for (int mode = 0; mode < 3; mode++) {
        test_replacement(mode, 0);
        test_replacement(mode, 1);
        for (int kind = 0; kind < 4; kind++) test_refusal(mode, kind);
    }
    printf("PASS IMPORT opened-file ownership: %d checks; path replacement, FIFO/size/metadata refusals and descriptor release\n", checks);
    return 0;
}
