// ariannamethod.h — AML: Arianna Method Language
// Reference implementation. THE KERNEL: movement IS language.
//
// Source of truth: github.com/ariannamethod/ariannamethod.ai
// Embed (copy) into your project. Do not link.
//
// ═══════════════════════════════════════════════════════════════════════════════
// AMK = prophecy physics, suffering, movement, tunneling
// PACKS = ritual overlays (CODES/RIC, DARKMATTER, NOTORCH)
// הרזוננס לא נשבר. המשך הדרך.
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef ARIANNAMETHOD_H
#define ARIANNAMETHOD_H

#include <stdlib.h>  // for rand(), RAND_MAX

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// PACK FLAGS
// ═══════════════════════════════════════════════════════════════════════════════

#define AM_PACK_CODES_RIC  0x01   // chordlock, tempolock, chirality
#define AM_PACK_DARKMATTER 0x02   // scars, gravity, antidotes
#define AM_PACK_NOTORCH    0x04   // microlearning commands

// ═══════════════════════════════════════════════════════════════════════════════
// VELOCITY MODES — movement IS language
// ═══════════════════════════════════════════════════════════════════════════════

#define AM_VEL_NOMOVE   0   // cold observer (temp = 0.5)
#define AM_VEL_WALK     1   // balanced (temp = 0.85)
#define AM_VEL_RUN      2   // high entropy chaos (temp = 1.2)
#define AM_VEL_BACKWARD (-1) // time rewind, debt forgiveness

// ═══════════════════════════════════════════════════════════════════════════════
// AM_State — the breath of the field
// ═══════════════════════════════════════════════════════════════════════════════

typedef struct {
  // PROPHECY PHYSICS
  int   prophecy;           // horizon: steps ahead (1..64)
  float destiny;            // bias toward most probable path (0..1)
  float wormhole;           // probability of spacetime skip (0..1)
  float calendar_drift;     // hebrew-gregorian drift (default 11.0)

  // ATTENTION PHYSICS
  float attend_focus;       // sharpness of attention (0..1)
  float attend_spread;      // blur/temperature (0..1)

  // TUNNELING
  float tunnel_threshold;   // dissonance gate (0..1)
  float tunnel_chance;      // activation probability (0..1)
  int   tunnel_skip_max;    // max compressed steps (1..24)

  // SUFFERING
  float pain;               // composite suffering (0..1)
  float tension;            // pressure buildup (0..1)
  float dissonance;         // symmetry-break (0..1)
  float debt;               // prophecy debt accumulator (0..∞, decays)

  // MOVEMENT
  int   pending_jump;       // queued jump (sim steps)
  int   velocity_mode;      // NOMOVE=0, WALK=1, RUN=2, BACKWARD=-1
  float velocity_magnitude; // current speed (0..1)
  float base_temperature;   // base temp before velocity modulation
  float effective_temp;     // computed: base + velocity influence
  float time_direction;     // -1 (rewind) to +1 (forward)
  float temporal_debt;      // accumulated from backward movement

  // LAWS OF NATURE
  float entropy_floor;      // minimum entropy (default 0.1)
  float resonance_ceiling;  // maximum resonance (default 0.95)
  float debt_decay;         // debt decay per step (default 0.998)
  float emergence_threshold;// unplanned pattern threshold (default 0.3)

  // PACK STATE
  unsigned int packs_enabled;  // bitmask of enabled packs

  // CODES/RIC pack state
  int   chordlock_on;
  int   tempolock_on;
  int   chirality_on;
  int   tempo;
  float pas_threshold;
  int   chirality_accum;

  // Dark matter pack state
  float dark_gravity;
  int   antidote_mode;

  // WORMHOLE STATE (queryable)
  int wormhole_active;          // 1 if wormhole fired this step, 0 otherwise

  // Cosmic physics coupling (from schumann.c)
  float cosmic_coherence_ref;

  // ═══ TEMPORAL SYMMETRY (from PITOMADOM) ═══
  int   temporal_mode;          // 0=prophecy, 1=retrodiction, 2=symmetric
  float temporal_alpha;         // 0=past focus, 1=future focus
  int   rtl_mode;               // Hebrew right-to-left encoding

  // ═══ EXPERT WEIGHTING ═══
  float expert_structural;      // grammar-focused (temp 0.7)
  float expert_semantic;        // meaning-focused (temp 0.9)
  float expert_creative;        // exploratory (temp 1.2)
  float expert_precise;         // conservative (temp 0.5)

  // ═══ EXTENDED LAWS ═══
  float presence_fade;          // token memory decay (default 0.95)
  float attractor_drift;        // attractor shift speed (default 0.01)
  float calendar_phase;         // 11-day conflict phase
  float wormhole_gate;          // spacetime jump activation threshold

  // ═══ RESONANCE MEMORY ═══
  float presence_decay;         // how quickly presence fades (default 0.9)
} AM_State;

