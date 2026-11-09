// Real packed sessions through the interpreter, bytecode and stepped programs.
#include "ariannamethod.h"
#include "gguf.h"
#include "harness/arch.h"
#ifdef NT_DOE_DECODER_API
#include "tests/qwen35_fixture.h"
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int run(int mode, const char *source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void *p = am_compile(source);
        if (!p) return 1;
        int rc = am_exec_compiled(p);
        am_free_compiled(p);
        return rc;
    }
    void *p = am_program_open(source);
    if (!p) return 1;
    while (!am_program_step(p, 1)) {}
    return am_program_close(p);
}

/* Only the BPE is separate. Its real vocabulary may be smaller than the head. */
static void write_tokenizer(const char *path, const char *family, int vocab) {
    const char *tokens[41], *merges[] = {"a b"};
    char words[41][12];
    assert(vocab >= 3 && vocab <= 41);
    for (int i = 0; i < vocab; i++) {
        snprintf(words[i], sizeof(words[i]), "unused%d", i);
        tokens[i] = words[i];
    }
    tokens[0] = "a"; tokens[1] = "b"; tokens[2] = "ab";
    gguf_writer *w = gguf_write_open(path);
    assert(w);
    gguf_write_kv_str(w, "general.architecture", family);
    gguf_write_kv_str(w, "tokenizer.ggml.model", "gpt2");
    gguf_write_kv_str_array(w, "tokenizer.ggml.tokens", tokens, vocab);
    gguf_write_kv_str_array(w, "tokenizer.ggml.merges", merges, 1);
    gguf_write_kv_u32(w, "tokenizer.ggml.eos_token_id", (uint32_t)(vocab - 1));
    assert(gguf_write_close(w) == 0);
}

#ifdef NT_DOE_DECODER_API
static void check_hybrid_reset(int mode) {
    char source[1024];
    snprintf(source, sizeof(source),
        "doe_open('hybrid/host.gguf','',16)\n"
        "doe_prompt(doe_encode('ab'))\ndoe_choose(0)\ndoe_finish('async')\n"
        "doe_save('hybrid%d.gguf')\ndoe_reset()\n"
        "reset = doe_prompt([3,4])\ndoe_close()\n"
        "doe_open('hybrid/host.gguf','hybrid%d.gguf',16)\n"
        "fresh = doe_prompt([3,4])\nassert(len(fresh)==40,'hybrid head')\n"
        "i = 0\nwhile i < len(fresh):\n"
        "    assert(fresh[i]==reset[i],'reset logits match a restarted Qwen3.5')\n"
        "    i = i + 1\ndoe_close()\n", mode, mode);
    assert(run(mode, source) == 0);
}
#endif

/* The AML harness and the CLI record the same turn. On this host the running
 * RMS telemetry (doe.rms_history) lands a few ULP apart, a reduction-order
 * difference between the two callers; the elected factors and every other
 * tensor stay byte-equal. Compare bytes, but allow doe.rms_history to drift
 * within a small ULP window. */
static int ulp_apart(float a, float b) {
    int32_t ia, ib;
    memcpy(&ia, &a, 4);
    memcpy(&ib, &b, 4);
    if (ia < 0) ia = INT32_MIN - ia;
    if (ib < 0) ib = INT32_MIN - ib;
    int64_t d = (int64_t)ia - ib;
    return d < 0 ? -d : d;
}

static int spore_eq(const char *pa, const char *pb) {
    gguf_file *a = gguf_open(pa), *b = gguf_open(pb);
    if (!a || !b || a->n_tensors != b->n_tensors) return 1;
    int bad = 0;
    for (uint64_t t = 0; t < a->n_tensors && !bad; t++) {
        const gguf_tensor_info *ta = &a->tensors[t], *tb = &b->tensors[t];
        if (strcmp(ta->name, tb->name) || ta->n_elements != tb->n_elements) { bad = 1; break; }
        const void *da = a->data + ta->offset, *db = b->data + tb->offset;
        size_t bytes = (size_t)ta->n_elements * 4;
        if (!strcmp(ta->name, "doe.rms_history")) {
            const float *fa = da, *fb = db;
            for (uint64_t i = 0; i < ta->n_elements; i++)
                if (ulp_apart(fa[i], fb[i]) > 4) { bad = 1; break; }
        } else if (memcmp(da, db, bytes)) bad = 1;
    }
    gguf_close(a);
    gguf_close(b);
    return bad;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "spore-eq")) return spore_eq(argv[2], argv[3]);
    const char *open = "doe_open('host.gguf','',16)\n";
    assert(am_exec(open) != 0 && strstr(am_get_error(), "backend unavailable"));
    am_use_notorch_doe();
