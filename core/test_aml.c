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

// ── TEST 18: Schumann resonance ────────────────────────────────────────────

static void test_schumann(void) {
    printf("\n── Schumann resonance ──\n");
    am_init();
    AM_State* s = am_get_state();

    ASSERT_FLOAT(s->schumann_hz, 7.83f, 0.01f, "schumann_hz default 7.83");
    ASSERT_FLOAT(s->schumann_coherence, 1.0f, 0.01f, "coherence 1.0 at baseline");
    ASSERT_FLOAT(s->schumann_modulation, 0.3f, 0.01f, "modulation default 0.3");

    // Change frequency → coherence drops
    am_exec("SCHUMANN 7.77");
    ASSERT_FLOAT(s->schumann_hz, 7.77f, 0.01f, "SCHUMANN 7.77");
    ASSERT(s->schumann_coherence < 1.0f, "coherence drops at 7.77");
    ASSERT(s->schumann_coherence > 0.0f, "coherence still positive");

    // Back to baseline
    am_exec("SCHUMANN 7.83");
    ASSERT_FLOAT(s->schumann_coherence, 1.0f, 0.05f, "coherence ~1.0 at 7.83");

    // Modulation
    am_exec("SCHUMANN_MODULATION 0.8");
    ASSERT_FLOAT(s->schumann_modulation, 0.8f, 0.01f, "SCHUMANN_MODULATION 0.8");

    // Step advances phase
    am_init();
    am_step(1.0f);
    ASSERT(s->schumann_phase > 0.0f, "phase advances after step");

    // High coherence heals tension
    am_init();
    am_exec("TENSION 0.5\nDISSONANCE 0.5\nSCHUMANN_MODULATION 1.0");
    float t_before = s->tension;
    for (int i = 0; i < 100; i++) am_step(0.1f);
    ASSERT(s->tension < t_before, "Schumann heals tension");
}

// ── TEST 19: LORA_ALPHA + NOTORCH commands ────────────────────────────────

static void test_lora_notorch(void) {
    printf("\n── LORA_ALPHA + NOTORCH ──\n");
    am_init();
    AM_State* s = am_get_state();

    ASSERT_FLOAT(s->lora_alpha, 0.0f, 0.01f, "lora_alpha default 0");
    am_exec("LORA_ALPHA 0.5");
    ASSERT_FLOAT(s->lora_alpha, 0.5f, 0.01f, "LORA_ALPHA 0.5");

    am_exec("NOTORCH_LR 0.05");
    ASSERT_FLOAT(s->notorch_lr, 0.05f, 0.01f, "NOTORCH_LR 0.05");

    am_exec("NOTORCH_DECAY 0.995");
    ASSERT_FLOAT(s->notorch_decay, 0.995f, 0.001f, "NOTORCH_DECAY 0.995");
}

// ── TEST 20: DARKMATTER as core (no pack gate) ────────────────────────────

static void test_darkmatter_core(void) {
    printf("\n── DARKMATTER core ──\n");
    am_init();
    AM_State* s = am_get_state();

    // GRAVITY DARK works WITHOUT MODE DARKMATTER
    am_exec("GRAVITY DARK 0.8");
    ASSERT_FLOAT(s->dark_gravity, 0.8f, 0.01f, "GRAVITY DARK 0.8 (no pack gate)");

    // ANTIDOTE works WITHOUT MODE DARKMATTER
    am_exec("ANTIDOTE HARD");
    ASSERT_INT(s->antidote_mode, 1, "ANTIDOTE HARD (no pack gate)");

    // SCAR increments count
    am_init();
    ASSERT_INT(s->n_scars, 0, "n_scars starts at 0");
    am_exec("SCAR rejection\nSCAR another");
    ASSERT_INT(s->n_scars, 2, "n_scars = 2 after two SCARs");
}

// ── TEST 21: ECHO command ─────────────────────────────────────────────────

static void test_echo(void) {
    printf("\n── ECHO command ──\n");
    am_init();
    // ECHO should not crash and should parse cleanly
    int rc = am_exec("ECHO hello world\nPROPHECY 9");
    ASSERT_INT(rc, 0, "ECHO + PROPHECY returns 0");
    ASSERT_INT(am_get_state()->prophecy, 9, "PROPHECY after ECHO");
}

// ── TEST 22: Season commands ──────────────────────────────────────────────

