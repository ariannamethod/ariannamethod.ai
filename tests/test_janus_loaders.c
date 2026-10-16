// tok_load and load_checkpoint on valid and invalid files. Each case runs in its own child
// process and the parent prints PASS/FAIL with the case name; `loaders NAME...` runs a subset.
// A refused file returns -1, leaves the tokenizer byte-identical and leaves behind no
// allocation (counted through the wrappers below) and no open descriptor.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "core/ariannamethod.h"

static long g_live;
static int g_fail_calloc;
static void* count_malloc(size_t n) { void* p = malloc(n); if (p) g_live++; return p; }
static void* count_calloc(size_t n, size_t s) {
    if (g_fail_calloc) { g_fail_calloc = 0; return NULL; }
    void* p = calloc(n, s);
    if (p) g_live++;
    return p;
}
static void count_free(void* p) { if (p) g_live--; free(p); }

#define malloc(n) count_malloc(n)
#define calloc(n, s) count_calloc(n, s)
#define free(p) count_free(p)
#define main janus_train_main
#include "janus/janus_train.c"
#undef main
#undef free
#undef calloc
#undef malloc

static int fail(const char* why) { fprintf(stderr, "    %s\n", why); return 1; }
// Counts open descriptors; a lowest-free probe misses a leak above a descriptor freed meanwhile.
static int open_fds(void) { int n = 0; for (int fd = 0; fd < 1024; fd++) n += fcntl(fd, F_GETFD) != -1; return n; }

static int g_q1 = -1, g_q2 = -1;
static void quiet(int on) {
    fflush(stdout); fflush(stderr);
    if (on) {
        g_q1 = dup(1); g_q2 = dup(2);
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd, 1); dup2(fd, 2); close(fd);
    } else {
        dup2(g_q1, 1); dup2(g_q2, 2); close(g_q1); close(g_q2);
    }
}