// Temporal modes
#define AM_TEMPORAL_PROPHECY     0
#define AM_TEMPORAL_RETRODICTION 1
#define AM_TEMPORAL_SYMMETRIC    2

// ═══════════════════════════════════════════════════════════════════════════════
// API
// ═══════════════════════════════════════════════════════════════════════════════

// Initialize kernel
void am_init(void);

// Pack management
void am_enable_pack(unsigned int pack_mask);
void am_disable_pack(unsigned int pack_mask);
int am_pack_enabled(unsigned int pack_mask);

// Reset commands
void am_reset_field(void);
void am_reset_debt(void);

// Execute DSL script
int am_exec(const char* script);

// State access
AM_State* am_get_state(void);
int am_take_jump(void);

// Copy state to float array (24 floats)
int am_copy_state(float* out);

// Step physics (call each frame, dt in seconds)
void am_step(float dt);

// ═══════════════════════════════════════════════════════════════════════════════
// SCHUMANN API (from schumann.c)
// ═══════════════════════════════════════════════════════════════════════════════

void schumann_init(void);
void schumann_set_hz(float hz);
void schumann_set_modulation(float strength);
void schumann_step(float dt);
float schumann_get_hz(void);
float schumann_get_coherence(void);
float schumann_get_modulation(void);
float schumann_get_phase(void);
float schumann_modulate(float direction);
float schumann_harmonic_signal(void);
int schumann_copy_state(float* out);  // 8 floats

// ═══════════════════════════════════════════════════════════════════════════════
// CONVENIENCE: Apply AMK state to generation
// ═══════════════════════════════════════════════════════════════════════════════

// Get temperature modulated by velocity mode
static inline float am_get_temperature(void) {
    AM_State* s = am_get_state();
    return s->effective_temp;
}

// Get destiny bias for sampling (affects top-k selection)
static inline float am_get_destiny_bias(void) {
    AM_State* s = am_get_state();
    return s->destiny;
}

// Check if tunneling should occur (based on dissonance)
static inline int am_should_tunnel(void) {
    AM_State* s = am_get_state();
    if (s->dissonance < s->tunnel_threshold) return 0;
    // Simple probability check
    float r = (float)rand() / (float)RAND_MAX;
    return r < s->tunnel_chance;
}

// Check if wormhole fired this step
static inline int am_get_wormhole_active(void) {
    AM_State* s = am_get_state();
    return s->wormhole_active;
}

// Apply pain/tension to logits (suppress extremes)
static inline void am_apply_suffering_to_logits(float* logits, int n) {
    AM_State* s = am_get_state();
    if (s->pain > 0.1f || s->tension > 0.1f) {
        float dampen = 1.0f - (s->pain * 0.3f + s->tension * 0.2f);
        for (int i = 0; i < n; i++) {
            logits[i] *= dampen;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif // ARIANNAMETHOD_H