#ifdef NT_DOE_DECODER_API
    assert(fixture("hybrid/host.gguf", 0) == 0);
    write_tokenizer("hybrid/tokenizer.gguf", "qwen35", 8);
#endif
    write_tokenizer("dense/tokenizer.gguf", "qwen2", 8);
    write_tokenizer("wrong/tokenizer.gguf", "qwen35", 8);
    write_tokenizer("oversized/tokenizer.gguf", "qwen2", 41);
    const char *separate = "doe_open('dense/host.gguf','',16)\n"
        "ids = doe_encode('ab')\nassert(len(ids)==1 and ids[0]==2,'external BPE merge')\n"
        "full = doe_prompt(ids)\nassert(len(full)==40,'padded output head')\n"
        "doe_choose(0)\ndoe_choose(7)\ndoe_finish('vote')\ndoe_close()\n";
    const char *bad[] = {
        "doe_open('host.gguf','',16)", "doe_save('bad.gguf')", "doe_choose(-1)",
        "doe_prompt([1.5])", "doe_prompt([40])", "doe_prompt([0-1])",
        "doe_prompt([1,2])\ndoe_finish('async')", "doe_prompt([2])\ndoe_reset()",
        "doe_prompt([2])\ndoe_choose(1)\ndoe_choose(1)", "doe_finish('other')",
        "doe_transplant('slot-does-not-exist.gguf',0)", "doe_encode('no tokenizer')",
        "SPAWN isolated:\n    doe_wait()\nAWAIT isolated"
    };
    for (int mode = 0; mode < 3; mode++) {
        char source[1024];
#ifdef NT_DOE_DECODER_API
        check_hybrid_reset(mode);
#endif
        assert(run(mode, separate) == 0);
        assert(run(mode, "doe_open('host.gguf','',16,'missing.gguf')") != 0);
        assert(run(mode, "doe_open('wrong/host.gguf','',16)") != 0);
        assert(run(mode, "doe_open('oversized/host.gguf','',16)") != 0);
        snprintf(source, sizeof(source),
            "%slogits = doe_prompt([3,4,5])\nassert(len(logits)==40,'vocab')\n"
            "cached = doe_logits()\nassert(cached[0]==logits[0],'logits snapshot')\n"
            "doe_choose(-1)\ndoe_finish('async')\ndoe_wait()\ndoe_save('mode%d.gguf')\n"
            "assert(doe_close()==1,'explicit close')\n", open, mode);
        assert(run(mode, source) == 0);
        for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
            snprintf(source, sizeof(source), "%s%s\n", open, bad[i]);
            assert(run(mode, source) != 0);
        }
        snprintf(source, sizeof(source),
            "%sdoe_prompt([3,4,5])\ndoe_choose(3)\ndoe_finish('async')\n", open);
        assert(run(mode, source) == 0); // teardown implicitly joins
        strcat(source, "assert(0,'injected failure')\n");
        assert(run(mode, source) != 0 && strstr(am_get_error(), "injected failure"));
    }
    // Interleaved contexts on the same thread each own a parliament.
    const char *held = "doe_open('host.gguf','mode0.gguf',16)\n"
        "doe_prompt([3])\ndoe_choose(5)\ndoe_finish('async')\ndoe_reset()\n";
    void *a = am_program_open(held), *b = am_program_open(held);
    assert(a && b);
    while (!am_program_step(a, 1)) am_program_step(b, 1);
    while (!am_program_step(b, 1)) {}
    assert(!am_program_close(a) && !am_program_close(b));
    puts("DOE context isolation, error teardown and three execution paths passed");
    return 0;
}