static unsigned char* read_file(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* b = malloc(*len + 1);
    if (b && fread(b, 1, *len, f) != *len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void write_file(const char* path, const void* b, size_t n) {
    FILE* f = fopen(path, "wb");
    if (!f || fwrite(b, 1, n, f) != n) { perror(path); exit(2); }
    fclose(f);
}

// ── Checkpoint ───────────────────────────────────────────────────

static unsigned char* g_ck;   // tiny model written by save_checkpoint
static size_t g_ck_len;

static TrainConfig tiny_cfg(void) {
    TrainConfig c;
    memset(&c, 0, sizeof(c));
    c.vocab_size = TOK_BASE; c.n_embd = 8; c.n_heads = 2; c.n_layers = 1; c.seq_len = 8;
    c.hidden_dim = compute_hidden_dim(c.n_embd);
    return c;
}

static int get_int(const unsigned char* b, size_t off) { int v; memcpy(&v, b + off, sizeof(v)); return v; }

// Offset of tensor record k: 0 wte, 1 wpe, 2 lm_head, 3..6 wq0..wo0, 7 w1_0, 8 w3_0, 9 w2_0.
static size_t rec_off(int k) {
    size_t off = 32;
    for (int i = 0; i < k; i++) off += 4 + 4 * (size_t)get_int(g_ck, off);
    return off;
}

static const char* ck_patched(size_t off, int v) {
    unsigned char* b = malloc(g_ck_len);
    memcpy(b, g_ck, g_ck_len);
    memcpy(b + off, &v, sizeof(v));
    write_file("bad.bin", b, g_ck_len);
    free(b);
    return "bad.bin";
}

static const char* ck_prefix(size_t n) { write_file("bad.bin", g_ck, n); return "bad.bin"; }

static char g_out[16384];

static int load_captured(TrainConfig* cfg, const char* path) {
    fflush(stdout); fflush(stderr);
    int o = dup(1), e = dup(2), fd = open("capture.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
    dup2(fd, 1); dup2(fd, 2); close(fd);
    int rc = load_checkpoint(cfg, path);
    fflush(stdout); fflush(stderr);
    dup2(o, 1); dup2(e, 2); close(o); close(e);
    FILE* f = fopen("capture.txt", "r");
    size_t n = f ? fread(g_out, 1, sizeof(g_out) - 1, f) : 0;
    if (f) fclose(f);
    g_out[n] = 0;
    return rc;
}

static int ckpt_refused(TrainConfig* cfg, const char* path, const char* want) {
    long live = g_live;
    int fds = open_fds();
    int rc = load_captured(cfg, path);
    if (rc != -1) return fail("load_checkpoint accepted the file");
    if (!strstr(g_out, want)) { fprintf(stderr, "    want \"%s\" in:\n%s", want, g_out); return 1; }
    if (g_live != live) return fail("allocation left behind");
    if (open_fds() != fds) return fail("descriptor left behind");
    return 0;
}

static int ckpt_roundtrip(void) {
    static const char* names[] = {"wte", "wpe", "lm_head", "wq0", "wk0", "wv0", "wo0",
                                  "w1_0", "w3_0", "w2_0"};
    float* want[10];
    int lens[10];
    for (int i = 0; i < 10; i++) {
        const float* d = am_get_var_array(names[i], &lens[i]);
        if (!d) return fail("tensor missing after init_weights");
        want[i] = malloc((size_t)lens[i] * sizeof(float));
        memcpy(want[i], d, (size_t)lens[i] * sizeof(float));
        float* z = calloc((size_t)lens[i], sizeof(float));
        am_set_var_array(names[i], z, lens[i]);
        free(z);
    }
    TrainConfig c = tiny_cfg();
    long live = g_live;
    int fds = open_fds();
    int rc = load_captured(&c, "janus_ckpt_step7.bin");
    if (rc != 7) { fprintf(stderr, "    rc %d:\n%s", rc, g_out); return 1; }
    if (c.vocab_size != TOK_BASE) return fail("vocab not taken from the header");
    if (g_live != live) return fail("allocation left behind");
    if (open_fds() != fds) return fail("descriptor left behind");
    for (int i = 0; i < 10; i++) {
        int len = 0;
        const float* d = am_get_var_array(names[i], &len);
        if (!d || len != lens[i] || memcmp(d, want[i], (size_t)len * sizeof(float)))
            return fail(names[i]);
    }
    return 0;
}

static int ckpt_bad_magic(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(0, 0), "bad checkpoint");
}
static int ckpt_vocab_zero(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(4, 0), "checkpoint vocab 0 outside 1..4096");
}
static int ckpt_vocab_huge(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(4, 1 << 30), "checkpoint vocab 1073741824 outside 1..4096");
}
static int ckpt_dim_mismatch(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(8, 16), "D=16 H=2 L=1 differs from config D=8 H=2 L=1");
}
static int ckpt_heads_mismatch(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(12, 4), "D=8 H=4 L=1 differs from config D=8 H=2 L=1");
}
static int ckpt_layers_mismatch(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(16, 2), "D=8 H=2 L=2 differs from config D=8 H=2 L=1");
}
static int ckpt_hidden_negative(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(28, -1), "checkpoint hidden -1 outside 0..16*8");
}
static int ckpt_hidden_huge(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(28, 16 * 8 + 1), "checkpoint hidden 129 outside 0..16*8");
}
static int ckpt_len_mismatch(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_patched(rec_off(1), 63), "tensor wpe has 63 floats, expected 8 x 8")
        || ckpt_refused(&c, ck_patched(rec_off(1), 65), "tensor wpe has 65 floats, expected 8 x 8");
}
static int ckpt_ends_before_tensor(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_prefix(rec_off(1)), "checkpoint ends before tensor wpe")
        || ckpt_refused(&c, ck_prefix(rec_off(9)), "checkpoint ends before tensor w2_0");
}
static int ckpt_truncated_in_tensor(void) {
    TrainConfig c = tiny_cfg();
    return ckpt_refused(&c, ck_prefix(rec_off(1) + 4 + 100), "tensor wpe is truncated")
        || ckpt_refused(&c, ck_prefix(g_ck_len - 1), "tensor w2_0 is truncated");
}
static int ckpt_shape_overflow(void) {
    TrainConfig c = tiny_cfg();
    c.n_embd = 1 << 20;   // wte 4096 x 2^20 does not fit in int
    int img[9] = {0x4A414E55, TOK_MAX_VOCAB, 1 << 20, 2, 1, 8, 7, 0, 0};
    write_file("bad.bin", img, sizeof(img));
    return ckpt_refused(&c, "bad.bin", "tensor wte has 0 floats, expected 4096 x 1048576");
}
static int ckpt_alloc_fail(void) {
    TrainConfig c = tiny_cfg();
    const char* path = ck_prefix(g_ck_len);
    g_fail_calloc = 1;
    return ckpt_refused(&c, path, "tensor wte: cannot allocate 2072 floats");
}
static int ckpt_install_refused(void) {
    TrainConfig c = tiny_cfg();
    c.n_embd = 4096;   // wte 259 x 4096 exceeds AM_MAX_ARRAY_SIZE
    size_t n = (size_t)TOK_BASE * 4096;
    unsigned char* b = calloc(1, 36 + n * sizeof(float));
    int hdr[9] = {0x4A414E55, TOK_BASE, 4096, 2, 1, 8, 7, 0, (int)n};
    memcpy(b, hdr, sizeof(hdr));
    write_file("bad.bin", b, 36 + n * sizeof(float));
    free(b);
    return ckpt_refused(&c, "bad.bin", "tensor wte (259 x 4096) refused by the runtime");
}
static int ckpt_every_prefix(void) {
    TrainConfig c = tiny_cfg();
    write_file("bad.bin", g_ck, g_ck_len);
    quiet(1);
    long live = g_live;
    int fds = open_fds();
    for (size_t n = g_ck_len; n-- > 0; ) {
        if (truncate("bad.bin", (off_t)n) != 0) { quiet(0); return fail("truncate"); }
        int rc = load_checkpoint(&c, "bad.bin");
        if (rc != -1 || g_live != live || open_fds() != fds) {
            quiet(0);
            fprintf(stderr, "    prefix %zu: rc %d live %+ld\n", n, rc, g_live - live);
            return 1;
        }
    }
    quiet(0);
    return 0;
}

