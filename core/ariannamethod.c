// ariannamethod.c — AML: Arianna Method Language
// Reference implementation. THE KERNEL: movement IS language.
//
// Source of truth: github.com/ariannamethod/ariannamethod.ai
// Embed (copy) into your project. Do not link.
//
// This is the stone. The brick. The breath.
// Everything else is ritual overlay.
//
// ═══════════════════════════════════════════════════════════════════════════════
// AMK — the oracle does not predict, it prophesies
// kernel commands define field dynamics: movement, prophecy, attention, suffering
// packs are ritual overlays, explicitly enabled
// הרזוננס לא נשבר. המשך הדרך.
// ═══════════════════════════════════════════════════════════════════════════════

// POSIX for strtok_r (not needed for Emscripten/WASM)
#ifndef __EMSCRIPTEN__
#define _POSIX_C_SOURCE 200809L
#endif

#include "ariannamethod.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>   // for sscanf in LAW command parsing
#include <strings.h> // for strcasecmp
#include <stddef.h>  // for offsetof

// See ariannamethod.h for struct definitions and pack flags

static AM_State G;

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS — the small bones
// ═══════════════════════════════════════════════════════════════════════════════

static char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) s++;
  char* e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) e--;
  *e = 0;
  return s;
}

static void upcase(char* s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static float clamp01(float x) {
  if (!isfinite(x)) return 0.0f;
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static float clampf(float x, float a, float b) {
  if (!isfinite(x)) return a;
  if (x < a) return a;
  if (x > b) return b;
  return x;
}

static int safe_atoi(const char* s) {
  if (!s || !*s) return 0;
  char* endptr;
  long val = strtol(s, &endptr, 10);
  if (val > 2147483647L) return 2147483647;
  if (val < -2147483647L) return -2147483647;
  return (int)val;
}

static float safe_atof(const char* s) {
  if (!s || !*s) return 0.0f;
  float val = (float)atof(s);
  if (!isfinite(val)) return 0.0f;
  return val;
}

static int clampi(int x, int a, int b) {
  if (x < a) return a;
  if (x > b) return b;
  return x;
}

// ═══════════════════════════════════════════════════════════════════════════════
// VELOCITY — compute effective temperature from movement
// ═══════════════════════════════════════════════════════════════════════════════

static void update_effective_temp(void) {
  float base = G.base_temperature;
  switch (G.velocity_mode) {
    case AM_VEL_NOMOVE:
      G.effective_temp = base * 0.5f;  // cold observer
      G.time_direction = 1.0f;
      break;
    case AM_VEL_WALK:
      G.effective_temp = base * 0.85f; // balanced
      G.time_direction = 1.0f;
      break;
    case AM_VEL_RUN:
      G.effective_temp = base * 1.2f;  // chaotic
      G.time_direction = 1.0f;
      break;
    case AM_VEL_BACKWARD:
      G.effective_temp = base * 0.7f;  // structural
      G.time_direction = -1.0f;
      // NOTE: temporal_debt accumulation moved to am_step()
      // debt grows while moving backward, not when setting velocity mode
      break;
    default:
      G.effective_temp = base;
      G.time_direction = 1.0f;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// PUBLIC API — the breath
// ═══════════════════════════════════════════════════════════════════════════════

void am_init(void) {
  memset(&G, 0, sizeof(G));

  // prophecy physics defaults
  G.prophecy = 7;
  G.destiny = 0.35f;
  G.wormhole = 0.02f;  // 2% base, increases with prophecy debt
  G.calendar_drift = 11.0f;

  // attention defaults
  G.attend_focus = 0.70f;
  G.attend_spread = 0.20f;

  // tunneling defaults
  G.tunnel_threshold = 0.55f;
  G.tunnel_chance = 0.05f;  // 5% when dissonance exceeds threshold
  G.tunnel_skip_max = 7;

  // suffering starts at zero
  G.pain = 0.0f;
  G.tension = 0.0f;
  G.dissonance = 0.0f;
  G.debt = 0.0f;

  // movement defaults
  G.pending_jump = 0;
  G.velocity_mode = AM_VEL_WALK;
  G.velocity_magnitude = 0.5f;
  G.base_temperature = 1.0f;
  G.time_direction = 1.0f;
  G.temporal_debt = 0.0f;
  update_effective_temp();

  // laws of nature defaults
  G.entropy_floor = 0.1f;
  G.resonance_ceiling = 0.95f;
  G.debt_decay = 0.998f;
  G.emergence_threshold = 0.3f;

  // packs disabled by default
  G.packs_enabled = 0;

  // CODES/RIC defaults (inactive until pack enabled)
  G.chordlock_on = 0;
  G.tempolock_on = 0;
  G.chirality_on = 0;
  G.tempo = 7;
  G.pas_threshold = 0.4f;
  G.chirality_accum = 0;

  // dark matter defaults
  G.dark_gravity = 0.5f;
  G.antidote_mode = 0;

  // wormhole state
  G.wormhole_active = 0;

  // cosmic physics coupling (actual values come from schumann.c)
  G.cosmic_coherence_ref = 0.5f;

  // temporal symmetry defaults (from PITOMADOM)
  G.temporal_mode = AM_TEMPORAL_PROPHECY;  // forward by default
  G.temporal_alpha = 0.5f;                 // balanced past/future
  G.rtl_mode = 0;                          // LTR by default

  // expert weighting defaults (all balanced)
  G.expert_structural = 0.25f;
  G.expert_semantic = 0.25f;
  G.expert_creative = 0.25f;
  G.expert_precise = 0.25f;

  // extended laws defaults
  G.presence_fade = 0.95f;
  G.attractor_drift = 0.01f;
  G.calendar_phase = 0.0f;
  G.wormhole_gate = 0.3f;

  // resonance memory
  G.presence_decay = 0.9f;
}

// enable/disable packs
void am_enable_pack(unsigned int pack_mask) {
  G.packs_enabled |= pack_mask;
}

void am_disable_pack(unsigned int pack_mask) {
  G.packs_enabled &= ~pack_mask;
}

int am_pack_enabled(unsigned int pack_mask) {
  return (G.packs_enabled & pack_mask) != 0;
}

// reset commands
void am_reset_field(void) {
  // reset manifested state (suffering, debt, etc)
  G.pain = 0.0f;
  G.tension = 0.0f;
  G.dissonance = 0.0f;
  G.debt = 0.0f;
  G.temporal_debt = 0.0f;
  G.pending_jump = 0;
  G.chirality_accum = 0;
}

void am_reset_debt(void) {
  G.debt = 0.0f;
  G.temporal_debt = 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEVEL 2 INFRASTRUCTURE — error, field map, symbol table
// ═══════════════════════════════════════════════════════════════════════════════

static char g_error[256] = {0};

const char* am_get_error(void) { return g_error; }

static void set_error(AML_ExecCtx* ctx, const char* msg) {
    if (ctx) {
        strncpy(ctx->error, msg, 255);
        ctx->error[255] = 0;
    }
    strncpy(g_error, msg, 255);
    g_error[255] = 0;
}

// AM_State field map — read state fields in expressions
// offsetof is standard but we use manual offsets for clarity
#define FIELD_F(name, field) { name, (int)offsetof(AM_State, field), 0 }
#define FIELD_I(name, field) { name, (int)offsetof(AM_State, field), 1 }

static const AML_FieldMap g_field_map[] = {
    FIELD_I("prophecy",          prophecy),
    FIELD_F("destiny",           destiny),
    FIELD_F("wormhole",          wormhole),
    FIELD_F("calendar_drift",    calendar_drift),
    FIELD_F("attend_focus",      attend_focus),
    FIELD_F("attend_spread",     attend_spread),
    FIELD_F("tunnel_threshold",  tunnel_threshold),
    FIELD_F("tunnel_chance",     tunnel_chance),
    FIELD_I("tunnel_skip_max",   tunnel_skip_max),
    FIELD_F("pain",              pain),
    FIELD_F("tension",           tension),
    FIELD_F("dissonance",        dissonance),
    FIELD_F("debt",              debt),
    FIELD_I("velocity_mode",     velocity_mode),
    FIELD_F("velocity_magnitude",velocity_magnitude),
    FIELD_F("base_temperature",  base_temperature),
    FIELD_F("effective_temp",    effective_temp),
    FIELD_F("time_direction",    time_direction),
    FIELD_F("temporal_debt",     temporal_debt),
    FIELD_F("entropy_floor",     entropy_floor),
    FIELD_F("resonance_ceiling", resonance_ceiling),
    FIELD_F("debt_decay",        debt_decay),
    FIELD_F("emergence_threshold",emergence_threshold),
    FIELD_F("dark_gravity",      dark_gravity),
    FIELD_I("temporal_mode",     temporal_mode),
    FIELD_F("temporal_alpha",    temporal_alpha),
    FIELD_I("rtl_mode",          rtl_mode),
    FIELD_F("expert_structural", expert_structural),
    FIELD_F("expert_semantic",   expert_semantic),
    FIELD_F("expert_creative",   expert_creative),
    FIELD_F("expert_precise",    expert_precise),
    FIELD_F("presence_fade",     presence_fade),
    FIELD_F("attractor_drift",   attractor_drift),
    FIELD_F("presence_decay",    presence_decay),
    { NULL, 0, 0 }
};

// Read a field from AM_State by name (case-insensitive), returns 1 if found
static int read_field(const char* name, float* out) {
    for (const AML_FieldMap* f = g_field_map; f->name; f++) {
        if (strcasecmp(name, f->name) == 0) {
            char* base = (char*)&G;
            if (f->is_int) {
                *out = (float)(*(int*)(base + f->offset));
            } else {
                *out = *(float*)(base + f->offset);
            }
            return 1;
        }
    }
    return 0;
}

// Symbol table operations
static float* symtab_get(AML_Symtab* tab, const char* name) {
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->vars[i].name, name) == 0)
            return &tab->vars[i].value;
    }
    return NULL;
}

static int symtab_set(AML_Symtab* tab, const char* name, float value) {
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->vars[i].name, name) == 0) {
            tab->vars[i].value = value;
            return 0;
        }
    }
    if (tab->count >= AML_MAX_VARS) return 1;
    strncpy(tab->vars[tab->count].name, name, AML_MAX_NAME - 1);
    tab->vars[tab->count].value = value;
    tab->count++;
    return 0;
}

