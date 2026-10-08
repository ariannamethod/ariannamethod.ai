/* Independent wire oracle, corruption rejection, publication and I/O faults. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL checkpoint line %d: %s (errno=%d)\n", __LINE__, #c, errno); exit(1); } } while (0)
#include "record_test_helpers.h"

typedef struct { unsigned char bytes[4096]; size_t len; } Bytes;
static size_t fields[5], keys_at[5], values_at[5];
static void u32_at(unsigned char* p, uint32_t x) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(x >> (8*i)); }
static void u64_at(unsigned char* p, uint64_t x) { for (int i = 0; i < 8; i++) p[i] = (unsigned char)(x >> (8*i)); }
static void bytes(Bytes* b, const void* p, size_t n) { CHECK(b->len + n <= sizeof(b->bytes)); memcpy(b->bytes + b->len, p, n); b->len += n; }
static void u32(Bytes* b, uint32_t x) { unsigned char p[4]; u32_at(p, x); bytes(b, p, 4); }
static void text(Bytes* b, const char* s) { u32(b, (uint32_t)strlen(s)); bytes(b, s, strlen(s)); }
static void field(Bytes* b, int i, const char* key, int tag, const Bytes* body) {
    fields[i] = b->len; u32(b, (uint32_t)strlen(key));
    unsigned char head[12] = {(unsigned char)tag, 0, 0, 0}; u64_at(head + 4, body->len);
    bytes(b, head, sizeof(head)); keys_at[i] = b->len; bytes(b, key, strlen(key));
    values_at[i] = b->len; bytes(b, body->bytes, body->len);
}
static uint32_t crc32(const unsigned char* header, const unsigned char* payload, size_t n) {
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < 28 + n; i++) {
        crc ^= i < 28 ? header[i] : payload[i - 28];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1)));
    }
    return crc ^ UINT32_C(0xffffffff);
}
static void checksum(Bytes* b) { u32_at(b->bytes + 28, crc32(b->bytes, b->bytes + 32, b->len - 32)); }
static Bytes golden(int duplicate_map_key) {
    Bytes result = {{'A','M','L','C','P',0,'\r','\n'}, 32};
    u32_at(result.bytes + 8, 1); u32_at(result.bytes + 24, 5);
    Bytes body = {{0}, 0}; u32(&body, 0x80000000); field(&result, 0, "scalar", 1, &body);
    body.len = 0; bytes(&body, "rain\nשלום", strlen("rain\nשלום")); field(&result, 1, "string", 2, &body);
    body.len = 0; u32(&body, 6); u32(&body, 2); u32(&body, 3);
    for (int i = 0; i < 6; i++) u32(&body, sample_bits[i]);
    field(&result, 2, "array", 3, &body);
    body.len = 0; u32(&body, 3); text(&body, ""); text(&body, "é"); text(&body, "x\n");
    field(&result, 3, "list", 4, &body);
    body.len = 0; u32(&body, 3); text(&body, "first"); u32(&body, 0x3f800000);
    text(&body, ""); u32(&body, 0x80000000);
    text(&body, duplicate_map_key ? "first" : "é"); u32(&body, 0xc0500000);
    field(&result, 4, "map", 5, &body);
    u64_at(result.bytes + 16, result.len - 32); checksum(&result); return result;
}
static void write_file(const char* path, const void* contents, size_t n) {
    FILE* f = fopen(path, "wb"); CHECK(f);
    CHECK(fwrite(contents, 1, n, f) == n && fclose(f) == 0);
}
static Bytes read_file(const char* path) {
    Bytes result = {{0}, 0}; FILE* f = fopen(path, "rb"); CHECK(f);
    result.len = fread(result.bytes, 1, sizeof(result.bytes), f);
    CHECK(!ferror(f) && feof(f) && fclose(f) == 0); return result;
}
static void expect_file(const char* path, const Bytes* expected) {
    Bytes actual = read_file(path); CHECK(actual.len == expected->len);
    CHECK(memcmp(actual.bytes, expected->bytes, actual.len) == 0);
}
static void expect_directory_clean(const char* directory) {
    DIR* d = opendir(directory); CHECK(d); struct dirent* entry; int files = 0;
    while ((entry = readdir(d))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        CHECK(!strcmp(entry->d_name, "state.amlcp")); files++;
    }
    CHECK(closedir(d) == 0 && files == 1);
}
static void rejected(const char* path, const Bytes* contents) {
    char error[256] = {0}; write_file(path, contents->bytes, contents->len);
    AM_Record* record = am_checkpoint_load(path, error, sizeof(error));
    CHECK(record == NULL && error[0]);
}
static void test_golden(const char* path) {
    Bytes expected = golden(0); AM_Record* record = make_sample(); char error[256];
    CHECK(am_checkpoint_save(record, path, error, sizeof(error)) == 1); expect_file(path, &expected);
    AM_Record* loaded = am_checkpoint_load(path, error, sizeof(error)); CHECK(loaded); check_sample(loaded);
    CHECK(am_checkpoint_save(loaded, path, error, sizeof(error)) == 1); expect_file(path, &expected);
    CHECK(am_file_exists(path, error, sizeof(error)) == 1);
    am_record_free(loaded); am_record_free(record);
    /* A second independent file encoder must be accepted and retain every bit. */
    write_file(path, expected.bytes, expected.len);
    loaded = am_checkpoint_load(path, error, sizeof(error)); CHECK(loaded); check_sample(loaded); am_record_free(loaded);
}
static void test_corruption(const char* path) {
    Bytes valid = golden(0);
    for (size_t n = 0; n < valid.len; n++) { Bytes short_file = valid; short_file.len = n; rejected(path, &short_file); }
    for (size_t i = 0; i < valid.len; i++) { Bytes changed = valid; changed.bytes[i] ^= 1; rejected(path, &changed); }
    for (int kind = 0; kind < 23; kind++) {
        Bytes changed = valid;
        switch (kind) {
            case 0: changed.bytes[0] = 'X'; break;
            case 1: u32_at(changed.bytes + 8, 2); break;
            case 2: u32_at(changed.bytes + 12, 1); break;
            case 3: u64_at(changed.bytes + 16, UINT64_MAX); break;
            case 4: u32_at(changed.bytes + 24, 256 + 1); break;
            case 5: u32_at(changed.bytes + 24, 4); break;
            case 6: changed.bytes[fields[0] + 4] = 99; break;
            case 7: changed.bytes[fields[0] + 5] = 1; break;
            case 8: u32_at(changed.bytes + fields[0], 1048577); break;
            case 9: u64_at(changed.bytes + fields[0] + 8, UINT64_MAX); break;
            case 10: memcpy(changed.bytes + keys_at[1], "scalar", 6); break;
            case 11: changed.bytes[keys_at[0]] = 0; break;
            case 12: changed.bytes[values_at[1]] = 0xff; break;
            case 13: changed.bytes[values_at[1]] = 0; break;
            case 14: u32_at(changed.bytes + values_at[2], 0); break;
            case 15: u32_at(changed.bytes + values_at[2], 1048577); break;
            case 16: u32_at(changed.bytes + values_at[2] + 4, 0); break;
            case 17: u32_at(changed.bytes + values_at[2] + 8, 4); break;
            case 18: u32_at(changed.bytes + values_at[2] + 4, 0xffffffff); break;
            case 19: u32_at(changed.bytes + values_at[3], 65537); break;
            case 20: u32_at(changed.bytes + values_at[4], 65537); break;
            case 21: u32_at(changed.bytes + values_at[4] + 4 + 4 + 5, 0x7f800000); break;
            case 22: changed.bytes[changed.len++] = 0; u64_at(changed.bytes + 16, changed.len - 32); break;
        }
        checksum(&changed); rejected(path, &changed);
    }
    Bytes duplicate = golden(1); rejected(path, &duplicate);
    char error[256]; int fd = open(path, O_WRONLY | O_TRUNC); CHECK(fd >= 0);
    CHECK(ftruncate(fd, (off_t)64 * 1024 * 1024 + 1) == 0 && close(fd) == 0);
    CHECK(am_checkpoint_load(path, error, sizeof(error)) == NULL && error[0]);
    write_file(path, valid.bytes, valid.len);
}
static void test_paths(const char* directory, const char* path) {
    char error[256], missing[512], blocked[1024], fifo[512];
    snprintf(missing, sizeof(missing), "%s/absent.amlcp", directory);
    snprintf(blocked, sizeof(blocked), "%s/child", path);
    CHECK(am_file_exists(missing, error, sizeof(error)) == 0);
    CHECK(am_file_exists(blocked, error, sizeof(error)) == 0);
    CHECK(am_file_exists(directory, error, sizeof(error)) < 0 && error[0]);
    CHECK(am_file_exists(NULL, error, sizeof(error)) < 0 && error[0]);
    CHECK(am_checkpoint_load(missing, error, sizeof(error)) == NULL && error[0]);
    CHECK(am_checkpoint_load(directory, error, sizeof(error)) == NULL && error[0]);
    snprintf(fifo, sizeof(fifo), "%s/fifo", directory); CHECK(mkfifo(fifo, 0600) == 0);
    CHECK(am_file_exists(fifo, error, sizeof(error)) < 0 && error[0]);
    CHECK(am_checkpoint_load(fifo, error, sizeof(error)) == NULL && error[0]);
    CHECK(unlink(fifo) == 0);
    AM_Record* record = make_sample(); Bytes previous = read_file(path);
    CHECK(am_checkpoint_save(record, blocked, error, sizeof(error)) < 0 && error[0]);
    expect_file(path, &previous);
    CHECK(am_checkpoint_save(record, directory, error, sizeof(error)) < 0 && error[0]);
    am_record_free(record); expect_directory_clean(directory);
}

