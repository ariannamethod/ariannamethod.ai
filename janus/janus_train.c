//go:build ignore

// janus_train.c — standalone training host (own main + repo-root includes); the
// ignore build tag keeps `go build -buildmode=c-shared` from pulling it into the
// libjanus Go package. Build it directly with cc / train_lambda.sh.
#define _POSIX_C_SOURCE 200809L
// janus_train.c — C training host for Janus transformer (v2: EvolvingTokenizer)
// Byte-level → BPE language model. No Python. No PyTorch.
//
// Usage: ./janus_train <text_file> [options]
//   --steps N        total training steps (default: 1000)
//   --lr F           learning rate (default: 0.001)
//   --seq-len N      sequence length (default: 128)
//   --n-embd N       embedding dim (default: 128)
//   --n-heads N      attention heads (default: 4)
//   --n-layers N     transformer layers (default: 4)
//   --log-every N    log interval (default: 10)
//   --save-every N   checkpoint interval (default: 100)
//   --resume PATH    resume from checkpoint
//   --min-lr F       min LR for cosine decay (default: 3e-5)
//   --weight-decay F AdamW weight decay (default: 0.1)
//   --grad-clip F    gradient clipping (default: 1.0)
//   --beta1/beta2 F  Adam betas (default: 0.9/0.95)
//   --warmup-steps N LR warmup (default: 300)
//   --bpe-after N    enable BPE after N bytes (default: 1)
//   --bpe-merges N   merges per round (default: 2000)
//   --bpe-retrain N  retrain every N bytes (default: 4000)
//   --evolve-every N check tokenizer evolution every N steps (default: 500)
//
// Build:
//   cc -O3 -march=native -o janus_train janus_train.c core/ariannamethod.c -lm -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "core/ariannamethod.h"
#include "janus/janus_tokenizer.h"
#ifdef USE_CUDA
#include "core/ariannamethod_cuda.h"
#endif

static FILE* g_logfile = NULL;
static void trainlog(const char* fmt, ...);

// ═══════════════════════════════════════════════════════════════════
// Config
// ═══════════════════════════════════════════════════════════════════

typedef struct {
    int    vocab_size;
    int    n_embd;
    int    hidden_dim;    // FFN hidden dimension (SwiGLU 2.67x n_embd)
    int    n_heads;
    int    n_layers;
    int    seq_len;
    float  lr;
    float  min_lr;        // minimum LR for cosine decay (lr * 0.1)
    float  weight_decay;  // AdamW weight decay (0.1)
    float  grad_clip;     // gradient clipping max norm (1.0)
    float  beta1;         // Adam beta1 (0.9)
    float  beta2;         // Adam beta2 (0.95 for transformers)
    int    grad_accum;    // gradient accumulation steps (effective batch = grad_accum)
    int    warmup_steps;  // LR warmup steps
    int    total_steps;
    int    log_every;
    int    save_every;
    int    evolve_every;
    char   data_path[512];
    char   model_path[512];
    char   resume_path[512];
    char   log_path[512];
    long   bpe_after;
    int    bpe_merges;
    long   bpe_retrain;
} TrainConfig;

// Compute SwiGLU hidden_dim: (n_embd * 8/3) rounded up to multiple of 16
static int compute_hidden_dim(int n_embd) {
    int h = (n_embd * 8) / 3;
    return (h + 15) & ~15;  // round up to 16
}

// ═══════════════════════════════════════════════════════════════════
// Auto-download training data from HuggingFace (FineWeb-Edu)
// Zero Python. Zero pip. Just curl + C.
// Paginated: fetches in chunks to avoid timeouts.
// ═══════════════════════════════════════════════════════════════════

#define DL_HF_DATASET "HuggingFaceFW/fineweb-edu"
#define DL_HF_CONFIG  "sample-10BT"
#define DL_HF_SPLIT   "train"
#define DL_CHUNK_SIZE  100
#define DL_TOTAL_ROWS  50000
#define DL_MIN_SIZE    100000

