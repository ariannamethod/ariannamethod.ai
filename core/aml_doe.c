// Optional live DOE sessions backed by the NoTorch CPU Harness.
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ariannamethod.h"
#include "harness/archs.h"
#include "harness/experts.h"
#include "examples/bpe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    gguf_file *host;
    void *model;
    bpe_tokenizer *tok;
    nt_dims dims;
    kv_cache *kv;
    nt_experts *experts;
    nt_consolidation *pending;
    int capacity, pos, active, stopped, eos;
    int *ids;
    float *logits;
} DoeSession;

static const char *await(void *ptr) {
    DoeSession *s = ptr;
    return nt_experts_await(&s->experts, &s->pending) ? "DOE consolidation failed" : NULL;
}

static const char *end_session(void *ptr) {
    DoeSession *s = ptr;
    const char *error = await(s);
    nt_experts_free(s->experts);
    kv_free(s->kv);
    bpe_free(s->tok);
    if (s->model) nt_arch_llama.free(s->model);
    if (s->host) gguf_close(s->host);
    free(s->ids);
    free(s->logits);
    free(s);
    return error;
}

static const char *start_session(const char *path, const char *spore, int capacity, void **out) {
    DoeSession *s = calloc(1, sizeof(*s));
    if (!s) return "DOE session allocation failed";
    const char *error = "DOE host path exceeds the GGUF reader limit";
    if (strlen(path) >= sizeof(s->host->path)) goto fail;
    error = "DOE needs a readable dense llama-family GGUF";
    s->host = gguf_open(path);
    if (!s->host || nt_pick_arch(s->host->arch) != &nt_arch_llama) goto fail;
    error = "DOE context exceeds the host context";
    if (s->host->ctx_len > 0 && capacity > s->host->ctx_len) goto fail;
    error = "DOE model load failed";
    s->model = nt_arch_llama.load(s->host, &s->dims);
    if (!s->model || s->dims.vocab < 1 || s->dims.vocab > AM_MAX_ARRAY_SIZE) goto fail;
    s->tok = bpe_load(path);
    error = "DOE tokenizer vocabulary mismatch";
    if (s->tok && bpe_n_vocab(s->tok) != s->dims.vocab) goto fail;
    const gguf_kv *eos = gguf_get_kv(s->host, "tokenizer.ggml.eos_token_id");
    s->eos = eos ? (int)eos->val.u32 : -1;
    error = "DOE expert state or host fingerprint rejected";
    s->experts = nt_experts_open(s->host, spore, NULL);
    if (!s->experts) goto fail;
    error = "DOE workspace allocation failed";
    s->capacity = capacity;
    s->ids = malloc(((size_t)capacity+1) * sizeof(*s->ids));
    s->logits = malloc((size_t)s->dims.vocab * sizeof(*s->logits));
    s->kv = kv_new(s->dims.n_layers, capacity, s->dims.kv_dim);
    if (!s->ids || !s->logits || !s->kv) goto fail;
    *out = s;
    return NULL;
fail:
    end_session(s);
    return error;
}

static const char *encode(void *ptr, const char *text, AM_Array **out) {
    DoeSession *s = ptr;
    if (!s->tok) return "DOE host has no GGUF tokenizer";
    int n = bpe_encode(s->tok, text, s->ids, s->capacity+1);
    if (n <= 0 || n > s->capacity) return "DOE text is empty or exceeds context";
    *out = am_array_new(n);
    if (!*out) return "DOE token array allocation failed";
    for (int i = 0; i < n; i++) (*out)->data[i] = (float)s->ids[i];
    return NULL;
}

static const char *forward(DoeSession *s, const int *ids, int n) {
    while (n > 0) {
        int k = n < NT_PREFILL_CHUNK ? n : NT_PREFILL_CHUNK;
        int rc = nt_llama_forward_hooks(s->model, s->kv, ids, k, s->pos,
            n == k ? s->logits : NULL, nt_experts_attend, nt_experts_observe, s->experts);
        if (rc != NT_OK) return nt_strerror(rc);
        s->pos += k;
        ids += k;
        n -= k;
    }
    for (int i = 0; i < s->dims.vocab; i++)
        if (!isfinite(s->logits[i])) return "DOE forward produced a nonfinite logit";
    return NULL;
}

