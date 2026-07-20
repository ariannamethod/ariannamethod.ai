// janus_tokenizer.h — EvolvingTokenizer for Janus
// BPE that grows during training. Port from molequla (Go → C).
// Pure C, zero deps. Include once in your .c file.
//
// "Vocab grows like mycelium. Old weights stay. New ones emerge."

#ifndef JANUS_TOKENIZER_H
#define JANUS_TOKENIZER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOK_MAX_VOCAB    4096
#define TOK_MAX_MERGES   (TOK_MAX_VOCAB - 259)
#define TOK_BOS          256
#define TOK_EOS          257
#define TOK_PAD          258
#define TOK_BASE         259

typedef struct {
    unsigned char bytes[64];
    int byte_len;
} TokenDef;

typedef struct {
    int a, b, result;
} Merge;

typedef struct {
    TokenDef tokens[TOK_MAX_VOCAB];
    int      vocab_size;

    Merge    merges[TOK_MAX_MERGES];
    int      n_merges;

    int      bpe_enabled;
    long     trained_chars;

    long     enable_after_chars;   // default 20000
    int      merges_per_round;     // default 384
    long     retrain_every_chars;  // default 4000
} EvolvingTokenizer;

// ── Init ─────────────────────────────────────────────────────────

static void tok_init(EvolvingTokenizer* t, long enable_after,
                     int merges_per_round, long retrain_every) {
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < 256; i++) {
        t->tokens[i].bytes[0] = (unsigned char)i;
        t->tokens[i].byte_len = 1;
    }
    t->tokens[TOK_BOS].byte_len = 0;
    t->tokens[TOK_EOS].byte_len = 0;
    t->tokens[TOK_PAD].byte_len = 0;
    t->vocab_size = TOK_BASE;
    t->enable_after_chars   = enable_after > 0       ? enable_after   : 20000;
    t->merges_per_round     = merges_per_round > 0   ? merges_per_round : 384;
    t->retrain_every_chars  = retrain_every > 0      ? retrain_every  : 4000;
}

// ── Pair hash table ──────────────────────────────────────────────

#define PHT_SIZE 262144
#define PHT_MASK (PHT_SIZE - 1)

typedef struct { int a, b, count, used; } PairSlot;
typedef struct { PairSlot slots[PHT_SIZE]; } PairHT;

static void      pht_clear(PairHT* h) { memset(h->slots, 0, sizeof(h->slots)); }
static unsigned   pht_hash(int a, int b) { return (unsigned)(a*7919 + b*104729) & PHT_MASK; }

static int* pht_get(PairHT* h, int a, int b) {
    unsigned idx = pht_hash(a, b);
    for (int i = 0; i < PHT_SIZE; i++) {
        PairSlot* s = &h->slots[(idx + i) & PHT_MASK];
        if (!s->used) { s->a = a; s->b = b; s->count = 0; s->used = 1; return &s->count; }
        if (s->a == a && s->b == b) return &s->count;
    }
    return NULL;
}

static void pht_best(PairHT* h, int* oa, int* ob, int* oc) {
    *oa = -1; *ob = -1; *oc = 0;
    for (int i = 0; i < PHT_SIZE; i++) {
        PairSlot* s = &h->slots[i];
        if (s->used && s->count > *oc) { *oa = s->a; *ob = s->b; *oc = s->count; }
    }
}

// ── BPE Training ─────────────────────────────────────────────────