static long hf_extract_texts(const char* json, long json_len, FILE* out) {
    long total = 0;
    const char* p = json;
    const char* end = json + json_len;
    
    while (p < end - 8) {
        if (p[0] == '"' && p[1] == 't' && p[2] == 'e' && p[3] == 'x' && 
            p[4] == 't' && p[5] == '"' && p[6] == ':' && p[7] == '"') {
            p += 8;
            const char* start = p;
            while (p < end) {
                if (*p == '\\') { p += 2; continue; }
                if (*p == '"') break;
                p++;
            }
            const char* s = start;
            while (s < p) {
                if (*s == '\\' && s + 1 < p) {
                    s++;
                    switch (*s) {
                        case 'n':  fputc('\n', out); total++; break;
                        case 't':  fputc('\t', out); total++; break;
                        case 'r':  break;
                        case '"':  fputc('"', out); total++; break;
                        case '\\': fputc('\\', out); total++; break;
                        case 'u':
                            if (s + 4 < p) {
                                unsigned int cp = 0;
                                for (int i = 1; i <= 4; i++) {
                                    char c = s[i]; cp <<= 4;
                                    if (c >= '0' && c <= '9') cp |= c - '0';
                                    else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
                                    else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
                                }
                                s += 4;
                                if (cp < 0x80) { fputc(cp, out); total++; }
                                else if (cp < 0x800) {
                                    fputc(0xC0|(cp>>6), out); fputc(0x80|(cp&0x3F), out); total+=2;
                                } else {
                                    fputc(0xE0|(cp>>12), out); fputc(0x80|((cp>>6)&0x3F), out);
                                    fputc(0x80|(cp&0x3F), out); total+=3;
                                }
                            }
                            break;
                        default: fputc(*s, out); total++; break;
                    }
                    s++;
                } else { fputc(*s, out); total++; s++; }
            }
            fputs("\n\n", out); total += 2;
            if (*p == '"') p++;
        } else { p++; }
    }
    return total;
}

static int data_auto_download(const char* out_path) {
    struct stat st;
    if (stat(out_path, &st) == 0 && st.st_size > DL_MIN_SIZE) {
        printf("[data] found existing %s (%ld bytes)\n", out_path, (long)st.st_size);
        return 0;
    }
    
    printf("[data] downloading FineWeb-Edu from HuggingFace...\n");
    printf("[data] no Python. no pip. just curl + C.\n");
    
    FILE* fout = fopen(out_path, "w");
    if (!fout) { fprintf(stderr, "[data] cannot create %s\n", out_path); return -1; }
    
    char tmp_json[256];
    snprintf(tmp_json, sizeof(tmp_json), "%s.tmp", out_path);
    
    long total_bytes = 0;
    int chunks = (DL_TOTAL_ROWS + DL_CHUNK_SIZE - 1) / DL_CHUNK_SIZE;
    
    for (int c = 0; c < chunks; c++) {
        int offset = c * DL_CHUNK_SIZE;
        int length = DL_CHUNK_SIZE;
        if (offset + length > DL_TOTAL_ROWS) length = DL_TOTAL_ROWS - offset;
        
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "curl -sL --connect-timeout 15 --max-time 60 "
            "'https://datasets-server.huggingface.co/rows?"
            "dataset=%s&config=%s&split=%s&offset=%d&length=%d' "
            "-o '%s' 2>/dev/null",
            DL_HF_DATASET, DL_HF_CONFIG, DL_HF_SPLIT,
            offset, length, tmp_json);
        
        int rc = system(cmd);
        if (rc != 0) {
            printf("\n[data] chunk %d/%d failed, stopping\n", c+1, chunks);
            break;
        }
        
        /* Open first, then fstat the descriptor — checking the path with stat()
         * and reopening by name is a TOCTOU window (the file may change between). */
        FILE* fj = fopen(tmp_json, "rb");
        if (!fj) { printf("x"); fflush(stdout); continue; }
        struct stat jst;
        if (fstat(fileno(fj), &jst) != 0 || jst.st_size < 100) {
            fclose(fj);
            printf("x"); fflush(stdout);
            continue;
        }
        char* json = (char*)malloc(jst.st_size + 1);
        if (!json) { fclose(fj); continue; }
        long jlen = fread(json, 1, jst.st_size, fj);
        json[jlen] = 0;
        fclose(fj);
        
        long extracted = hf_extract_texts(json, jlen, fout);
        free(json);
        total_bytes += extracted;
        
        printf("\r[data] %d/%d chunks, %.1f MB extracted",
               c+1, chunks, (double)total_bytes / (1024*1024));
        fflush(stdout);
    }
    
    printf("\n");
    fclose(fout);
    remove(tmp_json);
    
    if (total_bytes < DL_MIN_SIZE) {
        fprintf(stderr, "[data] only %ld bytes extracted — not enough\n", total_bytes);
        remove(out_path);
        return -1;
    }
    
    printf("[data] done: %.1f MB clean text from FineWeb-Edu\n",
           (double)total_bytes / (1024*1024));
    return 0;
}

// ═══════════════════════════════════════════════════════════════════
// Data loader
// ═══════════════════════════════════════════════════════════════════

typedef struct {
    unsigned char* raw;
    long           raw_size;
    int*           ids;
    long           ids_len;
    long           pos;
} DataLoader;

static int data_load(DataLoader* dl, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    dl->raw_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (dl->raw_size <= 0) { fclose(f); return 1; }
    dl->raw = (unsigned char*)malloc(dl->raw_size);
    size_t rd = fread(dl->raw, 1, dl->raw_size, f);
    fclose(f);
    dl->raw_size = (long)rd;
    dl->ids = NULL;
    dl->ids_len = 0;
    dl->pos = 0;
    return 0;
}

