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
#include <stdio.h>  // for sscanf in LAW command parsing

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
// EXEC — parse and execute DSL script
// returns 0 on success, nonzero on error
// ═══════════════════════════════════════════════════════════════════════════════

int am_exec(const char* script) {
  if (!script) return 0;  // empty script is OK

  size_t n = strlen(script);
  if (n == 0) return 0;   // empty string is OK

  // copy to mutable buffer
  char* buf = (char*)malloc(n + 1);
  if (!buf) return 2;
  memcpy(buf, script, n + 1);

  // line by line
  char* save = NULL;
  for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    char* t = trim(line);
    if (*t == 0) continue;       // empty line
    if (*t == '#') continue;     // comment

    // split: CMD ARG
    char* sp = t;
    while (*sp && !isspace((unsigned char)*sp)) sp++;
    char* cmd_end = sp;
    while (*sp && isspace((unsigned char)*sp)) sp++;
    char* arg = sp;

    *cmd_end = 0;
    upcase(t);

    // ─────────────────────────────────────────────────────────────────────────
    // AMK KERNEL COMMANDS — the bricks
    // ─────────────────────────────────────────────────────────────────────────

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

  free(buf);
  return 0;
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