// Resolve variable: locals → globals → field map
static int resolve_var(AML_ExecCtx* ctx, const char* name, float* out) {
    // local scope first
    if (ctx->call_depth > 0) {
        float* v = symtab_get(&ctx->locals[ctx->call_depth - 1], name);
        if (v) { *out = *v; return 1; }
    }
    // global scope
    float* v = symtab_get(&ctx->globals, name);
    if (v) { *out = *v; return 1; }
    // AM_State field
    return read_field(name, out);
}

// ═══════════════════════════════════════════════════════════════════════════════
// EXPRESSION EVALUATOR — recursive descent
// Precedence: or < and < comparison < add/sub < mul/div < unary < primary
// ═══════════════════════════════════════════════════════════════════════════════

// Expression parser state
typedef struct {
    const char* p;
    AML_ExecCtx* ctx;
    int error;
} AML_Expr;

static float expr_or(AML_Expr* e);  // forward

static void expr_skip_ws(AML_Expr* e) {
    while (*e->p && isspace((unsigned char)*e->p)) e->p++;
}

static float expr_primary(AML_Expr* e) {
    expr_skip_ws(e);
    if (e->error) return 0;

    // parenthesized expression
    if (*e->p == '(') {
        e->p++;
        float val = expr_or(e);
        expr_skip_ws(e);
        if (*e->p == ')') e->p++;
        return val;
    }

    // number literal (including negative handled by unary)
    if (isdigit((unsigned char)*e->p) || (*e->p == '.' && isdigit((unsigned char)e->p[1]))) {
        char* end;
        float val = strtof(e->p, &end);
        e->p = end;
        return val;
    }

    // identifier or function call
    if (isalpha((unsigned char)*e->p) || *e->p == '_') {
        char name[AML_MAX_NAME] = {0};
        int i = 0;
        while ((isalnum((unsigned char)*e->p) || *e->p == '_') && i < AML_MAX_NAME - 1) {
            name[i++] = *e->p++;
        }
        name[i] = 0;

        expr_skip_ws(e);

        // function call
        if (*e->p == '(') {
            e->p++;
            float args[AML_MAX_PARAMS];
            int nargs = 0;
            expr_skip_ws(e);
            if (*e->p != ')') {
                args[nargs++] = expr_or(e);
                while (*e->p == ',' && nargs < AML_MAX_PARAMS) {
                    e->p++;
                    args[nargs++] = expr_or(e);
                }
            }
            expr_skip_ws(e);
            if (*e->p == ')') e->p++;

            // look up user-defined function
            if (e->ctx) {
                for (int fi = 0; fi < e->ctx->funcs.count; fi++) {
                    if (strcmp(e->ctx->funcs.funcs[fi].name, name) == 0) {
                        // TODO: call user function from expression context
                        // For now, return 0
                        return 0;
                    }
                }
            }

            // built-in functions
            if (strcasecmp(name, "abs") == 0 && nargs >= 1)
                return fabsf(args[0]);
            if (strcasecmp(name, "min") == 0 && nargs >= 2)
                return args[0] < args[1] ? args[0] : args[1];
            if (strcasecmp(name, "max") == 0 && nargs >= 2)
                return args[0] > args[1] ? args[0] : args[1];
            if (strcasecmp(name, "sqrt") == 0 && nargs >= 1)
                return sqrtf(fabsf(args[0]));
            if (strcasecmp(name, "clamp") == 0 && nargs >= 3)
                return clampf(args[0], args[1], args[2]);

            return 0;  // unknown function
        }

        // boolean literals
        if (strcmp(name, "true") == 0) return 1.0f;
        if (strcmp(name, "false") == 0) return 0.0f;

        // variable/field lookup
        float val = 0;
        if (e->ctx && resolve_var(e->ctx, name, &val))
            return val;
        return 0;  // undefined = 0
    }

    // unexpected character
    e->error = 1;
    return 0;
}