static void data_encode(DataLoader* dl, EvolvingTokenizer* tok) {
    if (dl->ids) free(dl->ids);
    dl->ids = (int*)malloc(dl->raw_size * sizeof(int));
    if (!dl->ids) { fprintf(stderr, "ERROR: OOM encoding\n"); exit(1); }
    dl->ids_len = tok_encode_raw(tok, dl->raw, (int)dl->raw_size, dl->ids, (int)dl->raw_size);
    dl->pos = 0;
    printf("Encoded: %ld bytes → %ld tokens (ratio %.2fx)\n",
           dl->raw_size, dl->ids_len, (double)dl->raw_size / dl->ids_len);
}

static void data_get_batch(DataLoader* dl, float* tokens, float* targets, int seq_len) {
    // Random position each batch — prevents memorizing data order
    dl->pos = rand() % (dl->ids_len - seq_len - 1);
    for (int i = 0; i < seq_len; i++) {
        tokens[i] = (float)dl->ids[dl->pos + i];
        targets[i] = (float)dl->ids[dl->pos + i + 1];
    }
}

static void data_free(DataLoader* dl) {
    if (dl->raw) free(dl->raw);
    if (dl->ids) free(dl->ids);
    memset(dl, 0, sizeof(*dl));
}

// ═══════════════════════════════════════════════════════════════════
// Weight init — FIXED: FFN uses proper hidden_dim expansion
// ═══════════════════════════════════════════════════════════════════

static long count_params(TrainConfig* cfg) {
    return (long)cfg->vocab_size * cfg->n_embd +                          // wte
           (long)cfg->seq_len * cfg->n_embd +                             // wpe
           (long)cfg->n_layers * 4 * cfg->n_embd * cfg->n_embd +         // attn: wq,wk,wv,wo
           (long)cfg->n_layers * 2 * cfg->hidden_dim * cfg->n_embd +     // w1,w3: [hidden,embd]
           (long)cfg->n_layers * cfg->n_embd * cfg->hidden_dim +         // w2: [embd,hidden]
           (long)cfg->vocab_size * cfg->n_embd;                           // lm_head
}

/* Append printf-formatted text to buf at offset *off, saturating *off at cap.
 * snprintf returns the length it *would* have written, so accumulating it raw can
 * push *off past cap and make the next `cap - *off` underflow. Clamping keeps every
 * append in bounds. */
static void aml_appendf(char *buf, size_t cap, int *off, const char *fmt, ...) {
    if (*off < 0 || (size_t)*off >= cap) { *off = (int)cap; return; }
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf + *off, cap - (size_t)*off, fmt, ap);
    va_end(ap);
    if (r < 0) return;
    *off = ((size_t)r >= cap - (size_t)*off) ? (int)cap : *off + r;
}

static void init_weights(TrainConfig* cfg) {
    char script[32768];
    int n = 0; aml_appendf(script, sizeof(script), &n,
        "wte = matrix(%d, %d, 0.02)\nwpe = matrix(%d, %d, 0.02)\n",
        cfg->vocab_size, cfg->n_embd, cfg->seq_len, cfg->n_embd);
    for (int l = 0; l < cfg->n_layers; l++) {
        aml_appendf(script, sizeof(script), &n,
            "wq%d = matrix(%d, %d, 0.02)\nwk%d = matrix(%d, %d, 0.02)\n"
            "wv%d = matrix(%d, %d, 0.02)\nwo%d = matrix(%d, %d, 0.02)\n"
            "w1_%d = matrix(%d, %d, 0.02)\nw3_%d = matrix(%d, %d, 0.02)\n"
            "w2_%d = matrix(%d, %d, 0.02)\n",
            l, cfg->n_embd, cfg->n_embd, l, cfg->n_embd, cfg->n_embd,
            l, cfg->n_embd, cfg->n_embd, l, cfg->n_embd, cfg->n_embd,
            l, cfg->hidden_dim, cfg->n_embd, l, cfg->hidden_dim, cfg->n_embd,
            l, cfg->n_embd, cfg->hidden_dim);
    }
    aml_appendf(script, sizeof(script), &n,
        "lm_head = matrix(%d, %d, 0.02)\n", cfg->vocab_size, cfg->n_embd);
    if (am_exec(script) != 0) {
        fprintf(stderr, "ERROR: weight init: %s\n", am_get_error()); exit(1);
    }
    long p = count_params(cfg);
    printf("Parameters: %ld (%.2f M)\n", p, (double)p / 1e6);
}

// ═══════════════════════════════════════════════════════════════════
// Model scripts:
//   forward_script — TAPE START + params + forward + backward + accum grads + clear
//   step_script    — TAPE START + params + forward + backward + apply accum + clip + adamw + clear
//
// For grad_accum=1: just use step_script every step (no accumulation)
// For grad_accum>1: run forward_script (N-1) times, then step_script once
// ═══════════════════════════════════════════════════════════════════

