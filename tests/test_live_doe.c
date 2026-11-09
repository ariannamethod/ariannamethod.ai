// Real packed sessions through the interpreter, bytecode and stepped programs.
#include "ariannamethod.h"
#include <assert.h>
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

int main(void) {
    const char *open = "doe_open('host.gguf','',16)\n";
    assert(am_exec(open) != 0 && strstr(am_get_error(), "backend unavailable"));
    am_use_notorch_doe();
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