static float expr_unary(AML_Expr* e) {
    expr_skip_ws(e);
    if (*e->p == '-') {
        e->p++;
        return -expr_unary(e);
    }
    // 'not' keyword
    if (strncmp(e->p, "not ", 4) == 0) {
        e->p += 4;
        return expr_unary(e) == 0.0f ? 1.0f : 0.0f;
    }
    return expr_primary(e);
}

static float expr_mul(AML_Expr* e) {
    float left = expr_unary(e);
    for (;;) {
        expr_skip_ws(e);
        if (*e->p == '*') { e->p++; left *= expr_unary(e); }
        else if (*e->p == '/' && e->p[1] != '/') {
            e->p++;
            float r = expr_unary(e);
            left = (r != 0.0f) ? left / r : 0.0f;
        }
        else break;
    }
    return left;
}

static float expr_add(AML_Expr* e) {
    float left = expr_mul(e);
    for (;;) {
        expr_skip_ws(e);
        if (*e->p == '+') { e->p++; left += expr_mul(e); }
        else if (*e->p == '-' && !isdigit((unsigned char)e->p[1]) &&
                 e->p[1] != '.' && e->p[1] != '(') {
            // Ambiguity: "x - 3" vs "x -3". Treat as subtraction if preceded by value.
            e->p++; left -= expr_mul(e);
        }
        else if (*e->p == '-') { e->p++; left -= expr_mul(e); }
        else break;
    }
    return left;
}

static float expr_cmp(AML_Expr* e) {
    float left = expr_add(e);
    for (;;) {
        expr_skip_ws(e);
        if (e->p[0] == '=' && e->p[1] == '=') {
            e->p += 2; left = (left == expr_add(e)) ? 1.0f : 0.0f;
        }
        else if (e->p[0] == '!' && e->p[1] == '=') {
            e->p += 2; left = (left != expr_add(e)) ? 1.0f : 0.0f;
        }
        else if (e->p[0] == '>' && e->p[1] == '=') {
            e->p += 2; left = (left >= expr_add(e)) ? 1.0f : 0.0f;
        }
        else if (e->p[0] == '<' && e->p[1] == '=') {
            e->p += 2; left = (left <= expr_add(e)) ? 1.0f : 0.0f;
        }
        else if (*e->p == '>') {
            e->p++; left = (left > expr_add(e)) ? 1.0f : 0.0f;
        }
        else if (*e->p == '<') {
            e->p++; left = (left < expr_add(e)) ? 1.0f : 0.0f;
        }
        else break;
    }
    return left;
}