static void build_tape_header(char* s, int* n, int bufsize, TrainConfig* cfg) {
    aml_appendf(s, bufsize, n, "TAPE START\nTAPE PARAM_NO_DECAY wte\nTAPE PARAM_NO_DECAY wpe\n");
    for (int l = 0; l < cfg->n_layers; l++) {
        aml_appendf(s, bufsize, n,
            "TAPE PARAM wq%d\nTAPE PARAM wk%d\nTAPE PARAM wv%d\nTAPE PARAM wo%d\n"
            "TAPE PARAM w1_%d\nTAPE PARAM w3_%d\nTAPE PARAM w2_%d\n",
            l, l, l, l, l, l, l);
    }
    aml_appendf(s, bufsize, n, "TAPE PARAM lm_head\n");
}

static void build_forward_body(char* s, int* n, int bufsize, TrainConfig* cfg) {
    aml_appendf(s, bufsize, n, "h = seq_embed(wte, wpe, tokens, seq_len)\n");
    for (int l = 0; l < cfg->n_layers; l++) {
        aml_appendf(s, bufsize, n,
            "h_norm = seq_rmsnorm(h, seq_len, n_embd)\n"
            "q = seq_matvec(wq%d, h_norm, seq_len)\n"
            "k = seq_matvec(wk%d, h_norm, seq_len)\n"
            "v = seq_matvec(wv%d, h_norm, seq_len)\n"
            "attn_out = multi_head_attention(q, k, v, seq_len, n_embd, n_heads)\n"
            "attn_proj = seq_matvec(wo%d, attn_out, seq_len)\n"
            "h = add(h, attn_proj)\n"
            "h_norm = seq_rmsnorm(h, seq_len, n_embd)\n"
            "gate_pre = seq_matvec(w1_%d, h_norm, seq_len)\n"
            "gate = silu(gate_pre)\n"
            "up = seq_matvec(w3_%d, h_norm, seq_len)\n"
            "mlp_out = mul(gate, up)\n"
            "mlp_proj = seq_matvec(w2_%d, mlp_out, seq_len)\n"
            "h = add(h, mlp_proj)\n",
            l, l, l, l, l, l, l);
    }
    aml_appendf(s, bufsize, n,
        "h_norm = seq_rmsnorm(h, seq_len, n_embd)\n"
        "logits = seq_matvec(lm_head, h_norm, seq_len)\n"
        "loss = seq_cross_entropy(logits, targets, seq_len, vocab_size)\n"
        "TAPE BACKWARD loss\n");
}

// forward_script: forward + backward + accumulate grads + clear tape (grads saved in acc_grad)
static char* generate_forward_script(TrainConfig* cfg) {
    int bufsize = cfg->n_layers * 768 + 1024;
    char* s = (char*)malloc(bufsize);
    if (!s) return NULL;
    int n = 0;
    build_tape_header(s, &n, bufsize, cfg);
    build_forward_body(s, &n, bufsize, cfg);
    aml_appendf(s, bufsize, &n,
        "TAPE ACCUM_GRADS\n"
        "TAPE CLEAR\n");
    return s;
}

// step_script: forward + backward + apply accumulated grads + clip + adamw + clear
static char* generate_step_script(TrainConfig* cfg) {
    int bufsize = cfg->n_layers * 768 + 1024;
    char* s = (char*)malloc(bufsize);
    if (!s) return NULL;
    int n = 0;
    build_tape_header(s, &n, bufsize, cfg);
    build_forward_body(s, &n, bufsize, cfg);
    aml_appendf(s, bufsize, &n,
        "TAPE ACCUM_GRADS\n"
        "TAPE APPLY_ACCUM grad_accum\n"
        "TAPE CLIP_GRADS grad_clip\n"
        "TAPE ADAMW_STEP lr weight_decay beta1 beta2\n"
        "TAPE CLEAR\n");
    return s;
}

// ═══════════════════════════════════════════════════════════════════
// Checkpoint save/load — v2 format with hidden_dim in header[7]
// ═══════════════════════════════════════════════════════════════════

static void save_checkpoint(TrainConfig* cfg, int step, EvolvingTokenizer* tok) {
    char path[256];
    snprintf(path, sizeof(path), "janus_ckpt_step%d.bin", step);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    int header[8] = {0x4A414E55, cfg->vocab_size, cfg->n_embd, cfg->n_heads,
                     cfg->n_layers, cfg->seq_len, step, cfg->hidden_dim};
    fwrite(header, sizeof(int), 8, f);

    const char* gnames[] = {"wte", "wpe", "lm_head", NULL};
    for (int i = 0; gnames[i]; i++) {
        int len = 0;
        const float* data = am_get_var_array(gnames[i], &len);
        if (data && len > 0) { fwrite(&len, sizeof(int), 1, f); fwrite(data, sizeof(float), len, f); }
    }
    for (int l = 0; l < cfg->n_layers; l++) {
        const char* sfx[] = {"wq", "wk", "wv", "wo", "w1_", "w3_", "w2_", NULL};
        for (int s = 0; sfx[s]; s++) {
            char name[32];
            snprintf(name, sizeof(name), "%s%d", sfx[s], l);
            int len = 0;
            const float* data = am_get_var_array(name, &len);
            if (data && len > 0) { fwrite(&len, sizeof(int), 1, f); fwrite(data, sizeof(float), len, f); }
        }
    }
    long fsize = ftell(f);
    fclose(f);
    trainlog("  Checkpoint: %s (%.2f MB)\n", path, (double)fsize / (1024*1024));

    if (tok && tok->n_merges > 0) {
        char tpath[256];
        snprintf(tpath, sizeof(tpath), "janus_tok_step%d.bin", step);
        tok_save(tok, tpath);
        trainlog("  Tokenizer: %s (vocab=%d, merges=%d)\n", tpath, tok->vocab_size, tok->n_merges);
    }
}