static void tok_train_bpe(EvolvingTokenizer* t, const unsigned char* data,
                          long data_len, int num_merges) {
    if (data_len <= 1 || num_merges <= 0) return;

    long max_seq = data_len;
    if (max_seq > 2*1024*1024) max_seq = 2*1024*1024; // cap at 2M for speed
    int* seq = (int*)malloc(max_seq * sizeof(int));
    if (!seq) return;

    long seq_len = 0;
    for (long i = 0; i < data_len && seq_len < max_seq; i++)
        seq[seq_len++] = (int)data[i];

    PairHT* ht = (PairHT*)calloc(1, sizeof(PairHT));
    if (!ht) { free(seq); return; }

    for (int m = 0; m < num_merges && t->vocab_size < TOK_MAX_VOCAB; m++) {
        pht_clear(ht);
        for (long i = 0; i < seq_len - 1; i++) {
            int* c = pht_get(ht, seq[i], seq[i+1]);
            if (c) (*c)++;
        }

        int ba, bb, bc;
        pht_best(ht, &ba, &bb, &bc);
        if (bc < 2) break;

        int nid = t->vocab_size;
        TokenDef* ta = &t->tokens[ba];
        TokenDef* tb = &t->tokens[bb];
        int nlen = ta->byte_len + tb->byte_len;
        if (nlen > 63) continue;

        memcpy(t->tokens[nid].bytes, ta->bytes, ta->byte_len);
        memcpy(t->tokens[nid].bytes + ta->byte_len, tb->bytes, tb->byte_len);
        t->tokens[nid].byte_len = nlen;

        t->merges[t->n_merges] = (Merge){ba, bb, nid};
        t->n_merges++;
        t->vocab_size++;

        // Apply merge to sequence
        long j = 0;
        for (long i = 0; i < seq_len; i++) {
            if (i < seq_len - 1 && seq[i] == ba && seq[i+1] == bb) {
                seq[j++] = nid;
                i++;
            } else {
                seq[j++] = seq[i];
            }
        }
        seq_len = j;
    }
    free(ht);
    free(seq);
}

// ── Maybe evolve (call during training) ──────────────────────────
// Returns 1 if vocab changed

static int tok_maybe_evolve(EvolvingTokenizer* t, const unsigned char* data, long data_len) {
    int old_v = t->vocab_size;
    if (!t->bpe_enabled) {
        if (data_len >= t->enable_after_chars) {
            printf("[tok] BPE ON (corpus %ldB >= %ld)\n", data_len, t->enable_after_chars);
            tok_train_bpe(t, data, data_len, t->merges_per_round);
            t->bpe_enabled = 1;
            t->trained_chars = data_len;
            printf("[tok] %d merges, vocab %d → %d\n", t->n_merges, old_v, t->vocab_size);
            return 1;
        }
    } else {
        if (data_len - t->trained_chars >= t->retrain_every_chars) {
            printf("[tok] retrain BPE (+%ld bytes)\n", data_len - t->trained_chars);
            tok_train_bpe(t, data, data_len, t->merges_per_round);
            t->trained_chars = data_len;
            if (t->vocab_size != old_v)
                printf("[tok] vocab %d → %d\n", old_v, t->vocab_size);
            return (t->vocab_size != old_v);
        }
    }
    return 0;
}

// ── Encode (text → token IDs, no BOS/EOS) ────────────────────────
// Returns number of IDs written

static int tok_encode_raw(EvolvingTokenizer* t, const unsigned char* text,
                          int text_len, int* out, int max_out) {
    if (text_len <= 0) return 0;

    // Start with byte tokens
    int n = 0;
    for (int i = 0; i < text_len && n < max_out; i++)
        out[n++] = (int)text[i];

    if (!t->bpe_enabled) return n;

    // Apply merges in order
    for (int m = 0; m < t->n_merges; m++) {
        int a = t->merges[m].a, b = t->merges[m].b, r = t->merges[m].result;
        int j = 0;
        for (int i = 0; i < n; i++) {
            if (i < n - 1 && out[i] == a && out[i+1] == b) {
                out[j++] = r;
                i++;
            } else {
                out[j++] = out[i];
            }
        }
        n = j;
    }
    return n;
}

// ── Decode (token ID → bytes) ────────────────────────────────────

static int tok_decode_token(EvolvingTokenizer* t, int id,
                            unsigned char* buf, int max) {
    if (id < 0 || id >= t->vocab_size) return 0;
    if (id == TOK_BOS || id == TOK_EOS || id == TOK_PAD) return 0;
    TokenDef* td = &t->tokens[id];
    int n = td->byte_len < max ? td->byte_len : max;
    memcpy(buf, td->bytes, n);
    return n;
}

