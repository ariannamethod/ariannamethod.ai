/* Owned NoTorch parliament bridge: three execution paths and failed publication. */
#include "ariannamethod.h"
#include "notorch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "parliament bridge line %d: %s (%s)\n", __LINE__, #c, am_get_error()); exit(1); } } while (0)

static int run(int mode, const char *source) {
    if (!mode) return am_exec(source);
    if (mode == 1) {
        void *p = am_compile(source); if (!p) return 1;
        int rc = am_exec_compiled(p); am_free_compiled(p); return rc;
    }
    void *p = am_program_open(source); if (!p) return 1;
    int budget = AML_MAX_LINES + 1;
    while (!am_program_step(p, 1)) CHECK(--budget > 0);
    return am_program_close(p);
}

static void equal(const char *name, const float *expected, int n) {
    int size = 0;
    const float *got = am_get_var_array(name, &size);
    CHECK(got && size == n && !memcmp(got, expected, (size_t)n * sizeof(float)));
}

static int poison_mode;
static int poisoned_election(const float *v, const float *r, const float *a,
                            int n, int minimum, float c, float *out) {
    (void)v; (void)r; (void)a; (void)minimum; (void)c;
    for (int i = 0; i < n + 2; i++) out[i] = 0;
    out[n] = .5f; out[n + 1] = 1;
    if (poison_mode == 0) out[0] = 2; /* finite, invalid gate */
    if (poison_mode == 2) { out[0] = 1; out[n + 1] = 2; } /* dead slot elected */
    if (poison_mode == 3) { out[0] = .5f; out[1] = .5f; } /* two gates, k=1 */
    return 0;
}

int main(void) {
    const float votes[] = {-2, -1, 1, 2}, res[] = {0, 0, 0, 0}, alive[] = {1, 1, 1, 1};
    const float a[] = {1, 1, 1, 1}, b[] = {1, 1, 1, 1}, gates[] = {.5f, .5f}, x[] = {1, 2};
    const float oa[] = {.25f, -.125f, .5f, .25f}, ob[] = {.125f, .5f, -.25f, .125f};
    const float dy[] = {.125f, -.25f}, noise[] = {.01f};
    float elected[6], injected[2], adapted[9];
    CHECK(nt_parliament_elect_values(votes, res, alive, 4, 2, .5f, elected) == 0);
    CHECK(nt_parliament_inject_values(a, b, gates, x, 2, 2, 1, 1, injected) == 0);
    CHECK(nt_oja_values(oa, ob, x, dy, noise, 2, 2, 1, 1, .01f, .5f, adapted) == 0);
    CHECK(injected[0] == 4 && injected[1] == 5);
    const char *invalid[] = {
        "nt_parliament_elect()", "nt_parliament_elect([1],[0],[1],1)",
        "nt_parliament_elect([1],[0],[1],1,0,1)",
        "nt_parliament_elect('x',[0],[1],1,0)",
        "nt_parliament_elect([1],[0,0],[1],1,0)",
        "nt_parliament_elect([1],[0],[0.5],1,0)",
        "nt_parliament_elect([1],[0],[0],1,0)",
        "nt_parliament_elect([1],[0],[1],1.5,0)",
        "nt_parliament_elect([1],[0],[1],1,1.1)",
        "nt_parliament_elect([1e39],[0],[1],1,0)",
        "nt_parliament_elect(zeros(17),zeros(17),zeros(17),1,0)",
        "nt_parliament_inject([1],[1],[1],[1],1)",
        "nt_parliament_inject([1],[1],[1],[1],1,1,0)",
        "nt_parliament_inject([1],[1],[1],[1],0,1)",
        "nt_parliament_inject([1],[1],[1],[1],1.5,1)",
        "nt_parliament_inject([1],[1],[1],[1],1e39,1)",
        "nt_parliament_inject([1],[1],[1],[1],17,1)",
        "nt_parliament_inject([1],[1],[1],[1],1048576,1)",
        "nt_parliament_inject([1],[1],[2],[1],1,1)",
        "nt_parliament_inject([1],[1],[1],[1],1,'a')",
        "nt_parliament_inject([3e38],[3e38],[1],[3e38],1,1)",
        "nt_oja([1],[1],[1],[1],[0],0,0)",
        "nt_oja([1],[1],[1],[1],[0],1,0,0)",
        "nt_oja([1],[1],[1],[1],[0],-1,0,0)",
        "nt_oja([1],[1],[1],[1],[0],0.5,0,0)",
        "nt_oja([1],[1],[1],[1],[0],0,-1,0)",
        "nt_oja([1],[1],[1],[1],[0,0],0,0,0)",
        "nt_oja([1,2,3],[1,2,3],[1,2],[1,2],[0],0,0,0)",
        "nt_oja([1],[1],[1],[1],[0],0,1e39,0)",
        "nt_oja([1],[1],[1],[1],[0],0,0,1e39)",
        "nt_oja([1],[1],[1],[1],list_new(),0,0,0)",
        "nt_oja([1],[1],[1],[1],[0],'cursor',0,0)",
        "def nt_oja(a,b,c,d,e,f,g,h):\n    return a\n"
    };
    for (int mode = 0; mode < 3; mode++) {
        am_init(); am_use_notorch(); am_persistent_mode(1);
        CHECK(run(mode,
            "a = [1,1,1,1]\nb = [1,1,1,1]\nx = [1,2]\n"
            "e = nt_parliament_elect([-2,-1,1,2],[0,0,0,0],[1,1,1,1],2,0.5)\n"
            "y = nt_parliament_inject(a,b,[0.5,0.5],x,1,1)\n"
            "o = nt_oja([0.25,-0.125,0.5,0.25],[0.125,0.5,-0.25,0.125],x,[0.125,-0.25],[0.01],1,0.01,0.5)\n"
            "result = [99]\nsentinel = 0\n") == 0);
        equal("e", elected, 6); equal("y", injected, 2); equal("o", adapted, 9);
        equal("a", a, 4); equal("b", b, 4); equal("x", x, 2);
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char source[1024];
            snprintf(source, sizeof(source), "%s%s\nsentinel = 1\n",
                     !strncmp(invalid[i], "def ", 4) ? "" : "result = ", invalid[i]);
            CHECK(run(mode, source) != 0);
            CHECK(am_get_error()[0] && am_get_var_float("sentinel") == 0);
            const float previous[] = {99}; equal("result", previous, 1);
            equal("a", a, 4); equal("b", b, 4); equal("x", x, 2);
        }
        am_set_parliament_backend(NULL);
        CHECK(run(mode, "result = nt_parliament_elect([1],[0],[1],1,0)\n") != 0);
        CHECK(strstr(am_get_error(), "backend unavailable"));
        AM_ParliamentBackend extension = {.parliament_elect = poisoned_election};
        am_set_parliament_backend(&extension);
        for (poison_mode = 0; poison_mode < 4; poison_mode++) {
            const char *bad_backend = poison_mode < 2
                ? "result = nt_parliament_elect([1],[0],[1],1,0)\n"
                : poison_mode == 2
                ? "result = nt_parliament_elect([1,1],[0,0],[1,0],1,0)\n"
                : "result = nt_parliament_elect([1,1],[0,0],[1,1],1,0)\n";
            CHECK(run(mode, bad_backend) != 0);
            const float previous[] = {99}; equal("result", previous, 1);
        }
        am_persistent_clear(); am_persistent_mode(0);
    }
    printf("PASS parliament bridge: %d checks, interpreter/bytecode/resumable, missing and invalid backends\n", checks);
    return 0;
}