#ifdef AML_CHECKPOINT_IO_WRAP
enum { IO_NONE, IO_WRITE, IO_WRITE_ZERO, IO_WRITE_PARTIAL, IO_FILE_SYNC, IO_CLOSE,
       IO_RENAME, IO_DIR_SYNC, IO_READ, IO_FSTAT, IO_STAT_PERMISSION };
static int io_failure, io_seen;
ssize_t __real_write(int, const void*, size_t);
ssize_t __real_read(int, void*, size_t);
int __real_fsync(int);
int __real_close(int);
int __real_rename(const char*, const char*);
int __real_fstat(int, struct stat*);
int __real_stat(const char*, struct stat*);
ssize_t __wrap_write(int fd, const void* data, size_t n) {
    if (io_failure == IO_WRITE || io_failure == IO_WRITE_ZERO) { io_seen++; errno = ENOSPC; return io_failure == IO_WRITE ? -1 : 0; }
    if (io_failure == IO_WRITE_PARTIAL) {
        if (io_seen++ == 0) return __real_write(fd, data, n / 2);
        errno = ENOSPC; return -1;
    }
    return __real_write(fd, data, n);
}
ssize_t __wrap_read(int fd, void* data, size_t n) {
    if (io_failure == IO_READ) { io_seen++; errno = EIO; return -1; }
    return __real_read(fd, data, n);
}
int __wrap_fsync(int fd) {
    struct stat info; CHECK(__real_fstat(fd, &info) == 0);
    if ((io_failure == IO_FILE_SYNC && S_ISREG(info.st_mode)) ||
        (io_failure == IO_DIR_SYNC && S_ISDIR(info.st_mode))) { io_seen++; errno = EIO; return -1; }
    return __real_fsync(fd);
}
int __wrap_close(int fd) {
    if (io_failure == IO_CLOSE && !io_seen) { io_seen++; __real_close(fd); errno = EIO; return -1; }
    return __real_close(fd);
}
int __wrap_rename(const char* a, const char* b) {
    if (io_failure == IO_RENAME) { io_seen++; errno = EACCES; return -1; }
    return __real_rename(a, b);
}
int __wrap_fstat(int fd, struct stat* info) {
    if (io_failure == IO_FSTAT) { io_seen++; errno = EACCES; return -1; }
    return __real_fstat(fd, info);
}
int __wrap_stat(const char* path, struct stat* info) {
    if (io_failure == IO_STAT_PERMISSION) { io_seen++; errno = EACCES; return -1; }
    return __real_stat(path, info);
}
static void test_io_failures(const char* directory, const char* path) {
    AM_Record* record = make_sample(); Bytes valid = golden(0);
    const char old[] = "old committed bytes"; char error[256];
    for (int failure = IO_WRITE; failure <= IO_DIR_SYNC; failure++) {
        write_file(path, old, sizeof(old) - 1); io_failure = failure; io_seen = 0;
        int rc = am_checkpoint_save(record, path, error, sizeof(error)); io_failure = IO_NONE;
        CHECK(io_seen > 0 && error[0]);
        if (failure == IO_DIR_SYNC) { CHECK(rc == 2); expect_file(path, &valid); }
        else { CHECK(rc < 0); Bytes before = {{0}, sizeof(old) - 1}; memcpy(before.bytes, old, before.len); expect_file(path, &before); }
        expect_directory_clean(directory);
    }
    write_file(path, valid.bytes, valid.len);
    for (int failure = IO_READ; failure <= IO_FSTAT; failure++) {
        io_failure = failure; io_seen = 0;
        AM_Record* loaded = am_checkpoint_load(path, error, sizeof(error)); io_failure = IO_NONE;
        CHECK(io_seen > 0 && loaded == NULL && error[0]); expect_file(path, &valid);
    }
    io_failure = IO_CLOSE; io_seen = 0;
    AM_Record* loaded = am_checkpoint_load(path, error, sizeof(error)); io_failure = IO_NONE;
    CHECK(io_seen == 1 && loaded == NULL && error[0]); expect_file(path, &valid);
    io_failure = IO_STAT_PERMISSION; io_seen = 0;
    int exists = am_file_exists(path, error, sizeof(error)); io_failure = IO_NONE;
    CHECK(io_seen == 1 && exists < 0 && error[0]);
    am_record_free(record);
}
#endif
static void test_allocations(const char* directory, const char* path) {
#ifdef AML_CHECKPOINT_ALLOC_WRAP
    AM_Record* record = make_sample(); Bytes valid = golden(0); char error[256];
    for (int operation = 0; operation < 2; operation++) {
        int success = 0;
        for (int budget = 0; budget < 256; budget++) {
            write_file(path, valid.bytes, valid.len); CHECK(tracked_count == 0);
            allocation_budget = budget; AM_Record* loaded = NULL; int rc;
            if (operation == 0) { loaded = am_checkpoint_load(path, error, sizeof(error)); rc = loaded ? 1 : -1; }
            else rc = am_checkpoint_save(record, path, error, sizeof(error));
            allocation_budget = -1;
            if (rc > 0) { success = 1; if (loaded) check_sample(loaded); }
            else CHECK(error[0]);
            am_record_free(loaded); CHECK(tracked_count == 0); check_sample(record);
            expect_file(path, &valid); expect_directory_clean(directory);
            if (success) break;
        }
        CHECK(success);
    }
    am_record_free(record);
#else
    (void)directory; (void)path;
#endif
}
int main(void) {
    char directory[] = "/tmp/aml-record-checkpoint-XXXXXX"; CHECK(mkdtemp(directory));
    char path[512]; snprintf(path, sizeof(path), "%s/state.amlcp", directory);
    test_golden(path); test_corruption(path); test_paths(directory, path); test_allocations(directory, path);
#ifdef AML_CHECKPOINT_IO_WRAP
    test_io_failures(directory, path);
#endif
    CHECK(unlink(path) == 0 && rmdir(directory) == 0);
    printf("AML_CHECKPOINT_OK %d checks\n", checks); return 0;
}