static int tok_decode(EvolvingTokenizer* t, const int* ids, int n_ids,
                      unsigned char* buf, int max) {
    int pos = 0;
    for (int i = 0; i < n_ids && pos < max - 1; i++)
        pos += tok_decode_token(t, ids[i], buf + pos, max - pos);
    buf[pos] = 0;
    return pos;
}

// ── Save/Load ────────────────────────────────────────────────────

#define TOK_MAGIC 0x544F4B45

static int tok_save(EvolvingTokenizer* t, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    int hdr[8] = {TOK_MAGIC, t->vocab_size, t->n_merges, t->bpe_enabled,
                  (int)(t->trained_chars >> 32), (int)(t->trained_chars & 0xFFFFFFFF),
                  t->merges_per_round, 0};
    fwrite(hdr, sizeof(int), 8, f);
    // Merged token defs (only new ones beyond base 259)
    for (int i = TOK_BASE; i < t->vocab_size; i++) {
        fwrite(&t->tokens[i].byte_len, sizeof(int), 1, f);
        fwrite(t->tokens[i].bytes, 1, t->tokens[i].byte_len, f);
    }
    // Merges
    for (int i = 0; i < t->n_merges; i++) {
        int m[3] = {t->merges[i].a, t->merges[i].b, t->merges[i].result};
        fwrite(m, sizeof(int), 3, f);
    }
    fclose(f);
    return 0;
}

static int tok_load(EvolvingTokenizer* t, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    int hdr[8];
    if (fread(hdr, sizeof(int), 8, f) != 8 || hdr[0] != TOK_MAGIC) { fclose(f); return -1; }
    tok_init(t, 0, hdr[6], 0);
    t->bpe_enabled = hdr[3];
    t->trained_chars = ((long)hdr[4] << 32) | (unsigned)hdr[5];
    int sv = hdr[1], sm = hdr[2];
    for (int i = TOK_BASE; i < sv; i++) {
        int bl;
        if (fread(&bl, sizeof(int), 1, f) != 1) { fclose(f); return -1; }
        t->tokens[i].byte_len = bl;
        if (fread(t->tokens[i].bytes, 1, bl, f) != (size_t)bl) { fclose(f); return -1; }
    }
    t->vocab_size = sv;
    for (int i = 0; i < sm; i++) {
        int m[3];
        if (fread(m, sizeof(int), 3, f) != 3) { fclose(f); return -1; }
        t->merges[i] = (Merge){m[0], m[1], m[2]};
    }
    t->n_merges = sm;
    fclose(f);
    return 0;
}

// ── Expand AML embeddings when vocab grows ───────────────────────
// Needs am_get_var_array / am_set_var_matrix from ariannamethod.h

static int tok_expand_embeddings(int old_vocab, int new_vocab, int n_embd) {
    if (new_vocab <= old_vocab) return 0;
    const char* names[] = {"wte", "lm_head", NULL};
    for (int n = 0; names[n]; n++) {
        int len = 0;
        const float* old = am_get_var_array(names[n], &len);
        if (!old || len != old_vocab * n_embd) continue;
        float* buf = (float*)calloc((size_t)new_vocab * n_embd, sizeof(float));
        if (!buf) return -1;
        memcpy(buf, old, (size_t)old_vocab * n_embd * sizeof(float));
        for (int i = old_vocab * n_embd; i < new_vocab * n_embd; i++)
            buf[i] = ((float)rand() / RAND_MAX - 0.5f) * 0.04f;
        am_set_var_matrix(names[n], buf, new_vocab, n_embd);
        free(buf);
        printf("[tok] expanded %s: %d → %d rows\n", names[n], old_vocab, new_vocab);
    }
    return 1;
}

#endif // JANUS_TOKENIZER_H