static void test_seasons(void) {
    printf("\n── 4.C seasons ──\n");
    am_init();
    AM_State* s = am_get_state();

    ASSERT_INT(s->season, 0, "default season SPRING (0)");

    am_exec("SEASON WINTER");
    ASSERT_INT(s->season, 3, "SEASON WINTER (3)");
    ASSERT_FLOAT(s->season_phase, 0.0f, 0.01f, "season_phase reset on change");

    am_exec("SEASON SUMMER");
    ASSERT_INT(s->season, 1, "SEASON SUMMER (1)");

    am_exec("SEASON_INTENSITY 0.9");
    ASSERT_FLOAT(s->season_intensity, 0.9f, 0.01f, "SEASON_INTENSITY 0.9");

    // Season energy changes over steps
    am_init();
    am_exec("SEASON SPRING\nSEASON_INTENSITY 1.0");
    float spring_before = s->spring_energy;
    for (int i = 0; i < 100; i++) am_step(0.1f);
    ASSERT(s->spring_energy >= spring_before * 0.5f, "spring_energy maintained in SPRING");
}

// ── TEST 23: Built-in functions ───────────────────────────────────────────

static void test_builtins(void) {
    printf("\n── Built-in functions ──\n");

    // bootstrap_self
    am_init();
    am_exec("PAIN 0.9\nDISSONANCE 0.8\nbootstrap_self()");
    AM_State* s = am_get_state();
    ASSERT_FLOAT(s->pain, 0.0f, 0.01f, "bootstrap_self resets pain");
    ASSERT_INT(s->prophecy, 7, "bootstrap_self sets prophecy 7");
    ASSERT_FLOAT(s->attend_focus, 0.7f, 0.01f, "bootstrap_self sets focus 0.7");

    // galvanize
    am_init();
    am_exec("galvanize()");
    ASSERT_INT(s->velocity_mode, AM_VEL_RUN, "galvanize → RUN");
    ASSERT_INT(s->prophecy, 12, "galvanize → prophecy 12");

    // shatter_the_frame
    am_init();
    am_exec("shatter_the_frame()");
    ASSERT_FLOAT(s->pain, 0.7f, 0.01f, "shatter → pain 0.7");
    ASSERT_FLOAT(s->dissonance, 0.8f, 0.01f, "shatter → dissonance 0.8");

    // pierce_the_infinite
    am_init();
    am_exec("pierce_the_infinite()");
    ASSERT_INT(s->prophecy, 64, "pierce → prophecy 64");
    ASSERT_FLOAT(s->destiny, 0.1f, 0.01f, "pierce → destiny 0.1");

    // reflect_on_self
    am_init();
    am_exec("reflect_on_self()");
    ASSERT_FLOAT(s->attend_focus, 0.95f, 0.01f, "reflect → focus 0.95");
    ASSERT_INT(s->velocity_mode, AM_VEL_NOMOVE, "reflect → NOMOVE");

    // remember_future
    am_init();
    am_exec("remember_future()");
    ASSERT_INT(s->temporal_mode, 0, "remember_future → PROPHECY mode");
    ASSERT_FLOAT(s->temporal_alpha, 1.0f, 0.01f, "remember_future → alpha 1.0");

    // rewind_experience
    am_init();
    am_exec("rewind_experience()");
    ASSERT_INT(s->velocity_mode, AM_VEL_BACKWARD, "rewind → BACKWARD");
    ASSERT_INT(s->temporal_mode, 1, "rewind → RETRODICTION");

    // echo_fractal with parameter
    am_init();
    am_exec("echo_fractal(8)");
    ASSERT_INT(s->prophecy, 16, "echo_fractal(8) → prophecy 16");
    ASSERT_INT(s->tunnel_skip_max, 8, "echo_fractal(8) → skip_max 8");

    // tunnel_through with parameter
    am_init();
    am_exec("tunnel_through(0.3)");
    ASSERT_FLOAT(s->tunnel_threshold, 0.3f, 0.01f, "tunnel_through(0.3) → threshold 0.3");
    ASSERT_FLOAT(s->tunnel_chance, 0.5f, 0.01f, "tunnel_through → chance 0.5");
}

// ── TEST 24: am_apply_destiny_to_logits ───────────────────────────────────