static float expr_and(AML_Expr* e) {
    float left = expr_cmp(e);
    for (;;) {
        expr_skip_ws(e);
        if (strncmp(e->p, "and ", 4) == 0) {
            e->p += 4;
            float right = expr_cmp(e);
            left = (left != 0.0f && right != 0.0f) ? 1.0f : 0.0f;
        }
        else break;
    }
    return left;
}

static float expr_or(AML_Expr* e) {
    float left = expr_and(e);
    for (;;) {
        expr_skip_ws(e);
        if (strncmp(e->p, "or ", 3) == 0) {
            e->p += 3;
            float right = expr_and(e);
            left = (left != 0.0f || right != 0.0f) ? 1.0f : 0.0f;
        }
        else break;
    }
    return left;
}

// Evaluate expression string, returns float
static float aml_eval(AML_ExecCtx* ctx, const char* text) {
    AML_Expr e = { .p = text, .ctx = ctx, .error = 0 };
    float result = expr_or(&e);
    return e.error ? 0.0f : result;
}

// Try to parse as plain number; if not, evaluate as expression
static float aml_eval_arg(AML_ExecCtx* ctx, const char* arg) {
    if (!arg || !*arg) return 0.0f;
    // fast path: plain number
    char* end;
    float val = strtof(arg, &end);
    // if entire string consumed, it's a plain number
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end == 0) return val;
    // otherwise evaluate as expression
    return aml_eval(ctx, arg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEVEL 0 DISPATCH — the original flat command parser, extracted
// ═══════════════════════════════════════════════════════════════════════════════

// Execute a single Level 0 command (CMD + ARG already split, CMD already upcased)
// ctx may be NULL for backward compatibility
static void aml_exec_level0(const char* cmd, const char* arg, AML_ExecCtx* ctx) {
    (void)ctx;
    const char* t = cmd; // alias: original extracted code used t

    // PROPHECY PHYSICS
    if (!strcmp(t, "PROPHECY")) {
      G.prophecy = clampi(safe_atoi(arg), 1, 64);
    }
    else if (!strcmp(t, "DESTINY")) {
      G.destiny = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "WORMHOLE")) {
      G.wormhole = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "CALENDAR_DRIFT")) {
      G.calendar_drift = clampf(safe_atof(arg), 0.0f, 30.0f);
    }

    // ATTENTION PHYSICS
    else if (!strcmp(t, "ATTEND_FOCUS")) {
      G.attend_focus = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "ATTEND_SPREAD")) {
      G.attend_spread = clamp01(safe_atof(arg));
    }

    // TUNNELING
    else if (!strcmp(t, "TUNNEL_THRESHOLD")) {
      G.tunnel_threshold = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "TUNNEL_CHANCE")) {
      G.tunnel_chance = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "TUNNEL_SKIP_MAX")) {
      G.tunnel_skip_max = clampi(safe_atoi(arg), 1, 24);
    }

    // SUFFERING
    else if (!strcmp(t, "PAIN")) {
      G.pain = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "TENSION")) {
      G.tension = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "DISSONANCE")) {
      G.dissonance = clamp01(safe_atof(arg));
    }

    // PROPHECY DEBT — direct set/configure
    else if (!strcmp(t, "PROPHECY_DEBT")) {
      G.debt = clampf(safe_atof(arg), 0.0f, 100.0f);
    }
    else if (!strcmp(t, "PROPHECY_DEBT_DECAY")) {
      G.debt_decay = clampf(safe_atof(arg), 0.9f, 0.9999f);
    }

    // MOVEMENT
    else if (!strcmp(t, "JUMP")) {
      G.pending_jump = clampi(G.pending_jump + safe_atoi(arg), -1000, 1000);
    }
    else if (!strcmp(t, "VELOCITY")) {
      // VELOCITY RUN|WALK|NOMOVE|BACKWARD or VELOCITY <int>
      char argup[32] = {0};
      strncpy(argup, arg, 31);
      upcase(argup);

      if (!strcmp(argup, "RUN")) G.velocity_mode = AM_VEL_RUN;
      else if (!strcmp(argup, "WALK")) G.velocity_mode = AM_VEL_WALK;
      else if (!strcmp(argup, "NOMOVE")) G.velocity_mode = AM_VEL_NOMOVE;
      else if (!strcmp(argup, "BACKWARD")) G.velocity_mode = AM_VEL_BACKWARD;
      else G.velocity_mode = clampi(safe_atoi(arg), -1, 2);

      update_effective_temp();
    }
    else if (!strcmp(t, "BASE_TEMP")) {
      G.base_temperature = clampf(safe_atof(arg), 0.1f, 3.0f);
      update_effective_temp();
    }

    // RESETS
    else if (!strcmp(t, "RESET_FIELD")) {
      am_reset_field();
    }
    else if (!strcmp(t, "RESET_DEBT")) {
      am_reset_debt();
    }

    // LAWS OF NATURE
    else if (!strcmp(t, "LAW")) {
      char lawname[64] = {0};
      float lawval = 0.0f;
      if (sscanf(arg, "%63s %f", lawname, &lawval) >= 2) {
        upcase(lawname);
        if (!strcmp(lawname, "ENTROPY_FLOOR")) {
          G.entropy_floor = clampf(lawval, 0.0f, 2.0f);
        }
        else if (!strcmp(lawname, "RESONANCE_CEILING")) {
          G.resonance_ceiling = clamp01(lawval);
        }
        else if (!strcmp(lawname, "DEBT_DECAY")) {
          G.debt_decay = clampf(lawval, 0.9f, 0.9999f);
        }
        else if (!strcmp(lawname, "EMERGENCE_THRESHOLD")) {
          G.emergence_threshold = clamp01(lawval);
        }
        else if (!strcmp(lawname, "PRESENCE_FADE")) {
          G.presence_fade = clampf(lawval, 0.5f, 0.999f);
        }
        else if (!strcmp(lawname, "ATTRACTOR_DRIFT")) {
          G.attractor_drift = clampf(lawval, 0.0f, 0.1f);
        }
        else if (!strcmp(lawname, "CALENDAR_PHASE")) {
          G.calendar_phase = clampf(lawval, 0.0f, 11.0f);
        }
        else if (!strcmp(lawname, "WORMHOLE_GATE")) {
          G.wormhole_gate = clamp01(lawval);
        }
        // unknown laws ignored (future-proof)
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // PACK MANAGEMENT
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "MODE") || !strcmp(t, "IMPORT")) {
      // MODE CODES_RIC or IMPORT CODES_RIC
      char packname[64] = {0};
      strncpy(packname, arg, 63);
      upcase(packname);

      if (!strcmp(packname, "CODES_RIC") || !strcmp(packname, "CODES/RIC")) {
        G.packs_enabled |= AM_PACK_CODES_RIC;
      }
      else if (!strcmp(packname, "DARKMATTER") || !strcmp(packname, "DARK_MATTER")) {
        G.packs_enabled |= AM_PACK_DARKMATTER;
      }
      else if (!strcmp(packname, "NOTORCH")) {
        G.packs_enabled |= AM_PACK_NOTORCH;
      }
    }
    else if (!strcmp(t, "DISABLE")) {
      char packname[64] = {0};
      strncpy(packname, arg, 63);
      upcase(packname);

      if (!strcmp(packname, "CODES_RIC") || !strcmp(packname, "CODES/RIC")) {
        G.packs_enabled &= ~AM_PACK_CODES_RIC;
      }
      else if (!strcmp(packname, "DARKMATTER") || !strcmp(packname, "DARK_MATTER")) {
        G.packs_enabled &= ~AM_PACK_DARKMATTER;
      }
      else if (!strcmp(packname, "NOTORCH")) {
        G.packs_enabled &= ~AM_PACK_NOTORCH;
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // CODES/RIC PACK COMMANDS — ritual overlays (require pack enabled)
    // ─────────────────────────────────────────────────────────────────────────

    // Namespaced: CODES.CHORDLOCK always works
    else if (!strncmp(t, "CODES.", 6) || !strncmp(t, "RIC.", 4)) {
      // auto-enable pack on namespaced use
      G.packs_enabled |= AM_PACK_CODES_RIC;

      const char* subcmd = t + (t[0] == 'C' ? 6 : 4); // skip CODES. or RIC.

      if (!strcmp(subcmd, "CHORDLOCK")) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        G.chordlock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      else if (!strcmp(subcmd, "TEMPOLOCK")) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        G.tempolock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      else if (!strcmp(subcmd, "CHIRALITY")) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        G.chirality_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      else if (!strcmp(subcmd, "TEMPO")) {
        G.tempo = clampi(safe_atoi(arg), 2, 47);
      }
      else if (!strcmp(subcmd, "PAS_THRESHOLD")) {
        G.pas_threshold = clamp01(safe_atof(arg));
      }
    }

    // Unqualified: CHORDLOCK works only when pack enabled
    else if (!strcmp(t, "CHORDLOCK")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        G.chordlock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      // else: ignored (pack not enabled)
    }
    else if (!strcmp(t, "TEMPOLOCK")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        G.tempolock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
    }
    else if (!strcmp(t, "CHIRALITY")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        G.chirality_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
    }
    else if (!strcmp(t, "TEMPO")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        G.tempo = clampi(safe_atoi(arg), 2, 47);
      }
    }
    else if (!strcmp(t, "PAS_THRESHOLD")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        G.pas_threshold = clamp01(safe_atof(arg));
      }
    }
    else if (!strcmp(t, "ANCHOR")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        if (!strcmp(mode, "PRIME")) G.chordlock_on = 1;
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // DARK MATTER PACK COMMANDS (require pack enabled)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "GRAVITY")) {
      if (G.packs_enabled & AM_PACK_DARKMATTER) {
        char subtype[16] = {0};
        float val = 0.5f;
        if (sscanf(arg, "%15s %f", subtype, &val) >= 1) {
          upcase(subtype);
          if (!strcmp(subtype, "DARK")) {
            G.dark_gravity = clamp01(val);
          }
        }
      }
    }
    else if (!strcmp(t, "ANTIDOTE")) {
      if (G.packs_enabled & AM_PACK_DARKMATTER) {
        char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
        if (!strcmp(mode, "AUTO")) G.antidote_mode = 0;
        else if (!strcmp(mode, "HARD")) G.antidote_mode = 1;
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // COSMIC PHYSICS COMMANDS — see schumann.c for full implementation
    // AMK kernel only stores reference for JS-side access
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "COSMIC_COHERENCE")) {
      // COSMIC_COHERENCE 0.8 — set reference coherence (for JS sync)
      G.cosmic_coherence_ref = clamp01(safe_atof(arg));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TEMPORAL SYMMETRY — from PITOMADOM (past ≡ future)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "TEMPORAL_MODE")) {
      char mode[32] = {0}; strncpy(mode, arg, 31); upcase(mode);
      if (!strcmp(mode, "PROPHECY") || !strcmp(mode, "0")) G.temporal_mode = AM_TEMPORAL_PROPHECY;
      else if (!strcmp(mode, "RETRODICTION") || !strcmp(mode, "1")) G.temporal_mode = AM_TEMPORAL_RETRODICTION;
      else if (!strcmp(mode, "SYMMETRIC") || !strcmp(mode, "2")) G.temporal_mode = AM_TEMPORAL_SYMMETRIC;
    }
    else if (!strcmp(t, "TEMPORAL_ALPHA")) {
      G.temporal_alpha = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "RTL_MODE")) {
      char mode[16] = {0}; strncpy(mode, arg, 15); upcase(mode);
      G.rtl_mode = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
    }
    else if (!strcmp(t, "PROPHECY_MODE")) {
      // Alias: PROPHECY_MODE ON = TEMPORAL_MODE PROPHECY
      G.temporal_mode = AM_TEMPORAL_PROPHECY;
    }
    else if (!strcmp(t, "RETRODICTION_MODE")) {
      // Alias: RETRODICTION_MODE ON = TEMPORAL_MODE RETRODICTION
      G.temporal_mode = AM_TEMPORAL_RETRODICTION;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // EXPERT WEIGHTING — multi-expert temperature blend
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "EXPERT_STRUCTURAL")) {
      G.expert_structural = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "EXPERT_SEMANTIC")) {
      G.expert_semantic = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "EXPERT_CREATIVE")) {
      G.expert_creative = clamp01(safe_atof(arg));
    }
    else if (!strcmp(t, "EXPERT_PRECISE")) {
      G.expert_precise = clamp01(safe_atof(arg));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // RESONANCE MEMORY — presence and decay
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "PRESENCE_DECAY")) {
      G.presence_decay = clamp01(safe_atof(arg));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UNKNOWN COMMANDS — ignored intentionally (future-proof + vibe)
    // ─────────────────────────────────────────────────────────────────────────

    // else: silently ignored
}