static int load_checkpoint(TrainConfig* cfg, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return -1; }
    int header[8];
    if (fread(header, sizeof(int), 8, f) != 8 || header[0] != 0x4A414E55) {
        fprintf(stderr, "ERROR: bad checkpoint\n"); fclose(f); return -1;
    }
    int ckpt_step = header[6];
    cfg->vocab_size = header[1];
    int ckpt_hidden = header[7];  // 0 for old checkpoints (square FFN)
    
    printf("Loading checkpoint: step %d (V=%d D=%d H=%d L=%d hidden=%d)\n",
           ckpt_step, header[1], header[2], header[3], header[4],
           ckpt_hidden > 0 ? ckpt_hidden : header[2]);

    struct { const char* name; int rows; int cols; } globals[] = {
        {"wte",     cfg->vocab_size, cfg->n_embd},
        {"wpe",     cfg->seq_len,    cfg->n_embd},
        {"lm_head", cfg->vocab_size, cfg->n_embd},
    };
    for (int i = 0; i < 3; i++) {
        int len; fread(&len, sizeof(int), 1, f);
        float* buf = (float*)malloc(len * sizeof(float));
        fread(buf, sizeof(float), len, f);
        am_set_var_matrix(globals[i].name, buf, globals[i].rows, globals[i].cols);
        free(buf);
    }
    
    // Per-layer weights: attention are always [n_embd, n_embd]
    // FFN depends on checkpoint format (old=square, new=proper hidden_dim)
    int ffn_hidden = ckpt_hidden > 0 ? ckpt_hidden : cfg->n_embd;
    
    for (int l = 0; l < cfg->n_layers; l++) {
        // Attention weights: wq, wk, wv, wo — always [n_embd, n_embd]
        const char* attn_sfx[] = {"wq", "wk", "wv", "wo", NULL};
        for (int s = 0; attn_sfx[s]; s++) {
            char name[32];
            snprintf(name, sizeof(name), "%s%d", attn_sfx[s], l);
            int len; fread(&len, sizeof(int), 1, f);
            float* buf = (float*)malloc(len * sizeof(float));
            fread(buf, sizeof(float), len, f);
            am_set_var_matrix(name, buf, cfg->n_embd, cfg->n_embd);
            free(buf);
        }
        // FFN weights: w1_[hidden,embd], w3_[hidden,embd], w2_[embd,hidden]
        {
            char name[32];
            int len; float* buf;
            
            snprintf(name, 32, "w1_%d", l);
            fread(&len, sizeof(int), 1, f);
            buf = (float*)malloc(len * sizeof(float));
            fread(buf, sizeof(float), len, f);
            am_set_var_matrix(name, buf, ffn_hidden, cfg->n_embd);
            free(buf);
            
            snprintf(name, 32, "w3_%d", l);
            fread(&len, sizeof(int), 1, f);
            buf = (float*)malloc(len * sizeof(float));
            fread(buf, sizeof(float), len, f);
            am_set_var_matrix(name, buf, ffn_hidden, cfg->n_embd);
            free(buf);
            
            snprintf(name, 32, "w2_%d", l);
            fread(&len, sizeof(int), 1, f);
            buf = (float*)malloc(len * sizeof(float));
            fread(buf, sizeof(float), len, f);
            am_set_var_matrix(name, buf, cfg->n_embd, ffn_hidden);
            free(buf);
        }
    }
    fclose(f);
    printf("Loaded: %d weights from step %d\n", 3 + cfg->n_layers * 7, ckpt_step);
    return ckpt_step;
}

// ═══════════════════════════════════════════════════════════════════
// Args
// ═══════════════════════════════════════════════════════════════════