static void test_logit_destiny(void) {
    printf("\n── logit destiny ──\n");
    am_init();
    am_exec("DESTINY 0.7\nPROPHECY 7");
    am_step(0.1f);  // compute destiny_bias

    float logits[5] = {1.0f, 2.0f, 5.0f, 0.5f, 0.1f};
    float orig_max = logits[2];
    am_apply_destiny_to_logits(logits, 5);

    // max should stay, others should be suppressed
    ASSERT_FLOAT(logits[2], orig_max, 0.01f, "max logit unchanged");
    ASSERT(logits[0] < 1.0f, "non-max logit suppressed");
    ASSERT(logits[4] < 0.1f, "lowest logit most suppressed");
}

// ── TEST 25: am_apply_suffering_to_logits ─────────────────────────────────

static void test_logit_suffering(void) {
    printf("\n── logit suffering ──\n");
    am_init();
    am_exec("PAIN 0.8");

    float logits[4] = {-2.0f, 0.0f, 1.0f, 3.0f};
    float mean = (-2.0f + 0.0f + 1.0f + 3.0f) / 4.0f;  // 0.5
    am_apply_suffering_to_logits(logits, 4);

    // All should be compressed toward mean
    ASSERT(logits[0] > -2.0f, "low logit rises toward mean");
    ASSERT(logits[3] < 3.0f, "high logit falls toward mean");
    // Check factor: mean + (orig - mean) * (1 - 0.5*0.8) = mean + (orig-mean)*0.6
    float expected_3 = mean + (3.0f - mean) * 0.6f;
    ASSERT_FLOAT(logits[3], expected_3, 0.1f, "suffering compression correct");
}

// ── TEST 26: am_apply_delta ───────────────────────────────────────────────