static const char *logits(void *ptr, AM_Array **out) {
    DoeSession *s = ptr;
    if (!s->active || s->stopped) return "DOE logits require an active, unstopped turn";
    *out = am_array_new(s->dims.vocab);
    if (!*out) return "DOE logits allocation failed";
    memcpy((*out)->data, s->logits, (size_t)s->dims.vocab * sizeof(float));
    return NULL;
}

static const char *prompt(void *ptr, const AM_Array *tokens, AM_Array **out) {
    DoeSession *s = ptr;
    if (s->active) return "finish the active DOE turn before a new prompt";
    if (tokens->len < 1 || tokens->len > s->capacity-s->pos) return "DOE prompt exceeds available context";
    for (int i = 0; i < tokens->len; i++) {
        float id = tokens->data[i];
        if (!(id >= 0 && id < s->dims.vocab) || id != truncf(id)) return "invalid DOE prompt token";
        s->ids[i] = (int)id;
    }
    const char *error = await(s);
    if (!error) error = forward(s, s->ids, tokens->len);
    if (error) return error;
    s->active = 1;
    return logits(s, out);
}

static const char *choose(void *ptr, int request, int *chosen) {
    DoeSession *s = ptr;
    if (!s->active || s->stopped) return "DOE choice requires an active, unstopped turn";
    if (request < -1 || request >= s->dims.vocab) return "DOE choice outside vocabulary";
    int best = sample(s->logits, s->dims.vocab, 0);
    int id = request == -1 ? best : request;
    s->stopped = s->tok ? id == s->eos || bpe_is_eog(s->tok, id) : id <= 2;
    if (!s->stopped && s->pos == s->capacity) return "DOE choice exceeds available context";
    nt_experts_choice(s->experts, s->logits[best], s->logits[id]);
    *chosen = id;
    return s->stopped ? NULL : forward(s, &id, 1);
}

static const char *finish(void *ptr, AM_DoeMode mode) {
    DoeSession *s = ptr;
    if (!s->active) return "DOE finish requires an active turn";
    int rc = mode == AM_DOE_ASYNC ? nt_experts_submit(&s->experts, &s->pending)
                                  : nt_experts_finish(s->experts, mode == AM_DOE_LEARN);
    if (rc) return "DOE turn commit failed";
    s->active = s->stopped = 0;
    return NULL;
}

static const char *reset(void *ptr) {
    DoeSession *s = ptr;
    if (s->active) return "finish the active DOE turn before resetting KV";
    s->pos = 0; // Dense attention reads only the prefix below its write position.
    return NULL;
}

static const char *save(void *ptr, const char *path) {
    DoeSession *s = ptr;
    if (s->active) return "finish the active DOE turn before saving";
    const char *error = await(s);
    if (error) return error;
    return nt_experts_save(s->experts, path) ? "could not publish new completed DOE spore" : NULL;
}

static const char *transplant(void *ptr, const char *path, int slot) {
    DoeSession *s = ptr;
    if (s->active) return "finish the active DOE turn before transplanting";
    const char *error = await(s);
    if (error) return error;
    nt_experts *donor = nt_experts_open(s->host, path, NULL);
    if (!donor) return "DOE donor spore or host fingerprint rejected";
    int rc = nt_experts_copy_factors(s->experts, donor, slot);
    nt_experts_free(donor);
    return rc ? "DOE transplant requires matching living seats at completed turns" : NULL;
}

void am_use_notorch_doe(void) {
    const AM_DoeBackend backend = {
        .open = start_session, .close = end_session, .encode = encode, .prompt = prompt,
        .choose = choose, .logits = logits, .finish = finish, .reset = reset,
        .wait = await, .save = save, .transplant = transplant
    };
    am_set_doe_backend(&backend);
}