static void parse_args(int argc, char** argv, TrainConfig* cfg) {
    cfg->vocab_size = TOK_BASE;
    cfg->n_embd = 128; cfg->n_heads = 4; cfg->n_layers = 4;
    cfg->seq_len = 256; cfg->lr = 3e-4f;
    cfg->min_lr = 3e-5f;           // 10x decay (matches arianna.c)
    cfg->weight_decay = 0.1f;      // AdamW weight decay
    cfg->grad_clip = 1.0f;         // gradient clipping
    cfg->beta1 = 0.9f;             // Adam beta1
    cfg->beta2 = 0.95f;            // Adam beta2 (0.95 for transformers)
    cfg->grad_accum = 4;           // gradient accumulation (effective batch = 4)
    cfg->warmup_steps = 300;       // warmup steps
    cfg->total_steps = 1000; cfg->log_every = 10; cfg->save_every = 100;
    cfg->evolve_every = 500;
    cfg->bpe_after = 1; cfg->bpe_merges = 2000; cfg->bpe_retrain = 4000;
    cfg->hidden_dim = 0;  // computed after n_embd is set
    memset(cfg->data_path, 0, 512); memset(cfg->model_path, 0, 512);
    memset(cfg->resume_path, 0, 512); memset(cfg->log_path, 0, 512);

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            snprintf(cfg->data_path, 512, "%s", argv[i]);
        } else if (!strcmp(argv[i], "--steps") && i+1 < argc)      cfg->total_steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lr") && i+1 < argc)            cfg->lr = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--seq-len") && i+1 < argc)       cfg->seq_len = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n-embd") && i+1 < argc)        cfg->n_embd = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n-heads") && i+1 < argc)       cfg->n_heads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n-layers") && i+1 < argc)      cfg->n_layers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hidden-dim") && i+1 < argc)    cfg->hidden_dim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--log-every") && i+1 < argc)     cfg->log_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save-every") && i+1 < argc)    cfg->save_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--evolve-every") && i+1 < argc)  cfg->evolve_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--model") && i+1 < argc)         snprintf(cfg->model_path, 512, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--resume") && i+1 < argc)        snprintf(cfg->resume_path, 512, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--log-file") && i+1 < argc)      snprintf(cfg->log_path, 512, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--min-lr") && i+1 < argc)         cfg->min_lr = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--weight-decay") && i+1 < argc)  cfg->weight_decay = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--grad-clip") && i+1 < argc)     cfg->grad_clip = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--beta1") && i+1 < argc)         cfg->beta1 = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--beta2") && i+1 < argc)         cfg->beta2 = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--warmup-steps") && i+1 < argc)  cfg->warmup_steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--grad-accum") && i+1 < argc)    cfg->grad_accum = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bpe-after") && i+1 < argc)     cfg->bpe_after = atol(argv[++i]);
        else if (!strcmp(argv[i], "--bpe-merges") && i+1 < argc)    cfg->bpe_merges = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bpe-retrain") && i+1 < argc)   cfg->bpe_retrain = atol(argv[++i]);
        else if (!strcmp(argv[i], "--help")) {
            printf("Usage: janus_train <text_file> [options]\n"
                   "  --steps N         training steps (default: 1000)\n"
                   "  --lr F            learning rate (default: 3e-4)\n"
                   "  --min-lr F        min LR for cosine decay (default: 3e-5)\n"
                   "  --weight-decay F  AdamW weight decay (default: 0.1)\n"
                   "  --grad-clip F     gradient clipping max norm (default: 1.0)\n"
                   "  --beta1 F         Adam beta1 (default: 0.9)\n"
                   "  --beta2 F         Adam beta2 (default: 0.95)\n"
                   "  --warmup-steps N  LR warmup steps (default: 300)\n"
                   "  --grad-accum N    gradient accumulation steps (default: 4)\n"
                   "  --seq-len N       sequence length (default: 256)\n"
                   "  --n-embd N        embedding dim (default: 128)\n"
                   "  --n-heads N       attention heads (default: 4)\n"
                   "  --n-layers N      transformer layers (default: 4)\n"
                   "  --hidden-dim N    FFN hidden dim (default: 2.67x n_embd)\n"
                   "  --log-every N     log interval (default: 10)\n"
                   "  --save-every N    checkpoint interval (default: 100)\n"
                   "  --evolve-every N  tokenizer evolution interval (default: 500)\n"
                   "  --resume PATH     resume from checkpoint\n"
                   "  --bpe-after N     enable BPE after N bytes (default: 1)\n"
                   "  --bpe-merges N    merges per round (default: 2000)\n"
                   "  --bpe-retrain N   retrain every N bytes (default: 4000)\n");
            exit(0);
        }
    }
    if (!cfg->data_path[0]) {
        fprintf(stderr, "ERROR: no data file\nUsage: janus_train <text_file> [options]\n");
        exit(1);
    }
    if (cfg->n_embd % cfg->n_heads != 0) {
        fprintf(stderr, "ERROR: n_embd %d not divisible by n_heads %d\n",
                cfg->n_embd, cfg->n_heads); exit(1);
    }
    // Compute hidden_dim if not explicitly set
    if (cfg->hidden_dim <= 0) {
        cfg->hidden_dim = compute_hidden_dim(cfg->n_embd);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════

static void trainlog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); fflush(stdout);
    if (g_logfile) { va_start(ap, fmt); vfprintf(g_logfile, fmt, ap); va_end(ap); fflush(g_logfile); }
}