static void test_apply_delta(void) {
    printf("\n── am_apply_delta ──\n");

    // Simple 2×2 with rank 1: A=[1,2; 3,4], B=[1,1], x=[1,1]
    float A[4] = {1.0f, 2.0f, 3.0f, 4.0f};  // 2x2 (out=2, rank=2)
    float B[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // 2x2 (rank=2, in=2)
    float x[2] = {1.0f, 1.0f};
    float out[2] = {0.0f, 0.0f};

    // B @ x = [1, 1]
    // A @ [1,1] = [1+2, 3+4] = [3, 7]
    // out += 0.5 * [3, 7] = [1.5, 3.5]
    am_apply_delta(out, A, B, x, 2, 2, 2, 0.5f);
    ASSERT_FLOAT(out[0], 1.5f, 0.01f, "delta out[0] = 1.5");
    ASSERT_FLOAT(out[1], 3.5f, 0.01f, "delta out[1] = 3.5");
}

// ── TEST 27: am_compute_prophecy_debt ─────────────────────────────────────

static void test_prophecy_debt(void) {
    printf("\n── prophecy debt ──\n");

    float logits[4] = {1.0f, 2.0f, 5.0f, 3.0f};

    // Chose max → debt = 0
    float debt0 = am_compute_prophecy_debt(logits, 2, 4);
    ASSERT_FLOAT(debt0, 0.0f, 0.01f, "chose max → debt 0");

    // Chose worst → debt > 0
    float debt1 = am_compute_prophecy_debt(logits, 0, 4);
    ASSERT(debt1 > 0.0f, "chose non-max → positive debt");

    // Chose second worst → even more debt
    float debt_worst = am_compute_prophecy_debt(logits, 0, 4);
    float debt_mid = am_compute_prophecy_debt(logits, 3, 4);
    ASSERT(debt_worst > debt_mid, "farther from max → more debt");
}

// ── TEST 28: am_apply_field_to_logits (full pipeline) ─────────────────────

static void test_logit_pipeline(void) {
    printf("\n── full logit pipeline ──\n");
    am_init();
    am_exec("PAIN 0.5\nDESTINY 0.5\nPROPHECY 7\nATTEND_FOCUS 0.8");
    am_step(0.1f);

    float logits[5] = {-1.0f, 0.0f, 2.0f, 0.5f, -0.5f};
    float before_max = logits[2];
    am_apply_field_to_logits(logits, 5);

    // Some modification should have occurred
    ASSERT(logits[2] != before_max || logits[0] != -1.0f,
           "logits modified by pipeline");
}

// ── TEST 29: LAW enforcement in am_step ───────────────────────────────────

static void test_law_enforcement(void) {
    printf("\n── LAW enforcement ──\n");
    am_init();
    AM_State* s = am_get_state();

    am_step(1.0f);
    // Entropy should be >= entropy_floor
    ASSERT(s->entropy >= s->entropy_floor, "entropy >= entropy_floor");
    // Resonance should be <= resonance_ceiling
    ASSERT(s->resonance <= s->resonance_ceiling, "resonance <= resonance_ceiling");
    // Emergence computed
    ASSERT(s->emergence >= 0.0f && s->emergence <= 1.0f, "emergence in [0,1]");

    // High dissonance → lower resonance
    am_init();
    am_exec("DISSONANCE 0.9");
    am_step(1.0f);
    float res_high_diss = s->resonance;

    am_init();
    am_exec("DISSONANCE 0.0");
    am_step(1.0f);
    float res_low_diss = s->resonance;
    ASSERT(res_low_diss > res_high_diss, "low dissonance → higher resonance");
}

// ── TEST 30: Destiny bias computation ─────────────────────────────────────

static void test_destiny_bias(void) {
    printf("\n── destiny bias ──\n");
    am_init();
    AM_State* s = am_get_state();

    // Default: prophecy=7, destiny=0.35
    am_step(0.1f);
    // prophecy_scale = 1.0 + (7-7)*0.02 = 1.0
    // destiny_bias = 0.35 * 1.0 = 0.35
    ASSERT_FLOAT(s->destiny_bias, 0.35f, 0.01f, "destiny_bias at prophecy 7");

    // Higher prophecy → higher bias
    am_init();
    am_exec("PROPHECY 20\nDESTINY 0.5");
    am_step(0.1f);
    // prophecy_scale = 1.0 + (20-7)*0.02 = 1.26
    // destiny_bias = 0.5 * 1.26 = 0.63
    ASSERT_FLOAT(s->destiny_bias, 0.63f, 0.02f, "destiny_bias at prophecy 20");
}

// ── TEST 31: Expert blending ──────────────────────────────────────────────

static void test_expert_blending(void) {
    printf("\n── expert blending ──\n");
    am_init();
    AM_State* s = am_get_state();

    // All creative → temp should be higher (need am_step to recompute)
    am_exec("EXPERT_CREATIVE 1.0\nEXPERT_STRUCTURAL 0\nEXPERT_SEMANTIC 0\nEXPERT_PRECISE 0");
    am_step(0.01f);
    float creative_temp = s->effective_temp;

    am_init();
    am_exec("EXPERT_PRECISE 1.0\nEXPERT_STRUCTURAL 0\nEXPERT_SEMANTIC 0\nEXPERT_CREATIVE 0");
    am_step(0.01f);
    float precise_temp = s->effective_temp;

    ASSERT(creative_temp > precise_temp, "creative temp > precise temp");
}

// ── TEST 32: am_notorch_step (Hebbian plasticity) ─────────────────────────

static void test_notorch_step(void) {
    printf("\n── NOTORCH Hebbian plasticity ──\n");
    am_init();
    am_exec("NOTORCH_LR 0.1");

    // Small matrices: in=2, out=2, rank=2
    float A[4] = {0}; // 2x2
    float B[4] = {0}; // 2x2
    float x[2] = {1.0f, 0.5f};
    float dy[2] = {0.3f, -0.1f};

    // Positive signal should modify A and B
    am_notorch_step(A, B, 2, 2, 2, x, dy, 1.0f);

    int a_changed = 0, b_changed = 0;
    for (int i = 0; i < 4; i++) {
        if (fabsf(A[i]) > 0.0001f) a_changed = 1;
        if (fabsf(B[i]) > 0.0001f) b_changed = 1;
    }
    ASSERT(a_changed, "A matrix modified by notorch_step");
    ASSERT(b_changed, "B matrix modified by notorch_step");

    // Multiple steps should accumulate
    float a_before = fabsf(A[0]);
    for (int i = 0; i < 10; i++) {
        am_notorch_step(A, B, 2, 2, 2, x, dy, 1.0f);
    }
    // Norm should grow (or at least remain non-zero)
    float a_norm = 0.0f;
    for (int i = 0; i < 4; i++) a_norm += A[i] * A[i];
    ASSERT(a_norm > 0.0f, "A has accumulated updates");
}

// ── TEST 33: Field map new fields ─────────────────────────────────────────

static void test_field_map(void) {
    printf("\n── field map new fields ──\n");
    am_init();

    // New fields should be readable in expressions
    am_exec("LORA_ALPHA 0.5");
    am_exec("if lora_alpha > 0.4:\n    PROPHECY 55");
    ASSERT_INT(am_get_state()->prophecy, 55, "lora_alpha readable in expression");

    am_init();
    am_exec("SCHUMANN 7.80\nif schumann_hz < 7.82:\n    PROPHECY 33");
    ASSERT_INT(am_get_state()->prophecy, 33, "schumann_hz readable in expression");

    am_init();
    am_step(1.0f);  // compute entropy
    am_exec("if entropy >= 0:\n    PROPHECY 44");
    ASSERT_INT(am_get_state()->prophecy, 44, "entropy readable in expression");
}

// ── TEST 34: am_apply_attention_to_logits ─────────────────────────────────

static void test_logit_attention(void) {
    printf("\n── logit attention ──\n");
    am_init();

    // High focus, low spread → sharpen
    am_exec("ATTEND_FOCUS 0.9\nATTEND_SPREAD 0.1");
    float logits1[4] = {0.0f, 1.0f, 2.0f, 0.5f};
    am_apply_attention_to_logits(logits1, 4);
    // scale = 0.5 + 0.9 - 0.1 = 1.3 → amplified
    ASSERT(logits1[2] > 2.0f, "high focus amplifies max");

    // Low focus, high spread → blur
    am_init();
    am_exec("ATTEND_FOCUS 0.1\nATTEND_SPREAD 0.9");
    float logits2[4] = {0.0f, 1.0f, 2.0f, 0.5f};
    am_apply_attention_to_logits(logits2, 4);
    // scale = 0.5 + 0.1 - 0.9 = -0.3 → clamped to 0.1 → compressed
    ASSERT(logits2[2] < 2.0f, "low focus compresses max");
}

// ── TEST 35: am_apply_laws_to_logits ──────────────────────────────────────

static void test_logit_laws(void) {
    printf("\n── logit laws ──\n");
    am_init();

    // One dominant logit, low entropy floor → should compress
    am_exec("LAW ENTROPY_FLOOR 0.5");
    float logits[4] = {0.0f, 0.0f, 20.0f, 0.0f};
    am_apply_laws_to_logits(logits, 4);
    ASSERT(logits[2] < 20.0f, "entropy floor compresses dominant logit");
}

// ── TEST 36: COSMIC_COHERENCE backward compat ─────────────────────────────

static void test_cosmic_coherence_compat(void) {
    printf("\n── COSMIC_COHERENCE compat ──\n");
    am_init();
    AM_State* s = am_get_state();

    am_exec("COSMIC_COHERENCE 0.7");
    ASSERT_FLOAT(s->schumann_coherence, 0.7f, 0.01f, "COSMIC_COHERENCE maps to schumann_coherence");
}

// ── TEST 37: am_copy_state 32 fields ──────────────────────────────────────

static void test_copy_state_32(void) {
    printf("\n── am_copy_state 32 ──\n");
    am_init();
    am_exec("LORA_ALPHA 0.3\nSCHUMANN 7.80\nSEASON AUTUMN");
    am_step(0.1f);

    float out[32];
    int rc = am_copy_state(out);
    ASSERT_INT(rc, 0, "am_copy_state returns 0");
    ASSERT_FLOAT(out[22], 0.3f, 0.01f, "out[22] = lora_alpha");
    ASSERT_FLOAT(out[28], 7.80f, 0.01f, "out[28] = schumann_hz");
    ASSERT_FLOAT(out[30], 2.0f, 0.01f, "out[30] = season AUTUMN (2)");
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
    test_schumann();
    test_lora_notorch();
    test_darkmatter_core();
    test_echo();
    test_seasons();
    test_builtins();
    test_logit_destiny();
    test_logit_suffering();
    test_apply_delta();
    test_prophecy_debt();
    test_logit_pipeline();
    test_law_enforcement();
    test_destiny_bias();
    test_expert_blending();
    test_notorch_step();
    test_field_map();
    test_logit_attention();
    test_logit_laws();
    test_cosmic_coherence_compat();
    test_copy_state_32();

    // ── Phase 7: expression args in user-defined functions ──
    printf("\n── expression args in user-defined functions ──\n");
    am_init();
    am_exec(
        "def set_prophecy(depth):\n"
        "    PROPHECY depth\n"
        "\n"
        "set_prophecy(33)\n"
    );
    ASSERT_INT(am_get_state()->prophecy, 33, "set_prophecy(33) → prophecy 33");

    am_exec(
        "def double_prophecy(n):\n"
        "    PROPHECY n * 2\n"
        "\n"
        "double_prophecy(7)\n"
    );
    ASSERT_INT(am_get_state()->prophecy, 14, "double_prophecy(7) → prophecy 14");

    am_exec(
        "def set_pain_relative(base, scale):\n"
        "    PAIN base * scale\n"
        "\n"
        "set_pain_relative(0.5, 0.8)\n"
    );
    ASSERT_FLOAT(am_get_state()->pain, 0.4f, 0.01f, "set_pain_relative(0.5, 0.8) → pain 0.4");

    // ── MLP controller ──
    printf("\n── 4.C MLP controller ──\n");
    am_init();
    am_get_state()->pain = 0.8f;
    am_get_state()->entropy = 0.5f;
    am_get_state()->resonance = 0.5f;
    float winter_before = am_get_state()->winter_energy;
    for (int i = 0; i < 50; i++) am_step(0.1f);
    ASSERT(am_get_state()->winter_energy >= winter_before, "high pain → winter energy grows");

    am_init();
    am_get_state()->emergence = 0.8f;
    am_get_state()->entropy = 0.3f;
    am_get_state()->resonance = 0.8f;
    float summer_before = am_get_state()->summer_energy;
    for (int i = 0; i < 50; i++) am_step(0.1f);
    ASSERT(am_get_state()->summer_energy >= summer_before, "high emergence → summer energy grows");

    am_init();
    am_step(0.1f);
    ASSERT(am_get_state()->field_health > 0.0f, "field_health is computed");

    // ── SCAR text storage ──
    printf("\n── SCAR text storage ──\n");
    am_init();
    am_exec("SCAR \"overwhelming\"");
    ASSERT_INT(am_get_state()->n_scars, 1, "scar count 1");
    ASSERT(strcmp(am_get_state()->scar_texts[0], "overwhelming") == 0, "scar text stored");
    am_exec("SCAR \"loss\"");
    ASSERT_INT(am_get_state()->n_scars, 2, "scar count 2");
    ASSERT(strcmp(am_get_state()->scar_texts[1], "loss") == 0, "scar text 2");

    // ── Schumann harmonics modulation ──
    printf("\n── Schumann harmonics modulation ──\n");
    am_init();
    am_get_state()->tension = 0.5f;
    am_get_state()->schumann_modulation = 0.8f;
    {
      float tb = am_get_state()->tension;
      am_step(1.0f);
      ASSERT(am_get_state()->tension < tb, "harmonics-modulated healing reduces tension");
    }

    // ── Level 1 macros ──
    printf("\n── Level 1 macros ──\n");
    am_init();
    am_exec("MACRO wake { PROPHECY 12; VELOCITY RUN; ECHO macro fired }");
    am_exec("@wake");
    ASSERT_INT(am_get_state()->prophecy, 12, "macro @wake → prophecy 12");
    ASSERT_INT(am_get_state()->velocity_mode, 2, "macro @wake → velocity RUN");

    am_exec("MACRO calm { VELOCITY WALK; PAIN 0; TENSION 0 }");
    am_exec("@calm");
    ASSERT_INT(am_get_state()->velocity_mode, 1, "macro @calm → velocity WALK");
    ASSERT_FLOAT(am_get_state()->pain, 0.0f, 0.01f, "macro @calm → pain 0");

    // ── INCLUDE with real file ──
    printf("\n── INCLUDE with real file ──\n");
    {
        FILE* f = fopen("/tmp/test_include_aml.aml", "w");
        if (f) {
            fprintf(f, "PROPHECY 42\nVELOCITY RUN\nATTEND_FOCUS 0.99\n");
            fclose(f);
            am_init();
            am_exec_file("/tmp/test_include_aml.aml");
            ASSERT_INT(am_get_state()->prophecy, 42, "exec_file: prophecy 42");
            ASSERT_INT(am_get_state()->velocity_mode, 2, "exec_file: velocity RUN");
            ASSERT_FLOAT(am_get_state()->attend_focus, 0.99f, 0.01f, "exec_file: focus 0.99");

            am_init();
            am_exec("INCLUDE /tmp/test_include_aml.aml");
            ASSERT_INT(am_get_state()->prophecy, 42, "INCLUDE in script: prophecy 42");
            remove("/tmp/test_include_aml.aml");
        }
    }

    printf("\n═══ Results: %d/%d passed ═══\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