// ── Tokenizer ────────────────────────────────────────────────────

static EvolvingTokenizer* g_tok;   // trained: vocab TOK_BASE + 8, 8 merges
static EvolvingTokenizer* g_w;     // g_tok extended to full tables; cases edit their copy

static void tok_header(int hdr[8], int sv, int sm) {
    long tc = g_w->trained_chars;
    int h[8] = {TOK_MAGIC, sv, sm, g_w->bpe_enabled, (int)(tc >> 32), (int)(tc & 0xFFFFFFFF),
                g_w->merges_per_round, 0};
    memcpy(hdr, h, sizeof(h));
}

// hdr[1] - TOK_BASE token records and hdr[2] merge records taken from g_w; ids past the
// tables get a 1-byte token or the merge {0, 0, TOK_BASE}.
static void write_tok(const char* path, const int hdr[8]) {
    static const unsigned char pad[128];
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fwrite(hdr, sizeof(int), 8, f);
    for (int i = TOK_BASE; i < hdr[1]; i++) {
        int in = i < TOK_MAX_VOCAB, bl = in ? g_w->tokens[i].byte_len : 1;
        fwrite(&bl, sizeof(int), 1, f);
        if (bl > 0) fwrite(in && bl <= 64 ? g_w->tokens[i].bytes : pad, 1, (size_t)bl, f);
    }
    for (int k = 0; k < hdr[2]; k++) {
        Merge m = k < TOK_MAX_MERGES ? g_w->merges[k] : (Merge){0, 0, TOK_BASE};
        int r[3] = {m.a, m.b, m.result};
        fwrite(r, sizeof(int), 3, f);
    }
    fclose(f);
}

static int tok_refused(const char* path) {
    EvolvingTokenizer* t = malloc(sizeof(*t));
    EvolvingTokenizer* before = malloc(sizeof(*t));
    memset(t, 0x5A, sizeof(*t));
    memcpy(before, t, sizeof(*t));
    long live = g_live;
    int fds = open_fds();
    int rc = tok_load(t, path);
    int bad = rc != -1 ? fail("tok_load accepted the file")
            : memcmp(t, before, sizeof(*t)) ? fail("tokenizer changed by a refused file")
            : g_live != live ? fail("allocation left behind")
            : open_fds() != fds ? fail("descriptor left behind") : 0;
    free(t);
    free(before);
    return bad;
}

static int tok_written_refused(int sv, int sm) {
    int hdr[8];
    tok_header(hdr, sv, sm);
    write_tok("bad.tok", hdr);
    return tok_refused("bad.tok");
}