// ═══════════════════════════════════════════════════════════════════════════════
// PREPROCESSOR — split script into lines with indentation
// ═══════════════════════════════════════════════════════════════════════════════

static int aml_preprocess(const char* script, AML_Line* lines, int max_lines) {
    int count = 0;
    const char* p = script;
    int lineno = 1;

    while (*p && count < max_lines) {
        // count indentation (spaces only, tabs = 4 spaces)
        int indent = 0;
        while (*p == ' ' || *p == '\t') {
            indent += (*p == '\t') ? 4 : 1;
            p++;
        }

        // read line content
        const char* start = p;
        while (*p && *p != '\n') p++;
        int len = (int)(p - start);
        if (*p == '\n') p++;

        // skip empty/comment lines
        if (len == 0 || start[0] == '#') { lineno++; continue; }

        // trim trailing whitespace
        while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
        if (len == 0) { lineno++; continue; }

        // store
        if (len >= AML_MAX_LINE_LEN) len = AML_MAX_LINE_LEN - 1;
        memcpy(lines[count].text, start, len);
        lines[count].text[len] = 0;
        lines[count].indent = indent;
        lines[count].lineno = lineno;
        count++;
        lineno++;
    }
    return count;
}

// Find end of indented block starting at line[start+1]
static int aml_find_block_end(AML_Line* lines, int nlines, int start) {
    int base_indent = lines[start].indent;
    int i = start + 1;
    while (i < nlines && lines[i].indent > base_indent) i++;
    return i;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEVEL 2 EXECUTION — if/else, while, def, assignment, function calls
// ═══════════════════════════════════════════════════════════════════════════════

// Forward declarations
static int aml_exec_block(AML_ExecCtx* ctx, int start, int end);

// Register all function definitions (first pass)
static void aml_register_funcs(AML_ExecCtx* ctx) {
    for (int i = 0; i < ctx->nlines; i++) {
        char* text = ctx->lines[i].text;
        if (strncmp(text, "def ", 4) != 0) continue;

        // parse: def name(param1, param2):
        char* name_start = text + 4;
        while (*name_start == ' ') name_start++;
        char* paren = strchr(name_start, '(');
        if (!paren) continue;

        if (ctx->funcs.count >= AML_MAX_FUNCS) break;
        AML_Func* f = &ctx->funcs.funcs[ctx->funcs.count];

        int nlen = (int)(paren - name_start);
        if (nlen >= AML_MAX_NAME) nlen = AML_MAX_NAME - 1;
        memcpy(f->name, name_start, nlen);
        f->name[nlen] = 0;

        // parse params
        f->param_count = 0;
        char* pp = paren + 1;
        while (*pp && *pp != ')' && f->param_count < AML_MAX_PARAMS) {
            while (*pp == ' ' || *pp == ',') pp++;
            if (*pp == ')') break;
            char* pe = pp;
            while (*pe && *pe != ',' && *pe != ')' && *pe != ' ') pe++;
            int plen = (int)(pe - pp);
            if (plen >= AML_MAX_NAME) plen = AML_MAX_NAME - 1;
            memcpy(f->params[f->param_count], pp, plen);
            f->params[f->param_count][plen] = 0;
            f->param_count++;
            pp = pe;
        }

        f->body_start = i + 1;
        f->body_end = aml_find_block_end(ctx->lines, ctx->nlines, i);
        ctx->funcs.count++;

        // skip body
        i = f->body_end - 1;
    }
}

// Call a user-defined function
static int aml_call_func(AML_ExecCtx* ctx, AML_Func* f, float* args, int nargs) {
    if (ctx->call_depth >= AML_MAX_CALL_DEPTH) {
        set_error(ctx, "max call depth exceeded");
        return 1;
    }

    // push local scope
    ctx->call_depth++;
    AML_Symtab* locals = &ctx->locals[ctx->call_depth - 1];
    memset(locals, 0, sizeof(AML_Symtab));

    // bind params
    for (int i = 0; i < f->param_count && i < nargs; i++) {
        symtab_set(locals, f->params[i], args[i]);
    }

    // execute body
    int rc = aml_exec_block(ctx, f->body_start, f->body_end);

    // pop scope
    ctx->call_depth--;
    return rc;
}

// Execute a single line in Level 2 context
static int aml_exec_line(AML_ExecCtx* ctx, int idx) {
    char* text = ctx->lines[idx].text;

    // --- def: skip (already registered) ---
    if (strncmp(text, "def ", 4) == 0) {
        // skip body
        return aml_find_block_end(ctx->lines, ctx->nlines, idx);
    }

    // --- if/else ---
    if (strncmp(text, "if ", 3) == 0) {
        // strip trailing ':'
        char cond[AML_MAX_LINE_LEN];
        strncpy(cond, text + 3, AML_MAX_LINE_LEN - 1);
        cond[AML_MAX_LINE_LEN - 1] = 0;
        int clen = (int)strlen(cond);
        if (clen > 0 && cond[clen - 1] == ':') cond[clen - 1] = 0;

        float val = aml_eval(ctx, cond);
        int body_end = aml_find_block_end(ctx->lines, ctx->nlines, idx);

        // check for else
        int has_else = 0;
        int else_end = body_end;
        if (body_end < ctx->nlines) {
            char* next = ctx->lines[body_end].text;
            if (strcmp(next, "else:") == 0 || strncmp(next, "else:", 5) == 0) {
                has_else = 1;
                else_end = aml_find_block_end(ctx->lines, ctx->nlines, body_end);
            }
        }

        if (val != 0.0f) {
            aml_exec_block(ctx, idx + 1, body_end);
        } else if (has_else) {
            aml_exec_block(ctx, body_end + 1, else_end);
        }

        return has_else ? else_end : body_end;
    }

    // --- while ---
    if (strncmp(text, "while ", 6) == 0) {
        char cond[AML_MAX_LINE_LEN];
        strncpy(cond, text + 6, AML_MAX_LINE_LEN - 1);
        cond[AML_MAX_LINE_LEN - 1] = 0;
        int clen = (int)strlen(cond);
        if (clen > 0 && cond[clen - 1] == ':') cond[clen - 1] = 0;

        int body_end = aml_find_block_end(ctx->lines, ctx->nlines, idx);
        int iterations = 0;

        while (aml_eval(ctx, cond) != 0.0f && iterations < 10000) {
            aml_exec_block(ctx, idx + 1, body_end);
            iterations++;
        }
        return body_end;
    }

    // --- INCLUDE ---
    if (strncasecmp(text, "INCLUDE ", 8) == 0) {
        if (ctx->include_depth >= AML_MAX_INCLUDE) {
            set_error(ctx, "max include depth exceeded");
            return idx + 1;
        }
        char path[512];
        const char* fname = text + 8;
        while (*fname == ' ') fname++;

        if (fname[0] == '/') {
            strncpy(path, fname, 511);
        } else {
            snprintf(path, 512, "%s/%s", ctx->base_dir, fname);
        }
        path[511] = 0;

        ctx->include_depth++;
        am_exec_file(path);
        ctx->include_depth--;
        return idx + 1;
    }

    // --- assignment: name = expr ---
    {
        const char* eq = strchr(text, '=');
        if (eq && eq > text && eq[1] != '=' && eq[-1] != '!' &&
            eq[-1] != '<' && eq[-1] != '>') {
            // extract variable name
            char varname[AML_MAX_NAME] = {0};
            const char* p = text;
            int ni = 0;
            while (p < eq && ni < AML_MAX_NAME - 1) {
                if (!isspace((unsigned char)*p))
                    varname[ni++] = *p;
                p++;
            }
            varname[ni] = 0;

            if (ni > 0 && (isalpha((unsigned char)varname[0]) || varname[0] == '_')) {
                float val = aml_eval(ctx, eq + 1);
                if (ctx->call_depth > 0)
                    symtab_set(&ctx->locals[ctx->call_depth - 1], varname, val);
                else
                    symtab_set(&ctx->globals, varname, val);
                return idx + 1;
            }
        }
    }

    // --- function call: name(args) ---
    {
        char* paren = strchr(text, '(');
        if (paren && !strchr(text, '=')) {
            char fname[AML_MAX_NAME] = {0};
            int ni = 0;
            const char* p = text;
            while (p < paren && ni < AML_MAX_NAME - 1) {
                if (!isspace((unsigned char)*p))
                    fname[ni++] = *p;
                p++;
            }
            fname[ni] = 0;

            // find function
            for (int fi = 0; fi < ctx->funcs.count; fi++) {
                if (strcmp(ctx->funcs.funcs[fi].name, fname) == 0) {
                    // parse args
                    float args[AML_MAX_PARAMS];
                    int nargs = 0;
                    char argbuf[AML_MAX_LINE_LEN];
                    char* ap = paren + 1;
                    char* close = strchr(ap, ')');
                    if (close) {
                        int alen = (int)(close - ap);
                        memcpy(argbuf, ap, alen);
                        argbuf[alen] = 0;
                        // split by comma
                        char* save = NULL;
                        for (char* tok = strtok_r(argbuf, ",", &save);
                             tok && nargs < AML_MAX_PARAMS;
                             tok = strtok_r(NULL, ",", &save)) {
                            while (*tok == ' ') tok++;
                            args[nargs++] = aml_eval(ctx, tok);
                        }
                    }
                    aml_call_func(ctx, &ctx->funcs.funcs[fi], args, nargs);
                    return idx + 1;
                }
            }
        }
    }

    // --- Level 0 fallback: split CMD ARG, dispatch ---
    {
        char linebuf[AML_MAX_LINE_LEN];
        strncpy(linebuf, text, AML_MAX_LINE_LEN - 1);
        linebuf[AML_MAX_LINE_LEN - 1] = 0;

        char* sp = linebuf;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        char* cmd_end = sp;
        while (*sp && isspace((unsigned char)*sp)) sp++;
        char* arg = sp;
        *cmd_end = 0;
        upcase(linebuf);

        aml_exec_level0(linebuf, arg, ctx);
    }
    return idx + 1;
}

// Execute a block of lines [start, end)
static int aml_exec_block(AML_ExecCtx* ctx, int start, int end) {
    int i = start;
    while (i < end && i < ctx->nlines) {
        i = aml_exec_line(ctx, i);
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PUBLIC EXEC — AML Level 0 + Level 2
// ═══════════════════════════════════════════════════════════════════════════════

int am_exec(const char* script) {
    if (!script || !*script) return 0;
    g_error[0] = 0;

    // preprocess into lines
    AML_Line* lines = (AML_Line*)malloc(AML_MAX_LINES * sizeof(AML_Line));
    if (!lines) return 2;

    int nlines = aml_preprocess(script, lines, AML_MAX_LINES);
    if (nlines == 0) { free(lines); return 0; }

    // set up execution context
    AML_ExecCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.lines = lines;
    ctx.nlines = nlines;

    // first pass: register function definitions
    aml_register_funcs(&ctx);

    // second pass: execute top-level block
    aml_exec_block(&ctx, 0, nlines);

    free(lines);

    if (ctx.error[0]) {
        strncpy(g_error, ctx.error, 255);
        return 1;
    }
    return 0;
}

int am_exec_file(const char* path) {
    if (!path) return 1;
    g_error[0] = 0;

    FILE* f = fopen(path, "r");
    if (!f) {
        snprintf(g_error, 256, "cannot open: %s", path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        snprintf(g_error, 256, "bad size: %s (%ld)", path, sz);
        return 1;
    }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return 2; }

    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = 0;

    int rc = am_exec(buf);
    free(buf);
    return rc;
}

// ═══════════════════════════════════════════════════════════════════════════════
// STATE ACCESS — the exposed body
// ═══════════════════════════════════════════════════════════════════════════════

AM_State* am_get_state(void) {
  return &G;
}

int am_take_jump(void) {
  int j = G.pending_jump;
  G.pending_jump = 0;
  return j;
}

// ═══════════════════════════════════════════════════════════════════════════════
// WASM-SAFE STATE COPY — deterministic, ABI-stable interface
// writes 24 scalars in fixed order (extended from original 20)
// ═══════════════════════════════════════════════════════════════════════════════

int am_copy_state(float* out) {
  if (!out) return 1;

  // AMK core state (indices 0-12, original API compatible)
  out[0]  = (float)G.prophecy;
  out[1]  = G.destiny;
  out[2]  = G.wormhole;
  out[3]  = G.calendar_drift;
  out[4]  = G.attend_focus;
  out[5]  = G.attend_spread;
  out[6]  = G.tunnel_threshold;
  out[7]  = G.tunnel_chance;
  out[8]  = (float)G.tunnel_skip_max;
  out[9]  = (float)G.pending_jump;
  out[10] = G.pain;
  out[11] = G.tension;
  out[12] = G.dissonance;

  // Extended state (indices 13-19)
  out[13] = G.debt;
  out[14] = (float)G.velocity_mode;
  out[15] = G.effective_temp;
  out[16] = G.time_direction;
  out[17] = G.temporal_debt;
  out[18] = (float)G.packs_enabled;
  out[19] = (float)G.chordlock_on;  // sample pack state

  // Cosmic physics reference (index 20, actual state in schumann.c)
  out[20] = G.cosmic_coherence_ref;
  // Extended slots
  out[21] = (float)G.wormhole_active;
  // Slots 22-23 reserved for future use
  out[22] = 0.0f;
  out[23] = 0.0f;

  return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// STEP — advance field physics (call each frame)
// applies debt decay, temporal debt accumulation, etc.
// ═══════════════════════════════════════════════════════════════════════════════

void am_step(float dt) {
  // debt decay
  G.debt *= G.debt_decay;

  // clamp debt to prevent runaway
  if (G.debt > 100.0f) G.debt = 100.0f;

  // temporal debt: accumulates while moving backward, decays otherwise
  // the debt is proportional to time spent in backward movement
  if (G.velocity_mode == AM_VEL_BACKWARD && dt > 0.0f) {
    // accumulate debt proportional to time spent going backward
    // 0.01 per second of backward movement (dt is in seconds)
    G.temporal_debt += 0.01f * dt;
  } else {
    // decay when not moving backward (slower than regular debt)
    G.temporal_debt *= 0.9995f;
  }

  // clamp temporal debt
  if (G.temporal_debt > 10.0f) G.temporal_debt = 10.0f;

  // ─────────────────────────────────────────────────────────────────────────────
  // COSMIC COHERENCE MODULATION (reference from schumann.c)
  // High cosmic coherence → faster healing (tension/dissonance decay)
  // Actual Schumann state is managed by schumann.c; here we use the ref value
  // ─────────────────────────────────────────────────────────────────────────────
  if (G.cosmic_coherence_ref > 0.0f && dt > 0.0f) {
    // coherence_factor: 1.0 at max coherence, 0.5 at zero coherence
    float coherence_factor = 0.5f + 0.5f * G.cosmic_coherence_ref;

    // tension/dissonance decay faster with high coherence
    float heal_rate = 0.998f - (0.003f * coherence_factor);
    G.tension *= heal_rate;
    G.dissonance *= heal_rate;
  }
}