int main(int argc, char** argv) {
    srand((unsigned)time(NULL));
    TrainConfig cfg;
    parse_args(argc, argv, &cfg);

    if (cfg.log_path[0]) {
        g_logfile = fopen(cfg.log_path, "a");
        if (!g_logfile) fprintf(stderr, "WARNING: cannot open log %s\n", cfg.log_path);
    }

    printf("═══════════════════════════════════════════════════\n");
    printf("  JANUS v4 — AdamW + SwiGLU + EvolvingTokenizer\n");
    printf("  No Python. No PyTorch. BPE grows during training.\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("Data:       %s\n", cfg.data_path);
    printf("Embedding:  %d\n", cfg.n_embd);
    printf("Hidden:     %d (%.1fx expansion)\n", cfg.hidden_dim, (float)cfg.hidden_dim / cfg.n_embd);
    printf("Heads:      %d (head_dim=%d)\n", cfg.n_heads, cfg.n_embd / cfg.n_heads);
    printf("Layers:     %d\n", cfg.n_layers);
    printf("Seq len:    %d\n", cfg.seq_len);
    printf("LR:         %.6f → %.6f (cosine, warmup=%d)\n", cfg.lr, cfg.min_lr, cfg.warmup_steps);
    printf("AdamW:      wd=%.3f beta1=%.3f beta2=%.3f clip=%.1f\n",
           cfg.weight_decay, cfg.beta1, cfg.beta2, cfg.grad_clip);
    printf("Batch:      grad_accum=%d (effective batch=%d)\n", cfg.grad_accum, cfg.grad_accum);
    printf("Steps:      %d\n", cfg.total_steps);
    printf("BPE:        after %ld bytes, %d merges/round, retrain every %ld bytes\n",
           cfg.bpe_after, cfg.bpe_merges, cfg.bpe_retrain);
    if (cfg.resume_path[0]) printf("Resume:     %s\n", cfg.resume_path);
    printf("─────────────────────────────────────────────────\n");
    fflush(stdout);

    DataLoader dl;

    {
        struct stat _st;
        if (stat(cfg.data_path, &_st) != 0 || _st.st_size < DL_MIN_SIZE) {
            printf("[data] %s not found or too small, downloading FineWeb-Edu...\n", cfg.data_path);
            if (data_auto_download(cfg.data_path) != 0) {
                fprintf(stderr, "ERROR: could not download data. provide a file manually.\n");
                return 1;
            }
        }
    }
    if (data_load(&dl, cfg.data_path) != 0) return 1;
    printf("Data:       %ld bytes (%.2f MB)\n", dl.raw_size, (double)dl.raw_size / (1024*1024));

    EvolvingTokenizer tok;
    tok_init(&tok, cfg.bpe_after, cfg.bpe_merges, cfg.bpe_retrain);

    if (cfg.resume_path[0]) {
        char tpath[512];
        snprintf(tpath, sizeof(tpath), "%s", cfg.resume_path);
        char* p = strstr(tpath, "ckpt");
        if (p) { memcpy(p, "tok_", 4); }
        if (tok_load(&tok, tpath) == 0) {
            printf("Tokenizer loaded: vocab=%d, merges=%d, bpe=%s\n",
                   tok.vocab_size, tok.n_merges, tok.bpe_enabled ? "ON" : "OFF");
            cfg.vocab_size = tok.vocab_size;
        } else {
            printf("No tokenizer file, starting byte-level (vocab=%d)\n", cfg.vocab_size);
        }
    }

    if (!tok.bpe_enabled) {
        int changed = tok_maybe_evolve(&tok, dl.raw, dl.raw_size);
        if (changed) cfg.vocab_size = tok.vocab_size;
    }

    data_encode(&dl, &tok);

    if (dl.ids_len < cfg.seq_len + 2) {
        fprintf(stderr, "ERROR: encoded data too small (%ld tokens) for seq_len %d\n",
                dl.ids_len, cfg.seq_len); return 1;
    }

    char* forward_script = generate_forward_script(&cfg);
    char* step_script = generate_step_script(&cfg);
    if (!forward_script || !step_script) { fprintf(stderr, "ERROR: OOM\n"); return 1; }

    am_init();
    am_persistent_mode(1);
#ifdef USE_CUDA
    if (gpu_init() != 0) fprintf(stderr, "GPU init failed\n");
#endif

    {
        char hyper[512];
        snprintf(hyper, sizeof(hyper),
                 "n_embd = %d\nn_heads = %d\nn_layer = %d\nvocab_size = %d\nseq_len = %d\n"
                 "lr = %.6f\nweight_decay = %.6f\ngrad_clip = %.6f\nbeta1 = %.6f\nbeta2 = %.6f\n"
                 "grad_accum = %d\n",
                 cfg.n_embd, cfg.n_heads, cfg.n_layers, cfg.vocab_size, cfg.seq_len,
                 cfg.lr, cfg.weight_decay, cfg.grad_clip, cfg.beta1, cfg.beta2,
                 cfg.grad_accum);
        am_exec(hyper);
    }

    // Pre-compile training scripts for fast execution
    void* compiled_fwd = am_compile(forward_script);
    void* compiled_step = am_compile(step_script);
    if (!compiled_fwd || !compiled_step) {
        fprintf(stderr, "ERROR: failed to compile training scripts\n");
        return 1;
    }

    int start_step = 0;
    if (cfg.resume_path[0]) {
        start_step = load_checkpoint(&cfg, cfg.resume_path);
        if (start_step < 0) return 1;
        printf("Resuming from step %d → %d\n", start_step, cfg.total_steps);
    } else {
        init_weights(&cfg);
    }
    printf("Vocab:      %d (%s)\n", cfg.vocab_size,
           tok.bpe_enabled ? "BPE" : "byte-level");
    long p = count_params(&cfg);
    printf("Parameters: %ld (%.2f M)\n", p, (double)p / 1e6);
    printf("─────────────────────────────────────────────────\n");
    fflush(stdout);

    float* tokens = (float*)malloc(cfg.seq_len * sizeof(float));
    float* targets = (float*)malloc(cfg.seq_len * sizeof(float));

    float loss_sum = 0;
    int loss_count = 0;
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (int step = start_step + 1; step <= cfg.total_steps; step++) {
        // Cosine LR schedule with warmup (matches PyTorch/nanoGPT)
        float lr;
        if (step <= cfg.warmup_steps) {
            lr = cfg.lr * (float)step / (float)cfg.warmup_steps;
        } else {
            float decay_ratio = (float)(step - cfg.warmup_steps) /
                               (float)(cfg.total_steps - cfg.warmup_steps);
            float coeff = 0.5f * (1.0f + cosf(3.14159265f * decay_ratio));
            lr = cfg.min_lr + coeff * (cfg.lr - cfg.min_lr);
        }
        { char cmd[64]; snprintf(cmd, 64, "lr = %.8f", lr); am_exec(cmd); }

        // Gradient accumulation: run forward+backward N times, step once
        float micro_loss_sum = 0;
        int ga = cfg.grad_accum > 1 ? cfg.grad_accum : 1;
        for (int micro = 0; micro < ga; micro++) {
            data_get_batch(&dl, tokens, targets, cfg.seq_len);
            am_set_var_array("tokens", tokens, cfg.seq_len);
            am_set_var_array("targets", targets, cfg.seq_len);

            // Last micro-batch: also does clip + adamw + clear
            // Other micro-batches: only forward + backward + accum + clear
            void* cscript = (micro == ga - 1) ? compiled_step : compiled_fwd;
            int rc = am_exec_compiled(cscript);
            if (rc != 0) {
                fprintf(stderr, "ERROR at step %d micro %d: %s\n", step, micro, am_get_error());
                goto done;
            }
            micro_loss_sum += am_get_var_float("loss");
        }

        float loss = micro_loss_sum / (float)ga;
        float gnorm = am_get_var_float("grad_norm");
        loss_sum += loss;
        loss_count++;

        if (step % cfg.log_every == 0 || step == 1) {
            clock_gettime(CLOCK_MONOTONIC, &t_end);
            double elapsed = (t_end.tv_sec - t_start.tv_sec) +
                           (t_end.tv_nsec - t_start.tv_nsec) / 1e9;
            double tps = (double)((step - start_step) * cfg.seq_len) / elapsed;
            float avg = loss_sum / loss_count;
            trainlog("step %5d | loss %.4f (avg %.4f) | lr %.2e | gnorm %.4f | %.0f tok/s | %.1fs | V=%d\n",
                     step, loss, avg, lr, gnorm, tps, elapsed, cfg.vocab_size);
            loss_sum = 0;
            loss_count = 0;
        }

        if (cfg.evolve_every > 0 && step % cfg.evolve_every == 0) {
            int old_v = tok.vocab_size;
            int changed = tok_maybe_evolve(&tok, dl.raw, dl.raw_size);
            if (changed) {
                tok_expand_embeddings(old_v, tok.vocab_size, cfg.n_embd);
                cfg.vocab_size = tok.vocab_size;
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "vocab_size = %d", cfg.vocab_size);
                am_exec(cmd);
                data_encode(&dl, &tok);
                trainlog("[tok] vocab evolved to %d, data re-encoded to %ld tokens\n",
                         cfg.vocab_size, dl.ids_len);
            }
        }

        if (step % cfg.save_every == 0) {
            save_checkpoint(&cfg, step, &tok);
        }
    }

done:
    am_free_compiled(compiled_fwd);
    am_free_compiled(compiled_step);
    save_checkpoint(&cfg, cfg.total_steps, &tok);

    free(tokens); free(targets); free(forward_script); free(step_script);
    data_free(&dl);

    printf("─────────────────────────────────────────────────\n");
    printf("Training complete. Final vocab: %d\n", cfg.vocab_size);
    return 0;
}