static int tok_roundtrip(void) {
    if (g_tok->vocab_size != TOK_BASE + 8 || g_tok->n_merges != 8)
        return fail("fixture tokenizer did not learn 8 merges");
    tok_save(g_tok, "a.tok");
    EvolvingTokenizer* t = malloc(sizeof(*t));
    memset(t, 0x5A, sizeof(*t));
    long live = g_live;
    int fds = open_fds();
    if (tok_load(t, "a.tok") != 0) return fail("valid tokenizer refused");
    if (g_live != live) return fail("allocation left behind");
    if (open_fds() != fds) return fail("descriptor left behind");
    if (t->vocab_size != g_tok->vocab_size || t->n_merges != g_tok->n_merges ||
        t->bpe_enabled != g_tok->bpe_enabled || t->trained_chars != g_tok->trained_chars ||
        t->merges_per_round != g_tok->merges_per_round)
        return fail("header fields differ");
    for (int i = 0; i < t->vocab_size; i++)
        if (t->tokens[i].byte_len != g_tok->tokens[i].byte_len ||
            memcmp(t->tokens[i].bytes, g_tok->tokens[i].bytes, (size_t)t->tokens[i].byte_len))
            return fail("token bytes differ");
    if (memcmp(t->merges, g_tok->merges, (size_t)t->n_merges * sizeof(Merge)))
        return fail("merges differ");
    tok_save(t, "b.tok");
    size_t la = 0, lb = 0;
    unsigned char *a = read_file("a.tok", &la), *b = read_file("b.tok", &lb);
    if (!a || !b || la != lb || memcmp(a, b, la)) return fail("re-saved file differs");
    static const unsigned char text[] = "the field remembers";
    int ia[64], ib[64];
    int na = tok_encode_raw(g_tok, text, (int)sizeof(text) - 1, ia, 64);
    int nb = tok_encode_raw(t, text, (int)sizeof(text) - 1, ib, 64);
    if (na != nb || na >= (int)sizeof(text) - 1 || memcmp(ia, ib, (size_t)na * sizeof(int)))
        return fail("encoding differs");
    return 0;
}

static int tok_trained_chars_high_bit(void) {
    int hdr[8];
    tok_header(hdr, TOK_BASE + 8, 8);
    hdr[4] = -1; hdr[5] = -1;
    write_tok("hi.tok", hdr);
    EvolvingTokenizer* t = malloc(sizeof(*t));
    if (tok_load(t, "hi.tok") != 0) return fail("valid tokenizer refused");
    if (t->trained_chars != -1L) return fail("trained_chars not -1");
    return 0;
}

