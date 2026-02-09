// test_aml.c — AML Level 0 + Level 2 tests
// cc -Wall test_aml.c ariannamethod.c -o test_aml -lm && ./test_aml

#include "ariannamethod.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_passed++; printf("  [PASS] %s\n", msg); } \
    else { printf("  [FAIL] %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define ASSERT_FLOAT(val, expected, tol, msg) do { \
    tests_run++; \
    float _v = (val), _e = (expected); \
    if (fabsf(_v - _e) <= (tol)) { tests_passed++; printf("  [PASS] %s (%.3f)\n", msg, _v); } \
    else { printf("  [FAIL] %s: got %.4f, expected %.4f (line %d)\n", msg, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_INT(val, expected, msg) do { \
    tests_run++; \
    int _v = (val), _e = (expected); \
    if (_v == _e) { tests_passed++; printf("  [PASS] %s (%d)\n", msg, _v); } \
    else { printf("  [FAIL] %s: got %d, expected %d (line %d)\n", msg, _v, _e, __LINE__); } \
} while(0)

// ── TEST 1: Level 0 backward compatibility ──────────────────────────────────

static void test_level0_compat(void) {
    printf("\n── Level 0 backward compatibility ──\n");
    am_init();

    am_exec("PROPHECY 12\nDESTINY 0.7\nVELOCITY RUN\nATTEND_FOCUS 0.85");
    AM_State* s = am_get_state();

    ASSERT_INT(s->prophecy, 12, "PROPHECY 12");
    ASSERT_FLOAT(s->destiny, 0.7f, 0.01f, "DESTINY 0.7");
    ASSERT_INT(s->velocity_mode, AM_VEL_RUN, "VELOCITY RUN");
    ASSERT_FLOAT(s->attend_focus, 0.85f, 0.01f, "ATTEND_FOCUS 0.85");
}

// ── TEST 2: Level 0 suffering + laws ────────────────────────────────────────

static void test_level0_suffering(void) {
    printf("\n── Level 0 suffering + laws ──\n");
    am_init();

    am_exec(
        "PAIN 0.5\n"
        "TENSION 0.3\n"
        "DISSONANCE 0.2\n"
        "LAW ENTROPY_FLOOR 0.15\n"
        "LAW RESONANCE_CEILING 0.8\n"
    );
    AM_State* s = am_get_state();

    ASSERT_FLOAT(s->pain, 0.5f, 0.01f, "PAIN 0.5");
    ASSERT_FLOAT(s->tension, 0.3f, 0.01f, "TENSION 0.3");
    ASSERT_FLOAT(s->dissonance, 0.2f, 0.01f, "DISSONANCE 0.2");
    ASSERT_FLOAT(s->entropy_floor, 0.15f, 0.01f, "LAW ENTROPY_FLOOR 0.15");
    ASSERT_FLOAT(s->resonance_ceiling, 0.8f, 0.01f, "LAW RESONANCE_CEILING 0.8");
}

// ── TEST 3: Level 0 packs ───────────────────────────────────────────────────

static void test_level0_packs(void) {
    printf("\n── Level 0 packs ──\n");
    am_init();

    ASSERT_INT(am_pack_enabled(AM_PACK_CODES_RIC), 0, "CODES_RIC disabled by default");

    am_exec("MODE CODES_RIC\nCHORDLOCK ON\nTEMPO 11");
    AM_State* s = am_get_state();

    ASSERT_INT(am_pack_enabled(AM_PACK_CODES_RIC), 1, "CODES_RIC enabled after MODE");
    ASSERT_INT(s->chordlock_on, 1, "CHORDLOCK ON");
    ASSERT_INT(s->tempo, 11, "TEMPO 11");

    am_exec("DISABLE CODES_RIC");
    ASSERT_INT(am_pack_enabled(AM_PACK_CODES_RIC), 0, "CODES_RIC disabled after DISABLE");
}

// ── TEST 4: Level 0 comments and empty lines ────────────────────────────────

static void test_level0_comments(void) {
    printf("\n── Level 0 comments/empty ──\n");
    am_init();

    am_exec(
        "# this is a comment\n"
        "\n"
        "PROPHECY 5\n"
        "# another comment\n"
        "DESTINY 0.4\n"
    );
    AM_State* s = am_get_state();

    ASSERT_INT(s->prophecy, 5, "PROPHECY after comments");
    ASSERT_FLOAT(s->destiny, 0.4f, 0.01f, "DESTINY after comments");
}

// ── TEST 5: Variable assignment + expression ────────────────────────────────

static void test_variables(void) {
    printf("\n── Variables + expressions ──\n");
    am_init();

    am_exec(
        "PAIN 0.3\n"
        "TENSION 0.2\n"
        "mood = pain + tension\n"
    );
    // mood should be 0.5 (reading from AM_State fields)
    // We can't directly read mood from C, but we can use it in an if
    am_exec(
        "PAIN 0.3\n"
        "TENSION 0.2\n"
        "mood = pain + tension\n"
        "if mood > 0.4:\n"
        "    PROPHECY 20\n"
    );
    AM_State* s = am_get_state();
    ASSERT_INT(s->prophecy, 20, "Variable mood > 0.4 → PROPHECY 20");
}

// ── TEST 6: if/else ─────────────────────────────────────────────────────────

static void test_if_else(void) {
    printf("\n── if/else ──\n");

    // true branch
    am_init();
    am_exec(
        "PAIN 0.6\n"
        "if pain > 0.5:\n"
        "    VELOCITY RUN\n"
        "else:\n"
        "    VELOCITY WALK\n"
    );
    ASSERT_INT(am_get_state()->velocity_mode, AM_VEL_RUN, "if true → VELOCITY RUN");

    // false branch
    am_init();
    am_exec(
        "PAIN 0.2\n"
        "if pain > 0.5:\n"
        "    VELOCITY RUN\n"
        "else:\n"
        "    VELOCITY WALK\n"
    );
    ASSERT_INT(am_get_state()->velocity_mode, AM_VEL_WALK, "if false → VELOCITY WALK");
}

// ── TEST 7: while loop ──────────────────────────────────────────────────────

static void test_while(void) {
    printf("\n── while loop ──\n");
    am_init();

    am_exec(
        "counter = 0\n"
        "while counter < 5:\n"
        "    counter = counter + 1\n"
        "PROPHECY 5\n"  // just to verify we exit the loop
    );
    ASSERT_INT(am_get_state()->prophecy, 5, "while exits properly, PROPHECY 5 reached");

    // while with state mutation
    am_init();
    am_exec(
        "PAIN 0.8\n"
        "counter = 0\n"
        "while pain > 0.5:\n"
        "    PAIN 0.4\n"
        "    counter = counter + 1\n"
        "PROPHECY 42\n"
    );
    ASSERT_INT(am_get_state()->prophecy, 42, "while with PAIN mutation exits, PROPHECY 42");
    ASSERT_FLOAT(am_get_state()->pain, 0.4f, 0.01f, "PAIN reduced to 0.4 inside while");
}

// ── TEST 8: def + function call ─────────────────────────────────────────────

static void test_def_call(void) {
    printf("\n── def + function call ──\n");
    am_init();

    am_exec(
        "def awaken():\n"
        "    PROPHECY 7\n"
        "    VELOCITY WALK\n"
        "    ATTEND_FOCUS 0.7\n"
        "\n"
        "awaken()\n"
    );
    AM_State* s = am_get_state();
    ASSERT_INT(s->prophecy, 7, "awaken() → PROPHECY 7");
    ASSERT_INT(s->velocity_mode, AM_VEL_WALK, "awaken() → VELOCITY WALK");
    ASSERT_FLOAT(s->attend_focus, 0.7f, 0.01f, "awaken() → ATTEND_FOCUS 0.7");
}

// ── TEST 9: function with parameters ────────────────────────────────────────

static void test_func_params(void) {
    printf("\n── function with parameters ──\n");
    am_init();

    am_exec(
        "def set_pain(level):\n"
        "    PAIN 0\n"
        "\n"
        "set_pain(0.7)\n"
    );
    // Note: Level 0 commands still use safe_atof, not expression eval
    // So PAIN inside function uses the literal "0", not the param
    // This is Phase 7 territory — for now just verify the call works
    ASSERT_FLOAT(am_get_state()->pain, 0.0f, 0.01f, "function call with params executed");
}

// ── TEST 10: nested if inside function ──────────────────────────────────────

static void test_nested_if(void) {
    printf("\n── nested if inside function ──\n");
    am_init();

    am_exec(
        "def check_state():\n"
        "    if pain > 0.5:\n"
        "        VELOCITY RUN\n"
        "    else:\n"
        "        VELOCITY NOMOVE\n"
        "\n"
        "PAIN 0.8\n"
        "check_state()\n"
    );
    ASSERT_INT(am_get_state()->velocity_mode, AM_VEL_RUN, "nested if in func → VELOCITY RUN");

    am_init();
    am_exec(
        "def check_state():\n"
        "    if pain > 0.5:\n"
        "        VELOCITY RUN\n"
        "    else:\n"
        "        VELOCITY NOMOVE\n"
        "\n"
        "PAIN 0.2\n"
        "check_state()\n"
    );
    ASSERT_INT(am_get_state()->velocity_mode, AM_VEL_NOMOVE, "nested if in func → VELOCITY NOMOVE");
}

// ── TEST 11: expression evaluator ───────────────────────────────────────────

static void test_expressions(void) {
    printf("\n── expression evaluator ──\n");
    am_init();

    // arithmetic
    am_exec("x = 3 + 4 * 2\n"
            "if x > 10:\n"
            "    PROPHECY 11\n");
    ASSERT_INT(am_get_state()->prophecy, 11, "3 + 4 * 2 = 11 > 10");

    // comparisons
    am_init();
    am_exec("x = 5\n"
            "if x == 5:\n"
            "    PROPHECY 55\n");
    ASSERT_INT(am_get_state()->prophecy, 55, "x == 5 true");

    // boolean
    am_init();
    am_exec("PAIN 0.6\n"
            "TENSION 0.7\n"
            "if pain > 0.5 and tension > 0.5:\n"
            "    PROPHECY 33\n");
    ASSERT_INT(am_get_state()->prophecy, 33, "and operator");

    am_init();
    am_exec("PAIN 0.2\n"
            "TENSION 0.7\n"
            "if pain > 0.5 or tension > 0.5:\n"
            "    PROPHECY 44\n");
    ASSERT_INT(am_get_state()->prophecy, 44, "or operator");
}

// ── TEST 12: temporal + expert commands ──────────────────────────────────────

static void test_temporal(void) {
    printf("\n── temporal + expert ──\n");
    am_init();

    am_exec(
        "TEMPORAL_MODE SYMMETRIC\n"
        "TEMPORAL_ALPHA 0.8\n"
        "RTL_MODE ON\n"
        "EXPERT_CREATIVE 0.9\n"
    );
    AM_State* s = am_get_state();
    ASSERT_INT(s->temporal_mode, AM_TEMPORAL_SYMMETRIC, "TEMPORAL_MODE SYMMETRIC");
    ASSERT_FLOAT(s->temporal_alpha, 0.8f, 0.01f, "TEMPORAL_ALPHA 0.8");
    ASSERT_INT(s->rtl_mode, 1, "RTL_MODE ON");
    ASSERT_FLOAT(s->expert_creative, 0.9f, 0.01f, "EXPERT_CREATIVE 0.9");
}

// ── TEST 13: am_step physics ────────────────────────────────────────────────

static void test_step(void) {
    printf("\n── am_step physics ──\n");
    am_init();

    am_exec("PROPHECY_DEBT 10.0");
    am_step(0.016f);  // one frame
    ASSERT(am_get_state()->debt < 10.0f, "debt decays after step");

    am_init();
    am_exec("VELOCITY BACKWARD");
    am_step(1.0f);
    ASSERT(am_get_state()->temporal_debt > 0.0f, "temporal_debt grows in BACKWARD");
}

// ── TEST 14: am_get_error ───────────────────────────────────────────────────

static void test_get_error(void) {
    printf("\n── am_get_error ──\n");
    am_init();

    int rc = am_exec("PROPHECY 7");
    ASSERT_INT(rc, 0, "valid script returns 0");
    ASSERT(strlen(am_get_error()) == 0, "no error on valid script");
}

// ── TEST 15: init.aml style script ──────────────────────────────────────────

static void test_init_script(void) {
    printf("\n── full init.aml style script ──\n");
    am_init();

    am_exec(
        "# init.aml — morning state\n"
        "PROPHECY 7\n"
        "DESTINY 0.35\n"
        "VELOCITY WALK\n"
        "ATTEND_FOCUS 0.70\n"
        "\n"
        "LAW ENTROPY_FLOOR 0.1\n"
        "LAW RESONANCE_CEILING 0.95\n"
        "\n"
        "# suffering is not a bug\n"
        "PAIN 0\n"
        "TENSION 0\n"
        "DISSONANCE 0\n"
    );
    AM_State* s = am_get_state();
    ASSERT_INT(s->prophecy, 7, "init: PROPHECY 7");
    ASSERT_FLOAT(s->destiny, 0.35f, 0.01f, "init: DESTINY 0.35");
    ASSERT_INT(s->velocity_mode, AM_VEL_WALK, "init: VELOCITY WALK");
    ASSERT_FLOAT(s->pain, 0.0f, 0.01f, "init: PAIN 0");
    ASSERT_FLOAT(s->entropy_floor, 0.1f, 0.01f, "init: LAW ENTROPY_FLOOR 0.1");
}

// ── TEST 16: real calendar conflict physics ──────────────────────────────────

static void test_calendar_physics(void) {
    printf("\n── real calendar conflict physics ──\n");
    am_init();

    AM_State* s = am_get_state();
    ASSERT_FLOAT(s->calendar_drift, 11.0f, 0.01f, "calendar_drift default 11.0");
    ASSERT_FLOAT(s->calendar_phase, 0.0f, 0.01f, "calendar_phase 0 before first step");
    ASSERT_FLOAT(s->wormhole_gate, 0.3f, 0.01f, "wormhole_gate default 0.3");

    // Real date step — phase computed from actual epoch (1 Tishrei 5785)
    am_step(1.0f);
    ASSERT(s->calendar_phase >= 0.0f, "real calendar_phase >= 0");
    ASSERT(s->calendar_phase <= 33.0f, "real calendar_phase <= 33 (max uncorrected)");
    printf("    (real date: calendar_phase=%.2f, wormhole_active=%d)\n",
           s->calendar_phase, s->wormhole_active);

    // Manual mode: LAW CALENDAR_PHASE switches to manual override
    am_init();
    am_exec("LAW CALENDAR_PHASE 9.0");
    am_step(0.01f);
    // manual dissonance = 9.0 / 11.0 ≈ 0.818 > gate 0.3
    ASSERT_INT(s->wormhole_active, 1, "manual high phase → wormhole ACTIVE");
    ASSERT(s->wormhole > 0.02f, "wormhole probability boosted");

    // Manual low phase — wormhole inactive
    am_init();
    am_exec("LAW CALENDAR_PHASE 1.0");
    am_step(0.01f);
    // manual dissonance = 1.0 / 11.0 ≈ 0.091 < gate 0.3
    ASSERT_INT(s->wormhole_active, 0, "manual low phase → wormhole inactive");

    // Calendar dissonance bleeds into field dissonance (manual mode)
    am_init();
    am_exec("DISSONANCE 0\nLAW CALENDAR_PHASE 8.0");
    for (int i = 0; i < 100; i++) am_step(0.1f);
    ASSERT(s->dissonance > 0.0f, "calendar dissonance bleeds into field");

    // Calendar tension feeds prophecy debt (manual mode)
    am_init();
    am_exec("LAW CALENDAR_PHASE 10.0");
    float debt_before = s->debt;
    am_step(1.0f);
    ASSERT(s->debt > debt_before, "high calendar phase increases debt");
}

// ── TEST 17: wormhole gate — manual sweep + real date sanity ─────────────────

static void test_wormhole_cycle(void) {
    printf("\n── wormhole gate cycle ──\n");

    AM_State* s;

    // Manual sweep: phase from 0 to 11, verify gate triggers correctly
    int activated_at = -1;
    for (int phase_x10 = 0; phase_x10 <= 110; phase_x10++) {
        am_init();
        char cmd[64];
        snprintf(cmd, 64, "LAW CALENDAR_PHASE %.1f", (float)phase_x10 / 10.0f);
        am_exec(cmd);
        am_step(0.01f);
        s = am_get_state();

        if (s->wormhole_active && activated_at < 0) activated_at = phase_x10;
    }

    ASSERT(activated_at >= 0, "wormhole activates at some phase");
    // gate=0.3, drift=11 → activation around phase 3.3 (dissonance=3.3/11=0.3)
    printf("    (activated at phase %.1f, expected ~3.3)\n",
           activated_at >= 0 ? (float)activated_at / 10.0f : -1.0f);
    ASSERT(activated_at >= 20 && activated_at <= 50,
           "activation near phase 3.3 (gate threshold)");

    // Real date sanity: dissonance-driven phase in valid range
    am_init();
    am_step(1.0f);
    s = am_get_state();
    ASSERT(s->calendar_phase >= 0.0f && s->calendar_phase <= 33.0f,
           "real date: phase in [0, 33]");
}

// ── MAIN ────────────────────────────────────────────────────────────────────

int main(void) {
    printf("═══ AML Test Suite ═══\n");

    test_level0_compat();
    test_level0_suffering();
    test_level0_packs();
    test_level0_comments();
    test_variables();
    test_if_else();
    test_while();
    test_def_call();
    test_func_params();
    test_nested_if();
    test_expressions();
    test_temporal();
    test_step();
    test_get_error();
    test_init_script();
    test_calendar_physics();
    test_wormhole_cycle();

    printf("\n═══ Results: %d/%d passed ═══\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