static int tok_bad_magic(void) {
    int hdr[8];
    tok_header(hdr, TOK_BASE + 8, 8);
    hdr[0] = 0;
    write_tok("bad.tok", hdr);
    return tok_refused("bad.tok");
}
static int tok_missing_file(void) { return tok_refused("no-such-dir/none.tok"); }
static int tok_vocab_over_max(void) { return tok_written_refused(TOK_MAX_VOCAB + 1, 0); }
static int tok_vocab_under_base(void) {
    return tok_written_refused(TOK_BASE - 1, 0) || tok_written_refused(INT_MIN, 0);
}
static int tok_merges_negative(void) { return tok_written_refused(TOK_BASE + 8, -1); }
static int tok_merges_over_max(void) { return tok_written_refused(TOK_MAX_VOCAB, TOK_MAX_MERGES + 1); }
static int tok_merges_exceed_vocab(void) { return tok_written_refused(TOK_BASE + 8, 9); }
static int tok_bytelen_over(void) {
    g_w->tokens[TOK_BASE].byte_len = 65;
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_bytelen_nonpositive(void) {
    g_w->tokens[TOK_BASE].byte_len = 0;
    if (tok_written_refused(TOK_BASE + 8, 8)) return 1;
    g_w->tokens[TOK_BASE].byte_len = -1;
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_result_ge_vocab(void) {
    g_w->merges[0] = (Merge){0, 0, TOK_BASE + 8};
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_result_under_base(void) {
    g_w->merges[0] = (Merge){0, 0, TOK_BASE - 1};
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_a_ge_result(void) {
    g_w->merges[0] = (Merge){TOK_BASE, 0, TOK_BASE};
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_a_negative(void) {
    g_w->merges[0] = (Merge){-1, 0, TOK_BASE};
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_b_ge_result(void) {
    g_w->merges[0] = (Merge){0, TOK_BASE, TOK_BASE};
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_b_negative(void) {
    g_w->merges[0] = (Merge){0, -1, TOK_BASE};
    return tok_written_refused(TOK_BASE + 8, 8);
}
static int tok_truncated(void) {
    for (int sm = 8; sm >= 0; sm -= 8) {   // with merges, and with the last token record last
        int hdr[8];
        tok_header(hdr, TOK_BASE + 8, sm);
        write_tok("cut.tok", hdr);
        struct stat st;
        stat("cut.tok", &st);
        for (off_t n = st.st_size; n-- > 0; ) {
            if (truncate("cut.tok", n) != 0) return fail("truncate");
            if (tok_refused("cut.tok")) { fprintf(stderr, "    sm %d prefix %lld\n", sm, (long long)n); return 1; }
        }
    }
    return 0;
}

// ── Driver ───────────────────────────────────────────────────────

static void setup(void) {
    TrainConfig c = tiny_cfg();
    quiet(1);
    am_init();
    am_persistent_mode(1);
    init_weights(&c);
    save_checkpoint(&c, 7, NULL);
    quiet(0);
    g_ck = read_file("janus_ckpt_step7.bin", &g_ck_len);
    if (!g_ck) { perror("janus_ckpt_step7.bin"); exit(2); }

    static const char text[] = "the field remembers the field returns the field resonates ";
    unsigned char data[4096];
    size_t n = 0;
    while (n + sizeof(text) - 1 <= sizeof(data)) { memcpy(data + n, text, sizeof(text) - 1); n += sizeof(text) - 1; }
    g_tok = malloc(sizeof(*g_tok));
    tok_init(g_tok, 1, 8, 1);
    tok_train_bpe(g_tok, data, (long)n, 8);
    g_tok->bpe_enabled = 1;
    g_tok->trained_chars = 123456789012L;
    g_w = malloc(sizeof(*g_w));
    *g_w = *g_tok;
    for (int i = g_w->vocab_size; i < TOK_MAX_VOCAB; i++) {
        g_w->tokens[i].byte_len = 1;
        g_w->tokens[i].bytes[0] = 'x';
    }
    for (int k = g_w->n_merges; k < TOK_MAX_MERGES; k++) g_w->merges[k] = (Merge){0, 0, TOK_BASE};
}

#define CASE(f) {#f, f}
static const struct { const char* name; int (*fn)(void); } cases[] = {
    CASE(tok_roundtrip), CASE(tok_trained_chars_high_bit), CASE(tok_bad_magic),
    CASE(tok_missing_file), CASE(tok_vocab_over_max), CASE(tok_vocab_under_base),
    CASE(tok_merges_negative), CASE(tok_merges_over_max), CASE(tok_merges_exceed_vocab),
    CASE(tok_bytelen_over), CASE(tok_bytelen_nonpositive), CASE(tok_result_ge_vocab),
    CASE(tok_result_under_base), CASE(tok_a_ge_result), CASE(tok_a_negative),
    CASE(tok_b_ge_result), CASE(tok_b_negative), CASE(tok_truncated),
    CASE(ckpt_roundtrip), CASE(ckpt_bad_magic), CASE(ckpt_vocab_zero), CASE(ckpt_vocab_huge),
    CASE(ckpt_dim_mismatch), CASE(ckpt_heads_mismatch), CASE(ckpt_layers_mismatch),
    CASE(ckpt_hidden_negative), CASE(ckpt_hidden_huge), CASE(ckpt_len_mismatch),
    CASE(ckpt_ends_before_tensor), CASE(ckpt_truncated_in_tensor), CASE(ckpt_shape_overflow),
    CASE(ckpt_alloc_fail), CASE(ckpt_install_refused), CASE(ckpt_every_prefix),
};

int main(int argc, char** argv) {
    setup();
    int ran = 0, failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int pick = argc < 2;
        for (int a = 1; a < argc; a++) pick |= !strcmp(argv[a], cases[i].name);
        if (!pick) continue;
        fflush(stdout); fflush(stderr);
        pid_t pid = fork();
        if (pid == 0) _exit(cases[i].fn() ? 1 : 0);
        int st = 0;
        waitpid(pid, &st, 0);
        int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
        if (!ok && WIFSIGNALED(st)) fprintf(stderr, "    signal %d\n", WTERMSIG(st));
        printf("  %s [%s]\n", ok ? "PASS" : "FAIL", cases[i].name);
        ran++;
        failed += !ok;
    }
    printf("janus loaders: %d/%d cases passed\n", ran - failed, ran);
    return failed || !ran ? 1 : 0;
}
