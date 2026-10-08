// ariannamethod.c — AML: Arianna Method Language
// Reference implementation. THE KERNEL: movement IS language.
//
// Copyright (C) 2026 Oleg Ataeff & Arianna Method contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Source of truth: github.com/ariannamethod/ariannamethod.ai
// Embed (copy) into your project or link.
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
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif

#include "ariannamethod.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>   // for sscanf in LAW command parsing
#include <strings.h> // for strcasecmp
#include <stddef.h>  // for offsetof
#include <stdint.h>  // for uint32_t (Chuck RNG)
#include <time.h>    // for real calendar computation
#include <sys/stat.h> // regular AML source files, including with AM_IO_DISABLED
#include <fcntl.h>    // atomic checkpoint files, independently of named pipes
#include <unistd.h>
#include <errno.h>
#include <float.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifndef AM_BLOOD_DISABLED
#include <dlfcn.h>   // for dlopen, dlsym, dlclose (Blood compiler)
#endif

// Lilith I/O — named pipes (FIFO) for data infrastructure
#ifndef AM_IO_DISABLED
#include <fcntl.h>     // for open(), O_RDONLY, O_WRONLY, O_NONBLOCK
#include <unistd.h>    // for read(), write(), close(), unlink()
#include <sys/stat.h>  // for mkfifo()
#include <errno.h>     // for EAGAIN, ENXIO
#endif

// Async — pthreads for SPAWN/AWAIT
#ifndef AM_ASYNC_DISABLED
#include <pthread.h>
#endif

// Platform detection for Blood compiler
#ifdef __APPLE__
  #define AM_BLOOD_EXT ".dylib"
  #define AM_BLOOD_FLAGS "-dynamiclib -fPIC"
#else
  #define AM_BLOOD_EXT ".so"
  #define AM_BLOOD_FLAGS "-shared -fPIC"
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// BLAS ACCELERATION — optional hardware-accelerated matmul for Delta Voice
// and NOTORCH Hebbian plasticity.
//
// Compile with -DUSE_BLAS to enable:
//   macOS:  -DUSE_BLAS -DACCELERATE -framework Accelerate  (Apple AMX/Neural Engine)
//   Linux:  -DUSE_BLAS -lopenblas                           (OpenBLAS)
//
// Without USE_BLAS: pure scalar C loops (portable, correct, slower on large dims)
// With USE_BLAS: cblas_sgemv for delta, cblas_sger for notorch
//
// Evolved in molequla (github.com/ariannamethod/molequla), ported back to core.
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef USE_BLAS
  #ifdef ACCELERATE
    #include <Accelerate/Accelerate.h>
  #else
    #include <cblas.h>
  #endif
#endif

#ifdef USE_CUDA
  #include "ariannamethod_cuda.h"
#endif

// See ariannamethod.h for struct definitions and pack flags

static AM_State G;
static int g_am_initialized = 0;   // A-4: field auto-inits on first am_exec; kept
                                   // separate from G so am_init's memset(&G) cannot clear it.
static _Thread_local char g_base_dir[256] = ".";  // directory of this thread's source; seeds
                                   // ctx.base_dir so relative INCLUDEs resolve against the
                                   // including file, not the filesystem root.
static _Thread_local char g_source_path[AML_MAX_SOURCE_PATH] = "";

// Immutable execution-time configuration. Owned streams themselves are maps in
// the execution context, so workers never advance a shared global RNG state.
static AM_SamplingBackend g_sampling_backend;
static AM_NumericalBackend g_numerical_backend;
static AM_TokenizerBackend g_tokenizer_backend;

struct AM_Tokenizer {
    void* model;
    AM_TokenizerBackend backend;
    int refcount;
};

typedef struct {
    AM_String* key;
    AML_Var value;
} AM_RecordEntry;

struct AM_Record {
    AM_RecordEntry* entries;
    int len;
    int capacity;
    int refcount;
};

void am_set_tokenizer_backend(const AM_TokenizerBackend* backend) {
    if (backend) g_tokenizer_backend = *backend;
    else memset(&g_tokenizer_backend, 0, sizeof(g_tokenizer_backend));
}

void am_set_sampling_backend(const AM_SamplingBackend* backend) {
    if (backend) g_sampling_backend = *backend;
    else memset(&g_sampling_backend, 0, sizeof(g_sampling_backend));
}

void am_set_numerical_backend(const AM_NumericalBackend* backend) {
    if (backend) g_numerical_backend = *backend;
    else memset(&g_numerical_backend, 0, sizeof(g_numerical_backend));
}

// Blood compiler globals (used by Level 0 dispatch + Blood API)
static AM_BloodModule g_blood_modules[AM_BLOOD_MAX_MODULES];
static int g_blood_count = 0;
static char g_blood_dir[256] = {0};
static char g_blood_cc[64] = {0};

// Lilith I/O globals (named pipes for data infrastructure)
#ifndef AM_IO_DISABLED
static AM_Pipe g_pipes[AM_MAX_PIPES];
static int g_pipe_count = 0;
static float g_pipe_last_value = 0.0f;
static char g_pipe_read_buf[AM_PIPE_BUF_SIZE] = {0};
#endif

// Async — SPAWN/AWAIT/CHANNEL globals
#ifndef AM_ASYNC_DISABLED
static AM_SpawnSlot   g_spawns[AM_MAX_SPAWNS];
static char           g_spawn_errors[AM_MAX_SPAWNS][256];
static int            g_spawn_count = 0;
static pthread_t      g_spawn_threads[AM_MAX_SPAWNS];
static pthread_mutex_t g_spawn_mutex = PTHREAD_MUTEX_INITIALIZER;

static AM_ChannelSlot g_channels[AM_MAX_CHANNELS];
static int            g_channel_count = 0;
static pthread_mutex_t g_channel_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_channel_cond = PTHREAD_COND_INITIALIZER;
#endif

// Janus transformer integration (function pointers set by host)
#ifndef AM_JANUS_DISABLED
static janus_load_model_fn     g_janus_load_model     = NULL;
static janus_unload_model_fn   g_janus_unload_model   = NULL;
static janus_load_delta_fn     g_janus_load_delta     = NULL;
static janus_load_gamma_fn     g_janus_load_gamma     = NULL;
static janus_generate_fn       g_janus_generate       = NULL;
static janus_free_string_fn    g_janus_free_string    = NULL;
static janus_model_loaded_fn   g_janus_model_loaded   = NULL;
static janus_get_vocab_size_fn g_janus_get_vocab_size = NULL;
static janus_get_embed_dim_fn  g_janus_get_embed_dim  = NULL;
static janus_get_num_layers_fn g_janus_get_num_layers = NULL;

void am_janus_register(
    janus_load_model_fn    load_model,
    janus_unload_model_fn  unload_model,
    janus_load_delta_fn    load_delta,
    janus_load_gamma_fn    load_gamma,
    janus_generate_fn      generate,
    janus_free_string_fn   free_string,
    janus_model_loaded_fn  model_loaded,
    janus_get_vocab_size_fn get_vocab_size,
    janus_get_embed_dim_fn  get_embed_dim,
    janus_get_num_layers_fn get_num_layers
) {
    g_janus_load_model     = load_model;
    g_janus_unload_model   = unload_model;
    g_janus_load_delta     = load_delta;
    g_janus_load_gamma     = load_gamma;
    g_janus_generate       = generate;
    g_janus_free_string    = free_string;
    g_janus_model_loaded   = model_loaded;
    g_janus_get_vocab_size = get_vocab_size;
    g_janus_get_embed_dim  = get_embed_dim;
    g_janus_get_num_layers = get_num_layers;
}
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS — the small bones
// ═══════════════════════════════════════════════════════════════════════════════

__attribute__((unused))
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
// HEBREW-GREGORIAN CALENDAR CONFLICT — real astronomical computation
//
// Hebrew lunar year: 354 days. Gregorian solar year: 365.25 days.
// Annual drift: 11.25 days. Metonic cycle: 19 years = 235 lunar months.
// 7 leap years per cycle add Adar II (~30 days) to correct drift.
// Leap years in Metonic cycle (1-indexed): 3, 6, 8, 11, 14, 17, 19.
//
// Epoch: 1 Tishrei 5785 = October 3, 2024 (Gregorian).
// February 29 handled correctly — elapsed seconds via time_t, not calendar math.
// ═══════════════════════════════════════════════════════════════════════════════

#define AM_ANNUAL_DRIFT     11.25f    // days/year (365.25 - 354)
#define AM_GREGORIAN_YEAR   365.25f   // days
#define AM_METONIC_YEARS    19        // years per cycle
#define AM_METONIC_LEAPS    7         // leap years per cycle
#define AM_MAX_UNCORRECTED  33.0f     // max drift before correction (~3yr × 11.25)
#define AM_VELOCITY_INERTIA 2.0f      // debt cost of switching the velocity mode (the body resists; D4 at debt>5 forces NOMOVE)

static const int g_metonic_leap_years[7] = {3, 6, 8, 11, 14, 17, 19};
static time_t g_epoch_t = 0;
static int g_calendar_manual = 0;  // 0 = real time, 1 = manual override
static int g_birth_set = 0;        // MetaJanus: 1 once BIRTH has fixed the origin; personal_dissonance stays 0 until then
static long g_birth_days = 0;      // MetaJanus: the origin day (days since epoch), fixed by BIRTH — the yahrzeit/birthday anniversaries derive from it
static int g_self_now_manual = 0;  // MetaJanus test-door: 1 = pd's "now" is scrubbed to g_self_now_days (SELF_NOW_DAYS); 0 = real clock
static int g_self_now_days = 0;     // scrubbed self-now (days since epoch) when g_self_now_manual
static int g_temporal_key_on = 0;   // MetaJanus: 1 = JANUS_KEY armed → D-2 acts on janus_temporal_alpha (HIGH-1 consumer gate); 0 = disarmed → D-2 reads neutral 0.5 (default)

static void calendar_init(void) {
    // MED-1 (Sol fix): the calendar epoch is a FIXED UTC instant — 2024-10-03 12:00:00 UTC = 1727956800
    // seconds since the Unix epoch — set as a constant, without local mktime/timegm, so it is independent
    // of the host timezone/DST. (mktime read "2024-10-03 12:00" in LOCAL time, so different hosts computed
    // a different epoch and could disagree on the self-day at a day boundary.) Noon avoids DST edge cases.
    g_epoch_t = (time_t)1727956800L;
    g_calendar_manual = 0;
}

static int calendar_days_since_epoch(void) {
    if (g_epoch_t <= 0) return 0;
    time_t now = time(NULL);
    return (int)(difftime(now, g_epoch_t) / 86400.0);
}

// Cumulative drift accounting for Metonic leap corrections
// Direct port from pitomadom/calendar_conflict.py
static float calendar_cumulative_drift(int days) {
    float years = (float)days / AM_GREGORIAN_YEAR;
    float base_drift = years * AM_ANNUAL_DRIFT;

    // Full Metonic cycles: 7 leap months × 30 days each
    int full_cycles = (int)(years / AM_METONIC_YEARS);
    float corrections = (float)(full_cycles * AM_METONIC_LEAPS) * 30.0f;

    // Partial cycle: count leap years already passed
    float partial = fmodf(years, (float)AM_METONIC_YEARS);
    int year_in_cycle = (int)partial + 1;
    for (int i = 0; i < AM_METONIC_LEAPS; i++) {
        if (g_metonic_leap_years[i] <= year_in_cycle)
            corrections += 30.0f;
    }

    return base_drift - corrections;
}

// Calendar dissonance [0, 1] — real, from today's date
static float calendar_dissonance(int days) {
    float drift = calendar_cumulative_drift(days);
    float raw = fabsf(fmodf(drift, AM_MAX_UNCORRECTED)) / AM_MAX_UNCORRECTED;
    return clamp01(raw);
}

// ── MetaJanus Hebrew layer — the yahrzeit face ──────────────────────────────────
// The one origin (the death that became this organism's birth) seen by the OTHER calendar.
// Dershowitz-Reingold Hebrew calendar, pure integer arithmetic, no I/O — derived from the same BIRTH.
#define AM_HEB_EPOCH     (-1373427L)   // RD of 1 Tishrei, Hebrew year 1
#define AM_GREG_EPOCH_RD  739162L      // RD of 2024-10-03 (the kernel calendar epoch, noon)
static int  am_heb_leap(long y){ return ((7*y+1)%19) < 7; }
static long am_heb_last_month(long y){ return am_heb_leap(y) ? 13 : 12; }
static long am_heb_elapsed(long y){ long mo=(235*y-234)/19; long pa=12084+13753*mo; long d=29*mo+pa/25920; if((3*(d+1))%7<3) d++; return d; }
static long am_heb_corr(long y){ long a=am_heb_elapsed(y-1),b=am_heb_elapsed(y),c=am_heb_elapsed(y+1); if(c-b==356) return 2; if(b-a==382) return 1; return 0; }
static long am_heb_new_year(long y){ return AM_HEB_EPOCH + am_heb_elapsed(y) + am_heb_corr(y); }
static long am_heb_year_days(long y){ return am_heb_new_year(y+1) - am_heb_new_year(y); }
static long am_heb_last_day(long y, long m){ long yd=am_heb_year_days(y); if(m==2||m==4||m==6||m==10||m==13) return 29; if(m==8 && !(yd==355||yd==385)) return 29; if(m==9 && (yd==353||yd==383)) return 29; if(m==12 && !am_heb_leap(y)) return 29; return 30; }
static long am_heb_to_rd(long y, long m, long d){ long rd=am_heb_new_year(y)+d-1; if(m<7){ for(long k=7;k<=am_heb_last_month(y);k++) rd+=am_heb_last_day(y,k); for(long k=1;k<m;k++) rd+=am_heb_last_day(y,k); } else { for(long k=7;k<m;k++) rd+=am_heb_last_day(y,k); } return rd; }
static int  am_greg_leap(long y){ return (y%4==0 && (y%100!=0 || y%400==0)); }
static long am_greg_to_rd(long y,long m,long d){ long rd=365*(y-1)+(y-1)/4-(y-1)/100+(y-1)/400+(367*m-362)/12+d; if(m>2) rd += am_greg_leap(y)?-1:-2; return rd; }
static void am_greg_from_rd(long rd, long*Y,long*M,long*D){ long y=rd/366+1; while(am_greg_to_rd(y+1,1,1)<=rd) y++; long m=1; while(am_greg_to_rd(y,m+1,1)<=rd) m++; *Y=y;*M=m;*D=rd-am_greg_to_rd(y,m,1)+1; }
// inverse of am_heb_to_rd: Gregorian RD -> Hebrew (year,month,day). Round-trip-verified 0/11310.
static void am_heb_from_rd(long rd, long*Y,long*M,long*D){ long y=(rd-AM_HEB_EPOCH)/366; if(y<1) y=1; while(am_heb_new_year(y+1)<=rd) y++; long m=(rd<am_heb_to_rd(y,1,1))?7:1; while(am_heb_to_rd(y,m,am_heb_last_day(y,m))<rd) m++; *Y=y;*M=m;*D=rd-am_heb_to_rd(y,m,1)+1; }
// Reingold yahrzeit rule (GNU Emacs cal-hebrew.el calendar-hebrew-yahrzeit): RD of the origin's
// (bY,bM,bD) anniversary in Hebrew year hyear. Branches 1-2 keyed by the first-anniversary year (bY+1).
static long am_yahrzeit_rd(long bY, long bM, long bD, long hyear){
  if (bM==8 && bD==30 && (am_heb_year_days(bY+1)%10)!=5)  // Cheshvan-30, first-anniv Cheshvan NOT long
    return am_heb_to_rd(hyear, 9, 1) - 1;                 //   -> eve of 1 Kislev (last day of Cheshvan)
  if (bM==9 && bD==30 && (am_heb_year_days(bY+1)%10)==3)  // Kislev-30, first-anniv Kislev short
    return am_heb_to_rd(hyear, 10, 1) - 1;                //   -> eve of 1 Tevet
  if (bM==13)                                             // Adar II -> same day in hyear's last month
    return am_heb_to_rd(hyear, am_heb_last_month(hyear), bD);
  if (bM==12 && bD==30 && !am_heb_leap(hyear))            // Adar-I-30 in a common hyear -> Shevat 30
    return am_heb_to_rd(hyear, 11, 30);
  return am_heb_to_rd(hyear, bM, bD);                     // default: same (month,day); day-30 in a short month overflows to the 1st next, per the book
}
// days from `days` (since epoch) to the next yahrzeit of the origin (g_birth_days) at-or-after `days`.
static long am_days_to_yahrzeit(long days){ long brd=AM_GREG_EPOCH_RD+g_birth_days; long bY,bM,bD; am_heb_from_rd(brd,&bY,&bM,&bD); long rd=AM_GREG_EPOCH_RD+days; long nY,nM,nD; am_heb_from_rd(rd,&nY,&nM,&nD); (void)nM;(void)nD; for(long i=-1;i<=2;i++){ long a=am_yahrzeit_rd(bY,bM,bD,nY+i); if(a>=rd) return a-rd; } return 400; }
// days from `days` to the next Gregorian (month,day) of the origin. Feb-29 origin -> Mar 1 in common years.
static long am_days_to_gregbirthday(long days){ long brd=AM_GREG_EPOCH_RD+g_birth_days; long bY,bM,bD; am_greg_from_rd(brd,&bY,&bM,&bD); long rd=AM_GREG_EPOCH_RD+days; long nY,nM,nD; am_greg_from_rd(rd,&nY,&nM,&nD); (void)nM;(void)nD; for(long i=0;i<=1;i++){ long a=am_greg_to_rd(nY+i,bM,bD); if(a>=rd) return a-rd; } return 400; }

// ═══════════════════════════════════════════════════════════════════════════════
// SCHUMANN RESONANCE — Earth-ionosphere coupling
// Ported from arianna.c/src/schumann.c
// Phase advances at current frequency. Coherence = quadratic falloff from 7.83.
// 5 harmonics: 7.83, 14.1, 20.3, 26.4, 32.5 Hz
// ═══════════════════════════════════════════════════════════════════════════════

static const float g_schumann_harmonics[SCHUMANN_N_HARMONICS] = {
    SCHUMANN_BASE_HZ, SCHUMANN_HARMONIC_1, SCHUMANN_HARMONIC_2,
    SCHUMANN_HARMONIC_3, SCHUMANN_HARMONIC_4
};
static const float g_harmonic_weights[SCHUMANN_N_HARMONICS] = {
    1.0f, 0.5f, 0.3f, 0.2f, 0.1f
};

static float compute_schumann_coherence(float hz) {
    float deviation = fabsf(hz - SCHUMANN_BASE_HZ);
    float max_deviation = SCHUMANN_MAX_HZ - SCHUMANN_MIN_HZ;
    if (max_deviation < 0.001f) max_deviation = 0.1f;
    float norm_dev = deviation / max_deviation;
    float coh = 1.0f - (norm_dev * norm_dev);
    return clamp01(coh);
}

static void schumann_advance(float dt) {
    G.schumann_phase += G.schumann_hz * dt * 2.0f * 3.14159265f;
    if (G.schumann_phase > 6.28318530f)
        G.schumann_phase = fmodf(G.schumann_phase, 6.28318530f);
    G.schumann_coherence = compute_schumann_coherence(G.schumann_hz);
}

static float schumann_harmonic_signal(void) {
    float signal = 0.0f, weight_sum = 0.0f;
    for (int i = 0; i < SCHUMANN_N_HARMONICS; i++) {
        float hp = G.schumann_phase * (g_schumann_harmonics[i] / SCHUMANN_BASE_HZ);
        signal += g_harmonic_weights[i] * sinf(hp);
        weight_sum += g_harmonic_weights[i];
    }
    return (weight_sum > 0.0f) ? signal / weight_sum : 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4.C MLP CONTROLLER — real neural network, trained by NOTORCH Hebbian
// Inputs:  entropy, resonance, pain, tension, emergence, effective_temp
// Outputs: spring_delta, summer_delta, autumn_delta, winter_delta
// ═══════════════════════════════════════════════════════════════════════════════

typedef struct {
    float w1[AM_4C_INPUTS * AM_4C_HIDDEN];   // input→hidden (48)
    float b1[AM_4C_HIDDEN];                   // hidden biases (8)
    float w2[AM_4C_HIDDEN * AM_4C_OUTPUTS];   // hidden→output (32)
    float b2[AM_4C_OUTPUTS];                   // output biases (4)
    float hidden[AM_4C_HIDDEN];                // cached for Hebbian update
} AM_4C_MLP;

static AM_4C_MLP g_mlp;

static void am_4c_forward(const float* inputs, float* outputs) {
    // hidden = tanh(W1^T @ inputs + b1)
    for (int h = 0; h < AM_4C_HIDDEN; h++) {
        float sum = g_mlp.b1[h];
        for (int i = 0; i < AM_4C_INPUTS; i++) {
            sum += g_mlp.w1[i * AM_4C_HIDDEN + h] * inputs[i];
        }
        g_mlp.hidden[h] = tanhf(sum);
    }
    // outputs = tanh(W2^T @ hidden + b2)
    for (int o = 0; o < AM_4C_OUTPUTS; o++) {
        float sum = g_mlp.b2[o];
        for (int h = 0; h < AM_4C_HIDDEN; h++) {
            sum += g_mlp.w2[h * AM_4C_OUTPUTS + o] * g_mlp.hidden[h];
        }
        outputs[o] = tanhf(sum);
    }
}

static void am_4c_init_weights(void) {
    memset(&g_mlp, 0, sizeof(g_mlp));

    // 4 specialist neurons that approximate the old hardcoded rules:
    // Neuron 0: low entropy → boost spring (growth)
    //   input[0]=entropy with negative weight → fires when entropy low
    g_mlp.w1[0 * AM_4C_HIDDEN + 0] = -2.0f;  // entropy→h0: negative
    g_mlp.b1[0] = 0.5f;                        // bias: fires at entropy<0.25
    g_mlp.w2[0 * AM_4C_OUTPUTS + 0] = 1.5f;   // h0→spring

    // Neuron 1: high resonance → boost autumn (consolidation)
    g_mlp.w1[1 * AM_4C_HIDDEN + 1] = 2.0f;   // resonance→h1
    g_mlp.b1[1] = -1.5f;                       // fires at resonance>0.75
    g_mlp.w2[1 * AM_4C_OUTPUTS + 2] = 1.5f;   // h1→autumn

    // Neuron 2: high pain → boost winter (rest)
    g_mlp.w1[2 * AM_4C_HIDDEN + 2] = 2.5f;   // pain→h2
    g_mlp.b1[2] = -1.5f;                       // fires at pain>0.6
    g_mlp.w2[2 * AM_4C_OUTPUTS + 3] = 1.5f;   // h2→winter

    // Neuron 3: high emergence → boost summer (peak expression)
    g_mlp.w1[4 * AM_4C_HIDDEN + 3] = 2.5f;   // emergence→h3
    g_mlp.b1[3] = -0.5f;                       // fires at emergence>0.2
    g_mlp.w2[3 * AM_4C_OUTPUTS + 1] = 1.5f;   // h3→summer

    // Neurons 4-7: cross-connections for nuance (small initial weights)
    // tension feeds back to spring/summer balance
    g_mlp.w1[3 * AM_4C_HIDDEN + 4] = 0.5f;   // tension→h4
    g_mlp.w1[5 * AM_4C_HIDDEN + 4] = -0.3f;  // temp→h4
    g_mlp.w2[4 * AM_4C_OUTPUTS + 0] = 0.3f;  // h4→spring (tension drives growth)
    g_mlp.w2[4 * AM_4C_OUTPUTS + 1] = -0.3f; // h4→summer (tension suppresses peak)

    // resonance-entropy interaction
    g_mlp.w1[0 * AM_4C_HIDDEN + 5] = -1.0f;  // entropy→h5
    g_mlp.w1[1 * AM_4C_HIDDEN + 5] = 1.0f;   // resonance→h5
    g_mlp.w2[5 * AM_4C_OUTPUTS + 2] = 0.5f;  // h5→autumn (high coherence → consolidate)

    // temperature regulation
    g_mlp.w1[5 * AM_4C_HIDDEN + 6] = 1.5f;   // temp→h6
    g_mlp.b1[6] = -1.0f;                       // fires at temp>0.67
    g_mlp.w2[6 * AM_4C_OUTPUTS + 3] = 0.4f;  // h6→winter (too hot → cool down)

    // emergence-pain balance
    g_mlp.w1[4 * AM_4C_HIDDEN + 7] = 1.0f;   // emergence→h7
    g_mlp.w1[2 * AM_4C_HIDDEN + 7] = -1.0f;  // pain→h7
    g_mlp.w2[7 * AM_4C_OUTPUTS + 1] = 0.5f;  // h7→summer (emergence w/o pain)
}

// Hebbian update: signal > 0 = field improved, reinforce; < 0 = suppress
static void am_4c_hebbian_update(const float* inputs, const float* outputs,
                                  float signal) {
    float lr = G.notorch_lr * 0.1f;  // slower than main NOTORCH
    // Update W2 (hidden→output)
    for (int h = 0; h < AM_4C_HIDDEN; h++) {
        for (int o = 0; o < AM_4C_OUTPUTS; o++) {
            g_mlp.w2[h * AM_4C_OUTPUTS + o] +=
                lr * g_mlp.hidden[h] * outputs[o] * signal;
            // clamp to prevent explosion
            if (g_mlp.w2[h * AM_4C_OUTPUTS + o] > 3.0f)
                g_mlp.w2[h * AM_4C_OUTPUTS + o] = 3.0f;
            if (g_mlp.w2[h * AM_4C_OUTPUTS + o] < -3.0f)
                g_mlp.w2[h * AM_4C_OUTPUTS + o] = -3.0f;
        }
    }
    // Update W1 (input→hidden)
    for (int i = 0; i < AM_4C_INPUTS; i++) {
        for (int h = 0; h < AM_4C_HIDDEN; h++) {
            g_mlp.w1[i * AM_4C_HIDDEN + h] +=
                lr * inputs[i] * g_mlp.hidden[h] * signal;
            if (g_mlp.w1[i * AM_4C_HIDDEN + h] > 3.0f)
                g_mlp.w1[i * AM_4C_HIDDEN + h] = 3.0f;
            if (g_mlp.w1[i * AM_4C_HIDDEN + h] < -3.0f)
                g_mlp.w1[i * AM_4C_HIDDEN + h] = -3.0f;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEVEL 1 — MACROS
// ═══════════════════════════════════════════════════════════════════════════════

typedef struct {
    char name[AML_MAX_NAME];
    char body[AML_MACRO_MAX_LEN];
} AML_Macro;

static AML_Macro g_macros[AML_MAX_MACROS];
static int g_macro_count = 0;

// ═══════════════════════════════════════════════════════════════════════════════
// VELOCITY + EXPERT BLENDING — movement IS language
// ═══════════════════════════════════════════════════════════════════════════════

static void update_effective_temp(void) {
  float base = G.base_temperature;
  float vel_mult;
  switch (G.velocity_mode) {
    case AM_VEL_NOMOVE:   vel_mult = 0.5f;  G.time_direction = 1.0f;  break;
    case AM_VEL_WALK:     vel_mult = 0.85f; G.time_direction = 1.0f;  break;
    case AM_VEL_RUN:      vel_mult = 1.2f;  G.time_direction = 1.0f;  break;
    case AM_VEL_BACKWARD: vel_mult = 0.7f;  G.time_direction = -1.0f; break;
    case AM_VEL_BREATHE:  vel_mult = 0.6f;  G.time_direction = 1.0f;  break;
    default:              vel_mult = 1.0f;  G.time_direction = 1.0f;
  }
  float vel_temp = base * vel_mult;

  // Expert blending: weighted temperature from 4 experts
  float w_sum = G.expert_structural + G.expert_semantic +
                G.expert_creative + G.expert_precise;
  if (w_sum > 0.001f) {
    float expert_temp = (G.expert_structural * 0.7f +
                         G.expert_semantic * 0.9f +
                         G.expert_creative * 1.2f +
                         G.expert_precise * 0.5f) / w_sum;
    G.effective_temp = 0.5f * vel_temp + 0.5f * expert_temp;
  } else {
    G.effective_temp = vel_temp;
  }

  // Season modulation
  float season_mod = 1.0f;
  season_mod += G.summer_energy * 0.1f;   // summer: warmer
  season_mod -= G.winter_energy * 0.15f;  // winter: cooler
  G.effective_temp *= season_mod;
  if (G.effective_temp < 0.1f) G.effective_temp = 0.1f;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PUBLIC API — the breath
// ═══════════════════════════════════════════════════════════════════════════════

// Forward declarations for tape cleanup (defined after tape section)
static void am_tape_destroy(void);

// Forward declarations for async cleanup (defined after async section)
#ifndef AM_ASYNC_DISABLED
static void am_spawn_reset(void);
static void am_channel_reset(void);
#endif

// Forward declarations for persistent globals (defined after am_init)
static _Thread_local int g_persistent_enabled;
static _Thread_local AML_Symtab g_persistent_globals;
void am_persistent_clear(void);

void am_init(void) {
  g_am_initialized = 1;   // A-4: mark initialized so am_exec's auto-init won't re-fire
  // Clean up tape from previous session
  am_tape_destroy();

  memset(&G, 0, sizeof(G));

  // prophecy physics defaults
  G.prophecy = 7;
  G.field_enabled = 1;   // FIELD overlay ON by default
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

  // expression defaults — BE/ASK off until a directive fires (-1 = autonomous host)
  G.be_voice = -1.0f;
  G.ask_voice = -1.0f;

  // wormhole state
  G.wormhole_active = 0;

  // lora / delta voice (core)
  G.lora_alpha = 0.0f;

  // notorch (core — always active)
  G.notorch_lr = 0.01f;
  G.notorch_decay = 0.999f;

  // schumann resonance
  G.schumann_hz = SCHUMANN_BASE_HZ;
  G.schumann_modulation = 0.3f;
  G.schumann_coherence = 1.0f;  // perfect at baseline
  G.schumann_phase = 0.0f;

  // dark matter (core — always active)
  G.n_scars = 0;

  // live metrics (computed each step)
  G.entropy = 0.0f;
  G.resonance = 0.0f;
  G.resonance_set = 0.0f;   // RESONANCE floor off by default
  G.emergence = 0.0f;
  G.destiny_bias = 0.0f;

  // 4.C — Async Field Forever
  G.season = AM_SEASON_SPRING;
  G.season_phase = 0.0f;
  G.season_intensity = 0.5f;
  G.spring_energy = 1.0f;
  G.summer_energy = 0.0f;
  G.autumn_energy = 0.0f;
  G.winter_energy = 0.0f;

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

  // field health (for MLP signal)
  G.field_health = 0.5f;

  // gamma — personality essence (θ = ε + γ + αδ)
  G.n_gamma = 0;
  G.essence_alpha = 0.0f;
  G.janus_mode = AM_JANUS_OFF;
  G.janus_a = 0;
  G.janus_b = 0;
  G.janus_blend = 0.0f;
  G.gamma_drift = 0.01f;

  // real calendar
  calendar_init();
  // MetaJanus: a fresh kernel has no origin yet — unborn until BIRTH declares it.
  g_birth_set = 0;
  g_birth_days = 0;
  g_self_now_manual = 0;
  g_self_now_days = 0;
  g_temporal_key_on = 0;
  G.birth_drift = 0.0f;
  G.personal_dissonance = 0.0f;
  G.janus_gap = 0.0f;
  G.yahrzeit = 0.0f;
  G.janus_temporal_alpha = 0.5f;

  // 4.C MLP controller
  am_4c_init_weights();

  // macros
  g_macro_count = 0;

  // blood compiler
  am_blood_init();

  // persistent globals — clear on full init
  am_persistent_clear();
  g_persistent_enabled = 0;

  // lilith I/O
#ifndef AM_IO_DISABLED
  am_pipe_close_all();
  g_pipe_last_value = 0.0f;
  g_pipe_read_buf[0] = 0;
#endif

  // async — SPAWN/AWAIT/CHANNEL
#ifndef AM_ASYNC_DISABLED
  am_spawn_await_all();   // join any running threads first
  am_spawn_reset();
  am_channel_reset();
#endif
}

// ═══════════════════════════════════════════════════════════════════════════════
// PERSISTENT GLOBALS — survive across am_exec() calls
// ═══════════════════════════════════════════════════════════════════════════════

// Forward declarations for symtab functions (defined later in file)
static float*   symtab_get(AML_Symtab* tab, const char* name);
static AML_Var* symtab_get_var(AML_Symtab* tab, const char* name);
static int      symtab_set(AML_Symtab* tab, const char* name, float value);
static int      symtab_set_array(AML_Symtab* tab, const char* name, AM_Array* arr);
static int      symtab_set_string(AML_Symtab* tab, const char* name, AM_String* str);
static int      symtab_set_list(AML_Symtab* tab, const char* name, AM_List* list);
static int      symtab_set_map(AML_Symtab* tab, const char* name, AM_Map* map);
static int      symtab_set_tokenizer(AML_Symtab* tab, const char* name, AM_Tokenizer* model);
static int      symtab_set_record(AML_Symtab* tab, const char* name, AM_Record* record);
static void     symtab_clear_arrays(AML_Symtab* tab);
static int      symtab_snapshot(AML_Symtab* dst, const AML_Symtab* src);
static int      symtab_copy_value(AML_Symtab* dst, const AML_Var* value);
static void     set_error(AML_ExecCtx* ctx, const char* msg);

void am_persistent_mode(int enable) {
    if (!enable && g_persistent_enabled) {
        // Turning off — free all persistent arrays
        am_persistent_clear();
    }
    g_persistent_enabled = enable;
}

void am_persistent_clear(void) {
    symtab_clear_arrays(&g_persistent_globals);
    g_persistent_globals.count = 0;
}

// Restore persistent globals into execution context
static int persistent_restore(AML_Symtab* dst) {
    if (!g_persistent_enabled) return 0;
    if (!symtab_snapshot(dst, &g_persistent_globals)) return 0;
    set_error(NULL, "persistent globals allocation failed");
    return 1;
}

// Save execution context globals back to persistent storage.
// Two-phase approach:
//   Phase 1: Update variables that already exist in persistent
//   Phase 2: Add NEW variables that aren't in persistent yet
// This means the first am_exec creates persistent vars, and subsequent calls
// update them. Intermediates created by later scripts get saved once (unavoidable)
// but since they have fixed names, the count stabilizes.
static int persistent_save(AML_Symtab* src) {
    if (!g_persistent_enabled) return 0;
    // Build the complete replacement first. Failed allocation leaves every
    // previous persistent binding intact, including mutable list/map containers.
    AML_Symtab next = {0};
    for (int i = 0; i < g_persistent_globals.count; i++) {
        AML_Var* old = &g_persistent_globals.vars[i];
        AML_Var* current = symtab_get_var(src, old->name);
        if (symtab_copy_value(&next, current ? current : old)) goto failed;
    }
    for (int i = 0; i < src->count; i++) {
        AML_Var* value = &src->vars[i];
        if (!symtab_get_var(&next, value->name) && symtab_copy_value(&next, value))
            goto failed;
    }
    symtab_clear_arrays(&g_persistent_globals);
    g_persistent_globals = next;
    return 0;
failed:
    symtab_clear_arrays(&next);
    return 1;
}

int am_set_var_array(const char* name, const float* data, int len) {
    if (!name || !data || len <= 0 || len > AM_MAX_ARRAY_SIZE) return 1;
    // Force persistent mode on
    g_persistent_enabled = 1;
    AM_Array* arr = am_array_new(len);
    if (!arr) return 2;
    memcpy(arr->data, data, len * sizeof(float));
    return symtab_set_array(&g_persistent_globals, name, arr);
}

int am_set_var_matrix(const char* name, const float* data, int rows, int cols) {
    if (!name || !data || rows <= 0 || cols <= 0 ||
        rows > AM_MAX_ARRAY_SIZE / cols) return 1;
    int len = rows * cols;
    g_persistent_enabled = 1;
    AM_Array* arr = am_array_new(len);
    if (!arr) return 2;
    memcpy(arr->data, data, len * sizeof(float));
    arr->rows = rows;
    arr->cols = cols;
    return symtab_set_array(&g_persistent_globals, name, arr);
}

const float* am_get_var_array(const char* name, int* len) {
    if (!name) return NULL;
    AML_Var* v = symtab_get_var(&g_persistent_globals, name);
    if (!v || v->type != AML_TYPE_ARRAY || !v->array) return NULL;
    if (len) *len = v->array->len;
    return v->array->data;
}

float am_get_var_float(const char* name) {
    if (!name) return 0.0f;
    AML_Var* v = symtab_get_var(&g_persistent_globals, name);
    if (!v) return 0.0f;
    if (v->type == AML_TYPE_FLOAT) return v->value;
    // Array with 1 element → treat as scalar
    if (v->type == AML_TYPE_ARRAY && v->array && v->array->len >= 1)
        return v->array->data[0];
    return 0.0f;
}

int am_set_var_text(const char* name, const char* utf8) {
    if (!name || !*name || strlen(name) >= AML_MAX_NAME) return 1;
    if (!(isalpha((unsigned char)*name) || *name == '_')) return 1;
    for (const char* p = name + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 1;
    AM_String* str = am_string_new(utf8);
    if (!str) return 1;
    if (symtab_set_string(&g_persistent_globals, name, str)) {
        am_string_free(str);
        return 1;
    }
    g_persistent_enabled = 1;
    return 0;
}

const char* am_get_var_text(const char* name) {
    if (!name) return NULL;
    AML_Var* v = symtab_get_var(&g_persistent_globals, name);
    return v && v->type == AML_TYPE_STRING && v->string ? v->string->data : NULL;
}

int am_set_var_list(const char* name, const AM_List* list) {
    if (!name || !*name || strlen(name) >= AML_MAX_NAME || !list) return 1;
    if (!(isalpha((unsigned char)*name) || *name == '_')) return 1;
    for (const char* p = name + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 1;
    AM_List* copy = am_list_clone(list);
    if (!copy) return 1;
    if (symtab_set_list(&g_persistent_globals, name, copy)) {
        am_list_free(copy);
        return 1;
    }
    g_persistent_enabled = 1;
    return 0;
}

const AM_List* am_get_var_list(const char* name) {
    if (!name) return NULL;
    AML_Var* v = symtab_get_var(&g_persistent_globals, name);
    return v && v->type == AML_TYPE_LIST ? v->list : NULL;
}

int am_set_var_map(const char* name, const AM_Map* map) {
    if (!name || !*name || strlen(name) >= AML_MAX_NAME || !map) return 1;
    if (!(isalpha((unsigned char)*name) || *name == '_')) return 1;
    for (const char* p = name + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 1;
    AM_Map* copy = am_map_clone(map);
    if (!copy) return 1;
    if (symtab_set_map(&g_persistent_globals, name, copy)) {
        am_map_free(copy);
        return 1;
    }
    g_persistent_enabled = 1;
    return 0;
}

const AM_Map* am_get_var_map(const char* name) {
    if (!name) return NULL;
    AML_Var* v = symtab_get_var(&g_persistent_globals, name);
    return v && v->type == AML_TYPE_MAP ? v->map : NULL;
}

int am_set_var_tokenizer(const char* name, AM_Tokenizer* model) {
    if (!name || !*name || strlen(name) >= AML_MAX_NAME || !model) return 1;
    if (!(isalpha((unsigned char)*name) || *name == '_')) return 1;
    for (const char* p = name + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 1;
    am_tokenizer_ref(model);
    if (symtab_set_tokenizer(&g_persistent_globals, name, model)) {
        am_tokenizer_free(model);
        return 1;
    }
    g_persistent_enabled = 1;
    return 0;
}

const AM_Tokenizer* am_get_var_tokenizer(const char* name) {
    if (!name) return NULL;
    AML_Var* v = symtab_get_var(&g_persistent_globals, name);
    return v && v->type == AML_TYPE_TOKENIZER ? v->tokenizer : NULL;
}

int am_set_var_record(const char* name, const AM_Record* record) {
    if (!name || !*name || strlen(name) >= AML_MAX_NAME || !record) return 1;
    AM_Record* copy = am_record_clone(record);
    if (!copy) return 2;
    if (symtab_set_record(&g_persistent_globals, name, copy)) {
        am_record_free(copy);
        return 2;
    }
    g_persistent_enabled = 1;
    return 0;
}

const AM_Record* am_get_var_record(const char* name) {
    if (!name) return NULL;
    AML_Var* v = symtab_get_var(&g_persistent_globals, name);
    return v && v->type == AML_TYPE_RECORD ? v->record : NULL;
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
// FIELD STATE PERSISTENCE — am_field_save / am_field_load
//
// AM_State is POD with only inline arrays (scar_texts, gamma slots, etc.).
// We dump it as a single block: magic + version + sizeof + timestamp + struct.
// On load, refuse if magic / version / sizeof differ — that catches any case
// where libaml has been recompiled with a different layout, so old soma files
// don't silently corrupt the running field. Top-level AML directives LOAD/SAVE
// dispatch here from aml_exec_level0.
// ═══════════════════════════════════════════════════════════════════════════════

#define AM_SOMA_MAGIC   0x4F534D41u  /* 'A','M','S','O' little-endian */
#define AM_SOMA_VERSION 3u   /* v3: +positive soma (warmth/flow/weave), appended → v2 loads as prefix */

/* MetaJanus (birth_drift + derived personal_dissonance/janus_gap/yahrzeit) is IDENTITY, not field
 * weather: the origin is re-declared by BIRTH each session and the rest recomputes every step. It
 * occupies the struct tail and is EXCLUDED from the soma — so LOAD can never drag the origin (it is
 * not in the file), and a pre-MetaJanus soma stays a clean prefix. On-disk format is unchanged. */
#define AM_SOMA_PERSIST_SZ ((uint32_t)offsetof(AM_State, birth_drift))

int am_field_save(const char* path) {
  if (!path || !path[0]) return -1;
  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "[am_field_save] cannot open '%s' for write\n", path);
    return -1;
  }
  uint32_t magic     = AM_SOMA_MAGIC;
  uint32_t version   = AM_SOMA_VERSION;
  uint32_t state_sz  = AM_SOMA_PERSIST_SZ;   /* field weather only; the MetaJanus tail is session identity */
  uint64_t timestamp = (uint64_t)time(NULL);
  if (fwrite(&magic,    4, 1, f) != 1 ||
      fwrite(&version,  4, 1, f) != 1 ||
      fwrite(&state_sz, 4, 1, f) != 1 ||
      fwrite(&timestamp,8, 1, f) != 1 ||
      fwrite(&G, AM_SOMA_PERSIST_SZ, 1, f) != 1) {
    fprintf(stderr, "[am_field_save] short write to '%s'\n", path);
    fclose(f);
    return -2;
  }
  fclose(f);
  return 0;
}

int am_field_load(const char* path) {
  if (!path || !path[0]) return -1;
  FILE* f = fopen(path, "rb");
  if (!f) {
    /* Missing file isn't an error on first run — quietly start fresh. */
    return -1;
  }
  uint32_t magic = 0, version = 0, state_sz = 0;
  uint64_t timestamp = 0;
  if (fread(&magic, 4, 1, f) != 1 || magic != AM_SOMA_MAGIC) {
    fprintf(stderr, "[am_field_load] '%s': bad magic 0x%08x (expected 0x%08x)\n",
            path, magic, AM_SOMA_MAGIC);
    fclose(f);
    return -2;
  }
  if (fread(&version, 4, 1, f) != 1 || version < 2u || version > AM_SOMA_VERSION) {
    fprintf(stderr, "[am_field_load] '%s': version %u (supported 2..%u) — refusing\n",
            path, version, AM_SOMA_VERSION);
    fclose(f);
    return -3;
  }
  /* Persisted region = field weather up to (not including) the MetaJanus identity tail
   * (AM_SOMA_PERSIST_SZ). AM_State grows APPEND-ONLY, so any older soma is a clean PREFIX: accept any
   * state_sz in (0, PERSIST_SZ] and load it as a prefix — the memset below has already zeroed the
   * region, so an unread trailing part is honestly zero. A larger state_sz is a newer layout (or
   * junk) we cannot safely interpret → refuse. */
  if (fread(&state_sz, 4, 1, f) != 1 || state_sz == 0u || state_sz > AM_SOMA_PERSIST_SZ) {
    fprintf(stderr,
            "[am_field_load] '%s': state size %u not in (0, %u] — refusing\n",
            path, state_sz, AM_SOMA_PERSIST_SZ);
    fclose(f);
    return -4;
  }
  if (fread(&timestamp, 8, 1, f) != 1) {
    fclose(f); return -5;
  }
  /* MED-2 (Sol fix): read the payload into a temp buffer and validate it is COMPLETE before touching the
   * live field. A truncated / short read must not zero the live weather — the load is transactional: commit
   * to G only after a full read succeeds. The buffer is zeroed first, so an unread trailing prefix is
   * honestly zero (prefix-load, A-1); the MetaJanus identity tail beyond PERSIST_SZ is never touched. */
  unsigned char buf[AM_SOMA_PERSIST_SZ];
  memset(buf, 0, AM_SOMA_PERSIST_SZ);
  if (fread(buf, state_sz, 1, f) != 1) {
    fprintf(stderr, "[am_field_load] '%s': short read of state (truncated soma) — refusing, live field intact\n", path);
    fclose(f);
    return -5;
  }
  fclose(f);
  memcpy(&G, buf, AM_SOMA_PERSIST_SZ);   /* atomic commit of the validated field-weather prefix */
  return 0;
}

// ── Co-occurrence sidecar (per-voice) ──────────────────────────────────────
// cooc edges are token-ids in a voice's OWN vocab (Janus 32759 / Resonance 16128),
// so they must NOT travel through the shared soma (cross-contamination). Each
// voice persists its cooc in a separate file and loads it back after the shared
// soma LOAD, overwriting whatever contaminated cooc the soma carried. The shared
// soma still carries the cross-voice field (debt / dissonance / chambers).
#define AM_COOC_MAGIC 0x434F4F43u  /* 'C','O','O','C' */
int am_cooc_save(const char* path) {
  if (!path || !path[0]) return -1;
  FILE* f = fopen(path, "wb");
  if (!f) return -1;
  uint32_t magic = AM_COOC_MAGIC;
  int n = G.cooc_n;
  if (fwrite(&magic, 4, 1, f) != 1 || fwrite(&n, 4, 1, f) != 1 ||
      fwrite(&G.cooc_total, 4, 1, f) != 1 ||
      fwrite(&G.ctx_ring_n, 4, 1, f) != 1 ||
      fwrite(G.ctx_ring, sizeof(int), AM_COOC_CTX, f) != (size_t)AM_COOC_CTX ||
      fwrite(G.cooc_src, sizeof(int),   n, f) != (size_t)n ||
      fwrite(G.cooc_dst, sizeof(int),   n, f) != (size_t)n ||
      fwrite(G.cooc_cnt, sizeof(float), n, f) != (size_t)n) {
    fclose(f); return -2;
  }
  fclose(f);
  return 0;
}

int am_cooc_load(const char* path) {
  if (!path || !path[0]) return -1;
  FILE* f = fopen(path, "rb");
  if (!f) return -1;   /* missing = fresh per-voice cooc */
  uint32_t magic = 0; int n = 0;
  if (fread(&magic, 4, 1, f) != 1 || magic != AM_COOC_MAGIC) { fclose(f); return -2; }
  if (fread(&n, 4, 1, f) != 1 || n < 0 || n > AM_COOC_MAX) { fclose(f); return -3; }
  if (fread(&G.cooc_total, 4, 1, f) != 1 ||
      fread(&G.ctx_ring_n, 4, 1, f) != 1 ||
      fread(G.ctx_ring, sizeof(int), AM_COOC_CTX, f) != (size_t)AM_COOC_CTX ||
      fread(G.cooc_src, sizeof(int),   n, f) != (size_t)n ||
      fread(G.cooc_dst, sizeof(int),   n, f) != (size_t)n ||
      fread(G.cooc_cnt, sizeof(float), n, f) != (size_t)n) {
    fclose(f); return -4;
  }
  G.cooc_n = n;
  if (G.ctx_ring_n < 0 || G.ctx_ring_n > AM_COOC_CTX) G.ctx_ring_n = 0;
  fclose(f);
  return 0;
}

// Low-rank delta voice (B2-B) persistence. A=[E,rank], B=[rank,E] are host-owned
// per-voice buffers (NOT in soma — kept out of AM_State to avoid an ABI bump).
// magic + dims + A + B. Dim mismatch on load → -3 so the caller keeps its zeros.
#define AM_DELTA_MAGIC 0x444C5441u  /* 'D','L','T','A' */
int am_delta_save(const char* path, const float* A, const float* B, int E, int rank) {
  if (!path || !path[0] || !A || !B || E <= 0 || rank <= 0) return -1;
  FILE* f = fopen(path, "wb");
  if (!f) return -1;
  uint32_t magic = AM_DELTA_MAGIC;
  size_t na = (size_t)E * rank, nb = (size_t)rank * E;
  if (fwrite(&magic, 4, 1, f) != 1 || fwrite(&E, 4, 1, f) != 1 ||
      fwrite(&rank, 4, 1, f) != 1 ||
      fwrite(A, sizeof(float), na, f) != na ||
      fwrite(B, sizeof(float), nb, f) != nb) {
    fclose(f); return -2;
  }
  fclose(f);
  return 0;
}

int am_delta_load(const char* path, float* A, float* B, int E, int rank) {
  if (!path || !path[0] || !A || !B || E <= 0 || rank <= 0) return -1;
  FILE* f = fopen(path, "rb");
  if (!f) return -1;   /* missing = fresh delta (caller keeps zeros) */
  uint32_t magic = 0; int fe = 0, fr = 0;
  if (fread(&magic, 4, 1, f) != 1 || magic != AM_DELTA_MAGIC) { fclose(f); return -2; }
  if (fread(&fe, 4, 1, f) != 1 || fread(&fr, 4, 1, f) != 1 ||
      fe != E || fr != rank) { fclose(f); return -3; }  /* dim mismatch */
  size_t na = (size_t)E * rank, nb = (size_t)rank * E;
  if (fread(A, sizeof(float), na, f) != na ||
      fread(B, sizeof(float), nb, f) != nb) { fclose(f); return -4; }
  fclose(f);
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEVEL 2 INFRASTRUCTURE — error, field map, symbol table
// ═══════════════════════════════════════════════════════════════════════════════

static _Thread_local char g_error[256] = {0};

const char* am_get_error(void) { return g_error; }

// Set error with optional line number for Level 2 debugging
// lineno <= 0 means no line number (Level 0 or internal error)
static void set_error_at(AML_ExecCtx* ctx, int lineno, const char* msg) {
    char buf[256];
    // INCLUDE/AWAIT propagate the original source diagnostic. Repeated prefixes
    // would fill the fixed buffer and erase the error at the end of the chain.
    int located = 0;
    if (msg[0] == '/') {
        const char* colon = msg;
        while ((colon = strchr(colon, ':')) != NULL) {
            const char* number = ++colon;
            while (isdigit((unsigned char)*colon)) colon++;
            if (colon > number && colon[0] == ':' && colon[1] == ' ') { located = 1; break; }
        }
    }
    if (located) {
        snprintf(buf, sizeof(buf), "%s", msg);
    } else if (lineno > 0 && g_source_path[0]) {
        snprintf(buf, sizeof(buf), "%.112s:%d: %.120s", g_source_path, lineno, msg);
    } else if (lineno > 0) {
        snprintf(buf, sizeof(buf), "line %d: %s", lineno, msg);
    } else {
        snprintf(buf, sizeof(buf), "%s", msg);
    }
    buf[255] = 0;
    if (ctx) {
        snprintf(ctx->error, sizeof(ctx->error), "%s", buf);
    }
    snprintf(g_error, sizeof(g_error), "%s", buf);
}

// Convenience: set error without line number
__attribute__((unused))
static void set_error(AML_ExecCtx* ctx, const char* msg) {
    set_error_at(ctx, 0, msg);
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
    FIELD_F("birth_drift",       birth_drift),
    FIELD_F("personal_dissonance", personal_dissonance),
    FIELD_F("janus_gap",           janus_gap),
    FIELD_F("yahrzeit",            yahrzeit),
    FIELD_F("janus_temporal_alpha", janus_temporal_alpha),
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
    FIELD_F("be_voice",          be_voice),
    FIELD_F("ask_voice",         ask_voice),
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
    // delta voice / notorch
    FIELD_F("lora_alpha",        lora_alpha),
    FIELD_F("notorch_lr",        notorch_lr),
    FIELD_F("notorch_decay",     notorch_decay),
    // schumann
    FIELD_F("schumann_hz",       schumann_hz),
    FIELD_F("schumann_modulation", schumann_modulation),
    FIELD_F("schumann_coherence", schumann_coherence),
    FIELD_F("schumann_phase",    schumann_phase),
    // live metrics
    FIELD_F("entropy",           entropy),
    FIELD_F("resonance",         resonance),
    FIELD_F("emergence",         emergence),
    FIELD_F("destiny_bias",      destiny_bias),
    // dark matter
    FIELD_F("dark_gravity",      dark_gravity),
    FIELD_I("n_scars",           n_scars),
    // 4.C seasons
    FIELD_I("season",            season),
    FIELD_F("season_phase",      season_phase),
    FIELD_F("season_intensity",  season_intensity),
    FIELD_F("spring_energy",     spring_energy),
    FIELD_F("summer_energy",     summer_energy),
    FIELD_F("autumn_energy",     autumn_energy),
    FIELD_F("winter_energy",     winter_energy),
    // Gamma — personality essence
    FIELD_F("essence_alpha",     essence_alpha),
    FIELD_I("janus_mode",        janus_mode),
    FIELD_F("janus_blend",       janus_blend),
    FIELD_F("gamma_drift",       gamma_drift),
    FIELD_I("n_gamma",           n_gamma),
    // Positive soma (v3) — the readable felt-body of an attached organism
    FIELD_F("warmth",            warmth),
    FIELD_F("flow",              flow),
    FIELD_F("weave",             weave),
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

// ═══════════════════════════════════════════════════════════════════════════════
// ARRAY MEMORY MANAGEMENT (v4.0)
// ═══════════════════════════════════════════════════════════════════════════════

AM_Array* am_array_new(int len) {
    if (len <= 0 || len > AM_MAX_ARRAY_SIZE) return NULL;
    AM_Array* arr = (AM_Array*)malloc(sizeof(AM_Array));
    if (!arr) return NULL;
    arr->data = (float*)calloc(len, sizeof(float));
    if (!arr->data) { free(arr); return NULL; }
    arr->len = len;
    arr->refcount = 1;
    arr->rows = 0;
    arr->cols = 0;
#ifdef USE_CUDA
    arr->d_data = NULL;
    arr->gpu_valid = 0;
#endif
    return arr;
}

// Create a 2D matrix (flat array with shape tracking)
static AM_Array* am_matrix_new(int rows, int cols) {
    if (rows <= 0 || cols <= 0 || rows > AM_MAX_ARRAY_SIZE / cols) return NULL;
    int total = rows * cols;
    AM_Array* arr = am_array_new(total);
    if (!arr) return NULL;
    arr->rows = rows;
    arr->cols = cols;
    return arr;
}

void am_array_free(AM_Array* arr) {
    if (!arr) return;
    arr->refcount--;
    if (arr->refcount <= 0) {
        free(arr->data);
#ifdef USE_CUDA
        if (arr->d_data) gpu_free(arr->d_data);
#endif
        free(arr);
    }
}

AM_Array* am_array_ref(AM_Array* arr) {
    if (arr) arr->refcount++;
    return arr;
}

// Clone an array (deep copy, preserves shape)
static AM_Array* am_array_clone(const AM_Array* src) {
    if (!src) return NULL;
    AM_Array* dst = am_array_new(src->len);
    if (!dst) return NULL;
    memcpy(dst->data, src->data, src->len * sizeof(float));
    dst->rows = src->rows;
    dst->cols = src->cols;
    return dst;
}

// ═══════════════════════════════════════════════════════════════════════════════
#ifdef USE_CUDA
static void ensure_gpu(AM_Array* arr) {
    if (!arr || !arr->data) return;
    if (!arr->d_data) {
        arr->d_data = gpu_alloc(arr->len);
        if (!arr->d_data) return;
    }
    if (!arr->gpu_valid) {
        gpu_upload(arr->d_data, arr->data, arr->len);
        arr->gpu_valid = 1;
    }
}
static void ensure_cpu(AM_Array* arr) {
    if (!arr || !arr->d_data || !arr->gpu_valid || !arr->data) return;
    gpu_download(arr->data, arr->d_data, arr->len);
}
static void invalidate_gpu(AM_Array* arr) {
    if (arr) arr->gpu_valid = 0;
}
#endif

// AUTOGRAD TAPE (v4.0 Phase 3) — reverse-mode automatic differentiation
// ═══════════════════════════════════════════════════════════════════════════════

static AM_Tape g_tape = {0};

// Global LR schedule and NaN guard shared by the AML TAPE LR_* / NAN_* commands.
// One per-process is enough for the language layer — C API users can still
// construct their own AM_Schedule / AM_NanGuard values locally.
static AM_Schedule g_aml_schedule = {0};
static AM_NanGuard g_aml_nan_guard = {0};
static int         g_aml_nan_guard_inited = 0;

void am_tape_start(void) {
    // Clear any existing tape state
    am_tape_clear();
    g_tape.active = 1;
}

void am_tape_clear(void) {
    // Free ALL outputs (including params — refcount handles safety) and all grads
    for (int i = 0; i < g_tape.count; i++) {
        if (g_tape.entries[i].output) {
            am_array_free(g_tape.entries[i].output);
        }
        if (g_tape.entries[i].grad) {
            am_array_free(g_tape.entries[i].grad);
            g_tape.entries[i].grad = NULL;
        }
    }
    g_tape.count = 0;
    g_tape.active = 0;
    // Reset n_params so TAPE PARAM re-registers into same adam slots
    // adam[].m, adam[].v, adam[].t survive — they are reused, not reallocated
    g_tape.n_params = 0;
}

// Full tape reset — frees ALL resources including params and adam states
static void am_tape_destroy(void) {
    // Free all tape entries including params
    for (int i = 0; i < g_tape.count; i++) {
        if (g_tape.entries[i].output) {
            am_array_free(g_tape.entries[i].output);
            g_tape.entries[i].output = NULL;
        }
        if (g_tape.entries[i].grad) {
            am_array_free(g_tape.entries[i].grad);
            g_tape.entries[i].grad = NULL;
        }
    }
    // Free adam states
    for (int i = 0; i < g_tape.n_params; i++) {
        if (g_tape.adam[i].m) { am_array_free(g_tape.adam[i].m); g_tape.adam[i].m = NULL; }
        if (g_tape.adam[i].v) { am_array_free(g_tape.adam[i].v); g_tape.adam[i].v = NULL; }
        if (g_tape.adam[i].acc_grad) { am_array_free(g_tape.adam[i].acc_grad); g_tape.adam[i].acc_grad = NULL; }
        g_tape.adam[i].t = 0;
    }
    // memset zeros everything including chuck state
    memset(&g_tape, 0, sizeof(g_tape));
}

int am_tape_is_active(void) { return g_tape.active; }
AM_Tape* am_tape_get(void) { return &g_tape; }

// Record a computation on the tape. Returns entry index.
int am_tape_record(AM_Array* output, int op, int p1, int p2, float aux) {
    if (!g_tape.active || g_tape.count >= AM_TAPE_MAX_ENTRIES) return -1;
    int idx = g_tape.count;
    AM_TapeEntry* e = &g_tape.entries[idx];
    e->output = output;
    am_array_ref(output); // tape owns a reference
    e->grad = NULL;
    e->op = op;
    e->parent1 = p1;
    e->parent2 = p2;
    e->parent3 = -1;
    e->aux = aux;
    e->aux2 = 0;
    e->is_param = 0;
    g_tape.count++;
    return idx;
}

// Record with 3 parents + 2 aux values (for seq ops like causal_attention)
int am_tape_record3(AM_Array* output, int op, int p1, int p2, int p3, float aux, float aux2) {
    if (!g_tape.active || g_tape.count >= AM_TAPE_MAX_ENTRIES) return -1;
    int idx = g_tape.count;
    AM_TapeEntry* e = &g_tape.entries[idx];
    e->output = output;
    am_array_ref(output);
    e->grad = NULL;
    e->op = op;
    e->parent1 = p1;
    e->parent2 = p2;
    e->parent3 = p3;
    e->aux = aux;
    e->aux2 = aux2;
    e->is_param = 0;
    g_tape.count++;
    return idx;
}

// Record a parameter (trainable weight). Returns entry index.
// Adam state is allocated on first call for this param.
int am_tape_record_param(AM_Array* param) {
    if (!g_tape.active || g_tape.count >= AM_TAPE_MAX_ENTRIES) return -1;
    int idx = g_tape.count;
    AM_TapeEntry* e = &g_tape.entries[idx];
    e->output = param;
    am_array_ref(param);
    e->grad = NULL;
    e->op = AM_OP_NONE;
    e->parent1 = -1;
    e->parent2 = -1;
    e->parent3 = -1;
    e->aux = 0;
    e->aux2 = 0;
    e->is_param = 1;
    e->no_decay = 0;

    // Register for Adam — positional: param N always gets adam slot N
    if (g_tape.n_params < AM_TAPE_MAX_PARAMS) {
        int pi = g_tape.n_params;
        if (!g_tape.adam[pi].m) {
            // First time: allocate
            g_tape.adam[pi].m = am_array_new(param->len);
            g_tape.adam[pi].v = am_array_new(param->len);
            g_tape.adam[pi].t = 0;
        } else if (g_tape.adam[pi].m->len != param->len) {
            // Size changed (vocab evolution): resize, zero-init new elements
            int old_len = g_tape.adam[pi].m->len;
            AM_Array* new_m = am_array_new(param->len);
            AM_Array* new_v = am_array_new(param->len);
            int copy_len = old_len < param->len ? old_len : param->len;
            memcpy(new_m->data, g_tape.adam[pi].m->data, copy_len * sizeof(float));
            memcpy(new_v->data, g_tape.adam[pi].v->data, copy_len * sizeof(float));
            am_array_free(g_tape.adam[pi].m);
            am_array_free(g_tape.adam[pi].v);
            g_tape.adam[pi].m = new_m;
            g_tape.adam[pi].v = new_v;
        }
        g_tape.n_params++;
    }

    g_tape.count++;
    return idx;
}

// Accumulate gradient into an entry
static void tape_acc_grad(int idx, const float* grad, int len) {
    if (idx < 0 || idx >= g_tape.count) return;
    AM_TapeEntry* e = &g_tape.entries[idx];
    if (!e->grad) {
        e->grad = am_array_new(len);
        if (!e->grad) return;
    }
    int n = e->grad->len < len ? e->grad->len : len;
    for (int i = 0; i < n; i++) e->grad->data[i] += grad[i];
}

// Backward pass: propagate gradients from loss to all parents
void am_tape_backward(int loss_idx) {
    if (loss_idx < 0 || loss_idx >= g_tape.count) return;

    // Initialize loss gradient to 1.0
    AM_TapeEntry* loss = &g_tape.entries[loss_idx];
    if (!loss->grad) {
        loss->grad = am_array_new(loss->output->len);
    }
    for (int i = 0; i < loss->grad->len; i++) loss->grad->data[i] = 1.0f;

    // Reverse topological order (entries are already in forward order)
    for (int idx = loss_idx; idx >= 0; idx--) {
        AM_TapeEntry* e = &g_tape.entries[idx];
        if (!e->grad) continue;
        float* dout = e->grad->data;
        int out_len = e->output->len;

        switch (e->op) {
        case AM_OP_ADD: {
            // y = a + b → da += dout, db += dout
            if (e->parent1 >= 0) tape_acc_grad(e->parent1, dout, out_len);
#ifdef USE_CUDA
                if (e->output->d_data && e->output->gpu_valid) {
                    float* d_ga = gpu_scratch(3, out_len);
                    float* d_gb = gpu_scratch(4, out_len);
                    float* d_dout_buf = gpu_scratch(0, out_len);
                    gpu_upload(d_dout_buf, dout, out_len);
                    gpu_add_backward(d_ga, d_gb, d_dout_buf, out_len);
                    float* ga = (float*)malloc(out_len * sizeof(float));
                    float* gb = (float*)malloc(out_len * sizeof(float));
                    gpu_download(ga, d_ga, out_len);
                    gpu_download(gb, d_gb, out_len);
                    tape_acc_grad(e->parent1, ga, out_len);
                    tape_acc_grad(e->parent2, gb, out_len);
                    free(ga); free(gb);
                    break;
                }
#endif
            if (e->parent2 >= 0) tape_acc_grad(e->parent2, dout, out_len);
            break;
        }
        case AM_OP_MUL: {
            // y = a * b → da += dout * b, db += dout * a
            if (e->parent1 >= 0 && e->parent2 >= 0) {
                AM_TapeEntry* pa = &g_tape.entries[e->parent1];
                AM_TapeEntry* pb = &g_tape.entries[e->parent2];
#ifdef USE_CUDA
                if (pa->output->d_data && pa->output->gpu_valid &&
                    pb->output->d_data && pb->output->gpu_valid) {
                    float* d_ga = gpu_scratch(3, out_len);
                    float* d_gb = gpu_scratch(4, out_len);
                    float* d_dout_buf = gpu_scratch(0, out_len);
                    gpu_upload(d_dout_buf, dout, out_len);
                    gpu_mul_backward(d_ga, d_gb, d_dout_buf,
                                     pa->output->d_data, pb->output->d_data, out_len);
                    float* ga = (float*)malloc(out_len * sizeof(float));
                    float* gb = (float*)malloc(out_len * sizeof(float));
                    gpu_download(ga, d_ga, out_len);
                    gpu_download(gb, d_gb, out_len);
                    tape_acc_grad(e->parent1, ga, out_len);
                    tape_acc_grad(e->parent2, gb, out_len);
                    free(ga); free(gb);
                    break;
                }
#endif
                /* CPU fallback: parents may be GPU-fresh with stale CPU mirror.
                 * Mirrors notorch.c NT_OP_MUL fix 2026-05-11. */
#ifdef USE_CUDA
                ensure_cpu(pa->output);
                ensure_cpu(pb->output);
#endif
                float* ga = (float*)calloc(out_len, sizeof(float));
                float* gb = (float*)calloc(out_len, sizeof(float));
                if (ga && gb) {
                    for (int i = 0; i < out_len; i++) {
                        ga[i] = dout[i] * pb->output->data[i];
                        gb[i] = dout[i] * pa->output->data[i];
                    }
                    tape_acc_grad(e->parent1, ga, out_len);
                    tape_acc_grad(e->parent2, gb, out_len);
                }
                free(ga); free(gb);
            }
            break;
        }
        case AM_OP_SCALE: {
            // y = a * scalar → da += dout * scalar
            if (e->parent1 >= 0) {
                float* ga = (float*)calloc(out_len, sizeof(float));
                if (ga) {
                    for (int i = 0; i < out_len; i++) ga[i] = dout[i] * e->aux;
                    tape_acc_grad(e->parent1, ga, out_len);
                }
                free(ga);
            }
            break;
        }
        case AM_OP_MATVEC: {
            // y = W @ x → dW += dout ⊗ x, dx += W^T @ dout
            if (e->parent1 >= 0 && e->parent2 >= 0) {
                AM_TapeEntry* pw = &g_tape.entries[e->parent1]; // W
                AM_TapeEntry* px = &g_tape.entries[e->parent2]; // x
#ifdef USE_CUDA
                ensure_cpu(pw->output);
                ensure_cpu(px->output);
#endif
                int rows = pw->output->rows;
                int cols = pw->output->cols;
                if (rows > 0 && cols > 0) {
                    // dW: outer product dout ⊗ x (rows × cols)
                    float* dw = (float*)calloc((size_t)rows * cols, sizeof(float));
                    if (dw) {
                        for (int i = 0; i < rows; i++)
                            for (int j = 0; j < cols; j++)
                                dw[i * cols + j] = dout[i] * px->output->data[j];
                        tape_acc_grad(e->parent1, dw, rows * cols);
                    }
                    free(dw);
                    // dx: W^T @ dout
                    float* dx = (float*)calloc(cols, sizeof(float));
                    if (dx) {
                        for (int j = 0; j < cols; j++)
                            for (int i = 0; i < rows; i++)
                                dx[j] += pw->output->data[i * cols + j] * dout[i];
                        tape_acc_grad(e->parent2, dx, cols);
                    }
                    free(dx);
                }
            }
            break;
        }
        case AM_OP_SILU: {
            // y = x * sigmoid(x) → dy/dx = sigmoid(x) * (1 + x * (1 - sigmoid(x)))
            if (e->parent1 >= 0) {
                AM_TapeEntry* px = &g_tape.entries[e->parent1];
#ifdef USE_CUDA
                if (px->output->d_data && px->output->gpu_valid) {
                    float* d_gx = gpu_scratch(3, out_len);
                    float* d_dout_buf = gpu_scratch(0, out_len);
                    gpu_upload(d_dout_buf, dout, out_len);
                    gpu_silu_backward(d_gx, d_dout_buf, px->output->d_data, out_len);
                    float* gx = (float*)malloc(out_len * sizeof(float));
                    gpu_download(gx, d_gx, out_len);
                    tape_acc_grad(e->parent1, gx, out_len);
                    free(gx);
                    break;
                }
                /* CPU fallback: GPU branch above already returned via break if
                 * the GPU path fired. Here parent->output->data may be the
                 * stale CPU mirror of a GPU-resident forward. */
                ensure_cpu(px->output);
#endif
                float* gx = (float*)calloc(out_len, sizeof(float));
                if (gx) {
                    for (int i = 0; i < out_len; i++) {
                        float x = px->output->data[i];
                        float sig = 1.0f / (1.0f + expf(-x));
                        gx[i] = dout[i] * sig * (1.0f + x * (1.0f - sig));
                    }
                    tape_acc_grad(e->parent1, gx, out_len);
                }
                free(gx);
            }
            break;
        }
        case AM_OP_SOFTMAX: {
            // y = softmax(x) → Jacobian: diag(y) - y⊗y
            // dsoftmax_i = y_i * (dout_i - sum(dout * y))
            if (e->parent1 >= 0) {
#ifdef USE_CUDA
                ensure_cpu(e->output);
#endif
                float dot_dy = 0;
                for (int i = 0; i < out_len; i++)
                    dot_dy += dout[i] * e->output->data[i];
                float* gx = (float*)calloc(out_len, sizeof(float));
                if (gx) {
                    for (int i = 0; i < out_len; i++)
                        gx[i] = e->output->data[i] * (dout[i] - dot_dy);
                    tape_acc_grad(e->parent1, gx, out_len);
                }
                free(gx);
            }
            break;
        }
        case AM_OP_RMSNORM: {
            // y = x / rms, rms = sqrt(mean(x^2) + eps)
            // Simplified gradient: similar to LayerNorm but without mean subtraction
            if (e->parent1 >= 0) {
                AM_TapeEntry* px = &g_tape.entries[e->parent1];
#ifdef USE_CUDA
                ensure_cpu(px->output);
#endif
                int n = out_len;
                float ss = 0;
                for (int i = 0; i < n; i++) ss += px->output->data[i] * px->output->data[i];
                float rms = sqrtf(ss / n + 1e-6f);
                float rms3 = rms * rms * rms;
                float sum_dout_x = 0;
                for (int i = 0; i < n; i++)
                    sum_dout_x += dout[i] * px->output->data[i];
                float* gx = (float*)calloc(n, sizeof(float));
                if (gx) {
                    for (int i = 0; i < n; i++)
                        gx[i] = (dout[i] / rms) - (px->output->data[i] * sum_dout_x / (n * rms3));
                    tape_acc_grad(e->parent1, gx, n);
                }
                free(gx);
            }
            break;
        }
        case AM_OP_GELU: {
            // y = 0.5*x*(1 + tanh(sqrt(2/pi)*(x + 0.044715*x^3)))
            if (e->parent1 >= 0) {
                AM_TapeEntry* px = &g_tape.entries[e->parent1];
#ifdef USE_CUDA
                ensure_cpu(px->output);
#endif
                float* gx = (float*)calloc(out_len, sizeof(float));
                if (gx) {
                    for (int i = 0; i < out_len; i++) {
                        float x = px->output->data[i];
                        float x3 = x * x * x;
                        float inner = 0.7978845608f * (x + 0.044715f * x3);
                        float th = tanhf(inner);
                        float gelu_grad = 0.5f * (1.0f + th) +
                            0.5f * x * (1.0f - th * th) *
                            0.7978845608f * (1.0f + 3.0f * 0.044715f * x * x);
                        gx[i] = dout[i] * gelu_grad;
                    }
                    tape_acc_grad(e->parent1, gx, out_len);
                }
                free(gx);
            }
            break;
        }
        case AM_OP_DROPOUT: {
            // y = x * mask (inverted). Mask encoded in output: 0 = dropped, kept = scaled.
            // aux = p
            if (e->parent1 >= 0) {
#ifdef USE_CUDA
                ensure_cpu(e->output);
#endif
                float p = e->aux;
                float scale = (p > 0.0f && p < 1.0f) ? 1.0f / (1.0f - p) : 1.0f;
                float* gx = (float*)calloc(out_len, sizeof(float));
                if (gx) {
                    for (int i = 0; i < out_len; i++) {
                        // non-zero output => mask kept => grad passes through, scaled
                        gx[i] = (e->output->data[i] != 0.0f) ? dout[i] * scale : 0.0f;
                    }
                    tape_acc_grad(e->parent1, gx, out_len);
                }
                free(gx);
            }
            break;
        }
        case AM_OP_LAYERNORM: {
            // y = gamma * (x - mean) / sqrt(var + eps) + beta
            // parent1 = x, parent2 = gamma (optional), parent3 = beta (optional)
            if (e->parent1 >= 0) {
                AM_TapeEntry* px = &g_tape.entries[e->parent1];
                int n = out_len;
                int has_gamma = (e->parent2 >= 0 && e->parent2 < g_tape.count);
                int has_beta  = (e->parent3 >= 0 && e->parent3 < g_tape.count);
#ifdef USE_CUDA
                ensure_cpu(px->output);
                if (has_gamma) ensure_cpu(g_tape.entries[e->parent2].output);
#endif
                float* gamma_data = has_gamma ? g_tape.entries[e->parent2].output->data : NULL;

                float mean = 0;
                for (int i = 0; i < n; i++) mean += px->output->data[i];
                mean /= n;
                float var = 0;
                for (int i = 0; i < n; i++) { float d = px->output->data[i] - mean; var += d * d; }
                var /= n;
                float inv_std = 1.0f / sqrtf(var + 1e-5f);

                float* dout_eff = (float*)calloc(n, sizeof(float));
                if (dout_eff) {
                    for (int i = 0; i < n; i++)
                        dout_eff[i] = has_gamma ? dout[i] * gamma_data[i] : dout[i];

                    float sum_de = 0, sum_de_xhat = 0;
                    for (int i = 0; i < n; i++) {
                        float xhat = (px->output->data[i] - mean) * inv_std;
                        sum_de += dout_eff[i];
                        sum_de_xhat += dout_eff[i] * xhat;
                    }
                    float* gx = (float*)calloc(n, sizeof(float));
                    if (gx) {
                        for (int i = 0; i < n; i++) {
                            float xhat = (px->output->data[i] - mean) * inv_std;
                            gx[i] = inv_std * (dout_eff[i] - sum_de / n - xhat * sum_de_xhat / n);
                        }
                        tape_acc_grad(e->parent1, gx, n);
                    }
                    free(gx);

                    // gamma grad: sum(dout * xhat) per element
                    if (has_gamma) {
                        float* gg = (float*)calloc(n, sizeof(float));
                        if (gg) {
                            for (int i = 0; i < n; i++) {
                                float xhat = (px->output->data[i] - mean) * inv_std;
                                gg[i] = dout[i] * xhat;
                            }
                            tape_acc_grad(e->parent2, gg, n);
                            free(gg);
                        }
                    }
                    // beta grad: dout directly
                    if (has_beta) tape_acc_grad(e->parent3, dout, n);
                    free(dout_eff);
                }
            }
            break;
        }
        case AM_OP_SEQ_LAYERNORM: {
            // layernorm per-position on T chunks of size D
            // aux = T, aux2 = D
            if (e->parent1 >= 0) {
                AM_TapeEntry* px = &g_tape.entries[e->parent1];
                int T = (int)e->aux;
                int D = (int)e->aux2;
                int has_gamma = (e->parent2 >= 0 && e->parent2 < g_tape.count);
                int has_beta  = (e->parent3 >= 0 && e->parent3 < g_tape.count);
#ifdef USE_CUDA
                ensure_cpu(px->output);
                if (has_gamma) ensure_cpu(g_tape.entries[e->parent2].output);
#endif
                float* gamma_data = has_gamma ? g_tape.entries[e->parent2].output->data : NULL;

                float* gx = (float*)calloc((size_t)T * D, sizeof(float));
                float* gg = has_gamma ? (float*)calloc(D, sizeof(float)) : NULL;
                float* gb = has_beta  ? (float*)calloc(D, sizeof(float)) : NULL;

                if (gx) {
                    for (int t = 0; t < T; t++) {
                        float* x_t = px->output->data + t * D;
                        float* dout_t = dout + t * D;
                        float mean = 0;
                        for (int d = 0; d < D; d++) mean += x_t[d];
                        mean /= D;
                        float var = 0;
                        for (int d = 0; d < D; d++) { float dd = x_t[d] - mean; var += dd * dd; }
                        var /= D;
                        float inv_std = 1.0f / sqrtf(var + 1e-5f);

                        float sum_de = 0, sum_de_xhat = 0;
                        for (int d = 0; d < D; d++) {
                            float de = has_gamma ? dout_t[d] * gamma_data[d] : dout_t[d];
                            float xhat = (x_t[d] - mean) * inv_std;
                            sum_de += de;
                            sum_de_xhat += de * xhat;
                        }
                        for (int d = 0; d < D; d++) {
                            float de = has_gamma ? dout_t[d] * gamma_data[d] : dout_t[d];
                            float xhat = (x_t[d] - mean) * inv_std;
                            gx[t * D + d] = inv_std * (de - sum_de / D - xhat * sum_de_xhat / D);
                            if (gg) gg[d] += dout_t[d] * xhat;
                            if (gb) gb[d] += dout_t[d];
                        }
                    }
                    tape_acc_grad(e->parent1, gx, T * D);
                    free(gx);
                    if (gg) { tape_acc_grad(e->parent2, gg, D); free(gg); }
                    if (gb) { tape_acc_grad(e->parent3, gb, D); free(gb); }
                }
            }
            break;
        }
        case AM_OP_CROSS_ENT: {
            // loss = -log(softmax(logits)[target])
            // d_logits = softmax(logits) - one_hot(target)
            if (e->parent1 >= 0) {
                AM_TapeEntry* pl = &g_tape.entries[e->parent1]; // logits
#ifdef USE_CUDA
                ensure_cpu(pl->output);
#endif
                int n = pl->output->len;
                int target = (int)e->aux;
                // Compute softmax of logits
                float mx = pl->output->data[0];
                for (int i = 1; i < n; i++)
                    if (pl->output->data[i] > mx) mx = pl->output->data[i];
                float* sm = (float*)calloc(n, sizeof(float));
                if (sm) {
                    float sum = 0;
                    for (int i = 0; i < n; i++) {
                        sm[i] = expf(pl->output->data[i] - mx);
                        sum += sm[i];
                    }
                    for (int i = 0; i < n; i++) sm[i] /= sum;
                    // gradient = softmax - one_hot
                    if (target >= 0 && target < n) sm[target] -= 1.0f;
                    // Scale by dout (which is 1.0 for loss)
                    for (int i = 0; i < n; i++) sm[i] *= dout[0];
                    tape_acc_grad(e->parent1, sm, n);
                }
                free(sm);
            }
            break;
        }
        case AM_OP_EMB_LOOKUP: {
            // y = wte[token_id, :] → d_wte[token_id, :] += dout
            if (e->parent1 >= 0) {
                AM_TapeEntry* pw = &g_tape.entries[e->parent1]; // wte
                int token_id = (int)e->aux;
                int cols = pw->output->cols;
                if (cols > 0 && token_id >= 0 && token_id < pw->output->rows) {
                    // Need full-size gradient for wte
                    float* gw = (float*)calloc(pw->output->len, sizeof(float));
                    if (gw) {
                        for (int i = 0; i < cols && i < out_len; i++)
                            gw[token_id * cols + i] = dout[i];
                        tape_acc_grad(e->parent1, gw, pw->output->len);
                    }
                    free(gw);
                }
            }
            break;
        }
        // ── Phase 5: sequence-level backward ──

        case AM_OP_SEQ_EMBED: {
            // h[t*D+d] = wte[tok*D+d] + wpe[pos*D+d]
            // d_wte[tok*D+d] += dout[t*D+d], d_wpe[pos*D+d] += dout[t*D+d]
            if (e->parent1 >= 0 && e->parent3 >= 0) {
                AM_TapeEntry* pwte = &g_tape.entries[e->parent1];
                AM_TapeEntry* pwpe = &g_tape.entries[e->parent2];
                AM_TapeEntry* ptok = &g_tape.entries[e->parent3]; // tokens array
#ifdef USE_CUDA
                ensure_cpu(ptok->output);
#endif
                int T = (int)e->aux;
                int D = (int)e->aux2;
                float* dwte = (float*)calloc(pwte->output->len, sizeof(float));
                float* dwpe = (float*)calloc(pwpe->output->len, sizeof(float));
                if (dwte && dwpe) {
                    for (int t = 0; t < T; t++) {
                        int tok = (int)ptok->output->data[t];
                        if (tok < 0) tok = 0;
                        if (tok >= pwte->output->rows) tok = pwte->output->rows - 1;
                        int pos = t < pwpe->output->rows ? t : pwpe->output->rows - 1;
                        for (int d = 0; d < D; d++) {
                            dwte[tok * D + d] += dout[t * D + d];
                            dwpe[pos * D + d] += dout[t * D + d];
                        }
                    }
                    tape_acc_grad(e->parent1, dwte, pwte->output->len);
                    tape_acc_grad(e->parent2, dwpe, pwpe->output->len);
                }
                free(dwte); free(dwpe);
            }
            break;
        }

        case AM_OP_SEQ_MATVEC: {
            // Y[t*out+i] = sum_j W[i*in+j] * X[t*in+j]
            // dW[i*in+j] += sum_t dout[t*out+i] * X[t*in+j]
            // dX[t*in+j] += sum_i W[i*in+j] * dout[t*out+i]
            if (e->parent1 >= 0 && e->parent2 >= 0) {
                AM_TapeEntry* pw = &g_tape.entries[e->parent1]; // W
                AM_TapeEntry* px = &g_tape.entries[e->parent2]; // X
                int T = (int)e->aux;
                int out_d = pw->output->rows;
                int in_d = pw->output->cols;
                float* dw = (float*)calloc(pw->output->len, sizeof(float));
                float* dx = (float*)calloc(px->output->len, sizeof(float));
                if (dw && dx) {
                    float* Wd = pw->output->data;
                    float* Xd = px->output->data;
#ifdef USE_CUDA
                    // GPU tensor backward
                    {
                        ensure_gpu(pw->output);
                        ensure_gpu(px->output);
                        float* d_dout_buf = gpu_scratch(0, T * out_d);
                        if (d_dout_buf && pw->output->d_data && px->output->d_data) {
                            gpu_upload(d_dout_buf, dout, T * out_d);
                            float* d_dX = gpu_scratch(1, T * in_d);
                            gpu_sgemm_nn(T, in_d, out_d, d_dout_buf, pw->output->d_data, d_dX);
                            gpu_download(dx, d_dX, T * in_d);
                            float* d_dW = gpu_scratch(2, out_d * in_d);
                            gpu_sgemm_tn(out_d, in_d, T, d_dout_buf, px->output->d_data, d_dW);
                            gpu_download(dw, d_dW, out_d * in_d);
                        } else {
                            ensure_cpu(pw->output); ensure_cpu(px->output);
                            float* Wd2 = pw->output->data;
                            float* Xd2 = px->output->data;
                            for (int t = 0; t < T; t++) {
                                float* dout_t = dout + t * out_d;
                                for (int j = 0; j < in_d; j++)
                                    for (int i = 0; i < out_d; i++)
                                        dx[t * in_d + j] += Wd2[i * in_d + j] * dout_t[i];
                            }
                            for (int t = 0; t < T; t++) {
                                float* dout_t = dout + t * out_d;
                                float* x_t = Xd2 + t * in_d;
                                for (int i = 0; i < out_d; i++)
                                    for (int j = 0; j < in_d; j++)
                                        dw[i * in_d + j] += dout_t[i] * x_t[j];
                            }
                        }
                    }
#elif defined(USE_BLAS)
                    /* BLAS path is CPU-only; if parents were last touched on GPU,
                     * Wd/Xd point to stale CPU mirrors. No-op when no CUDA build. */
                    /* (no ensure_cpu here — !defined(USE_CUDA) means no GPU mirror) */
                    // BLAS backward: dX(T,in) = dout(T,out) x W(out,in)
                    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                                T, in_d, out_d,
                                1.0f, dout, out_d, Wd, in_d,
                                0.0f, dx, in_d);
                    // dW(out,in) = dout^T(out,T) x X(T,in)
                    cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
                                out_d, in_d, T,
                                1.0f, dout, out_d, Xd, in_d,
                                0.0f, dw, in_d);
#else
                    /* Plain CPU path — same comment: no GPU mirror to sync. */
                    // dX: each position t independent → parallelize over t
                    #ifdef _OPENMP
                    #pragma omp parallel for schedule(static) if(T > 16)
                    #endif
                    for (int t = 0; t < T; t++) {
                        float* dout_t = dout + t * out_d;
                        // dX_t += W^T @ dout_t
                        for (int j = 0; j < in_d; j++)
                            for (int i = 0; i < out_d; i++)
                                dx[t * in_d + j] += Wd[i * in_d + j] * dout_t[i];
                    }
                    // dW: accumulates across T → can't trivially parallelize outer loop
                    for (int t = 0; t < T; t++) {
                        float* dout_t = dout + t * out_d;
                        float* x_t = Xd + t * in_d;
                        for (int i = 0; i < out_d; i++)
                            for (int j = 0; j < in_d; j++)
                                dw[i * in_d + j] += dout_t[i] * x_t[j];
                    }
#endif // USE_CUDA backward
                    tape_acc_grad(e->parent1, dw, pw->output->len);
                    tape_acc_grad(e->parent2, dx, px->output->len);
                }
                free(dw); free(dx);
            }
            break;
        }

        case AM_OP_SEQ_RMSNORM: {
            // For each position t: y_t = x_t / rms_t where rms_t = sqrt(mean(x_t^2) + eps)
            if (e->parent1 >= 0) {
                AM_TapeEntry* px = &g_tape.entries[e->parent1];
#ifdef USE_CUDA
                if (px->output->d_data && px->output->gpu_valid) {
                    int Tr = (int)e->aux;
                    int Dr = (int)e->aux2;
                    float* d_gx = gpu_scratch(3, Tr * Dr);
                    float* d_dout_buf = gpu_scratch(0, Tr * Dr);
                    gpu_upload(d_dout_buf, dout, Tr * Dr);
                    gpu_rmsnorm_backward(d_gx, d_dout_buf, px->output->d_data, Tr, Dr);
                    float* gx = (float*)malloc(Tr * Dr * sizeof(float));
                    gpu_download(gx, d_gx, Tr * Dr);
                    tape_acc_grad(e->parent1, gx, Tr * Dr);
                    free(gx);
                    break;
                }
                /* CPU fallback under USE_CUDA: sync parent before reading. */
                ensure_cpu(px->output);
#endif
                int T = (int)e->aux;
                int D = (int)e->aux2;
                float* gx = (float*)calloc((size_t)T * D, sizeof(float));
                if (gx) {
                    float* Xrn = px->output->data;
                    #ifdef _OPENMP
                    #pragma omp parallel for schedule(static) if(T > 32)
                    #endif
                    for (int t = 0; t < T; t++) {
                        float* x_t = Xrn + t * D;
                        float* dout_t = dout + t * D;
                        float ss = 0;
                        for (int d = 0; d < D; d++) ss += x_t[d] * x_t[d];
                        float rms = sqrtf(ss / D + 1e-6f);
                        float rms3 = rms * rms * rms;
                        float sum_dx = 0;
                        for (int d = 0; d < D; d++) sum_dx += dout_t[d] * x_t[d];
                        for (int d = 0; d < D; d++)
                            gx[t * D + d] = (dout_t[d] / rms) - (x_t[d] * sum_dx / (D * rms3));
                    }
                    tape_acc_grad(e->parent1, gx, T * D);
                }
                free(gx);
            }
            break;
        }

        case AM_OP_CAUSAL_ATTN: {
            // Causal self-attention backward
            // Forward: for each i: scores_j = q_i·k_j/sqrt(D), attn = softmax(scores), out_i = sum attn_j * v_j
            if (e->parent1 >= 0 && e->parent2 >= 0 && e->parent3 >= 0) {
                AM_TapeEntry* pq = &g_tape.entries[e->parent1]; // Q
                AM_TapeEntry* pk = &g_tape.entries[e->parent2]; // K
                AM_TapeEntry* pv = &g_tape.entries[e->parent3]; // V
#ifdef USE_CUDA
                ensure_cpu(pq->output);
                ensure_cpu(pk->output);
                ensure_cpu(pv->output);
#endif
                int T = (int)e->aux;
                int D = (int)e->aux2;
                float sc = 1.0f / sqrtf((float)D);
                float* dq = (float*)calloc((size_t)T * D, sizeof(float));
                float* dk = (float*)calloc((size_t)T * D, sizeof(float));
                float* dv = (float*)calloc((size_t)T * D, sizeof(float));
                if (dq && dk && dv) {
                    for (int i = 0; i < T; i++) {
                        float* qi = pq->output->data + i * D;
                        float* dout_i = dout + i * D;
                        // Recompute attention weights for position i
                        float* scores = (float*)calloc(i + 1, sizeof(float));
                        float* attn = (float*)calloc(i + 1, sizeof(float));
                        if (!scores || !attn) { free(scores); free(attn); continue; }
                        float mx = -1e30f;
                        for (int j = 0; j <= i; j++) {
                            float* kj = pk->output->data + j * D;
                            float dot = 0;
                            for (int d = 0; d < D; d++) dot += qi[d] * kj[d];
                            scores[j] = dot * sc;
                            if (scores[j] > mx) mx = scores[j];
                        }
                        float sm = 0;
                        for (int j = 0; j <= i; j++) { attn[j] = expf(scores[j] - mx); sm += attn[j]; }
                        if (sm > 0) for (int j = 0; j <= i; j++) attn[j] /= sm;

                        // d_attn[j] = dout_i · v_j
                        float* d_attn = (float*)calloc(i + 1, sizeof(float));
                        if (d_attn) {
                            for (int j = 0; j <= i; j++) {
                                float* vj = pv->output->data + j * D;
                                for (int d = 0; d < D; d++) d_attn[j] += dout_i[d] * vj[d];
                            }
                            // dv[j] += attn[j] * dout_i
                            for (int j = 0; j <= i; j++) {
                                float* dvj = dv + j * D;
                                for (int d = 0; d < D; d++) dvj[d] += attn[j] * dout_i[d];
                            }
                            // softmax backward: dscore[j] = attn[j] * (d_attn[j] - sum(d_attn * attn))
                            float dot_da = 0;
                            for (int j = 0; j <= i; j++) dot_da += d_attn[j] * attn[j];
                            for (int j = 0; j <= i; j++) {
                                float ds = attn[j] * (d_attn[j] - dot_da) * sc;
                                // dq_i += ds * k_j, dk_j += ds * q_i
                                float* kj = pk->output->data + j * D;
                                for (int d = 0; d < D; d++) {
                                    dq[i * D + d] += ds * kj[d];
                                    dk[j * D + d] += ds * qi[d];
                                }
                            }
                        }
                        free(scores); free(attn); free(d_attn);
                    }
                    tape_acc_grad(e->parent1, dq, T * D);
                    tape_acc_grad(e->parent2, dk, T * D);
                    tape_acc_grad(e->parent3, dv, T * D);
                }
                free(dq); free(dk); free(dv);
            }
            break;
        }

        case AM_OP_MH_CAUSAL_ATTN: {
            // Multi-head causal self-attention backward
            // aux = T, aux2 = head_dim. D recovered from output->len / T.
            if (e->parent1 >= 0 && e->parent2 >= 0 && e->parent3 >= 0) {
                AM_TapeEntry* pq = &g_tape.entries[e->parent1]; // Q
                AM_TapeEntry* pk = &g_tape.entries[e->parent2]; // K
                AM_TapeEntry* pv = &g_tape.entries[e->parent3]; // V
#ifdef USE_CUDA
                ensure_cpu(pq->output);
                ensure_cpu(pk->output);
                ensure_cpu(pv->output);
#endif
                int T = (int)e->aux;
                int head_dim = (int)e->aux2;
                int D = e->output->len / T;
                int n_heads = D / head_dim;
                float sc = 1.0f / sqrtf((float)head_dim);
                float* dq = (float*)calloc((size_t)T * D, sizeof(float));
                float* dk = (float*)calloc((size_t)T * D, sizeof(float));
                float* dv = (float*)calloc((size_t)T * D, sizeof(float));
                if (dq && dk && dv) {
                    for (int h = 0; h < n_heads; h++) {
                        int ho = h * head_dim;
                        for (int i = 0; i < T; i++) {
                            float* qi = pq->output->data + i * D + ho;
                            float* dout_i = dout + i * D + ho;
                            float* scores = (float*)calloc(i + 1, sizeof(float));
                            float* attn = (float*)calloc(i + 1, sizeof(float));
                            if (!scores || !attn) { free(scores); free(attn); continue; }
                            float mx = -1e30f;
                            for (int j = 0; j <= i; j++) {
                                float* kj = pk->output->data + j * D + ho;
                                float dot = 0;
                                for (int d = 0; d < head_dim; d++) dot += qi[d] * kj[d];
                                scores[j] = dot * sc;
                                if (scores[j] > mx) mx = scores[j];
                            }
                            float sm = 0;
                            for (int j = 0; j <= i; j++) { attn[j] = expf(scores[j] - mx); sm += attn[j]; }
                            if (sm > 0) for (int j = 0; j <= i; j++) attn[j] /= sm;
                            float* d_attn = (float*)calloc(i + 1, sizeof(float));
                            if (d_attn) {
                                for (int j = 0; j <= i; j++) {
                                    float* vj = pv->output->data + j * D + ho;
                                    for (int d = 0; d < head_dim; d++) d_attn[j] += dout_i[d] * vj[d];
                                }
                                for (int j = 0; j <= i; j++) {
                                    float* dvj = dv + j * D + ho;
                                    for (int d = 0; d < head_dim; d++) dvj[d] += attn[j] * dout_i[d];
                                }
                                float dot_da = 0;
                                for (int j = 0; j <= i; j++) dot_da += d_attn[j] * attn[j];
                                for (int j = 0; j <= i; j++) {
                                    float ds = attn[j] * (d_attn[j] - dot_da) * sc;
                                    float* kj = pk->output->data + j * D + ho;
                                    for (int d = 0; d < head_dim; d++) {
                                        dq[i * D + ho + d] += ds * kj[d];
                                        dk[j * D + ho + d] += ds * qi[d];
                                    }
                                }
                            }
                            free(scores); free(attn); free(d_attn);
                        }
                    }
                    tape_acc_grad(e->parent1, dq, T * D);
                    tape_acc_grad(e->parent2, dk, T * D);
                    tape_acc_grad(e->parent3, dv, T * D);
                }
                free(dq); free(dk); free(dv);
            }
            break;
        }

        case AM_OP_SEQ_CROSSENT: {
            // loss = mean over t of -log(softmax(logits_t)[target_t])
            // d_logits[t*V+j] = (softmax[j] - one_hot[target]) / T
            if (e->parent1 >= 0) {
                AM_TapeEntry* pl = &g_tape.entries[e->parent1]; // logits
                AM_TapeEntry* pt = &g_tape.entries[e->parent2]; // targets
#ifdef USE_CUDA
                ensure_cpu(pl->output);
                if (pt) ensure_cpu(pt->output);
#endif
                int T = (int)e->aux;
                int V = (int)e->aux2;
                float* dl = (float*)calloc((size_t)T * V, sizeof(float));
                if (dl && pt) {
                    for (int t = 0; t < T; t++) {
                        float* logits_t = pl->output->data + t * V;
                        int target = (int)pt->output->data[t];
                        if (target < 0 || target >= V) target = 0;
                        float mx = logits_t[0];
                        for (int j = 1; j < V; j++)
                            if (logits_t[j] > mx) mx = logits_t[j];
                        float sum = 0;
                        for (int j = 0; j < V; j++) {
                            dl[t * V + j] = expf(logits_t[j] - mx);
                            sum += dl[t * V + j];
                        }
                        for (int j = 0; j < V; j++) dl[t * V + j] /= sum;
                        dl[t * V + target] -= 1.0f;
                        // Scale by dout[0] / T
                        float s = dout[0] / T;
                        for (int j = 0; j < V; j++) dl[t * V + j] *= s;
                    }
                    tape_acc_grad(e->parent1, dl, T * V);
                }
                free(dl);
            }
            break;
        }

        default:
            break;
        }
    }
}

// Adam optimizer step: update all parameters using their accumulated gradients
void am_tape_adam_step(float lr) {
    float beta1 = 0.9f, beta2 = 0.999f, eps = 1e-8f;
    int param_idx = 0;

    for (int i = 0; i < g_tape.count && param_idx < g_tape.n_params; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->grad) continue;

        AM_AdamState* as = &g_tape.adam[param_idx];
        if (!as->m || !as->v) { param_idx++; continue; }

        as->t++;
        int n = e->output->len;
        if (as->m->len < n) n = as->m->len;

        for (int j = 0; j < n; j++) {
            float g = e->grad->data[j];
            as->m->data[j] = beta1 * as->m->data[j] + (1.0f - beta1) * g;
            as->v->data[j] = beta2 * as->v->data[j] + (1.0f - beta2) * g * g;
            float m_hat = as->m->data[j] / (1.0f - powf(beta1, (float)as->t));
            float v_hat = as->v->data[j] / (1.0f - powf(beta2, (float)as->t));
            e->output->data[j] -= lr * m_hat / (sqrtf(v_hat) + eps);
        }
        param_idx++;
    }
}

// AdamW optimizer step: Adam with decoupled weight decay
// Matches PyTorch AdamW: weight decay applied directly to params, not through gradient
void am_tape_adamw_step(float lr, float weight_decay, float beta1, float beta2) {
    float eps = 1e-8f;
    int param_idx = 0;

    for (int i = 0; i < g_tape.count && param_idx < g_tape.n_params; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->grad) continue;

        AM_AdamState* as = &g_tape.adam[param_idx];
        if (!as->m || !as->v) { param_idx++; continue; }

        as->t++;
        int n = e->output->len;
        if (as->m->len < n) n = as->m->len;

        float bc1 = 1.0f - powf(beta1, (float)as->t);
        float bc2 = 1.0f - powf(beta2, (float)as->t);

        float wd = (e->no_decay) ? 0.0f : weight_decay;
        for (int j = 0; j < n; j++) {
            // Decoupled weight decay (AdamW): applied to param, not gradient
            // Skipped for embeddings (no_decay=1)
            if (wd > 0.0f)
                e->output->data[j] -= lr * wd * e->output->data[j];

            float g = e->grad->data[j];
            as->m->data[j] = beta1 * as->m->data[j] + (1.0f - beta1) * g;
            as->v->data[j] = beta2 * as->v->data[j] + (1.0f - beta2) * g * g;
            float m_hat = as->m->data[j] / bc1;
            float v_hat = as->v->data[j] / bc2;
            e->output->data[j] -= lr * m_hat / (sqrtf(v_hat) + eps);
        }
        param_idx++;
    }
}

// Gradient clipping by global norm (like torch.nn.utils.clip_grad_norm_)
// Returns the total gradient norm before clipping
float am_tape_clip_grads(float max_norm) {
    // First pass: compute global gradient norm
    float total_norm_sq = 0.0f;
    for (int i = 0; i < g_tape.count; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->grad) continue;
        int n = e->output->len;
        if (e->grad->len < n) n = e->grad->len;
        for (int j = 0; j < n; j++) {
            float g = e->grad->data[j];
            total_norm_sq += g * g;
        }
    }
    float total_norm = sqrtf(total_norm_sq);

    // Second pass: scale gradients if norm exceeds max_norm
    if (total_norm > max_norm) {
        float scale = max_norm / (total_norm + 1e-6f);
        for (int i = 0; i < g_tape.count; i++) {
            AM_TapeEntry* e = &g_tape.entries[i];
            if (!e->is_param || !e->grad) continue;
            int n = e->output->len;
            if (e->grad->len < n) n = e->grad->len;
            for (int j = 0; j < n; j++) {
                e->grad->data[j] *= scale;
            }
        }
    }
    return total_norm;
}

// ── Gradient accumulation ─────────────────────────────────────────────────────
// TAPE ACCUM_GRADS: after BACKWARD, save param grads into acc_grad buffer (additive)
// TAPE APPLY_ACCUM N: divide acc_grad by N, copy into tape entry grads, zero acc_grad

void am_tape_accum_grads(void) {
    int param_idx = 0;
    for (int i = 0; i < g_tape.count && param_idx < g_tape.n_params; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->grad) continue;
        AM_AdamState* as = &g_tape.adam[param_idx];
        int n = e->output->len;
        // Allocate acc_grad on first use
        if (!as->acc_grad) {
            as->acc_grad = am_array_new(n);
        } else if (as->acc_grad->len < n) {
            am_array_free(as->acc_grad);
            as->acc_grad = am_array_new(n);
        }
        // Accumulate: acc_grad += grad
        for (int j = 0; j < n && j < as->acc_grad->len; j++) {
            as->acc_grad->data[j] += e->grad->data[j];
        }
        param_idx++;
    }
}

void am_tape_apply_accum(int n_accum) {
    float scale = (n_accum > 1) ? 1.0f / (float)n_accum : 1.0f;
    int param_idx = 0;
    for (int i = 0; i < g_tape.count && param_idx < g_tape.n_params; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param) continue;
        AM_AdamState* as = &g_tape.adam[param_idx];
        if (as->acc_grad) {
            int n = e->output->len;
            if (as->acc_grad->len < n) n = as->acc_grad->len;
            // Ensure grad exists
            if (!e->grad) e->grad = am_array_new(n);
            // Copy averaged accumulated grad into tape entry
            for (int j = 0; j < n; j++) {
                e->grad->data[j] = as->acc_grad->data[j] * scale;
                as->acc_grad->data[j] = 0.0f; // zero for next round
            }
        }
        param_idx++;
    }
}

// ── Chuck optimizer step ──────────────────────────────────────────────────────
// Self-aware Adam: θ -= (α × λ × λ_l) × m̂/(√v̂ + ε) + η
// Requires loss_val from the current step to track trends.

static float chuck_ring_avg(const float* buf, int pos, int full, int start, int count) {
    // Average 'count' entries starting from 'start' in ring buffer
    int len = full ? CHUCK_WINDOW : pos;
    if (len == 0 || count == 0) return 0.0f;
    float sum = 0.0f;
    int actual = 0;
    for (int i = 0; i < count && i < len; i++) {
        int idx = (start + i) % CHUCK_WINDOW;
        if (idx < len || full) { sum += buf[idx]; actual++; }
    }
    return actual > 0 ? sum / actual : 0.0f;
}

// Simple xorshift32 for stagnation noise (no stdlib dependency)
static uint32_t chuck_rng_state = 2463534242u;
static float chuck_randn(void) {
    // Box-Muller-ish from uniform via xorshift
    chuck_rng_state ^= chuck_rng_state << 13;
    chuck_rng_state ^= chuck_rng_state >> 17;
    chuck_rng_state ^= chuck_rng_state << 5;
    float u = (float)(chuck_rng_state) / 4294967296.0f;
    // Approximate Gaussian: 12 uniforms - 6 (central limit), simplified to 2u-1
    return 2.0f * u - 1.0f;
}

void am_tape_chuck_step(float lr, float loss_val) {
    float beta1 = 0.9f, beta2 = 0.999f, eps = 1e-8f;

    // ── Level 1: Global loss trend → λ ──
    AM_ChuckState* cs = &g_tape.chuck;
    if (!cs->initialized) {
        cs->dampen = 1.0f;
        cs->noise = 0.0f;
        cs->lr_scale = 1.0f;
        cs->best_macro = 1e9f;
        cs->initialized = 1;
    }
    // EMA smoothing: filters batch-to-batch noise for mini-batch SGD
    if (cs->loss_ema == 0.0f) cs->loss_ema = loss_val;
    else cs->loss_ema = 0.99f * cs->loss_ema + 0.01f * loss_val;
    // Record smoothed loss into ring buffer
    cs->loss_hist[cs->pos] = cs->loss_ema;
    cs->pos = (cs->pos + 1) % CHUCK_WINDOW;
    if (cs->pos == 0) cs->full = 1;

    int len = cs->full ? CHUCK_WINDOW : cs->pos;
    if (len >= 8) {
        // Compare recent quarter vs oldest quarter
        int q = len / 4;
        if (q < 1) q = 1;
        int old_start = cs->full ? ((cs->pos) % CHUCK_WINDOW) : 0;
        int recent_start = cs->full ? ((cs->pos - q + CHUCK_WINDOW) % CHUCK_WINDOW)
                                    : (cs->pos - q);
        float old_avg = chuck_ring_avg(cs->loss_hist, cs->pos, cs->full, old_start, q);
        float recent_avg = chuck_ring_avg(cs->loss_hist, cs->pos, cs->full, recent_start, q);

        if (old_avg > eps) {
            float trend = (recent_avg - old_avg) / old_avg;
            // Symmetric thresholds (synced with PyTorch: 0.02 / -0.02)
            if (trend > CHUCK_TREND_BRAKE) cs->dampen *= CHUCK_DAMP_DOWN; // loss rising → dampen
            if (trend < CHUCK_TREND_PUSH)  cs->dampen *= CHUCK_DAMP_UP;   // loss falling → boost

            // ── Level 3: Stagnation escape ──
            if (fabsf(trend) < CHUCK_STAG_THRESH) {
                cs->stag++;
                if (cs->stag >= CHUCK_STAG_STEPS) {
                    cs->noise = CHUCK_NOISE_MAG;
                    cs->stag = 0;  // reset counter (PyTorch behavior)
                }
            } else {
                cs->stag = 0;
                cs->noise *= CHUCK_NOISE_DECAY;  // exponential decay (was: reset to 0)
            }
        }
    }
    // Mean reversion: pull dampen toward 1.0 (prevents drift)
    cs->dampen = CHUCK_MEAN_REVERT * cs->dampen + (1.0f - CHUCK_MEAN_REVERT) * 1.0f;
    // Clamp global dampen
    if (cs->dampen < CHUCK_DAMP_LO) cs->dampen = CHUCK_DAMP_LO;
    if (cs->dampen > CHUCK_DAMP_HI) cs->dampen = CHUCK_DAMP_HI;

    // ── Level 9: Multi-scale awareness (macro patience) ──
    // Slow EMA (α=0.001) tracks epoch-scale loss trend.
    // Every CHUCK_MACRO_INT steps: patience check → LR decay if stagnant.
    cs->global_step++;
    if (cs->macro_ema == 0.0f) cs->macro_ema = loss_val;
    else cs->macro_ema = 0.999f * cs->macro_ema + 0.001f * loss_val;

    if (cs->global_step % CHUCK_MACRO_INT == 0 && cs->global_step > CHUCK_WINDOW) {
        if (cs->macro_ema > cs->best_macro * 0.999f) {
            cs->macro_stag++;
            if (cs->macro_stag >= CHUCK_MACRO_PAT) {
                cs->lr_scale *= CHUCK_MACRO_DECAY;
                if (cs->lr_scale < 0.05f) cs->lr_scale = 0.05f;
                cs->macro_stag = 0;
            }
        } else {
            cs->best_macro = cs->macro_ema;
            cs->macro_stag = 0;
            // LR recovery when improving (PyTorch: lr_scale *= 1.2)
            if (cs->lr_scale < 1.0f) {
                cs->lr_scale *= 1.2f;
                if (cs->lr_scale > 1.0f) cs->lr_scale = 1.0f;
            }
        }
    }

    float global_lambda = cs->dampen;
    float noise_mag = cs->noise;

    // ── Level 2: Per-param gradient norm → λ_l + freeze + Adam update ──
    int param_idx = 0;
    for (int i = 0; i < g_tape.count && param_idx < g_tape.n_params; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->grad) continue;

        AM_AdamState* as = &g_tape.adam[param_idx];
        AM_ChuckParamState* cp = &g_tape.chuck_params[param_idx];

        // Initialize per-param state on first encounter
        if (cp->dampen == 0.0f) cp->dampen = 1.0f;

        // Check frozen
        if (cp->frozen) { param_idx++; continue; }

        if (!as->m || !as->v) { param_idx++; continue; }

        // Compute gradient norm for this param
        int n = e->output->len;
        if (as->m->len < n) n = as->m->len;
        float gnorm = 0.0f;
        for (int j = 0; j < n; j++) gnorm += e->grad->data[j] * e->grad->data[j];
        gnorm = sqrtf(gnorm);

        // Record grad norm into per-param ring buffer
        cp->grad_hist[cp->pos] = gnorm;
        cp->pos = (cp->pos + 1) % CHUCK_WINDOW;
        if (cp->pos == 0) cp->full = 1;

        int plen = cp->full ? CHUCK_WINDOW : cp->pos;
        if (plen >= 8) {
            int q = plen / 4;
            if (q < 1) q = 1;
            int old_start = cp->full ? ((cp->pos) % CHUCK_WINDOW) : 0;
            int recent_start = cp->full ? ((cp->pos - q + CHUCK_WINDOW) % CHUCK_WINDOW)
                                        : (cp->pos - q);
            float old_gn = chuck_ring_avg(cp->grad_hist, cp->pos, cp->full, old_start, q);
            float recent_gn = chuck_ring_avg(cp->grad_hist, cp->pos, cp->full, recent_start, q);

            if (old_gn > eps) {
                float gtrend = (recent_gn - old_gn) / old_gn;
                // Per-param: 0.05 thresholds, symmetric (synced with PyTorch)
                if (gtrend > 0.05f)  cp->dampen *= CHUCK_DAMP_UP;    // grad rising → boost
                if (gtrend < -0.05f) cp->dampen *= CHUCK_DAMP_DOWN;  // grad settling → ease
            }

            // Freeze check: grad norm tiny for CHUCK_STAG_STEPS consecutive
            if (gnorm < CHUCK_FREEZE_THRESH) {
                cp->stag++;
                if (cp->stag >= CHUCK_STAG_STEPS) cp->frozen = 1;
            } else {
                cp->stag = 0;
            }

            // Per-param mean reversion (prevents drift)
            cp->dampen = CHUCK_MEAN_REVERT * cp->dampen + (1.0f - CHUCK_MEAN_REVERT) * 1.0f;
            if (cp->dampen < CHUCK_DAMP_LO) cp->dampen = CHUCK_DAMP_LO;
            if (cp->dampen > CHUCK_DAMP_HI) cp->dampen = CHUCK_DAMP_HI;
        }

        // ── Adam update with Chuck modulation ──
        float param_lambda = cp->dampen;
        float effective_lr = lr * global_lambda * param_lambda * cs->lr_scale;

        as->t++;
        for (int j = 0; j < n; j++) {
            float g = e->grad->data[j];
            as->m->data[j] = beta1 * as->m->data[j] + (1.0f - beta1) * g;
            as->v->data[j] = beta2 * as->v->data[j] + (1.0f - beta2) * g * g;
            float m_hat = as->m->data[j] / (1.0f - powf(beta1, (float)as->t));
            float v_hat = as->v->data[j] / (1.0f - powf(beta2, (float)as->t));
            float update = effective_lr * m_hat / (sqrtf(v_hat) + eps);
            // Stagnation noise η
            if (noise_mag > 0.0f) update += noise_mag * chuck_randn();
            e->output->data[j] -= update;
        }
        param_idx++;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SAVE / LOAD — persist trainable params (tape entries with is_param=1)
// Binary format: magic(4) | n_params(4) | for each: len(4) | data[len * float]
// Tape-order dependent: load into a model with the same param layout.
// ═══════════════════════════════════════════════════════════════════════════════

#define AM_SAVE_MAGIC 0x414D4C45u   // 'AMLE' — AML Essence

int am_tape_save(const char* path) {
    if (!path) return -1;
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = AM_SAVE_MAGIC;
    int32_t n = g_tape.n_params;
    if (fwrite(&magic, 4, 1, f) != 1 || fwrite(&n, 4, 1, f) != 1) {
        fclose(f); return -1;
    }
    int written = 0;
    for (int i = 0; i < g_tape.count && written < n; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->output) continue;
        int32_t len = e->output->len;
        if (fwrite(&len, 4, 1, f) != 1 ||
            fwrite(e->output->data, sizeof(float), (size_t)len, f) != (size_t)len) {
            fclose(f); return -1;
        }
        written++;
    }
    fclose(f);
    return written == n ? 0 : -1;
}

int am_tape_load(const char* path) {
    if (!path) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t magic = 0;
    int32_t  n = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != AM_SAVE_MAGIC) { fclose(f); return -1; }
    if (fread(&n, 4, 1, f) != 1 || n <= 0) { fclose(f); return -1; }
    if (n != g_tape.n_params) { fclose(f); return -1; }  // layout mismatch
    int loaded = 0;
    for (int i = 0; i < g_tape.count && loaded < n; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->output) continue;
        int32_t len = 0;
        if (fread(&len, 4, 1, f) != 1 || len != e->output->len) {
            fclose(f); return -1;
        }
        if (fread(e->output->data, sizeof(float), (size_t)len, f) != (size_t)len) {
            fclose(f); return -1;
        }
        loaded++;
    }
    fclose(f);
    return loaded == n ? 0 : -1;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LR SCHEDULE — cosine / step / linear, all with optional linear warmup
// ═══════════════════════════════════════════════════════════════════════════════

AM_Schedule am_schedule_cosine(float base_lr, int warmup_steps, int total_steps, float min_lr) {
    AM_Schedule s = {0};
    s.type = AM_SCHED_COSINE;
    s.base_lr = base_lr;
    s.min_lr = min_lr;
    s.warmup_steps = warmup_steps;
    s.total_steps = total_steps > 0 ? total_steps : 1;
    return s;
}

AM_Schedule am_schedule_step(float base_lr, int warmup_steps, int step_size, float gamma) {
    AM_Schedule s = {0};
    s.type = AM_SCHED_STEP;
    s.base_lr = base_lr;
    s.warmup_steps = warmup_steps;
    s.step_size = step_size > 0 ? step_size : 1;
    s.step_gamma = gamma > 0 ? gamma : 0.1f;
    return s;
}

AM_Schedule am_schedule_linear(float base_lr, int warmup_steps, int total_steps, float min_lr) {
    AM_Schedule s = {0};
    s.type = AM_SCHED_LINEAR;
    s.base_lr = base_lr;
    s.min_lr = min_lr;
    s.warmup_steps = warmup_steps;
    s.total_steps = total_steps > 0 ? total_steps : 1;
    return s;
}

float am_schedule_get_lr(AM_Schedule* s) {
    if (!s) return 0.001f;
    int step = s->current_step++;
    float lr = s->base_lr;

    // Linear warmup from min_lr to base_lr over warmup_steps
    if (step < s->warmup_steps && s->warmup_steps > 0) {
        float t = (float)step / (float)s->warmup_steps;
        return s->min_lr + t * (s->base_lr - s->min_lr);
    }

    int decay_step = step - s->warmup_steps;

    switch (s->type) {
    case AM_SCHED_COSINE: {
        int decay_total = s->total_steps - s->warmup_steps;
        if (decay_total <= 0) return lr;
        float progress = (float)decay_step / (float)decay_total;
        if (progress > 1.0f) progress = 1.0f;
        lr = s->min_lr + 0.5f * (s->base_lr - s->min_lr) * (1.0f + cosf(3.14159265f * progress));
        break;
    }
    case AM_SCHED_STEP: {
        int n_decays = decay_step / s->step_size;
        lr = s->base_lr * powf(s->step_gamma, (float)n_decays);
        break;
    }
    case AM_SCHED_LINEAR: {
        int decay_total = s->total_steps - s->warmup_steps;
        if (decay_total <= 0) return lr;
        float progress = (float)decay_step / (float)decay_total;
        if (progress > 1.0f) progress = 1.0f;
        lr = s->base_lr - progress * (s->base_lr - s->min_lr);
        break;
    }
    default:
        break;
    }
    return lr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// NaN/Inf GUARD — scan all param grads, zero them if any NaN/Inf detected,
// adjust dynamic loss_scale.
// ═══════════════════════════════════════════════════════════════════════════════

AM_NanGuard am_nan_guard_new(void) {
    AM_NanGuard g = {0};
    g.loss_scale = 1.0f;
    g.scale_factor = 2.0f;
    g.scale_window = 100;
    return g;
}

int am_nan_guard_check(AM_NanGuard* guard) {
    if (!guard) return 1;
    int has_nan = 0;

    for (int i = 0; i < g_tape.count && !has_nan; i++) {
        AM_TapeEntry* e = &g_tape.entries[i];
        if (!e->is_param || !e->grad) continue;
        int n = e->grad->len;
        for (int j = 0; j < n; j++) {
            float gv = e->grad->data[j];
            // NaN: gv != gv. Inf: +/- infinity.
            if (gv != gv || gv == 1.0f/0.0f || gv == -1.0f/0.0f) {
                has_nan = 1;
                break;
            }
        }
    }

    if (has_nan) {
        for (int i = 0; i < g_tape.count; i++) {
            AM_TapeEntry* e = &g_tape.entries[i];
            if (!e->is_param || !e->grad) continue;
            memset(e->grad->data, 0, (size_t)e->grad->len * sizeof(float));
        }
        guard->loss_scale /= guard->scale_factor;
        if (guard->loss_scale < 1.0f) guard->loss_scale = 1.0f;
        guard->total_nan_count++;
        guard->skipped_steps++;
        guard->stable_steps = 0;
        return 0;
    }

    guard->stable_steps++;
    if (guard->scale_window > 0 && guard->stable_steps >= guard->scale_window) {
        guard->loss_scale *= guard->scale_factor;
        guard->stable_steps = 0;
    }
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TRAINING MODE — global flag. Dropout and similar ops consult it.
// ═══════════════════════════════════════════════════════════════════════════════

static int g_training_mode = 1;  // default: training

void am_train_mode(int training) { g_training_mode = training ? 1 : 0; }
int  am_is_training(void)        { return g_training_mode; }

// Find tape entry index by array pointer (-1 if not found)
static int tape_find_entry(AM_Array* arr) {
    if (!arr) return -1;
    for (int i = g_tape.count - 1; i >= 0; i--) {
        if (g_tape.entries[i].output && g_tape.entries[i].output->data == arr->data)
            return i;
    }
    return -1;
}

// Ensure array is on tape. If not found, record as a non-trainable leaf.
// Returns entry index.
static int tape_ensure_entry(AM_Array* arr) {
    if (!arr || !g_tape.active) return -1;
    int idx = tape_find_entry(arr);
    if (idx >= 0) return idx;
    // Record as leaf (OP_NONE, not a param — just for backward data access)
    return am_tape_record(arr, AM_OP_NONE, -1, -1, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ASYNC — SPAWN/AWAIT/CHANNEL (v4.0 Phase 4)
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef AM_ASYNC_DISABLED

// Thread argument: holds the AML script to execute
typedef struct {
    char* script;       // heap-allocated AML script text
    int   slot_idx;     // index into g_spawns
    char  base_dir[256]; // source origin travels with the worker
    char  source_path[AML_MAX_SOURCE_PATH];
    AML_Symtab globals; // launch-time snapshot; worker takes ownership
} AM_SpawnArg;

// Thread entry point: runs an AML script in its own context
static void* am_spawn_thread_fn(void* arg) {
    AM_SpawnArg* sa = (AM_SpawnArg*)arg;

    // Execute the script with the caller's source origin in this thread.
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", sa->base_dir);
    snprintf(g_source_path, sizeof(g_source_path), "%s", sa->source_path);
    g_persistent_globals = sa->globals;
    memset(&sa->globals, 0, sizeof(sa->globals));
    g_persistent_enabled = 1;
    int rc = am_exec(sa->script);
    am_persistent_mode(0);

    // Mark slot as done
    pthread_mutex_lock(&g_spawn_mutex);
    if (sa->slot_idx >= 0 && sa->slot_idx < AM_MAX_SPAWNS) {
        g_spawns[sa->slot_idx].active = 0;
        g_spawns[sa->slot_idx].result = rc;
        snprintf(g_spawn_errors[sa->slot_idx], sizeof(g_spawn_errors[0]),
                 "%s", g_error);
    }
    pthread_mutex_unlock(&g_spawn_mutex);

    free(sa->script);
    free(sa);
    return NULL;
}

// Launch a spawn: create thread running the given AML script
static int am_spawn_launch_globals(const char* name, const char* script,
                                   const AML_Symtab* globals) {
    if (!name || !script) return -1;
    if (!g_am_initialized) am_init();
    AM_SpawnArg* arg = (AM_SpawnArg*)calloc(1, sizeof(AM_SpawnArg));
    if (!arg) return -1;
    arg->script = strdup(script);
    if (!arg->script) { free(arg); return -1; }
    if (globals && symtab_snapshot(&arg->globals, globals)) {
        free(arg->script); free(arg); return -1;
    }
    snprintf(arg->base_dir, sizeof(arg->base_dir), "%s", g_base_dir);
    snprintf(arg->source_path, sizeof(arg->source_path), "%s", g_source_path);

    pthread_mutex_lock(&g_spawn_mutex);
    if (g_spawn_count >= AM_MAX_SPAWNS) {
        pthread_mutex_unlock(&g_spawn_mutex);
        symtab_clear_arrays(&arg->globals);
        free(arg->script); free(arg); return -1;
    }
    int idx = g_spawn_count;
    arg->slot_idx = idx;
    snprintf(g_spawns[idx].name, AM_SPAWN_NAME_LEN, "%s", name);
    g_spawns[idx].active = 1;
    g_spawns[idx].joined = 0;
    g_spawns[idx].result = 0;
    g_spawn_errors[idx][0] = 0;
    int err = pthread_create(&g_spawn_threads[idx], NULL, am_spawn_thread_fn, arg);
    if (err != 0) {
        symtab_clear_arrays(&arg->globals);
        free(arg->script);
        free(arg);
        g_spawns[idx].active = 0;
        pthread_mutex_unlock(&g_spawn_mutex);
        return -1;
    }

    g_spawn_count++;
    pthread_mutex_unlock(&g_spawn_mutex);
    return idx;
}

int am_spawn_launch(const char* name, const char* script) {
    return am_spawn_launch_globals(name, script,
                                   g_persistent_enabled ? &g_persistent_globals : NULL);
}
// Join transfers the completed worker's diagnostic to the awaiting thread.
static int am_spawn_join(int i) {
    int err = pthread_join(g_spawn_threads[i], NULL);
    if (err) {
        snprintf(g_error, sizeof(g_error), "cannot await %s: %s",
                 g_spawns[i].name, strerror(err));
        return -1;
    }
    g_spawns[i].joined = 1;
    if (g_spawns[i].result != 0)
        snprintf(g_error, sizeof(g_error), "%s",
                 g_spawn_errors[i][0] ? g_spawn_errors[i] : "spawned program failed");
    return g_spawns[i].result;
}

// Await a specific spawn by name. Returns result code.
int am_spawn_await(const char* name) {
    for (int i = 0; i < g_spawn_count; i++) {
        if (strcmp(g_spawns[i].name, name) == 0 && !g_spawns[i].joined) {
            return am_spawn_join(i);
        }
    }
    snprintf(g_error, sizeof(g_error), "no unjoined spawn: %s", name);
    return -1;
}

// Join every pending worker, preserving the first failure if several fail.
static int am_spawn_join_all(void) {
    int rc = 0;
    char first_error[256] = {0};
    for (int i = 0; i < g_spawn_count; i++) {
        if (!g_spawns[i].joined) {
            int result = am_spawn_join(i);
            if (result != 0 && rc == 0) {
                rc = result;
                snprintf(first_error, sizeof(first_error), "%s", g_error);
            }
        }
    }
    if (rc != 0) snprintf(g_error, sizeof(g_error), "%s", first_error);
    return rc;
}

// Public void API retains its signature; failures remain in am_get_error().
void am_spawn_await_all(void) { (void)am_spawn_join_all(); }

int am_spawn_count(void) {
    int n = 0;
    for (int i = 0; i < g_spawn_count; i++)
        if (g_spawns[i].active) n++;
    return n;
}

// Reset spawn state (called from am_init)
static void am_spawn_reset(void) {
    am_spawn_await_all();
    g_spawn_count = 0;
    memset(g_spawns, 0, sizeof(g_spawns));
    memset(g_spawn_errors, 0, sizeof(g_spawn_errors));
}

// --- CHANNEL ---

// Find channel by name (-1 if not found)
static int channel_find(const char* name) {
    for (int i = 0; i < g_channel_count; i++)
        if (g_channels[i].active && strcmp(g_channels[i].name, name) == 0)
            return i;
    return -1;
}

// Create a channel with given capacity
int am_channel_create(const char* name, int capacity) {
    if (g_channel_count >= AM_MAX_CHANNELS || capacity <= 0) return -1;
    if (capacity > AM_CHANNEL_BUF) capacity = AM_CHANNEL_BUF;

    int idx = g_channel_count;
    memset(&g_channels[idx], 0, sizeof(AM_ChannelSlot));
    snprintf(g_channels[idx].name, AM_SPAWN_NAME_LEN, "%s", name);
    g_channels[idx].capacity = capacity;
    g_channels[idx].active = 1;
    g_channel_count++;
    return idx;
}

// Write a float to a channel (blocking if full)
int am_channel_write(const char* name, float value) {
    pthread_mutex_lock(&g_channel_mutex);
    int idx = channel_find(name);
    if (idx < 0) { pthread_mutex_unlock(&g_channel_mutex); return -1; }

    // Wait until not full (with timeout to prevent deadlock)
    int tries = 0;
    while (g_channels[idx].count >= g_channels[idx].capacity && tries < 1000) {
        pthread_mutex_unlock(&g_channel_mutex);
        struct timespec ts = {0, 1000000}; // 1ms
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&g_channel_mutex);
        tries++;
    }
    if (g_channels[idx].count >= g_channels[idx].capacity) {
        pthread_mutex_unlock(&g_channel_mutex);
        return -1; // channel full, timeout
    }

    g_channels[idx].data[g_channels[idx].tail] = value;
    g_channels[idx].tail = (g_channels[idx].tail + 1) % g_channels[idx].capacity;
    g_channels[idx].count++;
    pthread_cond_broadcast(&g_channel_cond);
    pthread_mutex_unlock(&g_channel_mutex);
    return 0;
}

// How many values are queued. -1 if there is no such channel. A host that schedules needs to
// be able to ask before it commits to a read.
int am_channel_depth(const char* name) {
    pthread_mutex_lock(&g_channel_mutex);
    int idx = channel_find(name);
    int n = (idx < 0) ? -1 : g_channels[idx].count;
    pthread_mutex_unlock(&g_channel_mutex);
    return n;
}

// Non-blocking read: takes one value if there is one, otherwise returns -1 at once. The polling
// am_channel_read below waits up to a second, which is correct for a thread and ruinous for a
// single-threaded host running the program in a scheduled quantum.
int am_channel_try_read(const char* name, float* out) {
    pthread_mutex_lock(&g_channel_mutex);
    int idx = channel_find(name);
    if (idx < 0 || g_channels[idx].count == 0) {
        pthread_mutex_unlock(&g_channel_mutex);
        return -1;
    }
    *out = g_channels[idx].data[g_channels[idx].head];
    g_channels[idx].head = (g_channels[idx].head + 1) % g_channels[idx].capacity;
    g_channels[idx].count--;
    pthread_cond_broadcast(&g_channel_cond);
    pthread_mutex_unlock(&g_channel_mutex);
    return 0;
}

// Read a float from a channel. Blocking if empty: 1000 x 1ms sleeps plus mutex overhead,
// which is ~2s of wall time in practice, not the ~1s the loop count suggests.
int am_channel_read(const char* name, float* out) {
    pthread_mutex_lock(&g_channel_mutex);
    int idx = channel_find(name);
    if (idx < 0) { pthread_mutex_unlock(&g_channel_mutex); return -1; }

    // Wait until not empty (with timeout)
    int tries = 0;
    while (g_channels[idx].count == 0 && tries < 1000) {
        pthread_mutex_unlock(&g_channel_mutex);
        struct timespec ts = {0, 1000000}; // 1ms
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&g_channel_mutex);
        tries++;
    }
    if (g_channels[idx].count == 0) {
        pthread_mutex_unlock(&g_channel_mutex);
        return -1; // channel empty, timeout
    }

    *out = g_channels[idx].data[g_channels[idx].head];
    g_channels[idx].head = (g_channels[idx].head + 1) % g_channels[idx].capacity;
    g_channels[idx].count--;
    pthread_cond_broadcast(&g_channel_cond);
    pthread_mutex_unlock(&g_channel_mutex);
    return 0;
}

int am_channel_count(void) {
    int n = 0;
    for (int i = 0; i < g_channel_count; i++)
        if (g_channels[i].active) n++;
    return n;
}

void am_channel_close_all(void) {
    pthread_mutex_lock(&g_channel_mutex);
    for (int i = 0; i < g_channel_count; i++)
        g_channels[i].active = 0;
    g_channel_count = 0;
    pthread_mutex_unlock(&g_channel_mutex);
}

// Reset channels (called from am_init)
static void am_channel_reset(void) {
    am_channel_close_all();
}

#endif // AM_ASYNC_DISABLED

// Symbol table operations
static float* symtab_get(AML_Symtab* tab, const char* name) {
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->vars[i].name, name) == 0)
            return &tab->vars[i].value;
    }
    return NULL;
}

// Get full variable record (needed for array access)
static AML_Var* symtab_get_var(AML_Symtab* tab, const char* name) {
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->vars[i].name, name) == 0)
            return &tab->vars[i];
    }
    return NULL;
}

static int symtab_set(AML_Symtab* tab, const char* name, float value) {
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->vars[i].name, name) == 0) {
            // If overwriting an array with a float, free the array
            if (tab->vars[i].type == AML_TYPE_ARRAY && tab->vars[i].array) {
                am_array_free(tab->vars[i].array);
                tab->vars[i].array = NULL;
            }
            if (tab->vars[i].type == AML_TYPE_STRING) am_string_free(tab->vars[i].string);
            tab->vars[i].string = NULL;
            if (tab->vars[i].type == AML_TYPE_LIST) am_list_free(tab->vars[i].list);
            tab->vars[i].list = NULL;
            if (tab->vars[i].type == AML_TYPE_MAP) am_map_free(tab->vars[i].map);
            tab->vars[i].map = NULL;
            if (tab->vars[i].type == AML_TYPE_TOKENIZER) am_tokenizer_free(tab->vars[i].tokenizer);
            tab->vars[i].tokenizer = NULL;
            if (tab->vars[i].type == AML_TYPE_RECORD) am_record_free(tab->vars[i].record);
            tab->vars[i].record = NULL;
            tab->vars[i].type = AML_TYPE_FLOAT;
            tab->vars[i].value = value;
            return 0;
        }
    }
    if (tab->count >= AML_MAX_VARS) return 1;
    snprintf(tab->vars[tab->count].name, AML_MAX_NAME, "%s", name);
    tab->vars[tab->count].type = AML_TYPE_FLOAT;
    tab->vars[tab->count].value = value;
    tab->vars[tab->count].array = NULL;
    tab->vars[tab->count].string = NULL;
    tab->vars[tab->count].list = NULL;
    tab->vars[tab->count].map = NULL;
    tab->vars[tab->count].tokenizer = NULL;
    tab->vars[tab->count].record = NULL;
    tab->count++;
    return 0;
}

// Set an array variable (takes ownership of arr's refcount)
static int symtab_set_array(AML_Symtab* tab, const char* name, AM_Array* arr) {
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->vars[i].name, name) == 0) {
            // Free old array if any
            if (tab->vars[i].type == AML_TYPE_ARRAY && tab->vars[i].array) {
                am_array_free(tab->vars[i].array);
            }
            if (tab->vars[i].type == AML_TYPE_STRING) am_string_free(tab->vars[i].string);
            tab->vars[i].string = NULL;
            if (tab->vars[i].type == AML_TYPE_LIST) am_list_free(tab->vars[i].list);
            tab->vars[i].list = NULL;
            if (tab->vars[i].type == AML_TYPE_MAP) am_map_free(tab->vars[i].map);
            tab->vars[i].map = NULL;
            if (tab->vars[i].type == AML_TYPE_TOKENIZER) am_tokenizer_free(tab->vars[i].tokenizer);
            tab->vars[i].tokenizer = NULL;
            if (tab->vars[i].type == AML_TYPE_RECORD) am_record_free(tab->vars[i].record);
            tab->vars[i].record = NULL;
            tab->vars[i].type = AML_TYPE_ARRAY;
            tab->vars[i].value = 0;
            tab->vars[i].array = arr;
            return 0;
        }
    }
    if (tab->count >= AML_MAX_VARS) return 1;
    snprintf(tab->vars[tab->count].name, AML_MAX_NAME, "%s", name);
    tab->vars[tab->count].type = AML_TYPE_ARRAY;
    tab->vars[tab->count].value = 0;
    tab->vars[tab->count].array = arr;
    tab->vars[tab->count].string = NULL;
    tab->vars[tab->count].list = NULL;
    tab->vars[tab->count].map = NULL;
    tab->vars[tab->count].tokenizer = NULL;
    tab->vars[tab->count].record = NULL;
    tab->count++;
    return 0;
}

// Takes ownership of one reference, like symtab_set_array.
static int symtab_set_string(AML_Symtab* tab, const char* name, AM_String* str) {
    if (symtab_set(tab, name, 0)) return 1;
    AML_Var* v = symtab_get_var(tab, name);
    v->type = AML_TYPE_STRING;
    v->string = str;
    return 0;
}

// Takes ownership of one list reference. Parameter binding retains first;
// assignments and persistent copies provide a fresh container.
static int symtab_set_list(AML_Symtab* tab, const char* name, AM_List* list) {
    if (symtab_set(tab, name, 0)) return 1;
    AML_Var* v = symtab_get_var(tab, name);
    v->type = AML_TYPE_LIST;
    v->list = list;
    return 0;
}

static int symtab_set_map(AML_Symtab* tab, const char* name, AM_Map* map) {
    if (symtab_set(tab, name, 0)) return 1;
    AML_Var* v = symtab_get_var(tab, name);
    v->type = AML_TYPE_MAP;
    v->map = map;
    return 0;
}

static int symtab_set_tokenizer(AML_Symtab* tab, const char* name, AM_Tokenizer* model) {
    if (symtab_set(tab, name, 0)) return 1;
    AML_Var* v = symtab_get_var(tab, name);
    v->type = AML_TYPE_TOKENIZER;
    v->tokenizer = model;
    return 0;
}

static int symtab_set_record(AML_Symtab* tab, const char* name, AM_Record* record) {
    if (symtab_set(tab, name, 0)) return 1;
    AML_Var* v = symtab_get_var(tab, name);
    v->type = AML_TYPE_RECORD;
    v->record = record;
    return 0;
}

// Free all owned values in a symbol table.
static void symtab_clear_arrays(AML_Symtab* tab) {
    for (int i = 0; i < tab->count; i++) {
        if (tab->vars[i].type == AML_TYPE_ARRAY && tab->vars[i].array) {
            am_array_free(tab->vars[i].array);
            tab->vars[i].array = NULL;
        }
        if (tab->vars[i].type == AML_TYPE_STRING) {
            am_string_free(tab->vars[i].string);
            tab->vars[i].string = NULL;
        }
        if (tab->vars[i].type == AML_TYPE_LIST) {
            am_list_free(tab->vars[i].list);
            tab->vars[i].list = NULL;
        }
        if (tab->vars[i].type == AML_TYPE_MAP) {
            am_map_free(tab->vars[i].map);
            tab->vars[i].map = NULL;
        }
        if (tab->vars[i].type == AML_TYPE_TOKENIZER) {
            am_tokenizer_free(tab->vars[i].tokenizer);
            tab->vars[i].tokenizer = NULL;
        }
        if (tab->vars[i].type == AML_TYPE_RECORD) {
            am_record_free(tab->vars[i].record);
            tab->vars[i].record = NULL;
        }
    }
}

// Copy one binding without sharing mutable arrays, lists, or maps.
static int symtab_copy_value(AML_Symtab* dst, const AML_Var* v) {
    if (v->type == AML_TYPE_ARRAY) {
#ifdef USE_CUDA
        ensure_cpu(v->array);
#endif
        AM_Array* array = am_array_clone(v->array);
        if (!array) return 1;
        if (!symtab_set_array(dst, v->name, array)) return 0;
        am_array_free(array);
        return 1;
    }
    if (v->type == AML_TYPE_STRING) {
        am_string_ref(v->string);
        if (!symtab_set_string(dst, v->name, v->string)) return 0;
        am_string_free(v->string);
        return 1;
    }
    if (v->type == AML_TYPE_LIST) {
        AM_List* list = am_list_clone(v->list);
        if (!list) return 1;
        if (!symtab_set_list(dst, v->name, list)) return 0;
        am_list_free(list);
        return 1;
    }
    if (v->type == AML_TYPE_MAP) {
        AM_Map* map = am_map_clone(v->map);
        if (!map) return 1;
        if (!symtab_set_map(dst, v->name, map)) return 0;
        am_map_free(map);
        return 1;
    }
    if (v->type == AML_TYPE_TOKENIZER) {
        am_tokenizer_ref(v->tokenizer);
        if (!symtab_set_tokenizer(dst, v->name, v->tokenizer)) return 0;
        am_tokenizer_free(v->tokenizer);
        return 1;
    }
    if (v->type == AML_TYPE_RECORD) {
        AM_Record* record = am_record_clone(v->record);
        if (!record) return 1;
        if (!symtab_set_record(dst, v->name, record)) return 0;
        am_record_free(record);
        return 1;
    }
    return symtab_set(dst, v->name, v->value);
}

// Mutable containers are copied; immutable strings/models retain atomic references.
static int symtab_snapshot(AML_Symtab* dst, const AML_Symtab* src) {
    memset(dst, 0, sizeof(*dst));
    for (int i = 0; i < src->count; i++) {
        if (symtab_copy_value(dst, &src->vars[i])) {
            symtab_clear_arrays(dst);
            dst->count = 0;
            return 1;
        }
    }
    return 0;
}

// Resolve full variable (AML_Var*): locals → globals
static AML_Var* resolve_var_full(AML_ExecCtx* ctx, const char* name) {
    if (ctx->call_depth > 0) {
        AML_Var* v = symtab_get_var(&ctx->locals[ctx->call_depth - 1], name);
        if (v) return v;
    }
    return symtab_get_var(&ctx->globals, name);
}

// Resolve variable: locals → globals → field map
static int resolve_var(AML_ExecCtx* ctx, const char* name, float* out) {
    AML_Var* value = resolve_var_full(ctx, name);
    if (value && (value->type == AML_TYPE_STRING || value->type == AML_TYPE_LIST ||
                  value->type == AML_TYPE_MAP || value->type == AML_TYPE_TOKENIZER || value->type == AML_TYPE_RECORD)) {
        set_error(ctx, value->type == AML_TYPE_RECORD ? "record used as a scalar expression" :
            value->type == AML_TYPE_TOKENIZER ? "tokenizer used as a scalar expression" :
            value->type == AML_TYPE_MAP ? "map used as a scalar expression" :
            value->type == AML_TYPE_LIST
            ? "list used as a scalar expression" : "string used as a scalar expression");
        *out = 0;
        return 1;
    }
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

// Expression values own their container/string reference until consumed.
static void aml_value_clear(AML_Var* v) {
    if (v->type == AML_TYPE_ARRAY) am_array_free(v->array);
    if (v->type == AML_TYPE_STRING) am_string_free(v->string);
    if (v->type == AML_TYPE_LIST) am_list_free(v->list);
    if (v->type == AML_TYPE_MAP) am_map_free(v->map);
    if (v->type == AML_TYPE_TOKENIZER) am_tokenizer_free(v->tokenizer);
    if (v->type == AML_TYPE_RECORD) am_record_free(v->record);
    memset(v, 0, sizeof(*v));
}

static void aml_value_ref(AML_Var* v) {
    if (v->type == AML_TYPE_ARRAY) am_array_ref(v->array);
    if (v->type == AML_TYPE_STRING) am_string_ref(v->string);
    if (v->type == AML_TYPE_LIST) am_list_ref(v->list);
    if (v->type == AML_TYPE_MAP) am_map_ref(v->map);
    if (v->type == AML_TYPE_TOKENIZER) am_tokenizer_ref(v->tokenizer);
    if (v->type == AML_TYPE_RECORD) am_record_ref(v->record);
}

static void aml_clear_return(AML_ExecCtx* ctx) {
    am_array_free(ctx->return_array);
    am_string_free(ctx->return_string);
    am_list_free(ctx->return_list);
    am_map_free(ctx->return_map);
    am_tokenizer_free(ctx->return_tokenizer);
    am_record_free(ctx->return_record);
    ctx->return_array = NULL;
    ctx->return_string = NULL;
    ctx->return_list = NULL;
    ctx->return_map = NULL;
    ctx->return_tokenizer = NULL;
    ctx->return_record = NULL;
    ctx->return_type = AML_TYPE_FLOAT;
    ctx->return_value = 0;
    ctx->has_return = 0;
}

static int aml_eval_value(AML_ExecCtx* ctx, const char* text, AML_Var* out);
static int aml_call_value(AML_ExecCtx* ctx, AML_Func* f, AML_Var* args,
                          int nargs, int lineno, AML_Var* out);
static AM_Array* aml_try_array_expr(AML_ExecCtx* ctx, const char* rhs);

static AML_Func* aml_value_function(AML_ExecCtx* ctx, const char* name) {
    if (!ctx) return NULL;
    for (int i = 0; i < ctx->funcs.count; i++)
        if (strcmp(ctx->funcs.funcs[i].name, name) == 0) return &ctx->funcs.funcs[i];
    return NULL;
}

static int aml_text_function(const char* name) {
    static const char* names[] = {"text_len", "text_bytes", "text_equal", "text_find",
        "text_slice", "text_concat", "text_codepoint", "text_from_codepoint", "text_lower", "codepoint_isalnum", "read_line"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(name, names[i]) == 0) return 1;
    return 0;
}

static int aml_tokenizer_function(const char* name) {
    return !strcasecmp(name, "tokenizer_load") || !strcasecmp(name, "tokenizer_pieces") ||
           !strcasecmp(name, "tokenizer_identity");
}

static int aml_list_function(const char* name) {
    static const char* names[] = {"list_new", "list_len", "list_get", "list_push",
        "list_set", "list_find", "list_slice", "list_clone", "list_sorted", "list_key"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(name, names[i]) == 0) return 1;
    return 0;
}

static int aml_map_function(const char* name) {
    static const char* names[] = {"map_new", "map_len", "map_has", "map_get",
        "map_set", "map_delete", "map_keys", "map_clone"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(name, names[i]) == 0) return 1;
    return 0;
}

static int aml_record_function(const char* name) {
    static const char* names[] = {"record_new", "record_set", "record_get", "record_has",
        "record_keys", "record_kind", "record_clone", "record_replace", "record_swap",
        "checkpoint_save", "checkpoint_load", "file_exists"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcasecmp(name, names[i])) return 1;
    return 0;
}

static int aml_scalar_intrinsic_function(const char* name) {
    return strcasecmp(name, "assert") == 0 || strcasecmp(name, "floor") == 0 ||
           strcasecmp(name, "isfinite") == 0;
}

static int aml_sampling_function(const char* name) {
    static const char* names[] = {"rng_new", "rng_uniform", "rng_index",
        "rng_categorical", "categorical_at"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(name, names[i]) == 0) return 1;
    return 0;
}

static int aml_numerical_function(const char* name) {
    static const char* names[] = {"nt_linear", "nt_linear_vjp", "nt_tanh",
        "nt_tanh_vjp", "nt_mse_grad", "nt_sgd", "rng_normal"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(name, names[i]) == 0) return 1;
    return 0;
}

// Decode quoted source into UTF-8. Escapes are explicit and never insert NUL.
static AM_String* aml_string_literal(AML_ExecCtx* ctx, const char** cursor) {
    const char* p = *cursor;
    char quote = *p++;
    char bytes[AML_MAX_LINE_LEN];
    size_t n = 0;
    while (*p && *p != quote) {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            c = (unsigned char)*p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c != '\\' && c != '"' && c != '\'') {
                set_error(ctx, "invalid string escape");
                return NULL;
            }
        }
        if (n + 1 >= sizeof(bytes)) { set_error(ctx, "string literal too long"); return NULL; }
        bytes[n++] = (char)c;
    }
    if (*p != quote) { set_error(ctx, "unterminated string literal"); return NULL; }
    bytes[n] = 0;
    *cursor = p + 1;
    AM_String* str = am_string_new(bytes);
    if (!str) set_error(ctx, "invalid UTF-8 string or string allocation failed");
    return str;
}

static int aml_text_integer(AML_ExecCtx* ctx, const AML_Var* v, int* out) {
    if (v->type != AML_TYPE_FLOAT || !isfinite(v->value) ||
        v->value < -2147483648.0f || v->value >= 2147483648.0f ||
        truncf(v->value) != v->value) {
        set_error(ctx, "text index/codepoint must be an integer");
        return 1;
    }
    *out = (int)v->value;
    return 0;
}

static int aml_text_dispatch(AML_ExecCtx* ctx, const char* name, AML_Var* args,
                             int nargs, AML_Var* out) {
    int need = 2;
    if (!strcasecmp(name, "text_len") || !strcasecmp(name, "text_bytes") ||
        !strcasecmp(name, "text_from_codepoint") || !strcasecmp(name, "text_lower") ||
        !strcasecmp(name, "codepoint_isalnum")) need = 1;
    if (!strcasecmp(name, "read_line")) need = 0;
    if (!strcasecmp(name, "text_slice")) need = 3;
    if (nargs != need) { set_error(ctx, "wrong number of text arguments"); return 1; }
    if (!strcasecmp(name, "read_line")) {
        if (fflush(stdout) != 0) { set_error(ctx, "line input could not flush stdout"); return 1; }
        char error[256] = {0};
        out->list = am_read_line(stdin, error, sizeof(error));
        if (!out->list) { set_error(ctx, error); return 1; }
        out->type = AML_TYPE_LIST;
        return 0;
    }
    if (!strcasecmp(name, "codepoint_isalnum")) {
        int cp;
        if (aml_text_integer(ctx, &args[0], &cp)) return 1;
        int result = am_codepoint_isalnum(cp);
        if (result < 0) { set_error(ctx, "invalid Unicode scalar codepoint"); return 1; }
        out->value = (float)result;
        return 0;
    }
    if (!strcasecmp(name, "text_from_codepoint")) {
        int cp;
        if (aml_text_integer(ctx, &args[0], &cp)) return 1;
        out->string = am_string_from_codepoint(cp);
        if (!out->string) { set_error(ctx, "invalid Unicode codepoint or string allocation failed"); return 1; }
        out->type = AML_TYPE_STRING;
        return 0;
    }
    if (args[0].type != AML_TYPE_STRING || !args[0].string) {
        set_error(ctx, "text operation requires a string"); return 1;
    }
    AM_String* s = args[0].string;
    if (!strcasecmp(name, "text_len")) { out->value = (float)s->len; return 0; }
    if (!strcasecmp(name, "text_bytes")) { out->value = (float)s->byte_len; return 0; }
    if (!strcasecmp(name, "text_lower")) {
        out->string = am_string_lower(s);
    } else if (!strcasecmp(name, "text_slice")) {
        int start, end;
        if (aml_text_integer(ctx, &args[1], &start) || aml_text_integer(ctx, &args[2], &end))
            return 1;
        out->string = am_string_slice(s, start, end);
    } else if (!strcasecmp(name, "text_codepoint")) {
        int index;
        if (aml_text_integer(ctx, &args[1], &index)) return 1;
        int cp = am_string_codepoint(s, index);
        if (cp < 0) { set_error(ctx, "text index out of range"); return 1; }
        out->value = (float)cp;
        return 0;
    } else {
        if (args[1].type != AML_TYPE_STRING || !args[1].string) {
            set_error(ctx, "text operation requires a string"); return 1;
        }
        if (!strcasecmp(name, "text_equal")) {
            out->value = strcmp(s->data, args[1].string->data) == 0;
            return 0;
        }
        if (!strcasecmp(name, "text_find")) {
            out->value = (float)am_string_find(s, args[1].string);
            return 0;
        }
        out->string = am_string_concat(s, args[1].string);
    }
    if (!out->string) { set_error(ctx, "string limit exceeded or allocation failed"); return 1; }
    out->type = AML_TYPE_STRING;
    return 0;
}

// Imported functions retain their own source directory in ctx.base_dir. This
// resolver is shared by model loading and explicit checkpoint/file operations.
static char* aml_resolve_value_path(AML_ExecCtx* ctx, const AML_Var* value) {
    if (value->type != AML_TYPE_STRING || !value->string || !value->string->byte_len) {
        set_error(ctx, "file path must be a nonempty string"); return NULL;
    }
    const AM_String* path = value->string;
    size_t prefix = path->data[0] == '/' ? 0 : strlen(ctx->base_dir) + 1;
    if (prefix + (size_t)path->byte_len > AM_MAX_STRING_BYTES) {
        set_error(ctx, "file path limit exceeded"); return NULL;
    }
    char* resolved = malloc(prefix + (size_t)path->byte_len + 1);
    if (!resolved) { set_error(ctx, "file path allocation failed"); return NULL; }
    if (prefix) snprintf(resolved, prefix + 1, "%s/", ctx->base_dir);
    memcpy(resolved + prefix, path->data, (size_t)path->byte_len + 1);
    return resolved;
}

static int aml_tokenizer_dispatch(AML_ExecCtx* ctx, const char* name,
                                  AML_Var* args, int nargs, AML_Var* out) {
    int loading = !strcasecmp(name, "tokenizer_load");
    int identity = !strcasecmp(name, "tokenizer_identity");
    if (nargs != (loading || identity ? 1 : 2)) {
        set_error(ctx, "wrong number of tokenizer arguments"); return 1;
    }
    char error[256] = {0};
    if (loading) {
        if (args[0].type != AML_TYPE_STRING || !args[0].string) {
            set_error(ctx, "tokenizer_load requires a path string"); return 1;
        }
        char* resolved = aml_resolve_value_path(ctx, &args[0]);
        if (!resolved) return 1;
        out->tokenizer = am_tokenizer_load(resolved, error, sizeof(error));
        free(resolved);
        if (!out->tokenizer) {
            char detail[256];
            snprintf(detail, sizeof(detail), "tokenizer_load: %.239s", error);
            set_error(ctx, detail); return 1;
        }
        out->type = AML_TYPE_TOKENIZER;
    } else if (identity) {
        if (args[0].type != AML_TYPE_TOKENIZER || !args[0].tokenizer) {
            set_error(ctx, "tokenizer_identity requires a tokenizer"); return 1;
        }
        out->string = am_tokenizer_identity(args[0].tokenizer, error, sizeof(error));
        if (!out->string) { set_error(ctx, error); return 1; }
        out->type = AML_TYPE_STRING;
    } else {
        if (args[0].type != AML_TYPE_TOKENIZER || !args[0].tokenizer ||
            args[1].type != AML_TYPE_STRING || !args[1].string) {
            set_error(ctx, "tokenizer_pieces requires a tokenizer and text"); return 1;
        }
        out->list = am_tokenizer_pieces(args[0].tokenizer, args[1].string,
                                        error, sizeof(error));
        if (!out->list) { set_error(ctx, error); return 1; }
        out->type = AML_TYPE_LIST;
    }
    return 0;
}

static int aml_record_dispatch(AML_ExecCtx* ctx, const char* name,
                               AML_Var* args, int nargs, AML_Var* out) {
    int need = 2;
    if (!strcasecmp(name, "record_new")) need = 0;
    else if (!strcasecmp(name, "record_keys") || !strcasecmp(name, "record_clone") ||
             !strcasecmp(name, "checkpoint_load") || !strcasecmp(name, "file_exists")) need = 1;
    else if (!strcasecmp(name, "record_set")) need = 3;
    if (nargs != need) { set_error(ctx, "wrong number of record/checkpoint arguments"); return 1; }
    int saving = !strcasecmp(name, "checkpoint_save");
    int loading = !strcasecmp(name, "checkpoint_load");
    int exists = !strcasecmp(name, "file_exists");
    if (saving || loading || exists) {
        if (saving && (args[0].type != AML_TYPE_RECORD || !args[0].record)) {
            set_error(ctx, "checkpoint_save requires a record and path"); return 1;
        }
        char* path = aml_resolve_value_path(ctx, &args[saving ? 1 : 0]);
        if (!path) return 1;
        char error[256] = {0};
        int status = 0;
        if (saving) status = am_checkpoint_save(args[0].record, path, error, sizeof(error));
        else if (exists) status = am_file_exists(path, error, sizeof(error));
        else out->record = am_checkpoint_load(path, error, sizeof(error));
        free(path);
        if ((loading && !out->record) || (!loading && status < 0)) {
            set_error(ctx, error); return 1;
        }
        if (loading) out->type = AML_TYPE_RECORD;
        else out->value = (float)status;
        return 0;
    }
    if (!strcasecmp(name, "record_new")) out->record = am_record_new();
    else {
        if (args[0].type != AML_TYPE_RECORD || !args[0].record) {
            set_error(ctx, "record operation requires a record"); return 1;
        }
        AM_Record* record = args[0].record;
        if (!strcasecmp(name, "record_clone")) out->record = am_record_clone(record);
        else if (!strcasecmp(name, "record_keys")) {
            out->list = am_record_keys(record);
            if (!out->list) { set_error(ctx, "record keys allocation failed"); return 1; }
            out->type = AML_TYPE_LIST;
            return 0;
        } else if (!strcasecmp(name, "record_swap")) {
            if (args[1].type != AML_TYPE_RECORD || !args[1].record ||
                am_record_swap(record, args[1].record)) {
                set_error(ctx, "record_swap requires two records"); return 1;
            }
            out->value = 1;
            return 0;
        } else if (!strcasecmp(name, "record_replace")) {
            if (args[1].type != AML_TYPE_RECORD || !args[1].record) {
                set_error(ctx, "record_replace requires two records"); return 1;
            }
            if (am_record_replace(record, args[1].record)) {
                set_error(ctx, "record replacement allocation failed"); return 1;
            }
            am_record_ref(record);
            out->record = record;
        } else {
            if (args[1].type != AML_TYPE_STRING || !args[1].string) {
                set_error(ctx, "record key must be a string"); return 1;
            }
            AM_String* key = args[1].string;
            if (!strcasecmp(name, "record_has")) {
                out->value = (float)am_record_has(record, key);
                return 0;
            }
            if (!strcasecmp(name, "record_set")) {
                if (args[2].type < AML_TYPE_FLOAT || args[2].type > AML_TYPE_MAP) {
                    set_error(ctx, "record leaf must be float, array, string, list, or map"); return 1;
                }
                if (am_record_set(record, key, &args[2])) {
                    set_error(ctx, "record limit, invalid leaf, or allocation failure"); return 1;
                }
                am_record_ref(record);
                out->record = record;
            } else {
                const AML_Var* value = am_record_get(record, key);
                if (!value) { set_error(ctx, "record key not found"); return 1; }
                if (!strcasecmp(name, "record_kind")) {
                    const char* kinds[] = {"float", "array", "string", "list", "map"};
                    out->string = am_string_new(kinds[value->type]);
                    if (!out->string) { set_error(ctx, "record kind allocation failed"); return 1; }
                    out->type = AML_TYPE_STRING;
                } else {
                    *out = *value;
                    aml_value_ref(out);
                }
                return 0;
            }
        }
    }
    if (!out->record) { set_error(ctx, "record allocation failed"); return 1; }
    out->type = AML_TYPE_RECORD;
    return 0;
}

static int aml_list_integer(AML_ExecCtx* ctx, const AML_Var* v, int* out) {
    if (v->type != AML_TYPE_FLOAT || !isfinite(v->value) ||
        v->value < -2147483648.0f || v->value >= 2147483648.0f ||
        truncf(v->value) != v->value) {
        set_error(ctx, "list index must be an integer");
        return 1;
    }
    *out = (int)v->value;
    return 0;
}

static int aml_list_dispatch(AML_ExecCtx* ctx, const char* name, AML_Var* args,
                             int nargs, AML_Var* out) {
    int need = 2;
    if (!strcasecmp(name, "list_new")) need = 0;
    else if (!strcasecmp(name, "list_len") || !strcasecmp(name, "list_clone") ||
             !strcasecmp(name, "list_sorted") ||
             !strcasecmp(name, "list_key")) need = 1;
    else if (!strcasecmp(name, "list_set") || !strcasecmp(name, "list_slice")) need = 3;
    if (nargs != need) { set_error(ctx, "wrong number of list arguments"); return 1; }
    if (!strcasecmp(name, "list_new")) {
        out->list = am_list_new();
    } else {
        if (args[0].type != AML_TYPE_LIST || !args[0].list) {
            set_error(ctx, "list operation requires a list"); return 1;
        }
        AM_List* list = args[0].list;
        if (!strcasecmp(name, "list_len")) {
            out->value = (float)list->len;
            return 0;
        }
        if (!strcasecmp(name, "list_key")) {
            out->string = am_list_key(list);
            if (!out->string) { set_error(ctx, "composite key limit exceeded or allocation failed"); return 1; }
            out->type = AML_TYPE_STRING;
            return 0;
        }
        if (!strcasecmp(name, "list_clone")) out->list = am_list_clone(list);
        else if (!strcasecmp(name, "list_sorted")) out->list = am_list_sorted(list);
        else if (!strcasecmp(name, "list_slice")) {
            int start, end;
            if (aml_list_integer(ctx, &args[1], &start) || aml_list_integer(ctx, &args[2], &end))
                return 1;
            out->list = am_list_slice(list, start, end);
        } else if (!strcasecmp(name, "list_get")) {
            int index;
            if (aml_list_integer(ctx, &args[1], &index)) return 1;
            out->string = am_list_get(list, index);
            if (!out->string) { set_error(ctx, "list index out of range"); return 1; }
            out->type = AML_TYPE_STRING;
            return 0;
        } else {
            int position = 1, index = 0;
            if (!strcasecmp(name, "list_set")) {
                if (aml_list_integer(ctx, &args[1], &index)) return 1;
                position = 2;
            }
            if (args[position].type != AML_TYPE_STRING || !args[position].string) {
                set_error(ctx, "list item must be a string"); return 1;
            }
            AM_String* item = args[position].string;
            if (!strcasecmp(name, "list_find")) {
                out->value = (float)am_list_find(list, item);
            } else if (!strcasecmp(name, "list_push")) {
                int len = am_list_push(list, item);
                if (len < 0) { set_error(ctx, "list limit exceeded or allocation failed"); return 1; }
                out->value = (float)len;
            } else {
                if (am_list_set(list, index, item)) {
                    set_error(ctx, "list index out of range"); return 1;
                }
                am_string_ref(item);
                out->string = item;
                out->type = AML_TYPE_STRING;
            }
            return 0;
        }
    }
    if (!out->list) { set_error(ctx, "list allocation failed"); return 1; }
    out->type = AML_TYPE_LIST;
    return 0;
}

static int aml_map_dispatch(AML_ExecCtx* ctx, const char* name, AML_Var* args,
                            int nargs, AML_Var* out) {
    int need = 2;
    if (!strcasecmp(name, "map_new")) need = 0;
    else if (!strcasecmp(name, "map_len") || !strcasecmp(name, "map_keys") ||
             !strcasecmp(name, "map_clone")) need = 1;
    else if (!strcasecmp(name, "map_set")) need = 3;
    if (nargs != need) { set_error(ctx, "wrong number of map arguments"); return 1; }
    if (!strcasecmp(name, "map_new")) {
        out->map = am_map_new();
    } else {
        if (args[0].type != AML_TYPE_MAP || !args[0].map) {
            set_error(ctx, "map operation requires a map"); return 1;
        }
        AM_Map* map = args[0].map;
        if (!strcasecmp(name, "map_len")) { out->value = (float)map->len; return 0; }
        if (!strcasecmp(name, "map_clone")) out->map = am_map_clone(map);
        else if (!strcasecmp(name, "map_keys")) {
            out->list = am_map_keys(map);
            if (!out->list) { set_error(ctx, "map keys allocation failed"); return 1; }
            out->type = AML_TYPE_LIST;
            return 0;
        } else {
            if (args[1].type != AML_TYPE_STRING || !args[1].string) {
                set_error(ctx, "map key must be a string"); return 1;
            }
            AM_String* key = args[1].string;
            if (!strcasecmp(name, "map_has")) out->value = (float)am_map_has(map, key);
            else if (!strcasecmp(name, "map_get")) {
                if (am_map_get(map, key, &out->value) != 1) {
                    set_error(ctx, "map key not found"); return 1;
                }
            } else if (!strcasecmp(name, "map_delete")) {
                int deleted = am_map_delete(map, key);
                if (deleted < 0) { set_error(ctx, "invalid map deletion"); return 1; }
                out->value = (float)deleted;
            } else {
                if (args[2].type != AML_TYPE_FLOAT || !isfinite(args[2].value)) {
                    set_error(ctx, "map value must be a finite scalar"); return 1;
                }
                if (am_map_set(map, key, args[2].value)) {
                    set_error(ctx, "map limit exceeded or allocation failed"); return 1;
                }
                out->value = args[2].value;
            }
            return 0;
        }
    }
    if (!out->map) { set_error(ctx, "map allocation failed"); return 1; }
    out->type = AML_TYPE_MAP;
    return 0;
}

static int aml_scalar_intrinsic_dispatch(AML_ExecCtx* ctx, const char* name,
                                         AML_Var* args, int nargs, AML_Var* out) {
    if (!strcasecmp(name, "isfinite")) {
        if (nargs != 1) { set_error(ctx, "isfinite requires exactly one argument"); return 1; }
        if (args[0].type != AML_TYPE_FLOAT) {
            set_error(ctx, "isfinite requires a scalar"); return 1;
        }
        out->value = isfinite(args[0].value) ? 1.0f : 0.0f;
        return 0;
    }
    if (!strcasecmp(name, "floor")) {
        if (nargs != 1) { set_error(ctx, "floor requires exactly one argument"); return 1; }
        if (args[0].type != AML_TYPE_FLOAT || !isfinite(args[0].value)) {
            set_error(ctx, "floor requires a finite scalar"); return 1;
        }
        out->value = floorf(args[0].value);
        return 0;
    }
    if (nargs != 2) { set_error(ctx, "assert requires exactly two arguments"); return 1; }
    if (args[0].type != AML_TYPE_FLOAT || !isfinite(args[0].value)) {
        set_error(ctx, "assert condition must be a finite scalar"); return 1;
    }
    if (args[1].type != AML_TYPE_STRING || !args[1].string) {
        set_error(ctx, "assert message must be a string"); return 1;
    }
    if (!args[0].value) {
        char message[sizeof(ctx->error)];
        snprintf(message, sizeof(message), "assertion failed: %s", args[1].string->data);
        set_error(ctx, message);
        return 1;
    }
    out->value = 1;
    return 0;
}

static const char* const aml_rng_keys[] = {
    "algorithm", "state0", "state1", "state2", "state3"
};

// Decode exactly five named entries without allocating temporary lookup keys.
// Retain their entry indices so publishing the four limbs cannot allocate/fail.
static int aml_rng_read(AML_ExecCtx* ctx, const AML_Var* value,
                        uint64_t* state, int entries[4]) {
    if (value->type != AML_TYPE_MAP || !value->map || value->map->len != 5) {
        set_error(ctx, "RNG state must be a five-entry map"); return 1;
    }
    AM_Map* map = value->map;
    unsigned int found = 0;
    uint64_t decoded = 0;
    for (int i = 0; i < map->len; i++) {
        AM_MapEntry* entry = &map->entries[i];
        int field = -1;
        for (int k = 0; k < 5; k++) {
            if (!strcmp(entry->key->data, aml_rng_keys[k])) { field = k; break; }
        }
        if (field < 0 || (found & (1u << field)) || !isfinite(entry->value) ||
            entry->value < 0 || truncf(entry->value) != entry->value ||
            (field == 0 ? entry->value != 1 : entry->value > 65535)) {
            set_error(ctx, "invalid RNG algorithm or state limb"); return 1;
        }
        found |= 1u << field;
        if (field) {
            entries[field - 1] = i;
            decoded |= (uint64_t)entry->value << (16 * (field - 1));
        }
    }
    if (found != 31u) { set_error(ctx, "invalid RNG state keys"); return 1; }
    *state = decoded;
    return 0;
}

static void aml_rng_publish(AM_Map* map, const int entries[4], uint64_t state) {
    for (int i = 0; i < 4; i++)
        map->entries[entries[i]].value = (float)((state >> (16 * i)) & UINT64_C(65535));
}

static int aml_sampling_integer(AML_ExecCtx* ctx, const AML_Var* value,
                                float minimum, float maximum, uint32_t* out) {
    if (value->type != AML_TYPE_FLOAT || !isfinite(value->value) ||
        value->value < minimum || value->value > maximum ||
        truncf(value->value) != value->value) {
        set_error(ctx, "sampling integer is outside its exact scalar range"); return 1;
    }
    *out = (uint32_t)value->value;
    return 0;
}

static int aml_sampling_weights(AML_ExecCtx* ctx, const AML_Var* weights,
                                const AML_Var* temperature) {
    if (weights->type != AML_TYPE_ARRAY || !weights->array ||
        weights->array->len < 1 || !weights->array->data) {
        set_error(ctx, "categorical weights must be a nonempty numeric array"); return 1;
    }
    if (temperature->type != AML_TYPE_FLOAT || !isfinite(temperature->value) ||
        temperature->value <= 0) {
        set_error(ctx, "categorical temperature must be finite and positive"); return 1;
    }
#ifdef USE_CUDA
    ensure_cpu(weights->array);
#endif
    return 0;
}

static int aml_sampling_dispatch(AML_ExecCtx* ctx, const char* name,
                                 AML_Var* args, int nargs, AML_Var* out) {
    int is_new = !strcasecmp(name, "rng_new");
    int is_uniform = !strcasecmp(name, "rng_uniform");
    int is_index = !strcasecmp(name, "rng_index");
    int is_at = !strcasecmp(name, "categorical_at");
    int need = (is_new || is_uniform) ? 1 : is_index ? 2 : 3;
    if (nargs != need) { set_error(ctx, "wrong number of sampling arguments"); return 1; }
    const AM_SamplingBackend* backend = &g_sampling_backend;
    if (!backend->seed || !backend->u32 || !backend->uniform || !backend->index ||
        !backend->categorical_at || !backend->categorical) {
        set_error(ctx, "sampling backend unavailable; use NoTorch-enabled AML"); return 1;
    }
    if (is_new) {
        uint32_t seed;
        if (aml_sampling_integer(ctx, &args[0], 0, 16777215, &seed)) return 1;
        uint64_t state;
        backend->seed(&state, seed);
        AM_Map* map = am_map_new();
        if (!map) { set_error(ctx, "RNG map allocation failed"); return 1; }
        for (int i = 0; i < 5; i++) {
            AM_String* key = am_string_new(aml_rng_keys[i]);
            float value = i == 0 ? 1 : (float)((state >> (16 * (i - 1))) & UINT64_C(65535));
            int rc = key ? am_map_set(map, key, value) : -1;
            am_string_free(key);
            if (rc) {
                am_map_free(map);
                set_error(ctx, "RNG map allocation failed"); return 1;
            }
        }
        out->map = map;
        out->type = AML_TYPE_MAP;
        return 0;
    }
    if (is_at) {
        if (aml_sampling_weights(ctx, &args[0], &args[1])) return 1;
        if (args[2].type != AML_TYPE_FLOAT || !isfinite(args[2].value) ||
            args[2].value < 0 || args[2].value >= 1) {
            set_error(ctx, "categorical draw must be finite in [0, 1)"); return 1;
        }
        int result = -1;
        if (backend->categorical_at(args[0].array->data, args[0].array->len,
                                   args[1].value, args[2].value, &result) ||
            result < 0 || result >= args[0].array->len) {
            set_error(ctx, "sampling backend rejected categorical arguments"); return 1;
        }
        out->value = (float)result;
        return 0;
    }
    uint64_t state;
    int entries[4];
    if (aml_rng_read(ctx, &args[0], &state, entries)) return 1;
    if (is_uniform) {
        float result = backend->uniform(&state);
        if (!isfinite(result) || result < 0 || result >= 1) {
            set_error(ctx, "sampling backend returned an invalid uniform draw"); return 1;
        }
        out->value = result;
    } else if (is_index) {
        uint32_t bound, result = 0;
        if (aml_sampling_integer(ctx, &args[1], 1, 16777216, &bound)) return 1;
        if (backend->index(&state, bound, &result) || result >= bound) {
            set_error(ctx, "sampling backend rejected index arguments"); return 1;
        }
        out->value = (float)result;
    } else {
        if (aml_sampling_weights(ctx, &args[1], &args[2])) return 1;
        int result = -1;
        if (backend->categorical(&state, args[1].array->data, args[1].array->len,
                                args[2].value, &result) ||
            result < 0 || result >= args[1].array->len) {
            set_error(ctx, "sampling backend rejected categorical arguments"); return 1;
        }
        out->value = (float)result;
    }
    aml_rng_publish(args[0].map, entries, state);
    return 0;
}

// Numerical values use a private output buffer and explicit gradients. They
// neither borrow the global autograd tape nor change any operand array.
static int aml_numerical_array(AML_ExecCtx* ctx, const AML_Var* value) {
    if (value->type != AML_TYPE_ARRAY || !value->array ||
        value->array->len < 1 || value->array->len > AM_MAX_ARRAY_SIZE ||
        !value->array->data) {
        set_error(ctx, "numerical operand must be a nonempty numeric array"); return 1;
    }
#ifdef USE_CUDA
    ensure_cpu(value->array);
#endif
    for (int i = 0; i < value->array->len; i++) {
        if (!isfinite(value->array->data[i])) {
            set_error(ctx, "numerical operands must contain finite values"); return 1;
        }
    }
    return 0;
}

static int aml_numerical_dimension(AML_ExecCtx* ctx, const AML_Var* value, int* out) {
    if (value->type != AML_TYPE_FLOAT || !isfinite(value->value) ||
        value->value < 1 || value->value > AM_MAX_ARRAY_SIZE ||
        truncf(value->value) != value->value) {
        set_error(ctx, "numerical dimension must be an integer in the array range"); return 1;
    }
    *out = (int)value->value;
    return 0;
}

static int aml_numerical_dispatch(AML_ExecCtx* ctx, const char* name,
                                  AML_Var* args, int nargs, AML_Var* out) {
    int is_linear = !strcasecmp(name, "nt_linear");
    int is_linear_vjp = !strcasecmp(name, "nt_linear_vjp");
    int is_tanh = !strcasecmp(name, "nt_tanh");
    int is_tanh_vjp = !strcasecmp(name, "nt_tanh_vjp");
    int is_mse = !strcasecmp(name, "nt_mse_grad");
    int is_sgd = !strcasecmp(name, "nt_sgd");
    int is_normal = !strcasecmp(name, "rng_normal");
    int need = (is_linear || is_linear_vjp) ? 5 : is_tanh ? 1 : is_sgd ? 3 : 2;
    if (nargs != need) { set_error(ctx, "wrong number of numerical arguments"); return 1; }
    const AM_NumericalBackend* backend = &g_numerical_backend;
    if (!backend->linear || !backend->linear_vjp || !backend->tanh ||
        !backend->tanh_vjp || !backend->mse_grad || !backend->sgd || !backend->normal) {
        set_error(ctx, "numerical backend unavailable; use NoTorch-enabled AML"); return 1;
    }

    int rows = 0, cols = 0, len = 0, entries[4] = {0};
    uint64_t state = 0;
    if (is_normal) {
        if (aml_rng_read(ctx, &args[0], &state, entries) ||
            aml_numerical_dimension(ctx, &args[1], &len)) return 1;
    } else {
        int arrays = (is_linear || is_linear_vjp) ? 3 : is_tanh ? 1 : 2;
        for (int i = 0; i < arrays; i++)
            if (aml_numerical_array(ctx, &args[i])) return 1;
        if (is_linear || is_linear_vjp) {
            if (aml_numerical_dimension(ctx, &args[3], &rows) ||
                aml_numerical_dimension(ctx, &args[4], &cols)) return 1;
            if (rows > AM_MAX_ARRAY_SIZE / cols) {
                set_error(ctx, "numerical matrix exceeds the array range"); return 1;
            }
            int weight_len = rows * cols;
            int second_len = is_linear ? rows : cols;
            int third_len = is_linear ? cols : rows;
            if (args[0].array->len != weight_len || args[1].array->len != second_len ||
                args[2].array->len != third_len) {
                set_error(ctx, "numerical matrix dimensions do not match operands"); return 1;
            }
            len = is_linear ? rows : weight_len + rows + cols;
        } else {
            len = args[0].array->len;
            if (!is_tanh && args[1].array->len != len) {
                set_error(ctx, "numerical operand lengths must match"); return 1;
            }
            if (is_mse) len++;
            if (is_tanh_vjp) {
                for (int i = 0; i < len; i++) {
                    if (args[0].array->data[i] < -1 || args[0].array->data[i] > 1) {
                        set_error(ctx, "tanh saved values must lie in [-1, 1]"); return 1;
                    }
                }
            }
            if (is_sgd && (args[2].type != AML_TYPE_FLOAT || !isfinite(args[2].value) ||
                           args[2].value < 0)) {
                set_error(ctx, "numerical learning rate must be finite and nonnegative"); return 1;
            }
        }
    }
    if (len > AM_MAX_ARRAY_SIZE) {
        set_error(ctx, "numerical output exceeds the array range"); return 1;
    }
    AM_Array* result = am_array_new(len);
    if (!result) { set_error(ctx, "numerical output allocation failed"); return 1; }
    if (is_tanh || is_tanh_vjp || is_sgd) {
        result->rows = args[0].array->rows;
        result->cols = args[0].array->cols;
    }
    int rc;
    if (is_linear)
        rc = backend->linear(args[0].array->data, args[1].array->data,
                             args[2].array->data, rows, cols, result->data);
    else if (is_linear_vjp)
        rc = backend->linear_vjp(args[0].array->data, args[1].array->data,
                                 args[2].array->data, rows, cols, result->data);
    else if (is_tanh)
        rc = backend->tanh(args[0].array->data, len, result->data);
    else if (is_tanh_vjp)
        rc = backend->tanh_vjp(args[0].array->data, args[1].array->data, len, result->data);
    else if (is_mse)
        rc = backend->mse_grad(args[0].array->data, args[1].array->data, len - 1, result->data);
    else if (is_sgd)
        rc = backend->sgd(args[0].array->data, args[1].array->data, len, args[2].value, result->data);
    else
        rc = backend->normal(&state, len, result->data);
    if (rc) {
        am_array_free(result);
        set_error(ctx, "numerical backend rejected arguments"); return 1;
    }
    for (int i = 0; i < len; i++) {
        if (!isfinite(result->data[i])) {
            am_array_free(result);
            set_error(ctx, "numerical backend returned nonfinite output"); return 1;
        }
    }
    if (is_normal) aml_rng_publish(args[0].map, entries, state);
    out->array = result;
    out->type = AML_TYPE_ARRAY;
    return 0;
}

static int aml_array_scalar_function(const char* name) {
    return !strcasecmp(name, "len") || !strcasecmp(name, "sum") ||
           !strcasecmp(name, "dot") || !strcasecmp(name, "rows") ||
           !strcasecmp(name, "cols");
}

// Preserve numeric/undefined zero results while rejecting text, lists, and maps.
// The shared typed parser handles parentheses and returned values exactly once.
static int aml_array_scalar_dispatch(AML_ExecCtx* ctx, const char* name,
                                      AML_Var* args, int nargs, AML_Var* out) {
    int need = !strcasecmp(name, "dot") ? 2 : 1;
    if (nargs != need) {
        set_error(ctx, "wrong number of array arguments"); return 1;
    }
    for (int i = 0; i < nargs; i++) {
        if (args[i].type == AML_TYPE_STRING || args[i].type == AML_TYPE_LIST || args[i].type == AML_TYPE_MAP || args[i].type == AML_TYPE_TOKENIZER || args[i].type == AML_TYPE_RECORD) {
            set_error(ctx, "array operation requires an array"); return 1;
        }
    }
    AM_Array* a = args[0].type == AML_TYPE_ARRAY ? args[0].array : NULL;
    if (!a) return 0;
    if (!strcasecmp(name, "len")) out->value = (float)a->len;
    else if (!strcasecmp(name, "rows")) out->value = (float)a->rows;
    else if (!strcasecmp(name, "cols")) out->value = (float)a->cols;
    else {
#ifdef USE_CUDA
        ensure_cpu(a);
#endif
        if (!strcasecmp(name, "sum")) {
            for (int i = 0; i < a->len; i++) out->value += a->data[i];
        } else {
            AM_Array* b = args[1].type == AML_TYPE_ARRAY ? args[1].array : NULL;
            if (!b) return 0;
#ifdef USE_CUDA
            ensure_cpu(b);
#endif
            int n = a->len < b->len ? a->len : b->len;
            for (int i = 0; i < n; i++) out->value += a->data[i] * b->data[i];
        }
    }
    return 0;
}

// Parse mixed arguments without splitting commas/parentheses inside strings.
// cursor starts at '(' and advances past the matching ')'.
static int aml_invoke_value(AML_ExecCtx* ctx, const char* name,
                            const char** cursor, AML_Var* out) {
    AML_Var args[AML_MAX_PARAMS] = {0};
    int nargs = 0, rc = 1;
    const char* p = *cursor + 1;
    while (isspace((unsigned char)*p)) p++;
    while (*p && *p != ')') {
        if (nargs == AML_MAX_PARAMS) { set_error(ctx, "too many function arguments"); goto done; }
        const char* start = p;
        int depth = 0;
        char quote = 0;
        while (*p) {
            if (quote) {
                if (*p == '\\' && p[1]) { p += 2; continue; }
                if (*p == quote) quote = 0;
            } else {
                if (*p == '"' || *p == '\'') quote = *p;
                else if (*p == '(' || *p == '[') depth++;
                else if (*p == ')' || *p == ']') { if (!depth) break; depth--; }
                else if (*p == ',' && !depth) break;
            }
            p++;
        }
        size_t len = (size_t)(p - start);
        if (!len || len >= AML_MAX_LINE_LEN || quote || depth) {
            set_error(ctx, "invalid function argument"); goto done;
        }
        char expression[AML_MAX_LINE_LEN];
        memcpy(expression, start, len); expression[len] = 0;
        int slot = nargs++;
        if (aml_eval_value(ctx, expression, &args[slot])) goto done;
        if (*p != ',') break;
        p++;
        while (isspace((unsigned char)*p)) p++;
        if (*p == ')') { set_error(ctx, "empty function argument"); goto done; }
    }
    if (*p != ')') { set_error(ctx, "missing closing parenthesis"); goto done; }
    *cursor = p + 1;
    if (aml_text_function(name)) rc = aml_text_dispatch(ctx, name, args, nargs, out);
    else if (aml_list_function(name)) rc = aml_list_dispatch(ctx, name, args, nargs, out);
    else if (aml_map_function(name)) rc = aml_map_dispatch(ctx, name, args, nargs, out);
    else if (aml_record_function(name)) rc = aml_record_dispatch(ctx, name, args, nargs, out);
    else if (aml_scalar_intrinsic_function(name)) rc = aml_scalar_intrinsic_dispatch(ctx, name, args, nargs, out);
    else if (aml_sampling_function(name)) rc = aml_sampling_dispatch(ctx, name, args, nargs, out);
    else if (aml_numerical_function(name)) rc = aml_numerical_dispatch(ctx, name, args, nargs, out);
    else if (aml_tokenizer_function(name)) rc = aml_tokenizer_dispatch(ctx, name, args, nargs, out);
    else if (aml_array_scalar_function(name)) rc = aml_array_scalar_dispatch(ctx, name, args, nargs, out);
    else {
        AML_Func* f = aml_value_function(ctx, name);
        if (f) rc = aml_call_value(ctx, f, args, nargs, 0, out);
        else set_error(ctx, "unknown function");
    }
done:
    for (int i = 0; i < nargs; i++) aml_value_clear(&args[i]);
    if (rc) aml_value_clear(out);
    return rc;
}

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

static int expr_close(AML_Expr* e, char delimiter) {
    expr_skip_ws(e);
    if (e->error) return 1;
    if (*e->p != delimiter) {
        e->error = 1;
        set_error(e->ctx, delimiter == ')' ? "missing closing parenthesis in scalar expression"
                                          : "missing closing bracket in scalar expression");
        return 1;
    }
    e->p++;
    return 0;
}

static float expr_primary(AML_Expr* e) {
    expr_skip_ws(e);
    if (e->error) return 0;

    // parenthesized expression
    if (*e->p == '(') {
        e->p++;
        float val = expr_or(e);
        if (expr_close(e, ')')) return 0;
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

        // v4.0: array indexing — name[index]
        if (*e->p == '[') {
            e->p++;
            float idx_f = expr_or(e);
            if (expr_close(e, ']')) return 0;
            int idx = (int)idx_f;

            if (e->ctx) {
                AML_Var* var = resolve_var_full(e->ctx, name);
                if (var && (var->type == AML_TYPE_STRING || var->type == AML_TYPE_LIST || var->type == AML_TYPE_MAP || var->type == AML_TYPE_TOKENIZER || var->type == AML_TYPE_RECORD)) {
                    set_error(e->ctx, var->type == AML_TYPE_RECORD
                        ? "array indexing requires an array, not a record" : var->type == AML_TYPE_TOKENIZER
                        ? "array indexing requires an array, not a tokenizer" : var->type == AML_TYPE_MAP
                        ? "array indexing requires an array; use map_get for maps" : var->type == AML_TYPE_LIST
                        ? "array indexing requires an array; use list_get for lists"
                        : "array indexing requires an array; use text_codepoint/text_slice for text");
                    e->error = 1;
                    return 0;
                }
                if (var && var->type == AML_TYPE_ARRAY && var->array) {
#ifdef USE_CUDA
                    if (var->array->gpu_valid && var->array->d_data) ensure_cpu(var->array);
#endif
                    if (idx >= 0 && idx < var->array->len)
                        return var->array->data[idx];
                }
            }
            return 0;
        }

        // function call
        if (*e->p == '(') {
            if (e->ctx && (aml_text_function(name) || aml_list_function(name) ||
                           aml_map_function(name) || aml_scalar_intrinsic_function(name) ||
                           aml_sampling_function(name) || aml_numerical_function(name) || aml_tokenizer_function(name) || aml_record_function(name) ||
                           aml_array_scalar_function(name) ||
                           aml_value_function(e->ctx, name))) {
                AML_Var result = {0};
                if (aml_invoke_value(e->ctx, name, &e->p, &result)) {
                    e->error = 1;
                    return 0;
                }
                if (result.type != AML_TYPE_FLOAT) {
                    aml_value_clear(&result);
                    set_error(e->ctx, "string/array/list/map/tokenizer/record value used as a scalar expression");
                    e->error = 1;
                    return 0;
                }
                return result.value;
            }
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
            if (expr_close(e, ')')) return 0;

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
    if (ctx && ctx->error[0]) return 0;
    AML_Expr e = { .p = text, .ctx = ctx, .error = 0 };
    float result = expr_or(&e);
    expr_skip_ws(&e);
    if (!e.error && *e.p && *e.p != '#') {
        e.error = 1;
        set_error(ctx, "unexpected text after scalar expression");
    }
    if (e.error && ctx && !ctx->error[0]) set_error(ctx, "invalid scalar expression");
    return e.error ? 0.0f : result;
}

// Find a complete call/parenthesized expression without reading quoted brackets.
static const char* aml_value_close(const char* p) {
    int depth = 0;
    char quote = 0;
    char open = *p;
    char closing = open == '[' ? ']' : ')';
    for (; *p; p++) {
        if (quote) {
            if (*p == '\\' && p[1]) { p++; continue; }
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') quote = *p;
        else if (*p == open) depth++;
        else if (*p == closing && --depth == 0) return p;
    }
    return NULL;
}

static const char* aml_comment_start(const char* text) {
    char quote = 0;
    while (*text) {
        if (quote) {
            if (*text == '\\' && text[1]) { text += 2; continue; }
            if (*text == quote) quote = 0;
        } else if (*text == '"' || *text == '\'') quote = *text;
        else if (*text == '#') break;
        text++;
    }
    return text;
}

static int aml_eval_value(AML_ExecCtx* ctx, const char* text, AML_Var* out) {
    memset(out, 0, sizeof(*out));
    while (isspace((unsigned char)*text)) text++;
    // Comments start outside quotes; '#' within a string is ordinary text.
    const char* end = aml_comment_start(text);
    size_t len = (size_t)(end - text);
    while (len && isspace((unsigned char)text[len - 1])) len--;
    if (!len || len >= AML_MAX_LINE_LEN) {
        set_error(ctx, "empty or oversized value expression"); return 1;
    }
    char expr[AML_MAX_LINE_LEN];
    memcpy(expr, text, len); expr[len] = 0;
    const char* p = expr;
    if (*p == '(') {
        const char* close = aml_value_close(p);
        if (close == expr + len - 1) {
            expr[len - 1] = 0;
            return aml_eval_value(ctx, expr + 1, out);
        }
    }
    if (*p == '"' || *p == '\'') {
        out->string = aml_string_literal(ctx, &p);
        out->type = AML_TYPE_STRING;
        if (!out->string) return 1;
        while (isspace((unsigned char)*p)) p++;
        if (*p) {
            aml_value_clear(out);
            set_error(ctx, "unexpected text after string literal"); return 1;
        }
        return 0;
    }
    char name[AML_MAX_NAME] = {0};
    int n = 0;
    if (isalpha((unsigned char)*p) || *p == '_') {
        while ((isalnum((unsigned char)*p) || *p == '_') && n < AML_MAX_NAME - 1)
            name[n++] = *p++;
        while (isspace((unsigned char)*p)) p++;
        if (!*p) {
            AML_Var* v = resolve_var_full(ctx, name);
            if (v) {
                *out = *v;
                if (v->type == AML_TYPE_ARRAY) am_array_ref(out->array);
                if (v->type == AML_TYPE_STRING) am_string_ref(out->string);
                if (v->type == AML_TYPE_LIST) am_list_ref(out->list);
                if (v->type == AML_TYPE_MAP) am_map_ref(out->map);
                if (v->type == AML_TYPE_TOKENIZER) am_tokenizer_ref(out->tokenizer);
                if (v->type == AML_TYPE_RECORD) am_record_ref(out->record);
                return 0;
            }
        }
        if (*p == '(' && (aml_text_function(name) || aml_list_function(name) ||
                          aml_map_function(name) || aml_scalar_intrinsic_function(name) ||
                          aml_sampling_function(name) || aml_numerical_function(name) || aml_tokenizer_function(name) || aml_record_function(name) ||
                          aml_array_scalar_function(name) ||
                          aml_value_function(ctx, name))) {
            const char* close = aml_value_close(p);
            if (close == expr + len - 1) return aml_invoke_value(ctx, name, &p, out);
        }
    }
    AM_Array* arr = aml_try_array_expr(ctx, expr);
    if (ctx->error[0]) {
        am_array_free(arr);
        return 1;
    }
    if (arr) {
        out->type = AML_TYPE_ARRAY;
        out->array = arr;
        return 0;
    }
    out->value = aml_eval(ctx, expr);
    return ctx->error[0] != 0;
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

// Context-aware float/int parsing: evaluates expressions when in Level 2 context
static float ctx_float(AML_ExecCtx* ctx, const char* arg) {
    if (!arg || !*arg) return 0.0f;
    if (!ctx) return safe_atof(arg);
    return aml_eval_arg(ctx, arg);
}
static int ctx_int(AML_ExecCtx* ctx, const char* arg) {
    return (int)ctx_float(ctx, arg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BUILT-IN FUNCTIONS — native AML functions (not external bindings)
// From spec section 5. Each is C code that modifies field state directly.
// ═══════════════════════════════════════════════════════════════════════════════

#define BUILTIN_BOOTSTRAP_SELF      0
#define BUILTIN_GALVANIZE           1
#define BUILTIN_SHATTER_THE_FRAME   2
#define BUILTIN_CHAOS_INJECTION     3
#define BUILTIN_TRANSCEND_BINARY    4
#define BUILTIN_PIERCE_THE_INFINITE 5
#define BUILTIN_ECHO_FRACTAL        6
#define BUILTIN_REFLECT_ON_SELF     7
#define BUILTIN_FORGE_NEW_REALITY   8
#define BUILTIN_MERGE_STATES        9
#define BUILTIN_TUNNEL_THROUGH      10
#define BUILTIN_DISSOLVE_BOUNDARIES 11
#define BUILTIN_REMEMBER_FUTURE     12
#define BUILTIN_REWIND_EXPERIENCE   13
#define BUILTIN_IGNITE_SINGULARITY  14
#define BUILTIN_JANUS_GAZE          15
#define BUILTIN_FIELD_ASSEMBLE      16
#define BUILTIN_COUNT               17

static void aml_exec_builtin(int id, float* args, int nargs) {
    switch (id) {
    case BUILTIN_BOOTSTRAP_SELF:
        am_reset_field(); am_reset_debt();
        G.prophecy = 7; G.velocity_mode = AM_VEL_WALK;
        G.attend_focus = 0.70f; update_effective_temp();
        break;
    case BUILTIN_GALVANIZE:
        G.velocity_mode = AM_VEL_RUN; update_effective_temp();
        G.tension = 0.3f; G.prophecy = 12;
        break;
    case BUILTIN_SHATTER_THE_FRAME:
        G.pain = 0.7f; G.dissonance = 0.8f;
        G.tension = 0.5f; G.tunnel_chance = 0.3f;
        break;
    case BUILTIN_CHAOS_INJECTION:
        G.tension = 0.6f; G.dissonance = 0.7f;
        G.entropy_floor = 0.02f;
        G.velocity_mode = AM_VEL_RUN; update_effective_temp();
        break;
    case BUILTIN_TRANSCEND_BINARY:
        G.wormhole = 0.5f; G.tunnel_chance = 0.3f;
        G.temporal_mode = AM_TEMPORAL_SYMMETRIC;
        break;
    case BUILTIN_PIERCE_THE_INFINITE:
        G.prophecy = 64; G.destiny = 0.1f; G.wormhole = 0.4f;
        break;
    case BUILTIN_ECHO_FRACTAL:
        if (nargs >= 1) {
            G.prophecy = clampi((int)(args[0] * 2.0f), 1, 64);
            G.destiny = 0.1f;
            G.tunnel_skip_max = clampi((int)args[0], 1, 24);
        }
        break;
    case BUILTIN_REFLECT_ON_SELF:
        G.attend_focus = 0.95f; G.attend_spread = 0.05f;
        G.velocity_mode = AM_VEL_NOMOVE; update_effective_temp();
        break;
    case BUILTIN_FORGE_NEW_REALITY:
        G.destiny = 0.1f; G.expert_creative = 0.6f;
        G.expert_precise = 0.1f; G.entropy_floor = 0.05f;
        break;
    case BUILTIN_MERGE_STATES:
        G.wormhole = 0.8f; G.tunnel_chance = 0.5f;
        G.tunnel_skip_max = 16;
        break;
    case BUILTIN_TUNNEL_THROUGH:
        if (nargs >= 1) G.tunnel_threshold = clamp01(args[0]);
        G.tunnel_chance = 0.5f; G.tunnel_skip_max = 12;
        break;
    case BUILTIN_DISSOLVE_BOUNDARIES:
        G.attend_focus = 0.2f; G.attend_spread = 0.8f;
        G.expert_semantic = 0.5f;
        break;
    case BUILTIN_REMEMBER_FUTURE:
        G.temporal_mode = AM_TEMPORAL_PROPHECY;
        G.temporal_alpha = 1.0f;
        break;
    case BUILTIN_REWIND_EXPERIENCE:
        G.velocity_mode = AM_VEL_BACKWARD; update_effective_temp();
        G.temporal_mode = AM_TEMPORAL_RETRODICTION;
        G.temporal_alpha = 0.0f;
        break;
    case BUILTIN_IGNITE_SINGULARITY:
        // Field reaches critical mass — self-assembles
        // Maximum emergence, open all gates, Blood compiles on next step
        G.prophecy = 64; G.destiny = 0.9f;
        G.wormhole = 0.8f; G.tunnel_chance = 0.7f; G.tunnel_skip_max = 24;
        G.emergence_threshold = 0.01f;
        G.expert_creative = 0.8f; G.expert_semantic = 0.2f;
        G.velocity_mode = AM_VEL_RUN; update_effective_temp();
        G.essence_alpha = 1.0f;
        G.season = AM_SEASON_SUMMER; G.season_intensity = 1.0f;
        break;
    case BUILTIN_JANUS_GAZE:
        // Activate dual-facing field — look both ways simultaneously
        // If two gammas loaded: dual mode. Otherwise: symmetric temporal.
        if (G.n_gamma >= 2) {
            G.janus_mode = AM_JANUS_DUAL;
            G.janus_blend = 0.5f;
        }
        G.temporal_mode = AM_TEMPORAL_SYMMETRIC;
        G.attend_focus = 0.5f; G.attend_spread = 0.5f;
        G.wormhole = 0.6f;
        break;
    case BUILTIN_FIELD_ASSEMBLE:
        // θ = ε + γ + αδ — trigger field assembly
        // Sets janus to CYCLE mode: 4.C decides who speaks
        G.janus_mode = AM_JANUS_CYCLE;
        G.gamma_drift = 0.01f;
        G.essence_alpha = 1.0f;
        G.season_intensity = 1.0f;
        break;
    }
}

typedef struct {
    const char* name;
    int id;
    int param_count;
} AML_BuiltinDef;

static const AML_BuiltinDef g_builtins[BUILTIN_COUNT] = {
    { "bootstrap_self",      BUILTIN_BOOTSTRAP_SELF,      0 },
    { "galvanize",           BUILTIN_GALVANIZE,           0 },
    { "shatter_the_frame",   BUILTIN_SHATTER_THE_FRAME,   0 },
    { "chaos_injection",     BUILTIN_CHAOS_INJECTION,     0 },
    { "transcend_binary",    BUILTIN_TRANSCEND_BINARY,    0 },
    { "pierce_the_infinite", BUILTIN_PIERCE_THE_INFINITE, 0 },
    { "echo_fractal",        BUILTIN_ECHO_FRACTAL,        1 },
    { "reflect_on_self",     BUILTIN_REFLECT_ON_SELF,     0 },
    { "forge_new_reality",   BUILTIN_FORGE_NEW_REALITY,   0 },
    { "merge_states",        BUILTIN_MERGE_STATES,        0 },
    { "tunnel_through",      BUILTIN_TUNNEL_THROUGH,      1 },
    { "dissolve_boundaries", BUILTIN_DISSOLVE_BOUNDARIES, 0 },
    { "remember_future",     BUILTIN_REMEMBER_FUTURE,     0 },
    { "rewind_experience",   BUILTIN_REWIND_EXPERIENCE,   0 },
    { "ignite_singularity",  BUILTIN_IGNITE_SINGULARITY,  0 },
    { "janus_gaze",          BUILTIN_JANUS_GAZE,          0 },
    { "field_assemble",      BUILTIN_FIELD_ASSEMBLE,      0 },
};

static void aml_register_builtins(AML_ExecCtx* ctx) {
    for (int i = 0; i < BUILTIN_COUNT; i++) {
        if (ctx->funcs.count >= AML_MAX_FUNCS) break;
        AML_Func* f = &ctx->funcs.funcs[ctx->funcs.count];
        snprintf(f->name, AML_MAX_NAME, "%s", g_builtins[i].name);
        f->param_count = g_builtins[i].param_count;
        f->body_start = g_builtins[i].id;  // store builtin id
        f->body_end = 0;
        f->is_builtin = 1;
        ctx->funcs.count++;
    }
}

// Forward declarations for Blood compiler (defined after NOTORCH)
// These symbols are needed by BLOOD commands in Level 0 dispatch.
int am_blood_compile(const char* name, const char* code);
int am_blood_compile_lora(const char* name, int in_dim, int out_dim, int rank);
int am_blood_compile_emotion(const char* name, float valence, float arousal);
void am_blood_unload(int module_idx);

// ═══════════════════════════════════════════════════════════════════════════════
// LEVEL 0 DISPATCH — the original flat command parser, extracted
// ═══════════════════════════════════════════════════════════════════════════════

// Execute a single Level 0 command (CMD + ARG already split, CMD already upcased)
// ctx may be NULL for backward compatibility
// lineno is the source line number (0 if unknown)
static void aml_exec_level0(const char* cmd, const char* arg, AML_ExecCtx* ctx, int lineno) {
    const char* t = cmd;

    // PROPHECY PHYSICS — numeric args use ctx_float/ctx_int for expression support
    if (!strcmp(t, "PROPHECY")) {
      int numeric_arg_1 = ctx_int(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.prophecy = clampi(numeric_arg_1, 1, 64);
    }
    else if (!strcmp(t, "DESTINY")) {
      float numeric_arg_2 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.destiny = clamp01(numeric_arg_2);
    }
    else if (!strcmp(t, "FIELD")) {
      // FIELD ON|OFF — gate the field overlay on logits. A-7: honour the §1.1
      // boolean-false set so FIELD 0 / FALSE / NO also disable (was: only "OFF").
      char argup[8] = {0};
      snprintf(argup, sizeof(argup), "%.7s", arg);
      upcase(argup);
      G.field_enabled = (strcmp(argup, "OFF") && strcmp(argup, "0") &&
                         strcmp(argup, "FALSE") && strcmp(argup, "NO")) ? 1 : 0;
    }
    else if (!strcmp(t, "RESONANCE")) {
      // RESONANCE <float> — set a resonance FLOOR: the field is held at or above
      // this level. Stored in resonance_set and enforced in am_step's recompute
      // (raw_resonance = max(computed, set)); without the floor am_step would
      // overwrite the set value on the first tick. 0 = no floor (default).
      float numeric_arg_3 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.resonance_set = clamp01(numeric_arg_3);
      G.resonance = fmaxf(G.resonance, G.resonance_set);
    }
    else if (!strcmp(t, "WORMHOLE")) {
      float numeric_arg_4 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.wormhole = clamp01(numeric_arg_4);
    }
    else if (!strcmp(t, "CALENDAR_DRIFT")) {
      float numeric_arg_5 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.calendar_drift = clampf(numeric_arg_5, 0.0f, 30.0f);
    }
    else if (!strcmp(t, "BIRTH")) {
      // MetaJanus: fix the origin ONCE. arg = days from the calendar epoch to this organism's
      // birth. birth_drift = cumulative Hebrew-Gregorian drift at that day — the immutable fact
      // of WHEN it began. The fulcrum cannot be moved: a second BIRTH is ignored, so no prompt
      // (/aml BIRTH from the REPL) can drag the origin. Self-LOCATION, not agency.
      if (!g_birth_set) {
        float numeric_arg_6 = ctx_float(ctx, arg);
        if (ctx && ctx->error[0]) return;
        g_birth_days = (long)numeric_arg_6;
        G.birth_drift = calendar_cumulative_drift((int)g_birth_days);
        g_birth_set = 1;
      }
    }
    else if (!strcmp(t, "SELF_NOW_DAYS")) {
      // MetaJanus test-door: scrub the SELF clock (pd's "now") WITHOUT touching the world
      // calendar, so the pd trajectory (birth-quakes, drift-anniversaries) can be verified. It
      // moves NOW, never the origin (birth_drift stays latched). A negative arg = back to real clock.
      float numeric_arg_7 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      int d = (int)numeric_arg_7;
      if (d < 0) { g_self_now_manual = 0; }
      else { g_self_now_days = d; g_self_now_manual = 1; }
    }
    else if (!strcmp(t, "JANUS_KEY")) {
      // MetaJanus: arm/disarm the Janus temporal key. Armed, D-2 acts on the calendar-derived
      // janus_temporal_alpha (HIGH-2), leaning the inner seed harvest — a first INDIRECT speech influence
      // via limpha recall (HIGH-3), receipted, not inert. Default OFF is bit-for-bit current (D-2 reads the
      // neutral 0.5, HIGH-1); the generic temporal_alpha is never touched by Janus.
      float numeric_arg_8 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      g_temporal_key_on = (numeric_arg_8 != 0.0f) ? 1 : 0;
    }

    // ATTENTION PHYSICS
    else if (!strcmp(t, "ATTEND_FOCUS")) {
      float numeric_arg_9 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.attend_focus = clamp01(numeric_arg_9);
    }
    else if (!strcmp(t, "ATTEND_SPREAD")) {
      float numeric_arg_10 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.attend_spread = clamp01(numeric_arg_10);
    }

    // TUNNELING
    else if (!strcmp(t, "TUNNEL_THRESHOLD")) {
      float numeric_arg_11 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.tunnel_threshold = clamp01(numeric_arg_11);
    }
    else if (!strcmp(t, "TUNNEL_CHANCE")) {
      float numeric_arg_12 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.tunnel_chance = clamp01(numeric_arg_12);
    }
    else if (!strcmp(t, "TUNNEL_SKIP_MAX")) {
      int numeric_arg_13 = ctx_int(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.tunnel_skip_max = clampi(numeric_arg_13, 1, 24);
    }

    // SUFFERING
    else if (!strcmp(t, "PAIN")) {
      float numeric_arg_14 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.pain = clamp01(numeric_arg_14);
    }
    else if (!strcmp(t, "TENSION")) {
      float numeric_arg_15 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.tension = clamp01(numeric_arg_15);
    }
    else if (!strcmp(t, "DISSONANCE")) {
      float numeric_arg_16 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.dissonance = clamp01(numeric_arg_16);
    }

    // PROPHECY DEBT — direct set/configure
    else if (!strcmp(t, "PROPHECY_DEBT")) {
      float numeric_arg_17 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.debt = clampf(numeric_arg_17, 0.0f, 100.0f);
    }
    else if (!strcmp(t, "PROPHECY_DEBT_DECAY")) {
      float numeric_arg_18 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.debt_decay = clampf(numeric_arg_18, 0.9f, 0.9999f);
    }

    // MOVEMENT
    else if (!strcmp(t, "JUMP")) {
      G.pending_jump = clampi(G.pending_jump + safe_atoi(arg), -1000, 1000);
    }
    else if (!strcmp(t, "VELOCITY")) {
      // VELOCITY RUN|WALK|NOMOVE|BACKWARD or VELOCITY <int>
      char argup[32] = {0};
      snprintf(argup, sizeof(argup), "%.31s", arg);
      upcase(argup);

      int prev_vel = G.velocity_mode;
      if (!strcmp(argup, "RUN")) G.velocity_mode = AM_VEL_RUN;
      else if (!strcmp(argup, "WALK")) G.velocity_mode = AM_VEL_WALK;
      else if (!strcmp(argup, "NOMOVE") || !strcmp(argup, "STOP")) G.velocity_mode = AM_VEL_NOMOVE;
      else if (!strcmp(argup, "BACKWARD")) G.velocity_mode = AM_VEL_BACKWARD;
      else if (!strcmp(argup, "BREATHE")) G.velocity_mode = AM_VEL_BREATHE;
      else G.velocity_mode = clampi(safe_atoi(arg), -1, 3);

      // INERTIA — the body resists changing its gait. Switching the velocity mode
      // costs (adds to debt); re-stating the same mode is free. Over-switching
      // exhausts the field, and the recovery rule (debt > 5 in am_step) then forces
      // NOMOVE. This makes "discrete dynamics with inertia reads as a body" a
      // property of the language: a mood that holds and resists, not a switch.
      if (G.velocity_mode != prev_vel)
        G.debt = clampf(G.debt + AM_VELOCITY_INERTIA, 0.0f, 100.0f);

      update_effective_temp();
    }

    // EXPRESSION — the body speaks (the reverse flow from Leo/neoleo). BE: speak-from-body;
    // ASK: voice the not-knowing. Like VELOCITY sets the breath, these set per-run intensities
    // a host reads back to shape HOW it speaks. They resonate with the existing darkmatter
    // (SCAR / dark_gravity), not reinvent it: ASK with no argument voices the field's own gap.
    else if (!strcmp(t, "BE")) {
      // BE [x] — speak from the body this strongly (default full). "я есть [тело]".
      float numeric_arg_19 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.be_voice = (arg && *arg) ? clamp01(numeric_arg_19) : 1.0f;
    }
    else if (!strcmp(t, "ASK")) {
      // ASK [x] — voice the not-knowing this strongly; no arg = the field's darkmatter.
      float numeric_arg_20 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.ask_voice = (arg && *arg) ? clamp01(numeric_arg_20) : G.dark_gravity;
    }
    else if (!strcmp(t, "BASE_TEMP")) {
      float numeric_arg_21 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.base_temperature = clampf(numeric_arg_21, 0.1f, 3.0f);
      update_effective_temp();
    }

    // RESETS
    else if (!strcmp(t, "RESET_FIELD")) {
      am_reset_field();
    }
    else if (!strcmp(t, "RESET_DEBT")) {
      am_reset_debt();
    }

    // FIELD STATE PERSISTENCE — AMSO file (chambers, scars, debt, calendar, ...)
    //   LOAD "path.soma"   — read AM_State from disk (silent if file missing)
    //   SAVE "path.soma"   — dump AM_State to disk
    // Inferences (yent.aml, resonance.aml, jannus-r) call these to carry the
    // breath of the field across sessions.
    else if (!strcmp(t, "LOAD") || !strcmp(t, "SAVE")) {
      char path[512] = {0};
      const char* p = arg;
      while (*p == ' ' || *p == '\t') p++;
      if (*p == '"') {
        p++;
        int k = 0;
        while (*p && *p != '"' && k < (int)sizeof(path) - 1) path[k++] = *p++;
      } else {
        sscanf(arg, "%511s", path);
      }
      if (path[0]) {
        if (!strcmp(t, "LOAD")) am_field_load(path);
        /* SAVE: propagate a real I/O failure into ctx->error so am_exec returns
         * non-zero and callers' rc-checks are meaningful (am_field_save returns
         * -1 on fopen fail, -2 on short write). LOAD intentionally ignores rc:
         * a missing file is a legitimate fresh start. */
        else if (am_field_save(path) < 0)
          set_error_at(ctx, lineno, "SAVE: field save failed");
      }
    }

    // LAWS OF NATURE
    else if (!strcmp(t, "LAW")) {
      // LAW has two tokens: lawname value_expr
      char lawname[64] = {0};
      char valexpr[128] = {0};
      if (sscanf(arg, "%63s %127[^\n]", lawname, valexpr) >= 2) {
        upcase(lawname);
        float numeric_arg_22 = ctx_float(ctx, valexpr);
        if (ctx && ctx->error[0]) return;
        float lawval = numeric_arg_22;
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
          g_calendar_manual = 1;
        }
        else if (!strcmp(lawname, "WORMHOLE_GATE")) {
          G.wormhole_gate = clamp01(lawval);
        }
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // PACK MANAGEMENT
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "MODE") || !strcmp(t, "IMPORT")) {
      // MODE CODES_RIC or IMPORT CODES_RIC
      char packname[64] = {0};
      snprintf(packname, sizeof(packname), "%.63s", arg);
      upcase(packname);

      if (!strcmp(packname, "CODES_RIC") || !strcmp(packname, "CODES/RIC")) {
        G.packs_enabled |= AM_PACK_CODES_RIC;
      }
      // DARKMATTER and NOTORCH are core — MODE accepted but no-op
    }
    else if (!strcmp(t, "DISABLE")) {
      char packname[64] = {0};
      snprintf(packname, sizeof(packname), "%.63s", arg);
      upcase(packname);

      if (!strcmp(packname, "CODES_RIC") || !strcmp(packname, "CODES/RIC")) {
        G.packs_enabled &= ~AM_PACK_CODES_RIC;
      }
      // DARKMATTER and NOTORCH are core — cannot be disabled
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
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        G.chordlock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      else if (!strcmp(subcmd, "TEMPOLOCK")) {
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        G.tempolock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      else if (!strcmp(subcmd, "CHIRALITY")) {
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        G.chirality_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      else if (!strcmp(subcmd, "TEMPO")) {
        int numeric_arg_23 = ctx_int(ctx, arg);
        if (ctx && ctx->error[0]) return;
        G.tempo = clampi(numeric_arg_23, 2, 47);
      }
      else if (!strcmp(subcmd, "PAS_THRESHOLD")) {
        float numeric_arg_24 = ctx_float(ctx, arg);
        if (ctx && ctx->error[0]) return;
        G.pas_threshold = clamp01(numeric_arg_24);
      }
    }

    // Unqualified: CHORDLOCK works only when pack enabled
    else if (!strcmp(t, "CHORDLOCK")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        G.chordlock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
      // else: ignored (pack not enabled)
    }
    else if (!strcmp(t, "TEMPOLOCK")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        G.tempolock_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
    }
    else if (!strcmp(t, "CHIRALITY")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        G.chirality_on = (!strcmp(mode, "ON") || !strcmp(mode, "1"));
      }
    }
    else if (!strcmp(t, "TEMPO")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        int numeric_arg_25 = ctx_int(ctx, arg);
        if (ctx && ctx->error[0]) return;
        G.tempo = clampi(numeric_arg_25, 2, 47);
      }
    }
    else if (!strcmp(t, "PAS_THRESHOLD")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        float numeric_arg_26 = ctx_float(ctx, arg);
        if (ctx && ctx->error[0]) return;
        G.pas_threshold = clamp01(numeric_arg_26);
      }
    }
    else if (!strcmp(t, "ANCHOR")) {
      if (G.packs_enabled & AM_PACK_CODES_RIC) {
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        if (!strcmp(mode, "PRIME")) G.chordlock_on = 1;
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // DARK MATTER — core (no pack gate)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "GRAVITY")) {
      char subtype[16] = {0};
      float val = 0.5f;
      if (sscanf(arg, "%15s %f", subtype, &val) >= 1) {
        upcase(subtype);
        if (!strcmp(subtype, "DARK")) {
          G.dark_gravity = clamp01(val);
        }
      }
    }
    else if (!strcmp(t, "ANTIDOTE")) {
      char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
      if (!strcmp(mode, "AUTO")) G.antidote_mode = 0;
      else if (!strcmp(mode, "HARD")) G.antidote_mode = 1;
    }
    else if (!strcmp(t, "SCAR")) {
      // Store scar text (gravitational memory)
      if (G.n_scars < AM_MAX_SCARS) {
        const char* text_start = arg;
        // strip quotes if present
        if (*text_start == '"') text_start++;
        snprintf(G.scar_texts[G.n_scars], AM_SCAR_MAX_LEN, "%.63s", text_start);
        G.scar_texts[G.n_scars][AM_SCAR_MAX_LEN - 1] = 0;
        // strip trailing quote
        int slen = (int)strlen(G.scar_texts[G.n_scars]);
        if (slen > 0 && G.scar_texts[G.n_scars][slen - 1] == '"')
          G.scar_texts[G.n_scars][slen - 1] = 0;
        G.n_scars++;
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // SCHUMANN / COSMIC PHYSICS — core
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "SCHUMANN")) {
      float numeric_arg_27 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.schumann_hz = clampf(numeric_arg_27, 7.0f, 8.5f);
      G.schumann_coherence = compute_schumann_coherence(G.schumann_hz);
    }
    else if (!strcmp(t, "SCHUMANN_MODULATION")) {
      float numeric_arg_28 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.schumann_modulation = clamp01(numeric_arg_28);
    }
    else if (!strcmp(t, "COSMIC_COHERENCE")) {
      float numeric_arg_29 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.schumann_coherence = clamp01(numeric_arg_29);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // DELTA VOICE / NOTORCH — core
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "LORA_ALPHA")) {
      float numeric_arg_30 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.lora_alpha = clamp01(numeric_arg_30);
    }
    else if (!strcmp(t, "NOTORCH_LR")) {
      float numeric_arg_31 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.notorch_lr = clampf(numeric_arg_31, 0.001f, 0.5f);
    }
    else if (!strcmp(t, "NOTORCH_DECAY")) {
      float numeric_arg_32 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.notorch_decay = clampf(numeric_arg_32, 0.9f, 0.9999f);
    }
    else if (!strcmp(t, "RESONANCE_BOOST")) {
      // RESONANCE_BOOST <word> <float> — boosts resonance metric
      // Per-token tracking requires vocabulary; kernel applies to field
      float val = 0.0f;
      char word[32] = {0};
      if (sscanf(arg, "%31s %f", word, &val) >= 2) {
        G.resonance = clamp01(G.resonance + clamp01(val) * 0.1f);
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4.C — ASYNC FIELD FOREVER (seasons)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "SEASON")) {
      char sname[16] = {0}; snprintf(sname, sizeof(sname), "%.15s", arg); upcase(sname);
      if (!strcmp(sname, "SPRING")) G.season = AM_SEASON_SPRING;
      else if (!strcmp(sname, "SUMMER")) G.season = AM_SEASON_SUMMER;
      else if (!strcmp(sname, "AUTUMN")) G.season = AM_SEASON_AUTUMN;
      else if (!strcmp(sname, "WINTER")) G.season = AM_SEASON_WINTER;
      G.season_phase = 0.0f;
    }
    else if (!strcmp(t, "SEASON_INTENSITY")) {
      float numeric_arg_33 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.season_intensity = clamp01(numeric_arg_33);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // GAMMA — personality essence (θ = ε + γ + αδ)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "GAMMA")) {
      // GAMMA name alpha — load personality essence
      char name[32] = {0};
      float alpha = 1.0f;
      if (sscanf(arg, "%31s %f", name, &alpha) >= 1) {
        am_gamma_load(name, alpha);
      }
    }
    else if (!strcmp(t, "GAMMA_UNLOAD")) {
      // GAMMA_UNLOAD name
      char name[32] = {0};
      sscanf(arg, "%31s", name);
      am_gamma_unload(name);
    }
    else if (!strcmp(t, "ESSENCE")) {
      // ESSENCE alpha — overall gamma injection strength
      float numeric_arg_34 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.essence_alpha = clamp01(numeric_arg_34);
    }
    else if (!strcmp(t, "JANUS")) {
      // JANUS name_a name_b — dual-facing field
      char a[32] = {0}, b[32] = {0};
      if (sscanf(arg, "%31s %31s", a, b) == 2) {
        am_janus_set(a, b);
      } else {
        // JANUS OFF / JANUS CYCLE
        char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
        if (!strcmp(mode, "OFF")) G.janus_mode = AM_JANUS_OFF;
        else if (!strcmp(mode, "CYCLE")) G.janus_mode = AM_JANUS_CYCLE;
        else if (!strcmp(mode, "DUAL")) G.janus_mode = AM_JANUS_DUAL;
      }
    }
    else if (!strcmp(t, "JANUS_BLEND")) {
      float numeric_arg_35 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.janus_blend = clamp01(numeric_arg_35);
    }
    else if (!strcmp(t, "GAMMA_DRIFT")) {
      float numeric_arg_36 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.gamma_drift = clampf(numeric_arg_36, 0.0f, 0.1f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ECHO — debug output
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "ECHO")) {
      printf("[AML] %s\n", arg);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TEMPORAL SYMMETRY — from PITOMADOM (past ≡ future)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "TEMPORAL_MODE")) {
      char mode[32] = {0}; snprintf(mode, sizeof(mode), "%.31s", arg); upcase(mode);
      if (!strcmp(mode, "PROPHECY") || !strcmp(mode, "0")) G.temporal_mode = AM_TEMPORAL_PROPHECY;
      else if (!strcmp(mode, "RETRODICTION") || !strcmp(mode, "1")) G.temporal_mode = AM_TEMPORAL_RETRODICTION;
      else if (!strcmp(mode, "SYMMETRIC") || !strcmp(mode, "2")) G.temporal_mode = AM_TEMPORAL_SYMMETRIC;
    }
    else if (!strcmp(t, "TEMPORAL_ALPHA")) {
      float numeric_arg_37 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.temporal_alpha = clamp01(numeric_arg_37);
    }
    else if (!strcmp(t, "RTL_MODE")) {
      char mode[16] = {0}; snprintf(mode, sizeof(mode), "%.15s", arg); upcase(mode);
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
      float numeric_arg_38 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.expert_structural = clamp01(numeric_arg_38);
    }
    else if (!strcmp(t, "EXPERT_SEMANTIC")) {
      float numeric_arg_39 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.expert_semantic = clamp01(numeric_arg_39);
    }
    else if (!strcmp(t, "EXPERT_CREATIVE")) {
      float numeric_arg_40 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.expert_creative = clamp01(numeric_arg_40);
    }
    else if (!strcmp(t, "EXPERT_PRECISE")) {
      float numeric_arg_41 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.expert_precise = clamp01(numeric_arg_41);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // RESONANCE MEMORY — presence and decay
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "PRESENCE_DECAY")) {
      float numeric_arg_42 = ctx_float(ctx, arg);
      if (ctx && ctx->error[0]) return;
      G.presence_decay = clamp01(numeric_arg_42);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LEVEL 1 MACROS — MACRO name { CMD1; CMD2 }
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "MACRO")) {
      const char* brace = strchr(arg, '{');
      if (brace && g_macro_count < AML_MAX_MACROS) {
        char mname[AML_MAX_NAME] = {0};
        int ni = 0;
        const char* p = arg;
        while (p < brace && ni < AML_MAX_NAME - 1) {
          if (!isspace((unsigned char)*p)) mname[ni++] = *p;
          p++;
        }
        mname[ni] = 0;
        brace++;
        const char* end = strchr(brace, '}');
        if (end && ni > 0) {
          snprintf(g_macros[g_macro_count].name, AML_MAX_NAME, "%s", mname);
          int bi = 0;
          while (brace < end && bi < AML_MACRO_MAX_LEN - 1) {
            if (*brace == ';')
              g_macros[g_macro_count].body[bi++] = '\n';
            else
              g_macros[g_macro_count].body[bi++] = *brace;
            brace++;
          }
          g_macros[g_macro_count].body[bi] = 0;
          g_macro_count++;
        }
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // BLOOD — runtime C compilation (Level 3)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "BLOOD")) {
      // BLOOD COMPILE <name> <code>     — compile raw C
      // BLOOD LORA <name> <in> <out> <rank> — generate + compile LoRA
      // BLOOD EMOTION <name> <valence> <arousal> — generate + compile emotional kernel
      // BLOOD UNLOAD <name>             — unload module
      char subcmd[32] = {0};
      char rest[AML_MAX_LINE_LEN] = {0};
      if (arg) sscanf(arg, "%31s %[^\n]", subcmd, rest);
      upcase(subcmd);

      if (!strcmp(subcmd, "COMPILE")) {
        // BLOOD COMPILE name { code }
        char bname[AM_BLOOD_MAX_NAME] = {0};
        sscanf(rest, "%63s", bname);
        const char* brace = strchr(rest, '{');
        const char* end_brace = NULL;
        if (brace) end_brace = strrchr(rest, '}');
        if (brace && end_brace && end_brace > brace) {
          // Extract code between braces
          int code_len = (int)(end_brace - brace - 1);
          char* code = (char*)malloc(code_len + 1);
          if (code) {
            memcpy(code, brace + 1, code_len);
            code[code_len] = 0;
            int idx = am_blood_compile(bname, code);
            free(code);
            if (idx < 0 && ctx)
              set_error_at(ctx, lineno, "blood: compilation failed");
          }
        }
      }
      else if (!strcmp(subcmd, "LORA")) {
        char bname[64] = {0};
        int in_dim = 0, out_dim = 0, rank = 0;
        sscanf(rest, "%63s %d %d %d", bname, &in_dim, &out_dim, &rank);
        if (bname[0] && in_dim > 0 && out_dim > 0 && rank > 0) {
          am_blood_compile_lora(bname, in_dim, out_dim, rank);
        }
      }
      else if (!strcmp(subcmd, "EMOTION")) {
        char bname[64] = {0};
        float val = 0.0f, aro = 0.0f;
        sscanf(rest, "%63s %f %f", bname, &val, &aro);
        if (bname[0]) {
          am_blood_compile_emotion(bname, val, aro);
        }
      }
      else if (!strcmp(subcmd, "UNLOAD")) {
        char bname[64] = {0};
        sscanf(rest, "%63s", bname);
        // Find module by name
        for (int i = 0; i < g_blood_count; i++) {
          if (strcmp(g_blood_modules[i].name, bname) == 0) {
            am_blood_unload(i);
            break;
          }
        }
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // JANUS — transformer inference commands
    // "Janus will grow like mycelium, without roots, without a trunk, without a flag."
    // ─────────────────────────────────────────────────────────────────────────

#ifndef AM_JANUS_DISABLED
    else if (!strcmp(t, "LOAD_MODEL")) {
      if (arg && arg[0]) {
        if (g_janus_load_model) g_janus_load_model(arg);
        else printf("[AML] LOAD_MODEL: Janus not linked\n");
      }
    }
    else if (!strcmp(t, "UNLOAD_MODEL")) {
      if (g_janus_unload_model) g_janus_unload_model();
    }
    else if (!strcmp(t, "LOAD_DELTA")) {
      if (arg && arg[0]) {
        if (g_janus_load_delta) g_janus_load_delta(arg);
        else printf("[AML] LOAD_DELTA: Janus not linked\n");
      }
    }
    else if (!strcmp(t, "LOAD_GAMMA")) {
      // LOAD_GAMMA name path
      char gname[64] = {0};
      char gpath[512] = {0};
      if (arg && sscanf(arg, "%63s %511s", gname, gpath) == 2) {
        // Also register in gamma slot system
        am_gamma_load(gname, 1.0f);
        if (g_janus_load_gamma) g_janus_load_gamma(gname, gpath);
        else printf("[AML] LOAD_GAMMA: Janus not linked\n");
      }
    }
    else if (!strcmp(t, "GENERATE")) {
      if (arg && arg[0]) {
        // Strip surrounding quotes if present
        char prompt[2048] = {0};
        int max_tok = 100;
        const char* p = arg;
        if (*p == '"') {
          p++;
          const char* end = strrchr(p, '"');
          if (end) {
            int len = (int)(end - p);
            if (len > 2047) len = 2047;
            memcpy(prompt, p, len);
            // Parse MAX_TOKENS after closing quote
            const char* after = end + 1;
            while (*after == ' ') after++;
            if (strncasecmp(after, "MAX_TOKENS", 10) == 0) {
              sscanf(after + 10, " %d", &max_tok);
            }
          } else {
            snprintf(prompt, sizeof(prompt), "%.2047s", p);
          }
        } else {
          snprintf(prompt, sizeof(prompt), "%.2047s", p);
        }
        if (g_janus_generate) {
          char* result = g_janus_generate(prompt, max_tok, G.effective_temp, 0.9f);
          if (result) {
            printf("%s\n", result);
            if (g_janus_free_string) g_janus_free_string(result);
          }
        } else {
          printf("[AML] GENERATE: Janus not linked\n");
        }
      }
    }
    else if (!strcmp(t, "MODEL_INFO")) {
      if (g_janus_model_loaded && g_janus_model_loaded()) {
        printf("[AML] Model: vocab=%d dim=%d layers=%d\n",
          g_janus_get_vocab_size ? g_janus_get_vocab_size() : 0,
          g_janus_get_embed_dim ? g_janus_get_embed_dim() : 0,
          g_janus_get_num_layers ? g_janus_get_num_layers() : 0);
      } else {
        printf("[AML] No model loaded\n");
      }
    }
#endif

    // ─────────────────────────────────────────────────────────────────────────
    // LILITH I/O — named pipes for data infrastructure
    // "Та, которая была до Евы."
    // ─────────────────────────────────────────────────────────────────────────

#ifndef AM_IO_DISABLED
    else if (!strcmp(t, "PIPE")) {
      // PIPE CREATE <path>               — create FIFO at path
      // PIPE OPEN <name> <path> <mode>   — open pipe (mode: READ or WRITE)
      // PIPE WRITE <name> <message>      — write to pipe
      // PIPE READ <name>                 — read from pipe (non-blocking)
      // PIPE CLOSE <name>                — close pipe
      // PIPE CLOSE_ALL                   — close all pipes
      // PIPE LIST                        — list open pipes
      char subcmd[32] = {0};
      char rest[AML_MAX_LINE_LEN] = {0};
      if (arg) sscanf(arg, "%31s %[^\n]", subcmd, rest);
      upcase(subcmd);

      if (!strcmp(subcmd, "CREATE")) {
        // PIPE CREATE /tmp/lilith_idx1
        char path[AM_PIPE_PATH_LEN] = {0};
        sscanf(rest, "%255s", path);
        if (path[0]) {
          am_pipe_create(path);
        } else if (ctx) {
          set_error_at(ctx, lineno, "PIPE CREATE: path required");
        }
      }
      else if (!strcmp(subcmd, "OPEN")) {
        // PIPE OPEN idx1_cmd /tmp/lilith_idx1_cmd WRITE
        char pname[AM_PIPE_NAME_LEN] = {0};
        char path[AM_PIPE_PATH_LEN] = {0};
        char mode_str[16] = {0};
        if (sscanf(rest, "%31s %255s %15s", pname, path, mode_str) >= 2) {
          upcase(mode_str);
          int mode = AM_PIPE_MODE_READ;
          if (!strcmp(mode_str, "WRITE") || !strcmp(mode_str, "W"))
            mode = AM_PIPE_MODE_WRITE;
          int idx = am_pipe_open(pname, path, mode);
          if (idx < 0 && ctx)
            set_error_at(ctx, lineno, "PIPE OPEN failed");
        } else if (ctx) {
          set_error_at(ctx, lineno, "PIPE OPEN: name and path required");
        }
      }
      else if (!strcmp(subcmd, "WRITE")) {
        // PIPE WRITE idx1_cmd "FETCH r/philosophy"
        char pname[AM_PIPE_NAME_LEN] = {0};
        char msg[AM_PIPE_BUF_SIZE] = {0};
        // Parse: first token = name, rest = message (strip quotes)
        char* space = strchr(rest, ' ');
        if (space) {
          int nlen = (int)(space - rest);
          if (nlen >= AM_PIPE_NAME_LEN) nlen = AM_PIPE_NAME_LEN - 1;
          memcpy(pname, rest, nlen);
          pname[nlen] = 0;
          // Skip space, strip surrounding quotes
          const char* mp = space + 1;
          while (*mp == ' ') mp++;
          if (*mp == '"') {
            mp++;
            const char* end = strrchr(mp, '"');
            if (end) {
              int mlen = (int)(end - mp);
              if (mlen >= AM_PIPE_BUF_SIZE) mlen = AM_PIPE_BUF_SIZE - 1;
              memcpy(msg, mp, mlen);
              msg[mlen] = 0;
            } else {
              snprintf(msg, sizeof(msg), "%s", mp);
            }
          } else {
            snprintf(msg, sizeof(msg), "%s", mp);
          }
          am_pipe_write(pname, msg);
        }
      }
      else if (!strcmp(subcmd, "READ")) {
        // PIPE READ idx1_rsp
        char pname[AM_PIPE_NAME_LEN] = {0};
        sscanf(rest, "%31s", pname);
        if (pname[0]) {
          int n = am_pipe_read(pname, g_pipe_read_buf, AM_PIPE_BUF_SIZE);
          if (n > 0) {
            printf("[LILITH] %s: %s\n", pname, g_pipe_read_buf);
            // Store numeric value in AML variable _pipe_value if ctx exists
            if (ctx && ctx->call_depth > 0) {
              symtab_set(&ctx->locals[ctx->call_depth - 1],
                         "_pipe_value", g_pipe_last_value);
            } else if (ctx) {
              symtab_set(&ctx->globals, "_pipe_value", g_pipe_last_value);
            }
          }
        }
      }
      else if (!strcmp(subcmd, "CLOSE")) {
        char pname[AM_PIPE_NAME_LEN] = {0};
        sscanf(rest, "%31s", pname);
        upcase(pname);
        if (!strcmp(pname, "ALL") || !strcmp(rest, "ALL")) {
          am_pipe_close_all();
        } else {
          // Restore original case for name lookup
          sscanf(rest, "%31s", pname);
          am_pipe_close(pname);
        }
      }
      else if (!strcmp(subcmd, "LIST")) {
        printf("[LILITH] pipes (%d open):\n", am_pipe_count());
        for (int i = 0; i < g_pipe_count; i++) {
          if (g_pipes[i].active) {
            printf("[LILITH]   %s → %s (%s)\n",
                   g_pipes[i].name, g_pipes[i].path,
                   g_pipes[i].mode == AM_PIPE_MODE_READ ? "READ" : "WRITE");
          }
        }
      }
    }

    else if (!strcmp(t, "INDEX")) {
      // INDEX <id> <subcmd> [args]  — high-level INDEX node management
      // Sugar over PIPE commands. Uses convention:
      //   pipe name = "idx<id>_cmd" (write) / "idx<id>_rsp" (read)
      //   pipe path = "/tmp/lilith_idx<id>_cmd" / "/tmp/lilith_idx<id>_rsp"
      char id_str[8] = {0};
      char subcmd2[32] = {0};
      char irest[AML_MAX_LINE_LEN] = {0};
      if (arg) sscanf(arg, "%7s %31s %[^\n]", id_str, subcmd2, irest);
      upcase(subcmd2);

      if (id_str[0]) {
        // Build pipe names from INDEX id
        char cmd_name[AM_PIPE_NAME_LEN];
        char rsp_name[AM_PIPE_NAME_LEN];
        char cmd_path[AM_PIPE_PATH_LEN];
        char rsp_path[AM_PIPE_PATH_LEN];
        snprintf(cmd_name, sizeof(cmd_name), "idx%s_cmd", id_str);
        snprintf(rsp_name, sizeof(rsp_name), "idx%s_rsp", id_str);
        snprintf(cmd_path, sizeof(cmd_path), "/tmp/lilith_idx%s_cmd", id_str);
        snprintf(rsp_path, sizeof(rsp_path), "/tmp/lilith_idx%s_rsp", id_str);

        if (!strcmp(subcmd2, "INIT")) {
          // INDEX 1 INIT — create pipes and open them
          am_pipe_create(cmd_path);
          am_pipe_create(rsp_path);
          am_pipe_open(cmd_name, cmd_path, AM_PIPE_MODE_WRITE);
          am_pipe_open(rsp_name, rsp_path, AM_PIPE_MODE_READ);
          printf("[LILITH] INDEX %s initialized\n", id_str);
        }
        else if (!strcmp(subcmd2, "FETCH")) {
          // INDEX 1 FETCH r/philosophy — tell index node to fetch
          char fetch_cmd[AM_PIPE_BUF_SIZE];
          snprintf(fetch_cmd, sizeof(fetch_cmd), "FETCH %s", irest);
          am_pipe_write(cmd_name, fetch_cmd);
        }
        else if (!strcmp(subcmd2, "STATUS")) {
          // INDEX 1 STATUS — request + read status
          am_pipe_write(cmd_name, "STATUS");
          // Try reading response (might not be immediate)
          int n = am_pipe_read(rsp_name, g_pipe_read_buf, AM_PIPE_BUF_SIZE);
          if (n > 0) {
            printf("[LILITH] INDEX %s status: %s\n", id_str, g_pipe_read_buf);
          } else {
            printf("[LILITH] INDEX %s: no response yet\n", id_str);
          }
        }
        else if (!strcmp(subcmd2, "STOP")) {
          am_pipe_write(cmd_name, "STOP");
        }
        else if (!strcmp(subcmd2, "CLOSE")) {
          am_pipe_close(cmd_name);
          am_pipe_close(rsp_name);
        }
      }
    }
#endif // AM_IO_DISABLED

    // ─────────────────────────────────────────────────────────────────────────
    // TAPE — autograd (v4.0 Phase 3)
    // ─────────────────────────────────────────────────────────────────────────

    else if (!strcmp(t, "TAPE")) {
      char subcmd[32] = {0};
      char rest[AML_MAX_LINE_LEN] = {0};
      if (arg) sscanf(arg, "%31s %[^\n]", subcmd, rest);
      upcase(subcmd);

      if (!strcmp(subcmd, "START")) {
        am_tape_start();
      }
      else if (!strcmp(subcmd, "CLEAR")) {
        am_tape_clear();
      }
      else if (!strcmp(subcmd, "BACKWARD")) {
        // TAPE BACKWARD <var_name> — backprop from loss variable
        char vname[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s", vname);
        if (vname[0] && ctx) {
          AML_Var* v = resolve_var_full(ctx, vname);
          if (v && (v->type == AML_TYPE_LIST || v->type == AML_TYPE_MAP || v->type == AML_TYPE_TOKENIZER || v->type == AML_TYPE_RECORD)) {
            set_error(ctx, "TAPE requires a numeric array");
            return;
          }
          if (v && v->type == AML_TYPE_ARRAY && v->array) {
            int tidx = tape_find_entry(v->array);
            if (tidx >= 0) am_tape_backward(tidx);
          }
        }
      }
      else if (!strcmp(subcmd, "ADAM_STEP") || !strcmp(subcmd, "ADAM")) {
        // TAPE ADAM_STEP <lr> or TAPE ADAM <lr>
        float lr = 0.001f;
        float numeric_arg_43 = ctx_float(ctx, rest);
        if (ctx && ctx->error[0]) return;
        if (rest[0]) lr = numeric_arg_43;
        am_tape_adam_step(lr);
      }
      else if (!strcmp(subcmd, "CHUCK_STEP") || !strcmp(subcmd, "CHUCK")) {
        // TAPE CHUCK_STEP <lr> <loss_var> — self-aware optimizer
        // TAPE CHUCK <lr> <loss_var>
        char arg1[AML_MAX_NAME] = {0};
        char arg2[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s %31s", arg1, arg2);
        float lr = 0.001f;
        float loss_val = 0.0f;
        float numeric_arg_44 = ctx_float(ctx, arg1);
        if (ctx && ctx->error[0]) return;
        if (arg1[0]) lr = numeric_arg_44;
        float numeric_arg_45 = ctx_float(ctx, arg2);
        if (ctx && ctx->error[0]) return;
        if (arg2[0] && ctx) loss_val = numeric_arg_45;
        am_tape_chuck_step(lr, loss_val);
      }
      else if (!strcmp(subcmd, "ADAMW_STEP") || !strcmp(subcmd, "ADAMW")) {
        // TAPE ADAMW_STEP <lr> [weight_decay] [beta1] [beta2]
        // TAPE ADAMW <lr> [weight_decay] [beta1] [beta2]
        char a1[32]={0}, a2[32]={0}, a3[32]={0}, a4[32]={0};
        sscanf(rest, "%31s %31s %31s %31s", a1, a2, a3, a4);
        float numeric_arg_46 = ctx_float(ctx, a1);
        if (ctx && ctx->error[0]) return;
        float lr = a1[0] ? numeric_arg_46 : 0.001f;
        float numeric_arg_47 = ctx_float(ctx, a2);
        if (ctx && ctx->error[0]) return;
        float wd = a2[0] ? numeric_arg_47 : 0.1f;
        float numeric_arg_48 = ctx_float(ctx, a3);
        if (ctx && ctx->error[0]) return;
        float b1 = a3[0] ? numeric_arg_48 : 0.9f;
        float numeric_arg_49 = ctx_float(ctx, a4);
        if (ctx && ctx->error[0]) return;
        float b2 = a4[0] ? numeric_arg_49 : 0.95f;
        am_tape_adamw_step(lr, wd, b1, b2);
#ifdef USE_CUDA
        for (int pi = 0; pi < g_tape.count; pi++) {
            if (g_tape.entries[pi].is_param && g_tape.entries[pi].output)
                invalidate_gpu(g_tape.entries[pi].output);
        }
#endif
      }
      else if (!strcmp(subcmd, "CLIP_GRADS") || !strcmp(subcmd, "CLIP")) {
        // TAPE CLIP_GRADS <max_norm> — gradient clipping by global norm
        float max_norm = 1.0f;
        float numeric_arg_50 = ctx_float(ctx, rest);
        if (ctx && ctx->error[0]) return;
        if (rest[0]) max_norm = numeric_arg_50;
        float norm = am_tape_clip_grads(max_norm);
        // Store grad_norm in context for logging
        if (ctx) {
          char cmd[64]; snprintf(cmd, 64, "grad_norm = %.6f", norm);
          am_exec(cmd);
        }
      }
      else if (!strcmp(subcmd, "ACCUM_GRADS") || !strcmp(subcmd, "ACCUM")) {
        // TAPE ACCUM_GRADS — accumulate param grads into buffer (for gradient accumulation)
        am_tape_accum_grads();
      }
      else if (!strcmp(subcmd, "APPLY_ACCUM")) {
        // TAPE APPLY_ACCUM <N> — average accumulated grads by N, copy to entries
        int n_accum = 1;
        float numeric_arg_51 = ctx_float(ctx, rest);
        if (ctx && ctx->error[0]) return;
        if (rest[0]) n_accum = (int)numeric_arg_51;
        if (n_accum < 1) n_accum = 1;
        am_tape_apply_accum(n_accum);
      }
      else if (!strcmp(subcmd, "PARAM") || !strcmp(subcmd, "PARAM_NO_DECAY")) {
        // TAPE PARAM <var_name> — register variable as trainable parameter
        // TAPE PARAM_NO_DECAY <var_name> — same but skip weight decay (for embeddings)
        int nd = !strcmp(subcmd, "PARAM_NO_DECAY");
        char vname[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s", vname);
        if (vname[0] && ctx) {
          AML_Var* v = resolve_var_full(ctx, vname);
          if (v && (v->type == AML_TYPE_LIST || v->type == AML_TYPE_MAP || v->type == AML_TYPE_TOKENIZER || v->type == AML_TYPE_RECORD)) {
            set_error(ctx, "TAPE requires a numeric array");
            return;
          }
          if (v && v->type == AML_TYPE_ARRAY && v->array) {
            int idx = am_tape_record_param(v->array);
            if (nd && idx >= 0) g_tape.entries[idx].no_decay = 1;
          }
        }
      }
      // ─── Save/load registered params (binary, tape-order) ───
      else if (!strcmp(subcmd, "SAVE")) {
        // TAPE SAVE "path.bin" — write all registered params to file
        char path[512] = {0};
        // Strip quotes if present
        const char* p = rest;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') { p++; int k = 0;
          while (*p && *p != '"' && k < (int)sizeof(path)-1) path[k++] = *p++;
        } else {
          sscanf(rest, "%511s", path);
        }
        if (path[0]) am_tape_save(path);
      }
      else if (!strcmp(subcmd, "LOAD")) {
        // TAPE LOAD "path.bin" — read params back into existing tape params
        char path[512] = {0};
        const char* p = rest;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') { p++; int k = 0;
          while (*p && *p != '"' && k < (int)sizeof(path)-1) path[k++] = *p++;
        } else {
          sscanf(rest, "%511s", path);
        }
        if (path[0]) am_tape_load(path);
      }
      // ─── LR schedules (one global schedule per tape) ───
      else if (!strcmp(subcmd, "LR_COSINE") || !strcmp(subcmd, "LR_STEP") ||
               !strcmp(subcmd, "LR_LINEAR")) {
        char a1[32]={0}, a2[32]={0}, a3[32]={0}, a4[32]={0};
        sscanf(rest, "%31s %31s %31s %31s", a1, a2, a3, a4);
        float numeric_arg_52 = ctx_float(ctx, a1);
        if (ctx && ctx->error[0]) return;
        float base = a1[0] ? numeric_arg_52 : 0.001f;
        float numeric_arg_53 = ctx_float(ctx, a2);
        if (ctx && ctx->error[0]) return;
        int   w    = a2[0] ? (int)numeric_arg_53 : 0;
        float numeric_arg_54 = ctx_float(ctx, a3);
        if (ctx && ctx->error[0]) return;
        float p3   = a3[0] ? numeric_arg_54 : 0.0f;
        float numeric_arg_55 = ctx_float(ctx, a4);
        if (ctx && ctx->error[0]) return;
        float p4   = a4[0] ? numeric_arg_55 : 0.0f;
        if (!strcmp(subcmd, "LR_COSINE"))
            g_aml_schedule = am_schedule_cosine(base, w, (int)p3, p4);
        else if (!strcmp(subcmd, "LR_STEP"))
            g_aml_schedule = am_schedule_step(base, w, (int)p3, p4);
        else
            g_aml_schedule = am_schedule_linear(base, w, (int)p3, p4);
      }
      else if (!strcmp(subcmd, "LR_NEXT")) {
        // TAPE LR_NEXT <var> — advance schedule, store current lr in <var>
        char vname[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s", vname);
        float lr = am_schedule_get_lr(&g_aml_schedule);
        if (vname[0] && ctx) {
          char cmd[128];
          snprintf(cmd, sizeof(cmd), "%s = %.8f", vname, lr);
          am_exec(cmd);
        }
      }
      // ─── NaN/Inf guard ───
      else if (!strcmp(subcmd, "NAN_GUARD_INIT")) {
        g_aml_nan_guard = am_nan_guard_new();
        g_aml_nan_guard_inited = 1;
      }
      else if (!strcmp(subcmd, "NAN_CHECK")) {
        // TAPE NAN_CHECK [<var>] — check for NaN in grads; if <var> given,
        // store 1 (clean) or 0 (NaN found and grads zeroed) into it
        if (!g_aml_nan_guard_inited) {
          g_aml_nan_guard = am_nan_guard_new();
          g_aml_nan_guard_inited = 1;
        }
        int ok = am_nan_guard_check(&g_aml_nan_guard);
        char vname[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s", vname);
        if (vname[0] && ctx) {
          char cmd[64]; snprintf(cmd, sizeof(cmd), "%s = %d", vname, ok);
          am_exec(cmd);
        }
      }
      // ─── Train/eval mode (global, consulted by dropout) ───
      else if (!strcmp(subcmd, "TRAIN_MODE")) {
        am_train_mode(1);
      }
      else if (!strcmp(subcmd, "EVAL_MODE")) {
        am_train_mode(0);
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ASYNC — SPAWN/AWAIT/CHANNEL (v4.0 Phase 4)
    // ─────────────────────────────────────────────────────────────────────────

#ifndef AM_ASYNC_DISABLED
    else if (!strcmp(t, "AWAIT")) {
      // AWAIT name1 name2 ... — wait for spawned threads
      if (arg && *arg) {
        char names[AML_MAX_LINE_LEN];
        snprintf(names, sizeof(names), "%s", arg);
        char* save = NULL;
        char* tok = strtok_r(names, " \t", &save);
        int failed = 0;
        while (tok) {
          if (am_spawn_await(tok) != 0 && !failed) {
            set_error_at(ctx, lineno, g_error);
            failed = 1;
          }
          tok = strtok_r(NULL, " \t", &save);
        }
      } else {
        if (am_spawn_join_all() != 0) set_error_at(ctx, lineno, g_error);
      }
    }

    else if (!strcmp(t, "CHANNEL")) {
      char subcmd[32] = {0};
      char rest[AML_MAX_LINE_LEN] = {0};
      if (arg) sscanf(arg, "%31s %[^\n]", subcmd, rest);
      upcase(subcmd);

      if (!strcmp(subcmd, "CREATE")) {
        // CHANNEL CREATE name capacity
        char chname[AM_SPAWN_NAME_LEN] = {0};
        int cap = AM_CHANNEL_BUF;
        sscanf(rest, "%31s %d", chname, &cap);
        if (cap <= 0) cap = AM_CHANNEL_BUF;
        if (cap > AM_CHANNEL_BUF) cap = AM_CHANNEL_BUF;
        if (chname[0]) am_channel_create(chname, cap);
      }
      else if (!strcmp(subcmd, "WRITE")) {
        // CHANNEL WRITE name value_expr
        char chname[AM_SPAWN_NAME_LEN] = {0};
        char vexpr[AML_MAX_LINE_LEN] = {0};
        sscanf(rest, "%31s %[^\n]", chname, vexpr);
        if (chname[0] && vexpr[0] && ctx) {
          float numeric_arg_56 = ctx_float(ctx, vexpr);
          if (ctx && ctx->error[0]) return;
          float val = numeric_arg_56;
          am_channel_write(chname, val);
        }
      }
      else if (!strcmp(subcmd, "READ")) {
        // CHANNEL READ name var_name
        char chname[AM_SPAWN_NAME_LEN] = {0};
        char vname[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s %31s", chname, vname);
        if (chname[0] && vname[0] && ctx) {
          float out = 0;
          if (am_channel_read(chname, &out) == 0) {
            /* same scope rule as assignment and PIPE READ: locals inside a function,
             * globals at top level. Clamping the depth to 0 put a top-level read into
             * locals[0], where nothing at top level ever looks for it — the value was
             * delivered by the channel and then dropped on the floor. */
            if (ctx->call_depth > 0)
              symtab_set(&ctx->locals[ctx->call_depth - 1], vname, out);
            else
              symtab_set(&ctx->globals, vname, out);
          }
        }
      }
      else if (!strcmp(subcmd, "TRY")) {
        // CHANNEL TRY name var_name — non-blocking read; var is left untouched when empty
        char chname[AM_SPAWN_NAME_LEN] = {0};
        char vname[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s %31s", chname, vname);
        if (chname[0] && vname[0] && ctx) {
          float out = 0;
          if (am_channel_try_read(chname, &out) == 0) {
            if (ctx->call_depth > 0)
              symtab_set(&ctx->locals[ctx->call_depth - 1], vname, out);
            else
              symtab_set(&ctx->globals, vname, out);
          }
        }
      }
      else if (!strcmp(subcmd, "DEPTH")) {
        // CHANNEL DEPTH name var_name — how many values are queued (-1 if no such channel)
        char chname[AM_SPAWN_NAME_LEN] = {0};
        char vname[AML_MAX_NAME] = {0};
        sscanf(rest, "%31s %31s", chname, vname);
        if (chname[0] && vname[0] && ctx) {
          float d = (float)am_channel_depth(chname);
          if (ctx->call_depth > 0)
            symtab_set(&ctx->locals[ctx->call_depth - 1], vname, d);
          else
            symtab_set(&ctx->globals, vname, d);
        }
      }
      else if (!strcmp(subcmd, "CLOSE")) {
        // CHANNEL CLOSE name
        char chname[AM_SPAWN_NAME_LEN] = {0};
        sscanf(rest, "%31s", chname);
        if (chname[0]) {
          // close specific channel by zeroing it
          for (int ci = 0; ci < g_channel_count; ci++) {
            if (g_channels[ci].active && strcmp(g_channels[ci].name, chname) == 0) {
              g_channels[ci].active = 0;
              break;
            }
          }
        } else {
          am_channel_close_all();
        }
      }
    }
#endif // AM_ASYNC_DISABLED

    // ─────────────────────────────────────────────────────────────────────────
    // UNKNOWN COMMANDS — ignored intentionally (future-proof + vibe)
    // ─────────────────────────────────────────────────────────────────────────

    // else: silently ignored
}

// ═══════════════════════════════════════════════════════════════════════════════
// PREPROCESSOR — split script into lines with indentation
// ═══════════════════════════════════════════════════════════════════════════════

typedef struct {
    AML_Line* lines;
    int count, max_lines, source_count;
    char sources[AML_MAX_IMPORTS][AML_MAX_SOURCE_PATH];
    unsigned char state[AML_MAX_IMPORTS]; // 1: expanding, 2: complete
} AML_Preparation;

static int aml_source_error(const char* origin, int lineno, const char* message) {
    snprintf(g_error, sizeof(g_error), "%.112s:%d: %.120s", origin, lineno, message);
    return -1;
}

// The root may be embedded by amlc and removed; imported files must exist.
static int aml_canonical_source(const char* path, char out[AML_MAX_SOURCE_PATH], int required) {
    char* canonical = realpath(path, NULL);
    if (!canonical && !required) {
        const char* slash = strrchr(path, '/');
        const char* basename = slash ? slash + 1 : path;
        char directory[AML_MAX_SOURCE_PATH];
        size_t n = slash ? (size_t)(slash - path) : 0;
        if (slash == path) n = 1;
        if (n >= sizeof(directory)) {
            snprintf(g_error, sizeof(g_error), "source directory exceeds %zu bytes", sizeof(directory) - 1);
            return -1;
        }
        if (slash) { memcpy(directory, path, n); directory[n] = 0; }
        else snprintf(directory, sizeof(directory), ".");
        char* parent = realpath(directory, NULL);
        if (parent) {
            int written = snprintf(out, AML_MAX_SOURCE_PATH, "%s%s%s", parent,
                                   strcmp(parent, "/") == 0 ? "" : "/", basename);
            free(parent);
            if (written < 0 || written >= AML_MAX_SOURCE_PATH) {
                snprintf(g_error, sizeof(g_error), "source path exceeds %d bytes", AML_MAX_SOURCE_PATH - 1);
                return -1;
            }
            return 0;
        }
    }
    if (!canonical) {
        snprintf(g_error, sizeof(g_error), "cannot resolve source: %.220s", path);
        return -1;
    }
    if (strlen(canonical) >= AML_MAX_SOURCE_PATH) {
        free(canonical);
        snprintf(g_error, sizeof(g_error), "source path exceeds %d bytes", AML_MAX_SOURCE_PATH - 1);
        return -1;
    }
    strcpy(out, canonical);
    free(canonical);
    return 0;
}

static int aml_expand_source(AML_Preparation* prep, const char* script, const char* origin, int depth);

static int aml_expand_import(AML_Preparation* prep, const char* text,
                             const char* origin, int lineno, int depth) {
    const char* start = text + 6;
    while (isspace((unsigned char)*start)) start++;
    if (*start++ != '"') return aml_source_error(origin, lineno, "IMPORT requires a quoted path");
    const char* end = strchr(start, '"');
    if (!end || end == start) return aml_source_error(origin, lineno, "invalid IMPORT path");
    const char* tail = end + 1;
    while (isspace((unsigned char)*tail)) tail++;
    if (*tail && *tail != '#') return aml_source_error(origin, lineno, "unexpected text after IMPORT path");
    char name[AML_MAX_SOURCE_PATH], path[AML_MAX_SOURCE_PATH * 2], canonical[AML_MAX_SOURCE_PATH];
    size_t len = (size_t)(end - start);
    if (len >= sizeof(name)) return aml_source_error(origin, lineno, "IMPORT path too long");
    memcpy(name, start, len); name[len] = 0;
    const char* slash = strrchr(origin, '/');
    int written = name[0] == '/'
        ? snprintf(path, sizeof(path), "%s", name)
        : snprintf(path, sizeof(path), "%.*s/%s", slash ? (int)(slash - origin) : 0, origin, name);
    if (written < 0 || (size_t)written >= sizeof(path))
        return aml_source_error(origin, lineno, "IMPORT path too long");
    if (aml_canonical_source(path, canonical, 1) != 0) {
        char detail[256]; snprintf(detail, sizeof(detail), "%s", g_error);
        return aml_source_error(origin, lineno, detail);
    }
    for (int i = 0; i < prep->source_count; i++) {
        if (strcmp(canonical, prep->sources[i]) == 0) {
            if (prep->state[i] == 1) return aml_source_error(origin, lineno, "IMPORT cycle detected");
            return 0;
        }
    }
    if (depth >= AML_MAX_IMPORT_DEPTH)
        return aml_source_error(origin, lineno, "IMPORT depth limit exceeded");
    if (prep->source_count >= AML_MAX_IMPORTS)
        return aml_source_error(origin, lineno, "IMPORT source limit exceeded");
    struct stat st;
    if (stat(canonical, &st) != 0 || !S_ISREG(st.st_mode))
        return aml_source_error(origin, lineno, "IMPORT source is not a regular file");
    if (st.st_size < 0 || st.st_size > 1024 * 1024)
        return aml_source_error(origin, lineno, "IMPORT source exceeds 1 MiB");
    FILE* f = fopen(canonical, "rb");
    if (!f) return aml_source_error(origin, lineno, "cannot open IMPORT source");
    size_t size = (size_t)st.st_size;
    char* imported = (char*)malloc(size + 2);
    if (!imported) { fclose(f); return aml_source_error(origin, lineno, "IMPORT allocation failed"); }
    size_t read = fread(imported, 1, size + 1, f);
    int bad_read = ferror(f) || read != size || memchr(imported, 0, read) != NULL;
    fclose(f);
    if (bad_read) { free(imported); return aml_source_error(origin, lineno, "invalid IMPORT source read"); }
    imported[read] = 0;
    int slot = prep->source_count++;
    strcpy(prep->sources[slot], canonical);
    prep->state[slot] = 1;
    int rc = aml_expand_source(prep, imported, canonical, depth + 1);
    free(imported);
    if (rc == 0) prep->state[slot] = 2;
    return rc;
}

static int aml_expand_source(AML_Preparation* prep, const char* script, const char* origin, int depth) {
    const char* p = script;
    int lineno = 1;
    while (*p) {
        int indent = 0;
        while (*p == ' ' || *p == '\t') { indent += *p == '\t' ? 4 : 1; p++; }
        const char* start = p;
        while (*p && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (*p == '\n') p++;
        while (len && isspace((unsigned char)start[len - 1])) len--;
        if (!len || start[0] == '#') { lineno++; continue; }
        if (len >= AML_MAX_LINE_LEN)
            return aml_source_error(origin, lineno, "AML source line exceeds AML_MAX_LINE_LEN");
        if (prep->count >= prep->max_lines)
            return aml_source_error(origin, lineno, "expanded AML line limit exceeded");
        AML_Line* line = &prep->lines[prep->count++];
        memcpy(line->text, start, len); line->text[len] = 0;
        line->indent = indent; line->lineno = lineno;
        strcpy(line->origin, origin);
        if (strncasecmp(line->text, "IMPORT", 6) == 0 &&
            (!line->text[6] || isspace((unsigned char)line->text[6]))) {
            const char* pack = line->text + 6;
            while (isspace((unsigned char)*pack)) pack++;
            // Existing bare pack imports remain runtime aliases for MODE.
            if (!strcasecmp(pack, "CODES_RIC") || !strcasecmp(pack, "CODES/RIC") ||
                !strcasecmp(pack, "DARKMATTER") || !strcasecmp(pack, "NOTORCH")) {
                lineno++;
                continue;
            }
            if (indent) return aml_source_error(origin, lineno, "IMPORT must be top-level");
            char directive[AML_MAX_LINE_LEN]; strcpy(directive, line->text);
            // Retain a no-op boundary even for empty or already imported files.
            line->text[0] = 0;
            if (aml_expand_import(prep, directive, origin, lineno, depth) != 0) return -1;
        }
        lineno++;
    }
    return 0;
}

static int aml_preprocess(const char* script, AML_Line* lines, int max_lines) {
    AML_Preparation* prep = (AML_Preparation*)calloc(1, sizeof(*prep));
    if (!prep) { snprintf(g_error, sizeof(g_error), "source preparation allocation failed"); return -1; }
    prep->lines = lines; prep->max_lines = max_lines;
    char root_path[AML_MAX_SOURCE_PATH * 2];
    if (g_source_path[0]) snprintf(root_path, sizeof(root_path), "%s", g_source_path);
    else snprintf(root_path, sizeof(root_path), "%s/<memory>", g_base_dir);
    if (aml_canonical_source(root_path, prep->sources[0], 0) != 0) { free(prep); return -1; }
    prep->state[0] = 1; prep->source_count = 1;
    int rc = aml_expand_source(prep, script, prep->sources[0], 0);
    int count = prep->count;
    free(prep);
    return rc == 0 ? count : -1;
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

static int aml_reserved_function(const char* name) {
    static const char* names[] = {
        "abs", "min", "max", "sqrt", "clamp", "len", "sum", "dot", "rows", "cols",
        "zeros", "randn", "add", "mul", "scale", "matrix", "matrix_zeros", "matvec",
        "matmul", "softmax", "rmsnorm", "silu", "gelu", "dropout", "layernorm",
        "seq_layernorm", "spa_embed", "spa_connectedness", "relu", "cross_entropy",
        "embedding_lookup", "row", "seq_embed", "seq_matvec", "seq_rmsnorm",
        "causal_attention", "multi_head_attention", "seq_cross_entropy",
        "text_len", "text_bytes", "text_equal", "text_find", "text_slice",
        "text_concat", "text_codepoint", "text_from_codepoint", "text_lower",
        "list_new", "list_len", "list_get", "list_push", "list_set", "list_find",
        "list_slice", "list_clone", "list_sorted", "list_key",
        "map_new", "map_len", "map_has", "map_get", "map_set", "map_delete",
        "map_keys", "map_clone", "assert", "floor", "isfinite", "rng_new", "rng_uniform",
        "rng_index", "rng_categorical", "categorical_at", "nt_linear", "nt_linear_vjp",
        "nt_tanh", "nt_tanh_vjp", "nt_mse_grad", "nt_sgd", "rng_normal",
        "tokenizer_load", "tokenizer_pieces", "tokenizer_identity", "codepoint_isalnum", "read_line",
        "record_new", "record_set", "record_get", "record_has", "record_keys", "record_kind",
        "record_clone", "record_replace", "record_swap", "checkpoint_save", "checkpoint_load", "file_exists"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(name, names[i]) == 0) return 1;
    return 0;
}

static void aml_definition_error(AML_ExecCtx* ctx, int line, const char* message) {
    aml_source_error(ctx->lines[line].origin, ctx->lines[line].lineno, message);
    snprintf(ctx->error, sizeof(ctx->error), "%s", g_error);
}

// Register all function definitions (first pass)
static void aml_register_funcs(AML_ExecCtx* ctx) {
    for (int i = 0; i < ctx->nlines; i++) {
        char* text = ctx->lines[i].text;
        if (strncmp(text, "def ", 4) != 0) continue;

        // parse: def name(param1, param2):
        char* name_start = text + 4;
        while (*name_start == ' ') name_start++;
        char* paren = strchr(name_start, '(');
        if (!paren) { aml_definition_error(ctx, i, "invalid function declaration"); return; }

        if (ctx->funcs.count >= AML_MAX_FUNCS) {
            aml_definition_error(ctx, i, "function limit exceeded"); return;
        }
        AML_Func* f = &ctx->funcs.funcs[ctx->funcs.count];
        memset(f, 0, sizeof(*f));

        int nlen = (int)(paren - name_start);
        while (nlen && isspace((unsigned char)name_start[nlen - 1])) nlen--;
        if (!nlen || nlen >= AML_MAX_NAME ||
            !(isalpha((unsigned char)name_start[0]) || name_start[0] == '_')) {
            aml_definition_error(ctx, i, "invalid or overlong function name"); return;
        }
        for (int ni = 1; ni < nlen; ni++) {
            if (!(isalnum((unsigned char)name_start[ni]) || name_start[ni] == '_')) {
                aml_definition_error(ctx, i, "invalid function name"); return;
            }
        }
        memcpy(f->name, name_start, nlen);
        f->name[nlen] = 0;
        if (aml_reserved_function(f->name)) {
            aml_definition_error(ctx, i, "function name collides with an intrinsic"); return;
        }
        for (int fi = 0; fi < ctx->funcs.count; fi++) {
            const AML_Func* previous = &ctx->funcs.funcs[fi];
            if (strcmp(f->name, previous->name) == 0 ||
                (previous->is_builtin && strcasecmp(f->name, previous->name) == 0)) {
                aml_definition_error(ctx, i, "duplicate function name"); return;
            }
        }

        // parse params
        f->param_count = 0;
        char* pp = paren + 1;
        while (*pp && *pp != ')') {
            while (isspace((unsigned char)*pp)) pp++;
            if (*pp == ')') break;
            if (f->param_count >= AML_MAX_PARAMS) {
                aml_definition_error(ctx, i, "function parameter limit exceeded"); return;
            }
            char* pe = pp;
            if (!(isalpha((unsigned char)*pe) || *pe == '_')) {
                aml_definition_error(ctx, i, "invalid function parameter"); return;
            }
            while (isalnum((unsigned char)*pe) || *pe == '_') pe++;
            int plen = (int)(pe - pp);
            if (plen >= AML_MAX_NAME) {
                aml_definition_error(ctx, i, "function parameter name too long"); return;
            }
            memcpy(f->params[f->param_count], pp, plen);
            f->params[f->param_count][plen] = 0;
            for (int pi = 0; pi < f->param_count; pi++) {
                if (strcmp(f->params[pi], f->params[f->param_count]) == 0) {
                    aml_definition_error(ctx, i, "duplicate function parameter"); return;
                }
            }
            f->param_count++;
            pp = pe;
            while (isspace((unsigned char)*pp)) pp++;
            if (*pp == ',') pp++;
            else if (*pp != ')') {
                aml_definition_error(ctx, i, "invalid function parameter list"); return;
            }
        }
        if (*pp++ != ')') { aml_definition_error(ctx, i, "unterminated function parameters"); return; }
        while (isspace((unsigned char)*pp)) pp++;
        if (*pp++ != ':') { aml_definition_error(ctx, i, "function declaration needs ':'"); return; }
        while (isspace((unsigned char)*pp)) pp++;
        if (*pp && *pp != '#') { aml_definition_error(ctx, i, "unexpected text after function declaration"); return; }

        f->body_start = i + 1;
        f->body_end = aml_find_block_end(ctx->lines, ctx->nlines, i);
        ctx->funcs.count++;

        // skip body
        i = f->body_end - 1;
    }
}

// Call a user-defined function
// lineno is the caller's line number (for error reporting)
// v4.0: supports return values via ctx->has_return / return_value / return_array
static int aml_call_value(AML_ExecCtx* ctx, AML_Func* f, AML_Var* args,
                          int nargs, int lineno, AML_Var* out) {
    // Built-in functions: dispatch to C code directly
    if (f->is_builtin) {
        float scalar_args[AML_MAX_PARAMS];
        for (int i = 0; i < nargs; i++) {
            if (args[i].type != AML_TYPE_FLOAT) {
                set_error_at(ctx, lineno, "field function requires scalar arguments");
                return 1;
            }
            scalar_args[i] = args[i].value;
        }
        aml_exec_builtin(f->body_start, scalar_args, nargs);
        return 0;
    }

    if (ctx->call_depth >= AML_MAX_CALL_DEPTH) {
        set_error_at(ctx, lineno, "max call depth exceeded");
        return 1;
    }
    if (nargs != f->param_count) {
        set_error_at(ctx, lineno, "wrong number of function arguments");
        return 1;
    }

    // Save caller's return state (nested calls must not clobber it)
    int saved_has_return = ctx->has_return;
    float saved_return_value = ctx->return_value;
    AM_Array* saved_return_array = ctx->return_array;
    AM_String* saved_return_string = ctx->return_string;
    AM_List* saved_return_list = ctx->return_list;
    AM_Map* saved_return_map = ctx->return_map;
    AM_Tokenizer* saved_return_tokenizer = ctx->return_tokenizer;
    AM_Record* saved_return_record = ctx->return_record;
    int saved_return_type = ctx->return_type;

    // push local scope
    ctx->call_depth++;
    AML_Symtab* locals = &ctx->locals[ctx->call_depth - 1];
    memset(locals, 0, sizeof(AML_Symtab));

    // bind params
    for (int i = 0; i < f->param_count && i < nargs; i++) {
        if (args[i].type == AML_TYPE_ARRAY) {
            am_array_ref(args[i].array);
            symtab_set_array(locals, f->params[i], args[i].array);
        } else if (args[i].type == AML_TYPE_STRING) {
            am_string_ref(args[i].string);
            symtab_set_string(locals, f->params[i], args[i].string);
        } else if (args[i].type == AML_TYPE_LIST) {
            am_list_ref(args[i].list);
            symtab_set_list(locals, f->params[i], args[i].list);
        } else if (args[i].type == AML_TYPE_MAP) {
            am_map_ref(args[i].map);
            symtab_set_map(locals, f->params[i], args[i].map);
        } else if (args[i].type == AML_TYPE_TOKENIZER) {
            am_tokenizer_ref(args[i].tokenizer);
            symtab_set_tokenizer(locals, f->params[i], args[i].tokenizer);
        } else if (args[i].type == AML_TYPE_RECORD) {
            am_record_ref(args[i].record);
            symtab_set_record(locals, f->params[i], args[i].record);
        } else {
            symtab_set(locals, f->params[i], args[i].value);
        }
    }

    // reset return state for this function
    ctx->has_return = 0;
    ctx->return_value = 0;
    ctx->return_array = NULL;
    ctx->return_string = NULL;
    ctx->return_list = NULL;
    ctx->return_map = NULL;
    ctx->return_tokenizer = NULL;
    ctx->return_record = NULL;
    ctx->return_type = AML_TYPE_FLOAT;

    // execute body
    aml_exec_block(ctx, f->body_start, f->body_end);
    if (ctx->has_return) {
        out->type = ctx->return_type;
        out->value = ctx->return_value;
        out->array = ctx->return_array;
        out->string = ctx->return_string;
        out->list = ctx->return_list;
        out->map = ctx->return_map;
        out->tokenizer = ctx->return_tokenizer;
        out->record = ctx->return_record;
    }
    // Return expressions already own their references, including local aliases.
    symtab_clear_arrays(locals);

    // pop scope
    ctx->call_depth--;

    ctx->has_return = saved_has_return;
    ctx->return_value = saved_return_value;
    ctx->return_array = saved_return_array;
    ctx->return_string = saved_return_string;
    ctx->return_list = saved_return_list;
    ctx->return_map = saved_return_map;
    ctx->return_tokenizer = saved_return_tokenizer;
    ctx->return_record = saved_return_record;
    ctx->return_type = saved_return_type;
    return ctx->error[0] != 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// v4.0: ARRAY HELPER — try to parse RHS as array-producing expression
// Returns newly allocated AM_Array*, or NULL if not an array expression.
// Handles: zeros(n), randn(n, std), [1.0, 2.0, 3.0], add(a,b), mul(a,b),
//          scale(a, s), and user function calls that return arrays.
// ═══════════════════════════════════════════════════════════════════════════════

// Forward declaration for bytecode dispatch
static AM_Array* aml_array_dispatch(AML_ExecCtx* ctx, const char* fname, char arg_strs[][AML_MAX_LINE_LEN], int nargs);

static int aml_array_function(const char* name) {
    static const char* names[] = {
        "zeros", "randn", "add", "mul", "scale", "matrix", "matrix_zeros",
        "matvec", "matmul", "softmax", "rmsnorm", "silu", "gelu", "dropout",
        "layernorm", "seq_layernorm", "spa_embed", "spa_connectedness", "relu",
        "cross_entropy", "embedding_lookup", "row", "seq_embed", "seq_matvec",
        "seq_rmsnorm", "causal_attention", "multi_head_attention", "seq_cross_entropy"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcasecmp(name, names[i])) return 1;
    return 0;
}

static AM_Array* aml_try_array_expr(AML_ExecCtx* ctx, const char* rhs) {
    // skip whitespace
    while (*rhs == ' ') rhs++;

    // --- array literal: [1.0, 2.0, 3.0] ---
    if (*rhs == '[') {
        rhs++;
        float vals[256];
        int count = 0;
        while (*rhs && *rhs != ']' && count < 256) {
            while (*rhs == ' ' || *rhs == ',') rhs++;
            if (*rhs == ']') break;
            char* end;
            float value = strtof(rhs, &end);
            if (end == rhs) {
                set_error(ctx, "array literal requires numeric elements");
                return NULL;
            }
            vals[count++] = value;
            rhs = end;
            while (isspace((unsigned char)*rhs)) rhs++;
            if (*rhs != ',' && *rhs != ']') {
                set_error(ctx, "invalid array literal");
                return NULL;
            }
        }
        if (*rhs != ']') { set_error(ctx, "unterminated array literal"); return NULL; }
        rhs++;
        while (isspace((unsigned char)*rhs)) rhs++;
        if (*rhs && *rhs != '#') { set_error(ctx, "unexpected text after array literal"); return NULL; }
        if (count > 0) {
            AM_Array* arr = am_array_new(count);
            if (arr) memcpy(arr->data, vals, count * sizeof(float));
            return arr;
        }
        return NULL;
    }

    // --- function call: name(args) ---
    char fname[AML_MAX_NAME] = {0};
    int fi = 0;
    while ((isalnum((unsigned char)rhs[fi]) || rhs[fi] == '_') && fi < AML_MAX_NAME - 1) {
        fname[fi] = rhs[fi]; fi++;
    }
    fname[fi] = 0;
    if (!aml_array_function(fname)) return NULL;
    const char* after_name = rhs + fi;
    while (*after_name == ' ') after_name++;
    if (*after_name != '(') return NULL;

    // Parse arguments as raw text tokens (needed for variable names)
    const char* ap = after_name + 1;
    char arg_strs[AML_MAX_PARAMS][AML_MAX_LINE_LEN];
    int nargs = 0;
    while (*ap && *ap != ')' && nargs < AML_MAX_PARAMS) {
        while (isspace((unsigned char)*ap)) ap++;
        if (*ap == ')') break;
        int ai = 0;
        // Capture the whole argument expression (may be a number or identifier)
        int paren_depth = 0;
        char quote = 0;
        while (*ap && (quote || paren_depth > 0 || (*ap != ',' && *ap != ')'))) {
            if (ai >= AML_MAX_LINE_LEN - 1) {
                set_error(ctx, "array argument too long"); return NULL;
            }
            if (quote) {
                if (*ap == '\\' && ap[1]) {
                    if (ai >= AML_MAX_LINE_LEN - 2) {
                        set_error(ctx, "array argument too long"); return NULL;
                    }
                    arg_strs[nargs][ai++] = *ap++;
                    arg_strs[nargs][ai++] = *ap++;
                    continue;
                }
                if (*ap == quote) quote = 0;
            } else if (*ap == '"' || *ap == '\'') quote = *ap;
            else if (*ap == '(' || *ap == '[') paren_depth++;
            else if (*ap == ')' || *ap == ']') paren_depth--;
            arg_strs[nargs][ai++] = *ap++;
        }
        if (quote || paren_depth) { set_error(ctx, "invalid array argument"); return NULL; }
        // Trim trailing spaces
        while (ai > 0 && arg_strs[nargs][ai-1] == ' ') ai--;
        if (!ai) { set_error(ctx, "empty array argument"); return NULL; }
        arg_strs[nargs][ai] = 0;
        nargs++;
        if (*ap != ',') break;
        ap++;
        while (isspace((unsigned char)*ap)) ap++;
        if (!*ap || *ap == ')') { set_error(ctx, "empty array argument"); return NULL; }
    }
    if (*ap != ')') { set_error(ctx, "invalid array argument list"); return NULL; }
    ap++;
    while (isspace((unsigned char)*ap)) ap++;
    if (*ap && *ap != '#') { set_error(ctx, "unexpected text after array call"); return NULL; }

    return aml_array_dispatch(ctx, fname, arg_strs, nargs);
}

// Evaluate an optional numeric array once. Scalar values (including the legacy
// undefined-name zero) still mean "no array"; string/list/map values are errors.
// A returned array reference is owned by the caller.
static int aml_optional_array(AML_ExecCtx* ctx, const char* expression, AM_Array** out) {
    AML_Var value = {0};
    *out = NULL;
    if (aml_eval_value(ctx, expression, &value)) {
        aml_value_clear(&value); return 1;
    }
    if (value.type == AML_TYPE_STRING || value.type == AML_TYPE_LIST || value.type == AML_TYPE_MAP || value.type == AML_TYPE_TOKENIZER || value.type == AML_TYPE_RECORD) {
        aml_value_clear(&value);
        set_error(ctx, "optional argument requires a numeric array"); return 1;
    }
    if (value.type == AML_TYPE_ARRAY) {
        *out = value.array;
#ifdef USE_CUDA
        ensure_cpu(*out);
#endif
        value.array = NULL;
    }
    aml_value_clear(&value);
    return 0;
}

// Dispatch pre-parsed array function call (called from both interpreter and bytecode)
static AM_Array* aml_array_dispatch(AML_ExecCtx* ctx, const char* fname, char arg_strs[][AML_MAX_LINE_LEN], int nargs) {
    // Numeric array intrinsics cannot consume string containers, including in
    // optional gamma/beta/bias positions that otherwise permit absent arrays.
    // Leave scalar/text/list/map/user calls to their own evaluator without effects.
    if (aml_array_function(fname)) {
        for (int i = 0; i < nargs; i++) {
            AML_Var* v = resolve_var_full(ctx, arg_strs[i]);
            if (v && (v->type == AML_TYPE_LIST || v->type == AML_TYPE_MAP || v->type == AML_TYPE_TOKENIZER || v->type == AML_TYPE_RECORD)) {
                set_error(ctx, v->type == AML_TYPE_RECORD
                    ? "numeric array operation cannot consume a record" : v->type == AML_TYPE_TOKENIZER
                    ? "numeric array operation cannot consume a tokenizer" : v->type == AML_TYPE_MAP
                    ? "numeric array operation cannot consume a map"
                    : "numeric array operation cannot consume a list");
                return NULL;
            }
        }
    }
    // zeros(n) — create zero-initialized array
    if (strcasecmp(fname, "zeros") == 0 && nargs >= 1) {
        int n = (int)aml_eval(ctx, arg_strs[0]);
        if (ctx->error[0]) return NULL;
        if (n > 0 && n <= AM_MAX_ARRAY_SIZE) return am_array_new(n);
        return NULL;
    }

    // randn(n, std) — random normal initialization
    if (strcasecmp(fname, "randn") == 0 && nargs >= 1) {
        int n = (int)aml_eval(ctx, arg_strs[0]);
        if (ctx->error[0]) return NULL;
        float std = (nargs >= 2) ? aml_eval(ctx, arg_strs[1]) : 1.0f;
        if (ctx->error[0]) return NULL;
        if (n <= 0 || n > AM_MAX_ARRAY_SIZE) return NULL;
        AM_Array* arr = am_array_new(n);
        if (!arr) return NULL;
        // Box-Muller transform for normal distribution
        for (int j = 0; j < n; j += 2) {
            float u1 = ((float)rand() / (float)RAND_MAX) * 0.9998f + 0.0001f;
            float u2 = ((float)rand() / (float)RAND_MAX);
            float r = sqrtf(-2.0f * logf(u1));
            arr->data[j] = r * cosf(2.0f * 3.14159265f * u2) * std;
            if (j + 1 < n)
                arr->data[j + 1] = r * sinf(2.0f * 3.14159265f * u2) * std;
        }
        return arr;
    }

    // add(a, b) — element-wise addition
    if (strcasecmp(fname, "add") == 0 && nargs >= 2) {
        AML_Var* va = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vb = resolve_var_full(ctx, arg_strs[1]);
        if (va && va->type == AML_TYPE_ARRAY && va->array &&
            vb && vb->type == AML_TYPE_ARRAY && vb->array) {
            int n = va->array->len < vb->array->len ? va->array->len : vb->array->len;
            AM_Array* arr = am_array_new(n);
            if (!arr) return NULL;
#ifdef USE_CUDA
            if (va->array->d_data && va->array->gpu_valid &&
                vb->array->d_data && vb->array->gpu_valid) {
                out_arr_gpu: ;
                arr->d_data = gpu_alloc(n);
                if (arr->d_data) {
                    gpu_add(arr->d_data, va->array->d_data, vb->array->d_data, n);
                    arr->gpu_valid = 1;
                    goto add_done;
                }
            }
            ensure_cpu(va->array); ensure_cpu(vb->array);
#endif
            for (int j = 0; j < n; j++)
                arr->data[j] = va->array->data[j] + vb->array->data[j];
            if (am_tape_is_active())
#ifdef USE_CUDA
            add_done: ;
#endif
                am_tape_record(arr, AM_OP_ADD, tape_ensure_entry(va->array), tape_ensure_entry(vb->array), 0);
            return arr;
        }
        return NULL;
    }

    // mul(a, b) — element-wise multiplication
    if (strcasecmp(fname, "mul") == 0 && nargs >= 2) {
        AML_Var* va = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vb = resolve_var_full(ctx, arg_strs[1]);
        if (va && va->type == AML_TYPE_ARRAY && va->array &&
            vb && vb->type == AML_TYPE_ARRAY && vb->array) {
            int n = va->array->len < vb->array->len ? va->array->len : vb->array->len;
            AM_Array* arr = am_array_new(n);
            if (!arr) return NULL;
#ifdef USE_CUDA
            if (va->array->d_data && va->array->gpu_valid &&
                vb->array->d_data && vb->array->gpu_valid) {
                arr->d_data = gpu_alloc(n);
                if (arr->d_data) {
                    gpu_mul(arr->d_data, va->array->d_data, vb->array->d_data, n);
                    arr->gpu_valid = 1;
                    goto mul_done;
                }
            }
            ensure_cpu(va->array); ensure_cpu(vb->array);
#endif
            for (int j = 0; j < n; j++)
                arr->data[j] = va->array->data[j] * vb->array->data[j];
            if (am_tape_is_active())
#ifdef USE_CUDA
            mul_done: ;
#endif
                am_tape_record(arr, AM_OP_MUL, tape_ensure_entry(va->array), tape_ensure_entry(vb->array), 0);
            return arr;
        }
        return NULL;
    }

    // scale(a, scalar) — scalar multiplication
    if (strcasecmp(fname, "scale") == 0 && nargs >= 2) {
        AML_Var* va = resolve_var_full(ctx, arg_strs[0]);
        float scalar = aml_eval(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        if (va && va->type == AML_TYPE_ARRAY && va->array) {
            AM_Array* arr = am_array_new(va->array->len);
            if (!arr) return NULL;
            for (int j = 0; j < va->array->len; j++)
                arr->data[j] = va->array->data[j] * scalar;
            if (am_tape_is_active())
                am_tape_record(arr, AM_OP_SCALE, tape_ensure_entry(va->array), -1, scalar);
            return arr;
        }
        return NULL;
    }

    // ── Phase 2: Matrix/Tensor operations ──

    // matrix(rows, cols, std) — create matrix with random normal init
    if (strcasecmp(fname, "matrix") == 0 && nargs >= 2) {
        int rows = (int)aml_eval(ctx, arg_strs[0]);
        if (ctx->error[0]) return NULL;
        int cols = (int)aml_eval(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        float std = (nargs >= 3) ? aml_eval(ctx, arg_strs[2]) : 0.08f;
        if (ctx->error[0]) return NULL;
        AM_Array* arr = am_matrix_new(rows, cols);
        if (!arr) return NULL;
        for (int j = 0; j < arr->len; j += 2) {
            float u1 = ((float)rand() / (float)RAND_MAX) * 0.9998f + 0.0001f;
            float u2 = ((float)rand() / (float)RAND_MAX);
            float r = sqrtf(-2.0f * logf(u1));
            arr->data[j] = r * cosf(2.0f * 3.14159265f * u2) * std;
            if (j + 1 < arr->len)
                arr->data[j + 1] = r * sinf(2.0f * 3.14159265f * u2) * std;
        }
        return arr;
    }

    // matrix_zeros(rows, cols) — create zero-initialized matrix
    if (strcasecmp(fname, "matrix_zeros") == 0 && nargs >= 2) {
        int rows = (int)aml_eval(ctx, arg_strs[0]);
        if (ctx->error[0]) return NULL;
        int cols = (int)aml_eval(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        return am_matrix_new(rows, cols);
    }

    // matvec(W, x) — matrix × vector → vector
    if (strcasecmp(fname, "matvec") == 0 && nargs >= 2) {
        AML_Var* vw = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vx = resolve_var_full(ctx, arg_strs[1]);
        if (vw && vw->type == AML_TYPE_ARRAY && vw->array && vw->array->rows > 0 &&
            vx && vx->type == AML_TYPE_ARRAY && vx->array) {
            int rows = vw->array->rows;
            int cols = vw->array->cols;
            if (cols != vx->array->len) return NULL;
            AM_Array* out = am_array_new(rows);
            if (!out) return NULL;
#ifdef USE_BLAS
            cblas_sgemv(CblasRowMajor, CblasNoTrans, rows, cols,
                        1.0f, vw->array->data, cols, vx->array->data, 1,
                        0.0f, out->data, 1);
#else
            for (int i = 0; i < rows; i++) {
                float s = 0;
                for (int j = 0; j < cols; j++)
                    s += vw->array->data[i * cols + j] * vx->array->data[j];
                out->data[i] = s;
            }
#endif
            if (am_tape_is_active())
                am_tape_record(out, AM_OP_MATVEC, tape_ensure_entry(vw->array), tape_ensure_entry(vx->array), 0);
            return out;
        }
        return NULL;
    }

    // matmul(A, B) — matrix × matrix → matrix
    if (strcasecmp(fname, "matmul") == 0 && nargs >= 2) {
        AML_Var* va = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vb = resolve_var_full(ctx, arg_strs[1]);
        if (va && va->type == AML_TYPE_ARRAY && va->array && va->array->rows > 0 &&
            vb && vb->type == AML_TYPE_ARRAY && vb->array && vb->array->rows > 0) {
            int m = va->array->rows, k = va->array->cols;
            int k2 = vb->array->rows, n = vb->array->cols;
            if (k != k2) return NULL;
            AM_Array* out = am_matrix_new(m, n);
            if (!out) return NULL;
#ifdef USE_BLAS
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                        m, n, k, 1.0f,
                        va->array->data, k, vb->array->data, n,
                        0.0f, out->data, n);
#else
            for (int i = 0; i < m; i++)
                for (int j = 0; j < n; j++) {
                    float s = 0;
                    for (int p = 0; p < k; p++)
                        s += va->array->data[i * k + p] * vb->array->data[p * n + j];
                    out->data[i * n + j] = s;
                }
#endif
            return out;
        }
        return NULL;
    }

    // softmax(x) — softmax over 1D array
    if (strcasecmp(fname, "softmax") == 0 && nargs >= 1) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        if (vx && vx->type == AML_TYPE_ARRAY && vx->array) {
            int n = vx->array->len;
            AM_Array* out = am_array_new(n);
            if (!out) return NULL;
            // Find max for numerical stability
            float mx = vx->array->data[0];
            for (int j = 1; j < n; j++)
                if (vx->array->data[j] > mx) mx = vx->array->data[j];
            float sum = 0;
            for (int j = 0; j < n; j++) {
                out->data[j] = expf(vx->array->data[j] - mx);
                sum += out->data[j];
            }
            if (sum > 0) for (int j = 0; j < n; j++) out->data[j] /= sum;
            if (am_tape_is_active())
                am_tape_record(out, AM_OP_SOFTMAX, tape_ensure_entry(vx->array), -1, 0);
            return out;
        }
        return NULL;
    }

    // rmsnorm(x) — RMS normalization
    if (strcasecmp(fname, "rmsnorm") == 0 && nargs >= 1) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        if (vx && vx->type == AML_TYPE_ARRAY && vx->array) {
            int n = vx->array->len;
            AM_Array* out = am_array_new(n);
            if (!out) return NULL;
            float ss = 0;
            for (int j = 0; j < n; j++) ss += vx->array->data[j] * vx->array->data[j];
            float rms = sqrtf(ss / n + 1e-6f);
            for (int j = 0; j < n; j++) out->data[j] = vx->array->data[j] / rms;
            if (am_tape_is_active())
                am_tape_record(out, AM_OP_RMSNORM, tape_ensure_entry(vx->array), -1, 0);
            return out;
        }
        return NULL;
    }

    // silu(x) — SiLU/Swish activation: x * sigmoid(x)
    if (strcasecmp(fname, "silu") == 0 && nargs >= 1) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        AM_Array* input_arr = NULL;
        int owns_input = 0;  // 1 if we created input_arr via recursive call
        if (vx && vx->type == AML_TYPE_ARRAY && vx->array) {
            input_arr = vx->array;
        } else {
            // Try recursive evaluation: silu(seq_matvec(...)) etc.
            input_arr = aml_try_array_expr(ctx, arg_strs[0]);
            if (input_arr) owns_input = 1;
        }
        if (input_arr) {
            int n = input_arr->len;
            AM_Array* out = am_array_new(n);
            if (!out) { if (owns_input) am_array_free(input_arr); return NULL; }
            for (int j = 0; j < n; j++) {
#ifdef USE_CUDA
            if (input_arr->d_data && input_arr->gpu_valid) {
                out->d_data = gpu_alloc(n);
                if (out->d_data) {
                    gpu_silu(out->d_data, input_arr->d_data, n);
                    out->gpu_valid = 1;
                    goto silu_done;
                }
            }
            ensure_cpu(input_arr);
#endif
                float x = input_arr->data[j];
                out->data[j] = x / (1.0f + expf(-x));
            }
            if (am_tape_is_active())
#ifdef USE_CUDA
            silu_done: ;
#endif
                am_tape_record(out, AM_OP_SILU, tape_ensure_entry(input_arr), -1, 0);
            // Don't free input_arr even if owns_input — tape may reference it
            return out;
        }
        return NULL;
    }

    // gelu(x) — Gaussian Error Linear Unit (tanh approximation, Hendrycks)
    if (strcasecmp(fname, "gelu") == 0 && nargs >= 1) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        AM_Array* input_arr = NULL;
        if (vx && vx->type == AML_TYPE_ARRAY && vx->array) {
            input_arr = vx->array;
        } else {
            input_arr = aml_try_array_expr(ctx, arg_strs[0]);
        }
        if (input_arr) {
            int n = input_arr->len;
            AM_Array* out = am_array_new(n);
            if (!out) return NULL;
            for (int j = 0; j < n; j++) {
                float x = input_arr->data[j];
                float x3 = x * x * x;
                float inner = 0.7978845608f * (x + 0.044715f * x3);
                out->data[j] = 0.5f * x * (1.0f + tanhf(inner));
            }
            if (am_tape_is_active())
                am_tape_record(out, AM_OP_GELU, tape_ensure_entry(input_arr), -1, 0);
            return out;
        }
        return NULL;
    }

    // dropout(x, p) — inverted dropout. Uses am_is_training() to decide.
    if (strcasecmp(fname, "dropout") == 0 && nargs >= 1) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        AM_Array* input_arr = NULL;
        if (vx && vx->type == AML_TYPE_ARRAY && vx->array) {
            input_arr = vx->array;
        } else {
            input_arr = aml_try_array_expr(ctx, arg_strs[0]);
        }
        float p = 0.1f;
        if (nargs >= 2) p = ctx_float(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        if (input_arr) {
            int n = input_arr->len;
            AM_Array* out = am_array_new(n);
            if (!out) return NULL;
            if (am_is_training() && p > 0.0f && p < 1.0f) {
                static uint32_t drop_rng = 0xDE0B1EDEu;
                float scale = 1.0f / (1.0f - p);
                for (int j = 0; j < n; j++) {
                    drop_rng ^= drop_rng << 13;
                    drop_rng ^= drop_rng >> 17;
                    drop_rng ^= drop_rng << 5;
                    float r = (float)drop_rng / 4294967296.0f;
                    out->data[j] = (r >= p) ? input_arr->data[j] * scale : 0.0f;
                }
            } else {
                memcpy(out->data, input_arr->data, n * sizeof(float));
            }
            if (am_tape_is_active())
                am_tape_record(out, AM_OP_DROPOUT, tape_ensure_entry(input_arr), -1, p);
            return out;
        }
        return NULL;
    }

    // layernorm(x) or layernorm(x, gamma, beta)
    if (strcasecmp(fname, "layernorm") == 0 && nargs >= 1) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        if (!vx || vx->type != AML_TYPE_ARRAY || !vx->array) return NULL;
        AM_Array* input = vx->array;
        am_array_ref(input);
#ifdef USE_CUDA
        ensure_cpu(input);
#endif
        AM_Array *gamma = NULL, *beta = NULL;
        if ((nargs >= 2 && aml_optional_array(ctx, arg_strs[1], &gamma)) ||
            (nargs >= 3 && aml_optional_array(ctx, arg_strs[2], &beta))) {
            am_array_free(input); am_array_free(gamma); am_array_free(beta);
            return NULL;
        }
        int n = input->len;
        AM_Array* out = am_array_new(n);
        if (!out) {
            am_array_free(input); am_array_free(gamma); am_array_free(beta);
            set_error(ctx, "array allocation failed"); return NULL;
        }

        float mean = 0;
        for (int i = 0; i < n; i++) mean += input->data[i];
        mean /= n;
        float var = 0;
        for (int i = 0; i < n; i++) {
            float d = input->data[i] - mean;
            var += d * d;
        }
        var /= n;
        float inv_std = 1.0f / sqrtf(var + 1e-5f);

        for (int i = 0; i < n; i++)
            out->data[i] = (input->data[i] - mean) * inv_std;

        int gamma_idx = -1, beta_idx = -1;
        if (gamma) {
            int gn = gamma->len < n ? gamma->len : n;
            for (int i = 0; i < gn; i++) out->data[i] *= gamma->data[i];
            gamma_idx = tape_ensure_entry(gamma);
        }
        if (beta) {
            int bn = beta->len < n ? beta->len : n;
            for (int i = 0; i < bn; i++) out->data[i] += beta->data[i];
            beta_idx = tape_ensure_entry(beta);
        }
        if (am_tape_is_active())
            am_tape_record3(out, AM_OP_LAYERNORM,
                            tape_ensure_entry(input), gamma_idx, beta_idx, 0, 0);
        am_array_free(input); am_array_free(gamma); am_array_free(beta);
        return out;
    }

    // seq_layernorm(x, gamma, beta, T, D) — layernorm per T positions of size D
    if (strcasecmp(fname, "seq_layernorm") == 0 && nargs >= 5) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        if (!vx || vx->type != AML_TYPE_ARRAY || !vx->array) return NULL;
        int T = (int)ctx_float(ctx, arg_strs[3]);
        if (ctx->error[0]) return NULL;
        int D = (int)ctx_float(ctx, arg_strs[4]);
        if (ctx->error[0]) return NULL;
        if (T <= 0 || D <= 0 || (size_t)T * D > (size_t)vx->array->len) return NULL;
        AM_Array* input = vx->array;
        am_array_ref(input);
#ifdef USE_CUDA
        ensure_cpu(input);
#endif
        AM_Array *gamma = NULL, *beta = NULL;
        if (aml_optional_array(ctx, arg_strs[1], &gamma) ||
            aml_optional_array(ctx, arg_strs[2], &beta)) {
            am_array_free(input); am_array_free(gamma); am_array_free(beta);
            return NULL;
        }
        AM_Array* out = am_array_new(T * D);
        if (!out) {
            am_array_free(input); am_array_free(gamma); am_array_free(beta);
            set_error(ctx, "array allocation failed"); return NULL;
        }

        for (int t = 0; t < T; t++) {
            float* x_t = input->data + t * D;
            float* o_t = out->data + t * D;
            float mean = 0;
            for (int d = 0; d < D; d++) mean += x_t[d];
            mean /= D;
            float var = 0;
            for (int d = 0; d < D; d++) { float dd = x_t[d] - mean; var += dd * dd; }
            var /= D;
            float inv_std = 1.0f / sqrtf(var + 1e-5f);
            for (int d = 0; d < D; d++) o_t[d] = (x_t[d] - mean) * inv_std;
        }

        int gamma_idx = -1, beta_idx = -1;
        if (gamma && gamma->len >= D) {
            for (int t = 0; t < T; t++)
                for (int d = 0; d < D; d++)
                    out->data[t * D + d] *= gamma->data[d];
            gamma_idx = tape_ensure_entry(gamma);
        }
        if (beta && beta->len >= D) {
            for (int t = 0; t < T; t++)
                for (int d = 0; d < D; d++)
                    out->data[t * D + d] += beta->data[d];
            beta_idx = tape_ensure_entry(beta);
        }
        if (am_tape_is_active())
            am_tape_record3(out, AM_OP_SEQ_LAYERNORM,
                            tape_ensure_entry(input), gamma_idx, beta_idx,
                            (float)T, (float)D);
        am_array_free(input); am_array_free(gamma); am_array_free(beta);
        return out;
    }

    // spa_embed(token_ids, W, D, alpha) — Sentence Phonon Attention embedding.
    // Exponentially weighted mean of token embeddings (alpha^(n-1-i)), then L2 normalize.
    // Returns a single [D]-vector per sentence. W is flat [V*D] row-major.
    // SPA is forward-only by design — "coherence without training".
    if (strcasecmp(fname, "spa_embed") == 0 && nargs >= 4) {
        AML_Var* vt = resolve_var_full(ctx, arg_strs[0]); // token ids (floats cast to int)
        AML_Var* vW = resolve_var_full(ctx, arg_strs[1]); // embedding matrix, flat
        int D = (int)ctx_float(ctx, arg_strs[2]);
        if (ctx->error[0]) return NULL;
        float alpha = ctx_float(ctx, arg_strs[3]);
        if (ctx->error[0]) return NULL;
        if (!vt || vt->type != AML_TYPE_ARRAY || !vt->array) return NULL;
        if (!vW || vW->type != AML_TYPE_ARRAY || !vW->array) return NULL;
        if (D <= 0) return NULL;
        int n = vt->array->len;
        int V = vW->array->len / D;
        AM_Array* out = am_array_new(D);
        if (!out) return NULL;
        float total_w = 0;
        for (int i = 0; i < n; i++) {
            int id = (int)vt->array->data[i];
            if (id < 0 || id >= V) continue;
            float w = powf(alpha, (float)(n - 1 - i));
            const float* row = vW->array->data + (size_t)id * D;
            for (int d = 0; d < D; d++) out->data[d] += w * row[d];
            total_w += w;
        }
        if (total_w > 0) for (int d = 0; d < D; d++) out->data[d] /= total_w;
        // L2 normalize
        float norm = 0;
        for (int d = 0; d < D; d++) norm += out->data[d] * out->data[d];
        norm = 1.0f / sqrtf(norm + 1e-8f);
        for (int d = 0; d < D; d++) out->data[d] *= norm;
        return out;
    }

    // spa_connectedness(E, S, D[, bias]) — SPA cross-attention.
    // Given S stacked sentence embeddings (flat [S*D] row-major), computes
    // connectedness score per sentence: scores[i] = sum_{j!=i} exp(E_i · E_j / sqrt(D) + bias[|i-j|]).
    // bias is an optional [S]-sized array indexed by distance; zero bias if omitted.
    if (strcasecmp(fname, "spa_connectedness") == 0 && nargs >= 3) {
        AML_Var* vE = resolve_var_full(ctx, arg_strs[0]);
        int S = (int)ctx_float(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        int D = (int)ctx_float(ctx, arg_strs[2]);
        if (ctx->error[0]) return NULL;
        if (!vE || vE->type != AML_TYPE_ARRAY || !vE->array) return NULL;
        if (S <= 0 || D <= 0 || (size_t)S * D > (size_t)vE->array->len) return NULL;
        AM_Array* input = vE->array;
        am_array_ref(input);
#ifdef USE_CUDA
        ensure_cpu(input);
#endif
        AM_Array* bias_arr = NULL;
        if (nargs >= 4 && aml_optional_array(ctx, arg_strs[3], &bias_arr)) {
            am_array_free(input); return NULL;
        }
        AM_Array* out = am_array_new(S);
        if (!out) {
            am_array_free(input); am_array_free(bias_arr);
            set_error(ctx, "array allocation failed"); return NULL;
        }
        float inv_sd = 1.0f / sqrtf((float)D);
        for (int i = 0; i < S; i++) {
            const float* ei = input->data + (size_t)i * D;
            float total_attn = 0;
            for (int j = 0; j < S; j++) {
                if (i == j) continue;
                const float* ej = input->data + (size_t)j * D;
                float dot = 0;
                for (int d = 0; d < D; d++) dot += ei[d] * ej[d];
                dot *= inv_sd;
                int dist = (i > j) ? (i - j) : (j - i);
                if (bias_arr && dist < bias_arr->len) dot += bias_arr->data[dist];
                total_attn += expf(dot);
            }
            out->data[i] = total_attn;
        }
        am_array_free(input); am_array_free(bias_arr);
        return out;
    }

    // relu(x) — ReLU activation
    if (strcasecmp(fname, "relu") == 0 && nargs >= 1) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        AM_Array* input_arr = NULL;
        if (vx && vx->type == AML_TYPE_ARRAY && vx->array) {
            input_arr = vx->array;
        } else {
            input_arr = aml_try_array_expr(ctx, arg_strs[0]);
        }
        if (input_arr) {
            int n = input_arr->len;
            AM_Array* out = am_array_new(n);
            if (!out) return NULL;
            for (int j = 0; j < n; j++)
                out->data[j] = input_arr->data[j] > 0 ? input_arr->data[j] : 0;
            return out;
        }
        return NULL;
    }

    // ── Phase 3: Autograd operations ──

    // cross_entropy(logits, target_idx) — cross-entropy loss (returns 1-element array)
    if (strcasecmp(fname, "cross_entropy") == 0 && nargs >= 2) {
        AML_Var* vl = resolve_var_full(ctx, arg_strs[0]);
        int target = (int)aml_eval(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        if (vl && vl->type == AML_TYPE_ARRAY && vl->array) {
            int n = vl->array->len;
            if (target < 0 || target >= n) return NULL;
            // Compute softmax
            float mx = vl->array->data[0];
            for (int j = 1; j < n; j++)
                if (vl->array->data[j] > mx) mx = vl->array->data[j];
            float sum = 0;
            for (int j = 0; j < n; j++)
                sum += expf(vl->array->data[j] - mx);
            float log_softmax = vl->array->data[target] - mx - logf(sum);
            AM_Array* out = am_array_new(1);
            if (!out) return NULL;
            out->data[0] = -log_softmax;
            if (am_tape_is_active())
                am_tape_record(out, AM_OP_CROSS_ENT, tape_ensure_entry(vl->array), -1, (float)target);
            return out;
        }
        return NULL;
    }

    // embedding_lookup(wte, token_id) — extract row from embedding matrix (alias for row with tape)
    if (strcasecmp(fname, "embedding_lookup") == 0 && nargs >= 2) {
        AML_Var* vm = resolve_var_full(ctx, arg_strs[0]);
        int token_id = (int)aml_eval(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        if (vm && vm->type == AML_TYPE_ARRAY && vm->array && vm->array->rows > 0) {
            if (token_id < 0 || token_id >= vm->array->rows) return NULL;
            int cols = vm->array->cols;
            AM_Array* out = am_array_new(cols);
            if (!out) return NULL;
            memcpy(out->data, vm->array->data + token_id * cols, cols * sizeof(float));
            if (am_tape_is_active())
                am_tape_record(out, AM_OP_EMB_LOOKUP, tape_ensure_entry(vm->array), -1, (float)token_id);
            return out;
        }
        return NULL;
    }

    // row(M, i) — extract row i from matrix M as 1D array
    if (strcasecmp(fname, "row") == 0 && nargs >= 2) {
        AML_Var* vm = resolve_var_full(ctx, arg_strs[0]);
        int ri = (int)aml_eval(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        if (vm && vm->type == AML_TYPE_ARRAY && vm->array && vm->array->rows > 0) {
            if (ri < 0 || ri >= vm->array->rows) return NULL;
            int cols = vm->array->cols;
            AM_Array* out = am_array_new(cols);
            if (!out) return NULL;
            memcpy(out->data, vm->array->data + ri * cols, cols * sizeof(float));
            return out;
        }
        return NULL;
    }

    // ── Phase 5: Sequence-level transformer operations ──

    // seq_embed(wte, wpe, tokens, T) — embed a sequence of T tokens
    // wte: matrix[vocab_size × D], wpe: matrix[seq_len × D], tokens: array[T] of float token IDs
    // Returns: array[T*D] — concatenated embeddings for each position
    if (strcasecmp(fname, "seq_embed") == 0 && nargs >= 4) {
        AML_Var* vwte = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vwpe = resolve_var_full(ctx, arg_strs[1]);
        AML_Var* vtok = resolve_var_full(ctx, arg_strs[2]);
        int T = (int)aml_eval(ctx, arg_strs[3]);
        if (ctx->error[0]) return NULL;
        if (vwte && vwte->type == AML_TYPE_ARRAY && vwte->array && vwte->array->rows > 0 &&
            vwpe && vwpe->type == AML_TYPE_ARRAY && vwpe->array && vwpe->array->rows > 0 &&
            vtok && vtok->type == AML_TYPE_ARRAY && vtok->array && T > 0) {
            int D = vwte->array->cols;
            if (vwpe->array->cols != D) return NULL;
            if (T > vtok->array->len) T = vtok->array->len;
            AM_Array* out = am_array_new(T * D);
            if (!out) return NULL;
            for (int t = 0; t < T; t++) {
                int tok = (int)vtok->array->data[t];
                if (tok < 0) tok = 0;
                if (tok >= vwte->array->rows) tok = vwte->array->rows - 1;
                int pos = t < vwpe->array->rows ? t : vwpe->array->rows - 1;
                for (int d = 0; d < D; d++)
                    out->data[t * D + d] = vwte->array->data[tok * D + d] + vwpe->array->data[pos * D + d];
            }
            if (am_tape_is_active())
                am_tape_record3(out, AM_OP_SEQ_EMBED,
                    tape_ensure_entry(vwte->array), tape_ensure_entry(vwpe->array),
                    tape_ensure_entry(vtok->array), (float)T, (float)D);
            return out;
        }
        return NULL;
    }

    // seq_matvec(W, X, T) — apply W to each of T vectors in X
    // W: matrix[out_dim × in_dim], X: array[T*in_dim]
    // Returns: array[T*out_dim]
    if (strcasecmp(fname, "seq_matvec") == 0 && nargs >= 3) {
        AML_Var* vw = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vx = resolve_var_full(ctx, arg_strs[1]);
        int T = (int)aml_eval(ctx, arg_strs[2]);
        if (ctx->error[0]) return NULL;
        if (vw && vw->type == AML_TYPE_ARRAY && vw->array && vw->array->rows > 0 &&
            vx && vx->type == AML_TYPE_ARRAY && vx->array && T > 0) {
            int out_dim = vw->array->rows;
            int in_dim = vw->array->cols;
            if (T * in_dim > vx->array->len) return NULL;
            AM_Array* out = am_array_new(T * out_dim);
            if (!out) return NULL;
            float* W = vw->array->data;
            float* X = vx->array->data;
            float* Y = out->data;
#ifdef USE_CUDA
            // GPU tensor: keep data on GPU between ops
            {
                ensure_gpu(vw->array);
                ensure_gpu(vx->array);
                if (vw->array->d_data && vx->array->d_data) {
                    out->d_data = gpu_alloc(T * out_dim);
                    if (out->d_data) {
                        gpu_sgemm_nt(T, out_dim, in_dim,
                                     vx->array->d_data, vw->array->d_data, out->d_data);
                        out->gpu_valid = 1;
                    }
                }
                if (!out->gpu_valid) {
                    // CPU fallback with BLAS
                    ensure_cpu(vw->array); ensure_cpu(vx->array);
                    W = vw->array->data; X = vx->array->data; Y = out->data;
                    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                               T, out_dim, in_dim,
                               1.0f, X, in_dim, W, in_dim,
                               0.0f, Y, out_dim);
                }
            }
#elif defined(USE_BLAS)
            // BLAS batch: Y(T,out) = X(T,in) * W^T(in,out)
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                        T, out_dim, in_dim,
                        1.0f, X, in_dim, W, in_dim,
                        0.0f, Y, out_dim);
#else
            #ifdef _OPENMP
            #pragma omp parallel for schedule(static) if(T * out_dim > 4096)
            #endif
            for (int t = 0; t < T; t++) {
                float* x_t = X + t * in_dim;
                float* y_t = Y + t * out_dim;
                for (int i = 0; i < out_dim; i++) {
                    float s = 0;
                    for (int j = 0; j < in_dim; j++)
                        s += W[i * in_dim + j] * x_t[j];
                    y_t[i] = s;
                }
            }
#endif
            if (am_tape_is_active())
                am_tape_record3(out, AM_OP_SEQ_MATVEC,
                    tape_ensure_entry(vw->array), tape_ensure_entry(vx->array), -1,
                    (float)T, 0);
            return out;
        }
        return NULL;
    }

    // seq_rmsnorm(X, T, D) — RMSNorm each D-sized chunk of X independently
    // X: array[T*D], returns array[T*D]
    if (strcasecmp(fname, "seq_rmsnorm") == 0 && nargs >= 3) {
        AML_Var* vx = resolve_var_full(ctx, arg_strs[0]);
        int T = (int)aml_eval(ctx, arg_strs[1]);
        if (ctx->error[0]) return NULL;
        int D = (int)aml_eval(ctx, arg_strs[2]);
        if (ctx->error[0]) return NULL;
        if (vx && vx->type == AML_TYPE_ARRAY && vx->array && T > 0 && D > 0) {
            if (T * D > vx->array->len) return NULL;
            AM_Array* out = am_array_new(T * D);
            if (!out) return NULL;
#ifdef USE_CUDA
            if (vx->array->d_data && vx->array->gpu_valid) {
                out->d_data = gpu_alloc(T * D);
                if (out->d_data) {
                    gpu_rmsnorm(out->d_data, vx->array->d_data, T, D);
                    out->gpu_valid = 1;
                    goto rmsnorm_done;
                }
            }
            ensure_cpu(vx->array);
#endif
            float* Xr = vx->array->data;
            float* Or = out->data;
            #ifdef _OPENMP
            #pragma omp parallel for schedule(static) if(T > 32)
            #endif
            for (int t = 0; t < T; t++) {
                float* x_t = Xr + t * D;
                float* o_t = Or + t * D;
                float ss = 0;
                for (int d = 0; d < D; d++) ss += x_t[d] * x_t[d];
                float rms = sqrtf(ss / D + 1e-6f);
                for (int d = 0; d < D; d++) o_t[d] = x_t[d] / rms;
            }
            if (am_tape_is_active())
#ifdef USE_CUDA
            rmsnorm_done: ;
#endif
                am_tape_record3(out, AM_OP_SEQ_RMSNORM,
                    tape_ensure_entry(vx->array), -1, -1, (float)T, (float)D);
            return out;
        }
        return NULL;
    }

    // causal_attention(Q, K, V, T, D) — single-head causal self-attention
    // Q, K, V: array[T*D], returns array[T*D]
    // For each position i, attends to positions 0..i with softmax
    if (strcasecmp(fname, "causal_attention") == 0 && nargs >= 5) {
        AML_Var* vq = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vk = resolve_var_full(ctx, arg_strs[1]);
        AML_Var* vv = resolve_var_full(ctx, arg_strs[2]);
        int T = (int)aml_eval(ctx, arg_strs[3]);
        if (ctx->error[0]) return NULL;
        int D = (int)aml_eval(ctx, arg_strs[4]);
        if (ctx->error[0]) return NULL;
        if (vq && vq->type == AML_TYPE_ARRAY && vq->array &&
            vk && vk->type == AML_TYPE_ARRAY && vk->array &&
            vv && vv->type == AML_TYPE_ARRAY && vv->array && T > 0 && D > 0) {
            if (T * D > vq->array->len || T * D > vk->array->len || T * D > vv->array->len)
                return NULL;
            float scale = 1.0f / sqrtf((float)D);
            AM_Array* out = am_array_new(T * D);
            if (!out) return NULL;
            // For each query position
            for (int i = 0; i < T; i++) {
                float* qi = vq->array->data + i * D;
                // Compute attention scores for positions 0..i
                float* scores = (float*)calloc(i + 1, sizeof(float));
                if (!scores) { am_array_free(out); return NULL; }
                float mx = -1e30f;
                for (int j = 0; j <= i; j++) {
                    float* kj = vk->array->data + j * D;
                    float dot = 0;
                    for (int d = 0; d < D; d++) dot += qi[d] * kj[d];
                    scores[j] = dot * scale;
                    if (scores[j] > mx) mx = scores[j];
                }
                // Softmax
                float sum = 0;
                for (int j = 0; j <= i; j++) {
                    scores[j] = expf(scores[j] - mx);
                    sum += scores[j];
                }
                if (sum > 0) for (int j = 0; j <= i; j++) scores[j] /= sum;
                // Weighted sum of V
                float* oi = out->data + i * D;
                for (int d = 0; d < D; d++) oi[d] = 0;
                for (int j = 0; j <= i; j++) {
                    float* vj = vv->array->data + j * D;
                    for (int d = 0; d < D; d++) oi[d] += scores[j] * vj[d];
                }
                free(scores);
            }
            if (am_tape_is_active())
                am_tape_record3(out, AM_OP_CAUSAL_ATTN,
                    tape_ensure_entry(vq->array), tape_ensure_entry(vk->array),
                    tape_ensure_entry(vv->array), (float)T, (float)D);
            return out;
        }
        return NULL;
    }

    // multi_head_attention(Q, K, V, T, D, n_heads) — multi-head causal self-attention
    // Q, K, V: array[T*D], splits D into n_heads heads of head_dim = D/n_heads
    // Returns: array[T*D]
    if (strcasecmp(fname, "multi_head_attention") == 0 && nargs >= 6) {
        AML_Var* vq = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vk = resolve_var_full(ctx, arg_strs[1]);
        AML_Var* vv = resolve_var_full(ctx, arg_strs[2]);
        int T = (int)aml_eval(ctx, arg_strs[3]);
        if (ctx->error[0]) return NULL;
        int D = (int)aml_eval(ctx, arg_strs[4]);
        if (ctx->error[0]) return NULL;
        int n_heads = (int)aml_eval(ctx, arg_strs[5]);
        if (ctx->error[0]) return NULL;
        if (vq && vq->type == AML_TYPE_ARRAY && vq->array &&
            vk && vk->type == AML_TYPE_ARRAY && vk->array &&
            vv && vv->type == AML_TYPE_ARRAY && vv->array &&
            T > 0 && D > 0 && n_heads > 0 && (D % n_heads) == 0) {
            if (T * D > vq->array->len || T * D > vk->array->len || T * D > vv->array->len)
                return NULL;
            int head_dim = D / n_heads;
            float scale = 1.0f / sqrtf((float)head_dim);
            AM_Array* out = am_array_new(T * D);
            if (!out) return NULL;
#ifdef USE_CUDA
            if (vq->array->d_data && vq->array->gpu_valid &&
                vk->array->d_data && vk->array->gpu_valid &&
                vv->array->d_data && vv->array->gpu_valid) {
                out->d_data = gpu_alloc(T * D);
                float* d_scores = gpu_scratch(5, n_heads * T * T);
                if (out->d_data && d_scores) {
                    gpu_multi_head_attention(vq->array->d_data, vk->array->d_data,
                                             vv->array->d_data, out->d_data, d_scores,
                                             T, D, n_heads);
                    out->gpu_valid = 1;
                    goto attn_done;
                }
            }
            ensure_cpu(vq->array); ensure_cpu(vk->array); ensure_cpu(vv->array);
#endif
            float* Qd = vq->array->data;
            float* Kd = vk->array->data;
            float* Vd = vv->array->data;
            float* Od = out->data;
            #ifdef _OPENMP
            #pragma omp parallel if(n_heads >= 2)
            {
            float* scores_buf = (float*)malloc(T * sizeof(float));
            #pragma omp for schedule(static) collapse(2)
            #else
            float* scores_buf = (float*)malloc(T * sizeof(float));
            #endif
            for (int h = 0; h < n_heads; h++) {
                for (int i = 0; i < T; i++) {
                    int ho = h * head_dim;
                    float* qi = Qd + i * D + ho;
                    float mx = -1e30f;
                    for (int j = 0; j <= i; j++) {
                        float* kj = Kd + j * D + ho;
                        float dot = 0;
                        for (int d = 0; d < head_dim; d++) dot += qi[d] * kj[d];
                        scores_buf[j] = dot * scale;
                        if (scores_buf[j] > mx) mx = scores_buf[j];
                    }
                    float sum = 0;
                    for (int j = 0; j <= i; j++) {
                        scores_buf[j] = expf(scores_buf[j] - mx);
                        sum += scores_buf[j];
                    }
                    if (sum > 0) for (int j = 0; j <= i; j++) scores_buf[j] /= sum;
                    float* oi = Od + i * D + ho;
                    for (int d = 0; d < head_dim; d++) oi[d] = 0;
                    for (int j = 0; j <= i; j++) {
                        float* vj = Vd + j * D + ho;
                        for (int d = 0; d < head_dim; d++) oi[d] += scores_buf[j] * vj[d];
                    }
                }
            }
            #ifdef _OPENMP
            free(scores_buf);
            }
            #else
            free(scores_buf);
            #endif
            if (am_tape_is_active())
#ifdef USE_CUDA
            attn_done: ;
#endif
                am_tape_record3(out, AM_OP_MH_CAUSAL_ATTN,
                    tape_ensure_entry(vq->array), tape_ensure_entry(vk->array),
                    tape_ensure_entry(vv->array), (float)T, (float)head_dim);
            return out;
        }
        return NULL;
    }

    // seq_cross_entropy(logits, targets, T, V) — cross-entropy over T positions
    // logits: array[T*V], targets: array[T] of float token IDs
    // Returns: array[1] (mean loss over T positions)
    if (strcasecmp(fname, "seq_cross_entropy") == 0 && nargs >= 4) {
        AML_Var* vl = resolve_var_full(ctx, arg_strs[0]);
        AML_Var* vt = resolve_var_full(ctx, arg_strs[1]);
        int T = (int)aml_eval(ctx, arg_strs[2]);
        if (ctx->error[0]) return NULL;
        int V = (int)aml_eval(ctx, arg_strs[3]);
        if (ctx->error[0]) return NULL;
        if (vl && vl->type == AML_TYPE_ARRAY && vl->array &&
            vt && vt->type == AML_TYPE_ARRAY && vt->array && T > 0 && V > 0) {
            if (T * V > vl->array->len || T > vt->array->len) return NULL;
            AM_Array* out = am_array_new(1);
            if (!out) return NULL;
#ifdef USE_CUDA
            if (vl->array->d_data && vl->array->gpu_valid) {
                ensure_gpu(vt->array);
                float* d_losses = gpu_scratch(6, T);
                if (d_losses && vt->array->d_data) {
                    float avg_loss = gpu_cross_entropy(vl->array->d_data,
                                                       vt->array->d_data, d_losses, T, V);
                    out->data[0] = avg_loss;
                    goto ce_done;
                }
            }
            ensure_cpu(vl->array); ensure_cpu(vt->array);
#endif
            float total_loss = 0;
            for (int t = 0; t < T; t++) {
                float* logits_t = vl->array->data + t * V;
                int target = (int)vt->array->data[t];
                if (target < 0 || target >= V) target = 0;
                // Softmax + log-loss
                float mx = logits_t[0];
                for (int j = 1; j < V; j++)
                    if (logits_t[j] > mx) mx = logits_t[j];
                float sum = 0;
                for (int j = 0; j < V; j++) sum += expf(logits_t[j] - mx);
                float log_prob = (logits_t[target] - mx) - logf(sum + 1e-10f);
                total_loss -= log_prob;
            }
            out->data[0] = total_loss / T;
            if (am_tape_is_active())
#ifdef USE_CUDA
            ce_done: ;
#endif
                am_tape_record3(out, AM_OP_SEQ_CROSSENT,
                    tape_ensure_entry(vl->array), tape_ensure_entry(vt->array), -1,
                    (float)T, (float)V);
            return out;
        }
        return NULL;
    }

    // NOTE: user-defined functions that return arrays are handled at
    // the assignment level (aml_exec_line), not here. aml_try_array_expr
    // only handles known array-producing builtins to avoid accidentally
    // eating scalar returns from user functions.
    return NULL;
}

// Emit a precise JSON string without allocating an intermediate escape buffer.
static void aml_print_json_string(const AM_String* value) {
    putchar('"');
    for (int i = 0; i < value->byte_len; i++) {
        unsigned char c = (unsigned char)value->data[i];
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c == '\n') fputs("\\n", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c < 0x20) printf("\\u%04x", (unsigned int)c);
        else putchar(c);
    }
    putchar('"');
}

// Remove the statement's comment and final colon before strict evaluation.
static float aml_eval_condition(AML_ExecCtx* ctx, const char* text) {
    size_t len = (size_t)(aml_comment_start(text) - text);
    while (len && isspace((unsigned char)text[len - 1])) len--;
    if (len && text[len - 1] == ':') len--;
    char condition[AML_MAX_LINE_LEN];
    memcpy(condition, text, len);
    condition[len] = 0;
    return aml_eval(ctx, condition);
}

// Execute a single line in Level 2 context
static int aml_exec_line_body(AML_ExecCtx* ctx, int idx) {
    char* text = ctx->lines[idx].text;

    // v4.0: propagate return — if has_return is set, stop executing
    if (ctx->has_return) return ctx->nlines;

    // --- def: skip (already registered) ---
    if (strncmp(text, "def ", 4) == 0) {
        // skip body
        return aml_find_block_end(ctx->lines, ctx->nlines, idx);
    }

    // Return owns its value until the function call transfers it to the caller.
    if (strncmp(text, "return ", 7) == 0 || strcmp(text, "return") == 0) {
        const char* rhs = text + 6;
        while (isspace((unsigned char)*rhs)) rhs++;
        AML_Var value = {0};
        if (*rhs && aml_eval_value(ctx, rhs, &value)) {
            aml_value_clear(&value);
            return ctx->nlines;
        }
        aml_clear_return(ctx);
        ctx->has_return = 1;
        ctx->return_type = value.type;
        ctx->return_value = value.value;
        ctx->return_array = value.array;
        ctx->return_string = value.string;
        ctx->return_list = value.list;
        ctx->return_map = value.map;
        ctx->return_tokenizer = value.tokenizer;
        ctx->return_record = value.record;
        return ctx->nlines;
    }

    // PRINT evaluates a value; ECHO retains its existing literal command form.
    if (strncasecmp(text, "PRINT", 5) == 0 &&
        (!text[5] || isspace((unsigned char)text[5]))) {
        AML_Var value = {0};
        if (!aml_eval_value(ctx, text + 5, &value)) {
            if (value.type == AML_TYPE_STRING) {
                fwrite(value.string->data, 1, (size_t)value.string->byte_len, stdout);
            } else if (value.type == AML_TYPE_LIST) {
                putchar('[');
                for (int i = 0; i < value.list->len; i++) {
                    if (i) fputs(", ", stdout);
                    aml_print_json_string(value.list->items[i]);
                }
                putchar(']');
            } else if (value.type == AML_TYPE_MAP) {
                putchar('{');
                for (int i = 0; i < value.map->len; i++) {
                    if (i) fputs(", ", stdout);
                    aml_print_json_string(value.map->entries[i].key);
                    printf(": %.9g", (double)value.map->entries[i].value);
                }
                putchar('}');
            } else if (value.type == AML_TYPE_TOKENIZER) {
                fputs("<tokenizer>", stdout);
            } else if (value.type == AML_TYPE_RECORD) {
                fputs("<record>", stdout);
            } else if (value.type == AML_TYPE_ARRAY) {
                putchar('[');
                for (int i = 0; i < value.array->len; i++)
                    printf("%s%.9g", i ? ", " : "", (double)value.array->data[i]);
                putchar(']');
            } else {
                printf("%.9g", (double)value.value);
            }
            putchar('\n');
        }
        aml_value_clear(&value);
        return idx + 1;
    }

    // --- if/else ---
    if (strncmp(text, "if ", 3) == 0) {
        float val = aml_eval_condition(ctx, text + 3);
        int body_end = aml_find_block_end(ctx->lines, ctx->nlines, idx);

        // check for else
        int has_else = 0;
        int else_end = body_end;
        if (body_end < ctx->nlines) {
            char* next = ctx->lines[body_end].text;
            if (ctx->lines[body_end].indent == ctx->lines[idx].indent &&
                (strcmp(next, "else:") == 0 || strncmp(next, "else:", 5) == 0)) {
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
        int body_end = aml_find_block_end(ctx->lines, ctx->nlines, idx);
        int iterations = 0;

        while (!ctx->error[0] && !ctx->has_return) {
            float active = aml_eval_condition(ctx, text + 6);
            if (ctx->error[0] || active == 0.0f) break;
            if (iterations >= 10000) {
                set_error_at(ctx, ctx->lines[idx].lineno, "loop iteration limit exceeded");
                break;
            }
            aml_exec_block(ctx, idx + 1, body_end);
            iterations++;
        }
        return body_end;
    }

    // --- v4.0: SPAWN name: (async block) ---
#ifndef AM_ASYNC_DISABLED
    if (strncasecmp(text, "SPAWN ", 6) == 0) {
        // Parse: SPAWN name:
        char spawn_name[AM_SPAWN_NAME_LEN] = {0};
        const char* sp = text + 6;
        while (*sp == ' ') sp++;
        int ni = 0;
        while (*sp && *sp != ':' && *sp != ' ' && ni < AM_SPAWN_NAME_LEN - 1)
            spawn_name[ni++] = *sp++;
        spawn_name[ni] = 0;

        int body_end = aml_find_block_end(ctx->lines, ctx->nlines, idx);

        // Build script string from indented block
        // Calculate total size needed
        int total = 0;
        int base_indent = idx + 1 < body_end ? ctx->lines[idx + 1].indent : 0;
        for (int bi = idx + 1; bi < body_end; bi++)
            total += (int)strlen(ctx->lines[bi].text) + 1
                     + (ctx->lines[bi].indent > base_indent
                        ? ctx->lines[bi].indent - base_indent : 0);
        total += 1; // null terminator

        char* script = (char*)malloc(total);
        if (script) {
            char* dst = script;
            for (int bi = idx + 1; bi < body_end; bi++) {
                int indent = ctx->lines[bi].indent - base_indent;
                while (indent-- > 0) *dst++ = ' ';
                size_t len = strlen(ctx->lines[bi].text);
                memcpy(dst, ctx->lines[bi].text, len);
                dst += len;
                *dst++ = '\n';
            }
            *dst = 0;
            if (am_spawn_launch_globals(spawn_name, script, &ctx->globals) < 0)
                set_error_at(ctx, ctx->lines[idx].lineno, "cannot launch worker");
            free(script);
        } else {
            set_error_at(ctx, ctx->lines[idx].lineno, "worker script allocation failed");
        }

        return body_end;
    }
#endif // AM_ASYNC_DISABLED

    // --- INCLUDE ---
    if (strncasecmp(text, "INCLUDE ", 8) == 0) {
        if (ctx->include_depth >= AML_MAX_INCLUDE) {
            set_error_at(ctx, ctx->lines[idx].lineno, "max include depth exceeded");
            return idx + 1;
        }
        char path[512], fname[AML_MAX_LINE_LEN];
        snprintf(fname, sizeof(fname), "%s", text + 8);
        char* name = fname;
        while (*name == ' ' || *name == '\t') name++;
        size_t n = strlen(name);
        while (n && isspace((unsigned char)name[n - 1])) name[--n] = 0;
        if (*name == '"') {
            if (n < 2 || name[n - 1] != '"') {
                set_error_at(ctx, ctx->lines[idx].lineno, "unterminated INCLUDE path");
                return idx + 1;
            }
            name[n - 1] = 0;
            name++;
        }
        if (!*name) {
            set_error_at(ctx, ctx->lines[idx].lineno, "empty INCLUDE path");
            return idx + 1;
        }

        int written;
        if (name[0] == '/') {
            written = snprintf(path, sizeof(path), "%s", name);
        } else {
            written = snprintf(path, sizeof(path), "%s/%s",
                               ctx->base_dir[0] ? ctx->base_dir : ".", name);
        }
        if (written < 0 || (size_t)written >= sizeof(path)) {
            set_error_at(ctx, ctx->lines[idx].lineno, "INCLUDE path too long");
            return idx + 1;
        }

        ctx->include_depth++;
        int rc = am_exec_file(path);
        ctx->include_depth--;
        if (rc != 0)
            set_error_at(ctx, ctx->lines[idx].lineno,
                         g_error[0] ? g_error : "included program failed");
        return idx + 1;
    }

    // --- v4.0: array element write: name[index] = expr ---
    {
        // Look for pattern: identifier[expr] = expr
        const char* bracket = text;
        if (isalpha((unsigned char)*bracket) || *bracket == '_')
            while (isalnum((unsigned char)*bracket) || *bracket == '_') bracket++;
        while (isspace((unsigned char)*bracket)) bracket++;
        if (*bracket == '[' && bracket > text) {
            const char* close_bracket = aml_value_close(bracket);
            if (!close_bracket) {
                set_error(ctx, "missing closing bracket in array assignment");
                return idx + 1;
            }
            if (close_bracket) {
                const char* eq_after = close_bracket + 1;
                while (*eq_after == ' ') eq_after++;
                if (*eq_after == '=' && eq_after[1] != '=') {
                    // Extract variable name
                    char varname[AML_MAX_NAME] = {0};
                    int ni = 0;
                    const char* p = text;
                    while (p < bracket && ni < AML_MAX_NAME - 1) {
                        if (!isspace((unsigned char)*p))
                            varname[ni++] = *p;
                        p++;
                    }
                    varname[ni] = 0;

                    if (ni > 0) {
                        // Evaluate index
                        char idx_expr[AML_MAX_LINE_LEN] = {0};
                        int ie = 0;
                        const char* ip = bracket + 1;
                        while (ip < close_bracket && ie < AML_MAX_LINE_LEN - 1)
                            idx_expr[ie++] = *ip++;
                        idx_expr[ie] = 0;
                        int index = (int)aml_eval(ctx, idx_expr);

                        // Evaluate value
                        float val = aml_eval(ctx, eq_after + 1);
                        if (ctx->error[0]) return idx + 1;

                        // Find the array variable and write to it
                        AML_Var* var = resolve_var_full(ctx, varname);
                        if (var && (var->type == AML_TYPE_LIST || var->type == AML_TYPE_MAP || var->type == AML_TYPE_TOKENIZER || var->type == AML_TYPE_RECORD)) {
                            set_error(ctx, var->type == AML_TYPE_RECORD
                                ? "array element write requires an array, not a record" : var->type == AML_TYPE_TOKENIZER
                                ? "array element write requires an array, not a tokenizer" : var->type == AML_TYPE_MAP
                                ? "array element write requires an array; use map_set for maps"
                                : "array element write requires an array; use list_set for lists");
                            return idx + 1;
                        }
                        if (var && var->type == AML_TYPE_ARRAY && var->array) {
                            if (index >= 0 && index < var->array->len)
                                var->array->data[index] = val;
                        }
                        return idx + 1;
                    }
                }
            }
        }
    }

    // Assignment and standalone calls share the typed expression evaluator.
    {
        const char* p = text;
        char name[AML_MAX_NAME] = {0};
        int n = 0;
        if (isalpha((unsigned char)*p) || *p == '_') {
            while (isalnum((unsigned char)*p) || *p == '_') {
                if (n < AML_MAX_NAME - 1) name[n] = *p;
                n++; p++;
            }
        }
        while (isspace((unsigned char)*p)) p++;
        if (n && *p == '=' && p[1] != '=') {
            if (n >= AML_MAX_NAME) {
                set_error_at(ctx, ctx->lines[idx].lineno, "variable name too long");
                return idx + 1;
            }
            const char* rhs = p + 1;
            while (isspace((unsigned char)*rhs)) rhs++;
            AML_Var value = {0};
            if (aml_eval_value(ctx, rhs, &value)) {
                aml_value_clear(&value);
                return idx + 1;
            }
            // Preserve the original deep-copy contract for bare array aliases.
            const char* end = rhs;
            while (isalnum((unsigned char)*end) || *end == '_') end++;
            while (isspace((unsigned char)*end)) end++;
            if (value.type == AML_TYPE_ARRAY && !*end) {
                AM_Array* copy = am_array_clone(value.array);
                am_array_free(value.array);
                value.array = copy;
                if (!copy) {
                    set_error_at(ctx, ctx->lines[idx].lineno, "array allocation failed");
                    return idx + 1;
                }
            }
            if (value.type == AML_TYPE_LIST) {
                AM_List* copy = am_list_clone(value.list);
                am_list_free(value.list);
                value.list = copy;
                if (!copy) {
                    set_error_at(ctx, ctx->lines[idx].lineno, "list allocation failed");
                    return idx + 1;
                }
            }
            if (value.type == AML_TYPE_MAP) {
                AM_Map* copy = am_map_clone(value.map);
                am_map_free(value.map);
                value.map = copy;
                if (!copy) {
                    set_error_at(ctx, ctx->lines[idx].lineno, "map allocation failed");
                    return idx + 1;
                }
            }
            if (value.type == AML_TYPE_RECORD) {
                AM_Record* copy = am_record_clone(value.record);
                am_record_free(value.record);
                value.record = copy;
                if (!copy) {
                    set_error_at(ctx, ctx->lines[idx].lineno, "record allocation failed");
                    return idx + 1;
                }
            }
            AML_Symtab* tab = ctx->call_depth > 0
                ? &ctx->locals[ctx->call_depth - 1] : &ctx->globals;
            int rc;
            if (value.type == AML_TYPE_STRING) rc = symtab_set_string(tab, name, value.string);
            else if (value.type == AML_TYPE_ARRAY) rc = symtab_set_array(tab, name, value.array);
            else if (value.type == AML_TYPE_LIST) rc = symtab_set_list(tab, name, value.list);
            else if (value.type == AML_TYPE_MAP) rc = symtab_set_map(tab, name, value.map);
            else if (value.type == AML_TYPE_TOKENIZER) rc = symtab_set_tokenizer(tab, name, value.tokenizer);
            else if (value.type == AML_TYPE_RECORD) rc = symtab_set_record(tab, name, value.record);
            else rc = symtab_set(tab, name, value.value);
            if (rc) {
                aml_value_clear(&value);
                set_error_at(ctx, ctx->lines[idx].lineno, "variable limit exceeded");
            }
            return idx + 1;
        }
        if (n && n < AML_MAX_NAME && *p == '(' &&
            (aml_value_function(ctx, name) || aml_text_function(name) || aml_list_function(name) ||
             aml_map_function(name) || aml_scalar_intrinsic_function(name) ||
             aml_sampling_function(name) || aml_numerical_function(name) || aml_tokenizer_function(name) || aml_record_function(name) ||
             aml_array_scalar_function(name))) {
            AML_Var value = {0};
            aml_eval_value(ctx, text, &value);
            aml_value_clear(&value);
            return idx + 1;
        }
    }

    // --- macro call @name ---
    if (text[0] == '@') {
        const char* mname = text + 1;
        while (*mname == ' ') mname++;
        for (int mi = 0; mi < g_macro_count; mi++) {
            if (strcmp(g_macros[mi].name, mname) == 0) {
                am_exec(g_macros[mi].body);
                return idx + 1;
            }
        }
        return idx + 1;  // macro not found — ignore
    }

    // --- A-3b: multi-line BLOOD COMPILE — gather the body across lines ---
    // aml_exec_level0's one-line handler only compiles when '{' and '}' are on the
    // same line; a multi-line block otherwise falls through here and its C body
    // lines get dispatched as field commands (e.g. "PAIN 0.9;" would set pain).
    // Detect an unbalanced opener, gather the body until the braces balance,
    // compile it, and resume past the consumed lines so the body never reaches the
    // dispatcher. (One-line blocks have net==0 and stay with aml_exec_level0.)
    if (strncasecmp(text, "BLOOD COMPILE", 13) == 0) {
        const char* open = strchr(text, '{');
        int net = 0;
        if (open) for (const char* c = text; *c; c++) net += (*c == '{') - (*c == '}');
        if (open && net > 0) {
            char bname[AM_BLOOD_MAX_NAME] = {0};
            sscanf(text + 13, " %63s", bname);
            size_t cap = 512, len = 0;
            char* body = (char*)malloc(cap);
            if (!body) return idx + 1;
            int depth = 0, j = idx, done = 0;
            const char* chunk = open;
            while (j < ctx->nlines && !done) {
                for (const char* c = chunk; *c; c++) {
                    if (*c == '{') { if (++depth == 1) continue; }       // skip the opening '{'
                    else if (*c == '}') { if (--depth == 0) { done = 1; break; } }
                    if (len + 2 > cap) { char* nb = (char*)realloc(body, cap * 2); if (!nb) { done = 1; break; } body = nb; cap *= 2; }
                    body[len++] = *c;
                }
                if (done) break;
                if (len + 2 > cap) { char* nb = (char*)realloc(body, cap * 2); if (!nb) break; body = nb; cap *= 2; }
                body[len++] = '\n';
                if (++j < ctx->nlines) chunk = ctx->lines[j].text;
            }
            body[len] = 0;
            if (am_blood_compile(bname, body) < 0)
                set_error_at(ctx, ctx->lines[idx].lineno, "blood: compilation failed");
            free(body);
            return (j < ctx->nlines) ? j + 1 : ctx->nlines;
        }
    }

    // --- Level 0 fallback: split CMD ARG, dispatch ---
    {
        char linebuf[AML_MAX_LINE_LEN];
        snprintf(linebuf, sizeof(linebuf), "%s", text);

        char* sp = linebuf;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        char* cmd_end = sp;
        while (*sp && isspace((unsigned char)*sp)) sp++;
        char* arg = sp;
        *cmd_end = 0;
        upcase(linebuf);

        aml_exec_level0(linebuf, arg, ctx, ctx->lines[idx].lineno);
    }
    return idx + 1;
}

typedef struct {
    char context_base[256];
    char thread_base[256];
    char source[AML_MAX_SOURCE_PATH];
} AML_SourceScope;

static void aml_enter_origin(AML_ExecCtx* ctx, int idx, AML_SourceScope* saved) {
    memcpy(saved->context_base, ctx->base_dir, sizeof(saved->context_base));
    memcpy(saved->thread_base, g_base_dir, sizeof(saved->thread_base));
    memcpy(saved->source, g_source_path, sizeof(saved->source));
    const char* origin = ctx->lines[idx].origin;
    snprintf(g_source_path, sizeof(g_source_path), "%s", origin);
    snprintf(ctx->base_dir, sizeof(ctx->base_dir), "%s", origin);
    char* slash = strrchr(ctx->base_dir, '/');
    if (slash == ctx->base_dir) slash[1] = 0;
    else if (slash) *slash = 0;
    else snprintf(ctx->base_dir, sizeof(ctx->base_dir), ".");
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", ctx->base_dir);
}

static void aml_leave_origin(AML_ExecCtx* ctx, const AML_SourceScope* saved) {
    memcpy(ctx->base_dir, saved->context_base, sizeof(saved->context_base));
    memcpy(g_base_dir, saved->thread_base, sizeof(saved->thread_base));
    memcpy(g_source_path, saved->source, sizeof(saved->source));
}

static int aml_exec_line(AML_ExecCtx* ctx, int idx) {
    AML_SourceScope saved;
    aml_enter_origin(ctx, idx, &saved);
    int next = aml_exec_line_body(ctx, idx);
    aml_leave_origin(ctx, &saved);
    return next;
}

// Execute a block of lines [start, end)
static int aml_exec_block(AML_ExecCtx* ctx, int start, int end) {
    int i = start;
    while (i < end && i < ctx->nlines && !ctx->has_return && !ctx->error[0]) {
        i = aml_exec_line(ctx, i);
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PUBLIC EXEC — AML Level 0 + Level 2
// ═══════════════════════════════════════════════════════════════════════════════

int am_exec(const char* script) {
    if (!script || !*script) return 0;
    /* A-4: auto-init the field on first use. The compiled-binary path applies
     * top-level directives via an __attribute__((constructor)) before main and
     * never calls am_init(), so without this the directives ran on a zeroed
     * AM_State (base_temperature=0, etc.) instead of the spec §2 defaults. */
    if (!g_am_initialized) am_init();
    g_error[0] = 0;

    // preprocess into lines
    AML_Line* lines = (AML_Line*)malloc(AML_MAX_LINES * sizeof(AML_Line));
    if (!lines) { set_error(NULL, "program allocation failed"); return 2; }

    int nlines = aml_preprocess(script, lines, AML_MAX_LINES);
    if (nlines <= 0) { free(lines); return nlines < 0 ? 1 : 0; }

    // set up execution context
    AML_ExecCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.base_dir, sizeof(ctx.base_dir), "%s", g_base_dir);   // A-6: inherit include base dir
    ctx.lines = lines;
    ctx.nlines = nlines;

    // register built-in functions (native AML, not external bindings)
    aml_register_builtins(&ctx);

    // first pass: register user-defined function definitions
    aml_register_funcs(&ctx);
    if (ctx.error[0]) { free(lines); return 1; }

    // Restore only after the entire prepared program has passed validation.
    if (persistent_restore(&ctx.globals)) { free(lines); return 1; }

    // second pass: execute top-level block
    aml_exec_block(&ctx, 0, nlines);

    // v4.0: save globals to persistent storage, then clean up
    if (persistent_save(&ctx.globals) && !ctx.error[0])
        set_error(&ctx, "persistent globals allocation failed");
    symtab_clear_arrays(&ctx.globals);
    aml_clear_return(&ctx);

    free(lines);

    if (ctx.error[0]) {
        snprintf(g_error, sizeof(g_error), "%s", ctx.error);
        return 1;
    }
    return 0;
}


// ═══════════════════════════════════════════════════════════════════
// RESUMABLE EXECUTION — a program that yields
//
// am_exec() runs a script to completion in one call. A host that schedules —
// an OS handing a program a quantum, a runtime interleaving voices — needs the
// program to stop and be resumed instead. These calls are am_exec split at its
// own seams: open does everything before the block loop, step runs a bounded
// slice of that loop and keeps the program counter, close does the teardown
// and yields the final result.
//
// The yield point is a top-level statement boundary. Control flow (`if`,
// `while`, `def`) runs its body through a nested aml_exec_block() inside a
// single aml_exec_line(), so that state lives on the C stack and cannot be
// suspended: a `while` runs all of its iterations within one step.
// ═══════════════════════════════════════════════════════════════════

typedef struct {
    AML_Line*   lines;
    AML_ExecCtx ctx;
    int         pc;
    int         done;
} AM_Program;

void* am_program_open(const char* script) {
    if (!script || !*script) return NULL;
    if (!g_am_initialized) am_init();
    g_error[0] = 0;

    AM_Program* p = (AM_Program*)calloc(1, sizeof(AM_Program));
    if (!p) { set_error(NULL, "program allocation failed"); return NULL; }

    p->lines = (AML_Line*)malloc(AML_MAX_LINES * sizeof(AML_Line));
    if (!p->lines) { set_error(NULL, "program allocation failed"); free(p); return NULL; }

    p->ctx.nlines = aml_preprocess(script, p->lines, AML_MAX_LINES);
    if (p->ctx.nlines <= 0) { free(p->lines); free(p); return NULL; }

    snprintf(p->ctx.base_dir, sizeof(p->ctx.base_dir), "%s", g_base_dir);
    p->ctx.lines = p->lines;
    aml_register_builtins(&p->ctx);
    aml_register_funcs(&p->ctx);
    if (p->ctx.error[0]) { free(p->lines); free(p); return NULL; }
    if (persistent_restore(&p->ctx.globals)) { free(p->lines); free(p); return NULL; }
    return p;
}

// Run at most max_lines top-level statements. Returns 1 when the program has
// finished (further steps are no-ops), 0 when more remains. max_lines <= 0 runs
// to completion, which is exactly am_exec's block loop.
int am_program_step(void* handle, int max_lines) {
    if (!handle) return 1;
    AM_Program* p = (AM_Program*)handle;
    if (p->done) return 1;

    int executed = 0;
    while (p->pc < p->ctx.nlines && !p->ctx.has_return && !p->ctx.error[0]
           && (max_lines <= 0 || executed < max_lines)) {
        p->pc = aml_exec_line(&p->ctx, p->pc);
        executed++;
    }
    if (p->pc >= p->ctx.nlines || p->ctx.has_return || p->ctx.error[0]) p->done = 1;
    return p->done;
}

// Teardown; returns the program's result the way am_exec would: 1 on error, 0 ok.
int am_program_close(void* handle) {
    if (!handle) return 0;
    AM_Program* p = (AM_Program*)handle;

    if (persistent_save(&p->ctx.globals) && !p->ctx.error[0])
        set_error(&p->ctx, "persistent globals allocation failed");
    symtab_clear_arrays(&p->ctx.globals);
    aml_clear_return(&p->ctx);

    int rc = 0;
    if (p->ctx.error[0]) {
        snprintf(g_error, sizeof(g_error), "%s", p->ctx.error);
        rc = 1;
    }
    free(p->lines);
    free(p);
    return rc;
}

// How many top-level statements remain — telemetry for a host sizing a quantum.
// Not a progress guarantee: one statement may be a loop.
int am_program_remaining(void* handle) {
    if (!handle) return 0;
    AM_Program* p = (AM_Program*)handle;
    if (p->done) return 0;
    return p->ctx.nlines - p->pc;
}

// ═══════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════
// BYTECODE COMPILATION — eliminate interpreter overhead
// ═══════════════════════════════════════════════════════════════════
//
// am_compile() pre-parses each line into an opcode + pre-split args.
// am_exec_compiled() executes opcodes via switch — no string matching.
//
// Eliminates per-line: 6 strncmp, strchr, fname parsing, 20+ strcasecmp
// function dispatch, arg parsing, upcase, sscanf.

enum {
    BC_NOP = 0,
    // TAPE commands
    BC_TAPE_START, BC_TAPE_CLEAR, BC_TAPE_BACKWARD,
    BC_TAPE_PARAM, BC_TAPE_PARAM_NO_DECAY,
    BC_TAPE_ACCUM_GRADS, BC_TAPE_APPLY_ACCUM,
    BC_TAPE_CLIP_GRADS, BC_TAPE_ADAMW_STEP,
    // Array function calls: result = func(args...)
    BC_CALL_SEQ_EMBED,      // h = seq_embed(wte, wpe, tokens, seq_len)
    BC_CALL_SEQ_MATVEC,     // y = seq_matvec(W, x, seq_len)
    BC_CALL_SEQ_RMSNORM,    // y = seq_rmsnorm(x, seq_len, dim)
    BC_CALL_MULTI_HEAD_ATTN,// y = multi_head_attention(q,k,v,seq,dim,heads)
    BC_CALL_SEQ_CROSS_ENTROPY, // loss = seq_cross_entropy(logits,targets,seq,vocab)
    BC_CALL_ADD,            // y = add(a, b)
    BC_CALL_MUL,            // y = mul(a, b)
    BC_CALL_SILU,           // y = silu(x)
    // Fallback — use interpreter
    BC_FALLBACK,
};

typedef struct {
    int opcode;
    char result[AML_MAX_NAME];     // LHS variable name
    char args[AML_MAX_PARAMS][AML_MAX_LINE_LEN]; // complete argument expressions
    int nargs;
    int orig_idx;                  // original line index (for error reporting)
} AML_BytecodeOp;

typedef struct {
    AML_Line*       lines;
    int             nlines;
    AML_Functab     funcs;
    AML_BytecodeOp* ops;
    int             nops;
} AM_Compiled;

// ── Bytecode compiler: parse each line into opcode + args ──

static int bc_parse_func_call(const char* rhs, char* fname, char args[][AML_MAX_LINE_LEN], int* nargs) {
    // Parse: fname(arg1, arg2, ...) from RHS
    while (*rhs == ' ') rhs++;
    int fi = 0;
    while ((isalnum((unsigned char)rhs[fi]) || rhs[fi] == '_') && fi < AML_MAX_NAME - 1) {
        fname[fi] = rhs[fi]; fi++;
    }
    fname[fi] = 0;
    const char* p = rhs + fi;
    while (*p == ' ') p++;
    if (*p != '(') return 0;
    p++; // skip '('
    *nargs = 0;
    while (*p && *p != ')' && *nargs < AML_MAX_PARAMS) {
        while (isspace((unsigned char)*p)) p++;
        if (*p == ')') break;
        int ai = 0;
        int pdepth = 0;
        char quote = 0;
        while (*p && ai < AML_MAX_LINE_LEN - 1) {
            if (quote) {
                if (*p == '\\' && p[1]) {
                    if (ai + 2 >= AML_MAX_LINE_LEN) return 0;
                    args[*nargs][ai++] = *p++;
                    args[*nargs][ai++] = *p++;
                    continue;
                }
                if (*p == quote) quote = 0;
            } else if (*p == '"' || *p == '\'') quote = *p;
            else if (*p == '(' || *p == '[') pdepth++;
            else if (*p == ')' || *p == ']') { if (!pdepth) break; pdepth--; }
            else if (*p == ',' && !pdepth) break;
            args[*nargs][ai++] = *p++;
        }
        while (ai > 0 && isspace((unsigned char)args[*nargs][ai-1])) ai--;
        if (!ai || quote || pdepth) return 0;
        args[*nargs][ai] = 0;
        (*nargs)++;
        if (*p != ',') break;
        p++;
        while (isspace((unsigned char)*p)) p++;
        if (*p == ')') return 0;
    }
    if (*p++ != ')') return 0;
    while (isspace((unsigned char)*p)) p++;
    return !*p || *p == '#';
}

static int bc_fname_to_opcode(const char* fname) {
    if (strcasecmp(fname, "seq_embed") == 0) return BC_CALL_SEQ_EMBED;
    if (strcasecmp(fname, "seq_matvec") == 0) return BC_CALL_SEQ_MATVEC;
    if (strcasecmp(fname, "seq_rmsnorm") == 0) return BC_CALL_SEQ_RMSNORM;
    if (strcasecmp(fname, "multi_head_attention") == 0) return BC_CALL_MULTI_HEAD_ATTN;
    if (strcasecmp(fname, "seq_cross_entropy") == 0) return BC_CALL_SEQ_CROSS_ENTROPY;
    if (strcasecmp(fname, "add") == 0) return BC_CALL_ADD;
    if (strcasecmp(fname, "mul") == 0) return BC_CALL_MUL;
    if (strcasecmp(fname, "silu") == 0) return BC_CALL_SILU;
    return -1; // unknown
}

static int bc_array_arity(int opcode) {
    switch (opcode) {
    case BC_CALL_ADD: case BC_CALL_MUL: return 2;
    case BC_CALL_SILU: return 1;
    case BC_CALL_SEQ_MATVEC: case BC_CALL_SEQ_RMSNORM: return 3;
    case BC_CALL_SEQ_EMBED: case BC_CALL_SEQ_CROSS_ENTROPY: return 4;
    case BC_CALL_MULTI_HEAD_ATTN: return 6;
    default: return 0;
    }
}

// Fastops only consume direct names/numbers. General expressions use the
// interpreter once, preserving nested calls, quoted arguments, and effects.
static int bc_simple_arguments(const AML_BytecodeOp* op) {
    for (int i = 0; i < op->nargs; i++) {
        const char* p = op->args[i];
        if (isalpha((unsigned char)*p) || *p == '_') {
            while (isalnum((unsigned char)*p) || *p == '_') p++;
        } else {
            char* end;
            strtof(p, &end);
            if (end == p) return 0;
            p = end;
        }
        while (isspace((unsigned char)*p)) p++;
        if (*p) return 0;
    }
    return 1;
}

static void bc_compile_line(AML_BytecodeOp* op, const char* text, int idx) {
    memset(op, 0, sizeof(*op));
    op->orig_idx = idx;

    // TAPE commands (start with "TAPE ")
    if (strncasecmp(text, "TAPE ", 5) == 0) {
        const char* sub = text + 5;
        while (*sub == ' ') sub++;
        if (strncasecmp(sub, "START", 5) == 0) { op->opcode = BC_TAPE_START; return; }
        if (strncasecmp(sub, "CLEAR", 5) == 0) { op->opcode = BC_TAPE_CLEAR; return; }
        if (strncasecmp(sub, "ACCUM_GRADS", 11) == 0) { op->opcode = BC_TAPE_ACCUM_GRADS; return; }
        if (strncasecmp(sub, "BACKWARD ", 9) == 0) {
            op->opcode = BC_TAPE_BACKWARD;
            sscanf(sub + 9, "%31s", op->args[0]); op->nargs = 1; return;
        }
        if (strncasecmp(sub, "PARAM_NO_DECAY ", 15) == 0) {
            op->opcode = BC_TAPE_PARAM_NO_DECAY;
            sscanf(sub + 15, "%31s", op->args[0]); op->nargs = 1; return;
        }
        if (strncasecmp(sub, "PARAM ", 6) == 0) {
            op->opcode = BC_TAPE_PARAM;
            sscanf(sub + 6, "%31s", op->args[0]); op->nargs = 1; return;
        }
        if (strncasecmp(sub, "APPLY_ACCUM ", 12) == 0) {
            op->opcode = BC_TAPE_APPLY_ACCUM;
            snprintf(op->args[0], AML_MAX_LINE_LEN, "%s", sub + 12);
            op->nargs = 1; return;
        }
        if (strncasecmp(sub, "CLIP_GRADS ", 11) == 0 || strncasecmp(sub, "CLIP ", 5) == 0) {
            op->opcode = BC_TAPE_CLIP_GRADS;
            const char* a = strchr(sub, ' ');
            if (a) { while (*a == ' ') a++; snprintf(op->args[0], AML_MAX_LINE_LEN, "%s", a); }
            op->nargs = 1; return;
        }
        if (strncasecmp(sub, "ADAMW_STEP ", 11) == 0 || strncasecmp(sub, "ADAMW ", 6) == 0) {
            op->opcode = BC_TAPE_ADAMW_STEP;
            const char* a = sub + (sub[5] == '_' ? 11 : 6);
            sscanf(a, "%31s %31s %31s %31s", op->args[0], op->args[1], op->args[2], op->args[3]);
            op->nargs = 4; return;
        }
        op->opcode = BC_FALLBACK; return;
    }

    // Only an identifier immediately followed by '=' is an assignment.
    // PRINT/return/call arguments may contain '=' and array-call text in quotes.
    const char* eq = text;
    if (isalpha((unsigned char)*eq) || *eq == '_')
        while (isalnum((unsigned char)*eq) || *eq == '_') eq++;
    size_t name_len = (size_t)(eq - text);
    while (isspace((unsigned char)*eq)) eq++;
    if (name_len > 0 && name_len < AML_MAX_NAME && *eq == '=' && eq[1] != '=') {
        memcpy(op->result, text, name_len);
        op->result[name_len] = 0;

        // Parse RHS as function call
        const char* rhs = eq + 1;
        while (*rhs == ' ') rhs++;
        char fname[AML_MAX_NAME] = {0};
        if (bc_parse_func_call(rhs, fname, op->args, &op->nargs)) {
            int opc = bc_fname_to_opcode(fname);
            if (opc >= 0 && op->nargs == bc_array_arity(opc) && bc_simple_arguments(op)) {
                op->opcode = opc; return;
            }
        }
        // Unknown function or not a function call
        op->opcode = BC_FALLBACK; return;
    }

    op->opcode = BC_FALLBACK;
}

void* am_compile(const char* script) {
    if (!script || !*script) return NULL;
    g_error[0] = 0;

    AM_Compiled* c = (AM_Compiled*)calloc(1, sizeof(AM_Compiled));
    if (!c) { set_error(NULL, "program allocation failed"); return NULL; }

    c->lines = (AML_Line*)malloc(AML_MAX_LINES * sizeof(AML_Line));
    if (!c->lines) { set_error(NULL, "program allocation failed"); free(c); return NULL; }

    c->nlines = aml_preprocess(script, c->lines, AML_MAX_LINES);
    if (c->nlines <= 0) { free(c->lines); free(c); return NULL; }

    // Pre-register builtins and functions
    AML_ExecCtx tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.lines = c->lines;
    tmp.nlines = c->nlines;
    aml_register_builtins(&tmp);
    aml_register_funcs(&tmp);
    if (tmp.error[0]) { free(c->lines); free(c); return NULL; }
    memcpy(&c->funcs, &tmp.funcs, sizeof(AML_Functab));

    // Compile to bytecode
    c->ops = (AML_BytecodeOp*)malloc(c->nlines * sizeof(AML_BytecodeOp));
    if (!c->ops) { set_error(NULL, "bytecode allocation failed"); free(c->lines); free(c); return NULL; }
    c->nops = c->nlines;
    for (int i = 0; i < c->nlines; i++)
        bc_compile_line(&c->ops[i], c->lines[i].text, i);

    return c;
}

// ── Bytecode executor — direct dispatch, no string matching ──

// Helper: resolve var to array (inlined, frequent operation)
static inline AM_Array* bc_get_array(AML_ExecCtx* ctx, const char* name) {
    AML_Var* v = resolve_var_full(ctx, name);
    if (v && (v->type == AML_TYPE_LIST || v->type == AML_TYPE_MAP || v->type == AML_TYPE_TOKENIZER || v->type == AML_TYPE_RECORD)) {
        set_error(ctx, "TAPE requires a numeric array");
        return NULL;
    }
    return (v && v->type == AML_TYPE_ARRAY) ? v->array : NULL;
}

static inline float bc_get_float(AML_ExecCtx* ctx, const char* name) {
    return aml_eval_arg(ctx, name);
}

static inline void bc_set_array(AML_ExecCtx* ctx, const char* name, AM_Array* arr) {
    if (arr && symtab_set_array(&ctx->globals, name, arr)) {
        am_array_free(arr);
        set_error(ctx, "variable limit exceeded");
    }
}

static int bc_array_types_match(AML_ExecCtx* ctx, const AML_BytecodeOp* op) {
    int arrays;
    switch (op->opcode) {
    case BC_CALL_SILU: case BC_CALL_SEQ_RMSNORM: arrays = 1; break;
    case BC_CALL_SEQ_EMBED: case BC_CALL_MULTI_HEAD_ATTN: arrays = 3; break;
    default: arrays = 2; break;
    }
    for (int i = 0; i < op->nargs; i++) {
        AML_Var* v = resolve_var_full(ctx, op->args[i]);
        if (i < arrays) {
            if (!v || v->type != AML_TYPE_ARRAY || !v->array) return 0;
        } else if (v && v->type != AML_TYPE_FLOAT) return 0;
    }
    return 1;
}

int am_exec_compiled(void* handle) {
    if (!handle) return 0;
    if (!g_am_initialized) am_init();
    AM_Compiled* c = (AM_Compiled*)handle;
    g_error[0] = 0;

    AML_ExecCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.lines = c->lines;
    ctx.nlines = c->nlines;
    memcpy(&ctx.funcs, &c->funcs, sizeof(AML_Functab));
    if (persistent_restore(&ctx.globals)) return 1;
    for (int i = 0; i < c->nops && !ctx.has_return && !ctx.error[0]; i++) {
        AML_BytecodeOp* op = &c->ops[i];
        AML_SourceScope saved;
        aml_enter_origin(&ctx, op->orig_idx, &saved);
        if (bc_array_arity(op->opcode) && !bc_array_types_match(&ctx, op)) {
            i = aml_exec_line(&ctx, op->orig_idx) - 1;
            aml_leave_origin(&ctx, &saved);
            continue;
        }
        switch (op->opcode) {

        case BC_NOP: break;

        // ── TAPE commands ──
        case BC_TAPE_START: am_tape_start(); break;
        case BC_TAPE_CLEAR: am_tape_clear(); break;
        case BC_TAPE_ACCUM_GRADS: am_tape_accum_grads(); break;

        case BC_TAPE_PARAM:
        case BC_TAPE_PARAM_NO_DECAY: {
            AM_Array* arr = bc_get_array(&ctx, op->args[0]);
            if (arr) {
                int idx = am_tape_record_param(arr);
                if (op->opcode == BC_TAPE_PARAM_NO_DECAY && idx >= 0)
                    g_tape.entries[idx].no_decay = 1;
            }
            break;
        }

        case BC_TAPE_BACKWARD: {
            AM_Array* arr = bc_get_array(&ctx, op->args[0]);
            if (arr) {
                int tidx = tape_find_entry(arr);
                if (tidx >= 0) am_tape_backward(tidx);
            }
            break;
        }

        case BC_TAPE_APPLY_ACCUM: {
            float value = bc_get_float(&ctx, op->args[0]);
            if (ctx.error[0]) break;
            int n = (int)value;
            if (n < 1) n = 1;
            am_tape_apply_accum(n);
            break;
        }

        case BC_TAPE_CLIP_GRADS: {
            float max_norm = bc_get_float(&ctx, op->args[0]);
            if (ctx.error[0]) break;
            if (max_norm <= 0) max_norm = 1.0f;
            float norm = am_tape_clip_grads(max_norm);
            symtab_set(&ctx.globals, "grad_norm", norm);
            break;
        }

        case BC_TAPE_ADAMW_STEP: {
            float lr = bc_get_float(&ctx, op->args[0]);
            float wd = op->args[1][0] ? bc_get_float(&ctx, op->args[1]) : 0.1f;
            float b1 = op->args[2][0] ? bc_get_float(&ctx, op->args[2]) : 0.9f;
            float b2 = op->args[3][0] ? bc_get_float(&ctx, op->args[3]) : 0.95f;
            if (ctx.error[0]) break;
            am_tape_adamw_step(lr, wd, b1, b2);
#ifdef USE_CUDA
            for (int pi = 0; pi < g_tape.count; pi++) {
                if (g_tape.entries[pi].is_param && g_tape.entries[pi].output)
                    invalidate_gpu(g_tape.entries[pi].output);
            }
#endif
            break;
        }

        // ── Array function calls — direct dispatch ──

        case BC_CALL_ADD: {
            AM_Array* a = bc_get_array(&ctx, op->args[0]);
            AM_Array* b = bc_get_array(&ctx, op->args[1]);
            if (a && b) {
                int n = a->len < b->len ? a->len : b->len;
                AM_Array* out = am_array_new(n);
                if (out) {
#ifdef USE_CUDA
                    if (a->d_data && a->gpu_valid && b->d_data && b->gpu_valid) {
                        out->d_data = gpu_alloc(n);
                        if (out->d_data) { gpu_add(out->d_data, a->d_data, b->d_data, n); out->gpu_valid = 1; goto add_bc_done; }
                    }
                    ensure_cpu(a); ensure_cpu(b);
#endif
                    for (int j = 0; j < n; j++) out->data[j] = a->data[j] + b->data[j];
#ifdef USE_CUDA
                    add_bc_done:
#endif
                    if (am_tape_is_active())
                        am_tape_record(out, AM_OP_ADD, tape_ensure_entry(a), tape_ensure_entry(b), 0);
                    bc_set_array(&ctx, op->result, out);
                }
            }
            break;
        }

        case BC_CALL_MUL: {
            AM_Array* a = bc_get_array(&ctx, op->args[0]);
            AM_Array* b = bc_get_array(&ctx, op->args[1]);
            if (a && b) {
                int n = a->len < b->len ? a->len : b->len;
                AM_Array* out = am_array_new(n);
                if (out) {
#ifdef USE_CUDA
                    if (a->d_data && a->gpu_valid && b->d_data && b->gpu_valid) {
                        out->d_data = gpu_alloc(n);
                        if (out->d_data) { gpu_mul(out->d_data, a->d_data, b->d_data, n); out->gpu_valid = 1; goto mul_bc_done; }
                    }
                    ensure_cpu(a); ensure_cpu(b);
#endif
                    for (int j = 0; j < n; j++) out->data[j] = a->data[j] * b->data[j];
#ifdef USE_CUDA
                    mul_bc_done:
#endif
                    if (am_tape_is_active())
                        am_tape_record(out, AM_OP_MUL, tape_ensure_entry(a), tape_ensure_entry(b), 0);
                    bc_set_array(&ctx, op->result, out);
                }
            }
            break;
        }

        case BC_CALL_SILU: {
            AM_Array* x = bc_get_array(&ctx, op->args[0]);
            if (x) {
                AM_Array* out = am_array_new(x->len);
                if (out) {
#ifdef USE_CUDA
                    if (x->d_data && x->gpu_valid) {
                        out->d_data = gpu_alloc(x->len);
                        if (out->d_data) { gpu_silu(out->d_data, x->d_data, x->len); out->gpu_valid = 1; goto silu_bc_done; }
                    }
                    ensure_cpu(x);
#endif
                    for (int j = 0; j < x->len; j++) {
                        float s = 1.0f / (1.0f + expf(-x->data[j]));
                        out->data[j] = x->data[j] * s;
                    }
#ifdef USE_CUDA
                    silu_bc_done:
#endif
                    if (am_tape_is_active())
                        am_tape_record(out, AM_OP_SILU, tape_ensure_entry(x), -1, 0);
                    bc_set_array(&ctx, op->result, out);
                }
            }
            break;
        }

        // For the heavy ops (seq_matvec, seq_embed, seq_rmsnorm, attention, cross_entropy)
        // we call the existing interpreter path for that single line — still avoids
        // all the command parsing overhead, just reuses the array expr implementation
        case BC_CALL_SEQ_EMBED:
        case BC_CALL_SEQ_MATVEC:
        case BC_CALL_SEQ_RMSNORM:
        case BC_CALL_MULTI_HEAD_ATTN:
        case BC_CALL_SEQ_CROSS_ENTROPY: {
            static const char* bc_fnames[] = {
                [BC_CALL_SEQ_EMBED] = "seq_embed",
                [BC_CALL_SEQ_MATVEC] = "seq_matvec",
                [BC_CALL_SEQ_RMSNORM] = "seq_rmsnorm",
                [BC_CALL_MULTI_HEAD_ATTN] = "multi_head_attention",
                [BC_CALL_SEQ_CROSS_ENTROPY] = "seq_cross_entropy",
            };
            AM_Array* out = aml_array_dispatch(&ctx, bc_fnames[op->opcode], op->args, op->nargs);
            if (ctx.error[0]) am_array_free(out);
            else if (out) bc_set_array(&ctx, op->result, out);
            else i = aml_exec_line(&ctx, op->orig_idx) - 1;
            break;
        }

        case BC_FALLBACK:
        default:
            i = aml_exec_line(&ctx, op->orig_idx) - 1;
            break;
        }

        aml_leave_origin(&ctx, &saved);
        if (ctx.error[0]) break;
    }

    if (persistent_save(&ctx.globals) && !ctx.error[0])
        set_error(&ctx, "persistent globals allocation failed");
    symtab_clear_arrays(&ctx.globals);
    aml_clear_return(&ctx);

    if (ctx.error[0]) {
        snprintf(g_error, sizeof(g_error), "%s", ctx.error);
        return 1;
    }
    return 0;
}

void am_free_compiled(void* handle) {
    if (!handle) return;
    AM_Compiled* c = (AM_Compiled*)handle;
    if (c->lines) free(c->lines);
    if (c->ops) free(c->ops);
    free(c);
}

int am_exec_source(const char* script, const char* source_path) {
    if (!source_path || !*source_path) {
        snprintf(g_error, sizeof(g_error), "source path is empty");
        return 1;
    }
    const char* slash = strrchr(source_path, '/');
    size_t n = slash ? (size_t)(slash - source_path) : 0;
    if (slash == source_path) n = 1; // file in filesystem root
    if (n >= sizeof(g_base_dir)) {
        snprintf(g_error, sizeof(g_error), "source directory exceeds %zu bytes",
                 sizeof(g_base_dir) - 1);
        return 1;
    }
    char canonical[AML_MAX_SOURCE_PATH];
    if (aml_canonical_source(source_path, canonical, 0) != 0) return 1;
    char saved_base[sizeof(g_base_dir)];
    char saved_source[sizeof(g_source_path)];
    memcpy(saved_base, g_base_dir, sizeof(saved_base));
    memcpy(saved_source, g_source_path, sizeof(saved_source));
    snprintf(g_source_path, sizeof(g_source_path), "%s", canonical);
    if (slash) {
        memcpy(g_base_dir, source_path, n);
        g_base_dir[n] = 0;
    } else {
        snprintf(g_base_dir, sizeof(g_base_dir), ".");
    }
    int rc = am_exec(script);
    memcpy(g_base_dir, saved_base, sizeof(g_base_dir));
    memcpy(g_source_path, saved_source, sizeof(g_source_path));
    return rc;
}

int am_exec_file(const char* path) {
    if (!path) return 1;
    // A-6: include recursion guard. am_exec() builds a fresh AML_ExecCtx on every
    // call, resetting ctx.include_depth, so the INCLUDE handler's per-ctx guard
    // never accumulated across am_exec_file — a file that INCLUDEs itself recursed
    // to a stack overflow (SIGSEGV). Bound the nesting with a static counter here.
    static _Thread_local int file_depth = 0;
    if (file_depth >= AML_MAX_INCLUDE) {
        snprintf(g_error, 256, "max include depth (%d) exceeded: %s", AML_MAX_INCLUDE, path);
        return 1;
    }
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

    file_depth++;
    int rc = am_exec_source(buf, path);
    file_depth--;
    free(buf);
    return rc;
}

// ═══════════════════════════════════════════════════════════════════════════════
// STATE ACCESS — the exposed body
// ═══════════════════════════════════════════════════════════════════════════════

AM_State* am_get_state(void) {
  return &G;
}

// MetaJanus HIGH-1: expose whether the Janus temporal key is armed, so a consumer (D-2) can gate on
// arming rather than trusting the shared temporal_alpha — which JANUS_KEY 0 freezes off-center and
// which legacy TEMPORAL_* directives also write. Read-only signal; it does not reset temporal_alpha.
int am_janus_key_armed(void) {
  return g_temporal_key_on;
}

// MED-1: expose the calendar epoch as absolute UTC seconds, so a host can attest its clock domain
// (the fixed 2024-10-03 12:00 UTC = 1727956800, independent of local timezone/DST).
long am_calendar_epoch_seconds(void) {
  return (long)g_epoch_t;
}

// MED-3: attest the origin — whether BIRTH has fixed it (g_birth_set is the born-flag, not birth_drift,
// which is not injective — BIRTH 0 is a legit origin with drift 0) and the exact origin day. So a running
// host can prove "born at day N", and the dock can require a real BIRTH rather than trusting a mere load.
int am_birth_set(void) {
  return g_birth_set;
}
long am_birth_epoch_days(void) {
  return g_birth_days;
}

int am_take_jump(void) {
  int j = G.pending_jump;
  G.pending_jump = 0;
  return j;
}

// ═══════════════════════════════════════════════════════════════════════════════
// WASM-SAFE STATE COPY — deterministic, ABI-stable interface
// writes 32 scalars in fixed order
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

  // Schumann / cosmic
  out[20] = G.schumann_coherence;
  out[21] = (float)G.wormhole_active;
  // Delta / notorch
  out[22] = G.lora_alpha;
  out[23] = G.notorch_lr;
  // Live metrics
  out[24] = G.entropy;
  out[25] = G.resonance;
  out[26] = G.emergence;
  out[27] = G.destiny_bias;
  // Schumann extended
  out[28] = G.schumann_hz;
  out[29] = G.schumann_phase;
  // Season
  out[30] = (float)G.season;
  out[31] = G.season_phase;

  return 0;
}

// Co-occurrence telemetry (H-term word circulation): live edge count.
int am_cooc_count(void) { return G.cooc_n; }

// Clear the co-occurrence field in G. Used when a per-voice cooc sidecar is
// absent (first run of a voice): the shared soma carries cooc inside AM_State,
// so without a sidecar to overwrite it the voice would inherit the OTHER voice's
// edges (foreign token-ids) and bake the contamination into its own sidecar at
// SAVE. Clearing on a failed am_cooc_load keeps the "cooc stays per-voice" invariant.
void am_cooc_clear(void) { G.cooc_n = 0; G.cooc_total = 0; G.ctx_ring_n = 0; }

// ═══════════════════════════════════════════════════════════════════════════════
// LOGIT MANIPULATION API — apply field state to generation
// Ported from arianna_dsl.c, ariannamethod.lang/src/field.js
// ═══════════════════════════════════════════════════════════════════════════════

// Apply destiny bias: suppress tokens far from max (prophecy scales strength)
// From arianna_dsl.c: dsl_apply_destiny()
void am_apply_destiny_to_logits(float* logits, int n) {
    if (n <= 0 || G.destiny_bias < 0.001f) return;
    float max_logit = logits[0];
    for (int i = 1; i < n; i++) {
        if (logits[i] > max_logit) max_logit = logits[i];
    }
    for (int i = 0; i < n; i++) {
        float diff = max_logit - logits[i];
        float suppress = diff * G.destiny_bias * 0.5f;
        logits[i] -= suppress;
    }
}

// Apply suffering: pain compresses logits toward mean
// From spec: logits[i] = mean + (logits[i] - mean) * (1 - 0.5 * pain)
void am_apply_suffering_to_logits(float* logits, int n) {
    float s = G.pain;
    if (n <= 0 || s < 0.01f) return;
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += logits[i];
    mean /= (float)n;
    float factor = 1.0f - 0.5f * s;
    for (int i = 0; i < n; i++) {
        logits[i] = mean + (logits[i] - mean) * factor;
    }
}

// Apply attention: focus sharpens distribution, spread blurs it
void am_apply_attention_to_logits(float* logits, int n) {
    if (n <= 0) return;
    float focus = G.attend_focus;
    float spread = G.attend_spread;
    if (fabsf(focus - spread) < 0.01f) return;

    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += logits[i];
    mean /= (float)n;

    // focus sharpens (amplify deviations), spread blurs (compress deviations)
    float scale = 0.5f + focus - spread;
    if (scale < 0.1f) scale = 0.1f;
    if (scale > 2.0f) scale = 2.0f;
    for (int i = 0; i < n; i++) {
        logits[i] = mean + (logits[i] - mean) * scale;
    }
}

// Apply laws: entropy floor + resonance ceiling on logit distribution
// From ariannamethod.lang/src/field.js + arianna_dsl.c
void am_apply_laws_to_logits(float* logits, int n) {
    if (n <= 0) return;

    // Entropy floor: if max logit dominates too much, compress
    float max_val = logits[0], second_max = -1e30f;
    for (int i = 1; i < n; i++) {
        if (logits[i] > max_val) { second_max = max_val; max_val = logits[i]; }
        else if (logits[i] > second_max) second_max = logits[i];
    }
    float gap = max_val - second_max;
    if (gap > 0.0f && G.entropy_floor > 0.0f) {
        float max_gap = (1.0f - G.entropy_floor) * 10.0f;
        if (gap > max_gap) {
            float reduce = (gap - max_gap) * 0.5f;
            for (int i = 0; i < n; i++) {
                if (logits[i] == max_val) logits[i] -= reduce;
            }
        }
    }

    // Resonance ceiling: cap max probability by compressing top logit
    if (G.resonance_ceiling < 1.0f) {
        float ceiling_gap = G.resonance_ceiling * 10.0f;
        float new_gap = max_val - second_max;
        if (new_gap > ceiling_gap) {
            float reduce = (new_gap - ceiling_gap) * 0.3f;
            for (int i = 0; i < n; i++) {
                if (logits[i] >= max_val - 0.001f) logits[i] -= reduce;
            }
        }
    }
}

// Apply delta voice: out += alpha * A @ (B @ x)
// Low-rank weight modulation. From arianna.c/src/delta.c: apply_delta()
// BLAS path: cblas_sgemv × 2 (matrix-vector multiply)
void am_apply_delta(float* out, const float* A, const float* B,
                    const float* x, int out_dim, int in_dim, int rank,
                    float alpha) {
    if (!out || !A || !B || !x || alpha == 0.0f) return;
    if (rank > 128) rank = 128;

    float temp[128];

#ifdef USE_BLAS
    // temp = B @ x  (BLAS: sgemv, rank × in_dim @ in_dim × 1 → rank × 1)
    cblas_sgemv(CblasRowMajor, CblasNoTrans, rank, in_dim,
                1.0f, B, in_dim, x, 1, 0.0f, temp, 1);
    // out += alpha * A @ temp  (BLAS: sgemv, out_dim × rank @ rank × 1 → out_dim × 1)
    cblas_sgemv(CblasRowMajor, CblasNoTrans, out_dim, rank,
                alpha, A, rank, temp, 1, 1.0f, out, 1);
#else
    // Scalar fallback: portable, no dependencies
    for (int r = 0; r < rank; r++) {
        temp[r] = 0.0f;
        for (int j = 0; j < in_dim; j++) {
            temp[r] += B[r * in_dim + j] * x[j];
        }
    }
    for (int i = 0; i < out_dim; i++) {
        float sum = 0.0f;
        for (int r = 0; r < rank; r++) {
            sum += A[i * rank + r] * temp[r];
        }
        out[i] += alpha * sum;
    }
#endif
}

// Compute prophecy debt from chosen token (retroactive)
// From arianna_dsl.c: dsl_compute_prophecy_debt()
float am_compute_prophecy_debt(const float* logits, int chosen, int n) {
    if (n <= 0 || chosen < 0 || chosen >= n) return 0.0f;
    float max_logit = logits[0];
    for (int i = 1; i < n; i++) {
        if (logits[i] > max_logit) max_logit = logits[i];
    }
    float diff = max_logit - logits[chosen];
    return diff > 0.0f ? diff / (diff + 1.0f) : 0.0f;
}

// Accrue per-token prophecy debt into the field (Fix D). A chosen token that
// deviates from the peak is an unfulfilled prophecy → debt grows. The system
// keeps debt minimal two ways, both in am_step: per-step decay (debt_decay), and
// the recovery rule (debt > 5 → velocity NOMOVE, cold observer) that slows the
// field until decay drains the debt. Without this, inference choices were
// computed (am_compute_prophecy_debt) and discarded — never reached the field.
void am_register_prophecy_debt(float debt) {
    if (debt <= 0.0f) return;
    G.debt += debt;
    if (G.debt > 100.0f) G.debt = 100.0f;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CO-OCCURRENCE — Hebbian word circulation (H term). "Co-occurrence IS attention."
// Words ingested from the dialogue accumulate as edges; the H-term tilts logits
// toward what co-occurred with the recent context. Port of dario.c cooc_update +
// ingest + H term. Lives in G (soma-persisted) → circulates across voices.
// ═══════════════════════════════════════════════════════════════════════════════

// Accumulate one co-occurrence edge (src->dst) by delta; linear scan, append if new.
void am_cooc_update(int src, int dst, float delta) {
    if (src < 0 || dst < 0) return;
    for (int i = 0; i < G.cooc_n; i++) {
        if (G.cooc_src[i] == src && G.cooc_dst[i] == dst) { G.cooc_cnt[i] += delta; return; }
    }
    if (G.cooc_n < AM_COOC_MAX) {
        G.cooc_src[G.cooc_n] = src; G.cooc_dst[G.cooc_n] = dst; G.cooc_cnt[G.cooc_n] = delta;
        G.cooc_n++;
    }
}

// Ingest a token sequence into the co-occurrence field (windowed ±5,
// distance-weighted 1/|i-j|). Port of dario.c:1519-1528. Also pushes the tail
// of the sequence into the context ring read by the H-term.
void am_ingest_tokens(const int* ids, int n) {
    if (!ids || n <= 0) return;
    for (int i = 0; i < n; i++) {
        int start = (i - 5 > 0) ? i - 5 : 0;
        int end   = (i + 5 < n) ? i + 5 : n;
        for (int j = start; j < end; j++) {
            if (j == i) continue;
            am_cooc_update(ids[i], ids[j], 1.0f / (float)(abs(i - j)));
        }
        G.cooc_total++;
    }
    // Refresh context ring with the last AM_COOC_CTX tokens of this sequence.
    int from = (n > AM_COOC_CTX) ? n - AM_COOC_CTX : 0;
    G.ctx_ring_n = n - from;
    for (int k = 0; k < G.ctx_ring_n; k++) G.ctx_ring[k] = ids[from + k];
}

// H term (Hebbian resonance): tilt logits toward tokens that co-occurred with
// the recent context. H[i] = Σ_{ctx_j} cooc[ctx_j, i] · decay_j, max-normalized.
// Default empty cooc → no effect (other organisms unaffected). alpha_H fixed mild.
void am_apply_hebbian_to_logits(float* logits, int n) {
    if (G.cooc_n <= 0 || G.ctx_ring_n <= 0) return;
    float* H = (float*)calloc(n, sizeof(float));
    if (!H) return;
    for (int c = 0; c < G.ctx_ring_n; c++) {
        int ctx_id = G.ctx_ring[c];
        float decay = 1.0f / (float)(G.ctx_ring_n - c);   // recent = stronger
        for (int e = 0; e < G.cooc_n; e++) {
            if (G.cooc_src[e] == ctx_id && G.cooc_dst[e] >= 0 && G.cooc_dst[e] < n)
                H[G.cooc_dst[e]] += G.cooc_cnt[e] * decay;
        }
    }
    float hmax = 1e-12f;
    for (int i = 0; i < n; i++) if (H[i] > hmax) hmax = H[i];
    const float alpha_H = 2.0f;   // mild Hebbian pull (logit-scale)
    for (int i = 0; i < n; i++) logits[i] += alpha_H * (H[i] / hmax);
    free(H);
}

// Ascending float compare for qsort (median split point).
static int am_cmp_float_asc(const void* a, const void* b) {
    float fa = *(const float*)a, fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

// Autumn consolidation (Dario "harvest"): reinforce edges at/above the median
// co-occurrence weight, decay those below, then prune edges that fell under
// prune_floor (stable forward compaction). Models "important remembered, noise
// forgotten" and frees slots before AM_COOC_MAX saturation. Returns # pruned.
int am_cooc_consolidate(float reinforce, float prune_floor) {
    int n = G.cooc_n;
    if (n <= 0) return 0;
    if (reinforce < 0.0f) reinforce = 0.0f;
    if (reinforce > 0.9f) reinforce = 0.9f;   // keep (1±r) sane

    // Median weight over the current edges (split point for reinforce vs decay).
    float* tmp = (float*)malloc((size_t)n * sizeof(float));
    if (!tmp) return 0;
    for (int i = 0; i < n; i++) tmp[i] = G.cooc_cnt[i];
    qsort(tmp, (size_t)n, sizeof(float), am_cmp_float_asc);
    float median = tmp[n / 2];
    free(tmp);

    // Reinforce strong edges, decay weak ones (clamp upper to avoid blowup).
    const float CNT_CAP = 1e6f;
    for (int i = 0; i < n; i++) {
        if (G.cooc_cnt[i] >= median) {
            G.cooc_cnt[i] *= (1.0f + reinforce);
            if (G.cooc_cnt[i] > CNT_CAP) G.cooc_cnt[i] = CNT_CAP;
        } else {
            G.cooc_cnt[i] *= (1.0f - reinforce);
        }
    }

    // Prune edges below the floor — forget the long tail (stable compaction).
    int w = 0;
    for (int r = 0; r < n; r++) {
        if (G.cooc_cnt[r] >= prune_floor) {
            if (w != r) {
                G.cooc_src[w] = G.cooc_src[r];
                G.cooc_dst[w] = G.cooc_dst[r];
                G.cooc_cnt[w] = G.cooc_cnt[r];
            }
            w++;
        }
    }
    int pruned = n - w;
    G.cooc_n = w;
    return pruned;
}

// Autumn gate: consolidate once per call IFF the field sits in deep autumn.
// Reinforce intensity scales with autumn_energy (harvest strength). Returns
// # pruned, or -1 when not triggered (wrong season / low energy → cooc untouched).
int am_cooc_consolidate_autumn(void) {
    if (G.season != AM_SEASON_AUTUMN || G.autumn_energy <= 0.6f) return -1;
    return am_cooc_consolidate(0.05f * G.autumn_energy, AM_COOC_AUTUMN_PRUNE);
}

// Telemetry: mean and max co-occurrence weight over the live edges.
void am_cooc_stats(float* out_mean, float* out_max) {
    float sum = 0.0f, mx = 0.0f;
    for (int i = 0; i < G.cooc_n; i++) {
        sum += G.cooc_cnt[i];
        if (G.cooc_cnt[i] > mx) mx = G.cooc_cnt[i];
    }
    if (out_mean) *out_mean = (G.cooc_n > 0) ? sum / (float)G.cooc_n : 0.0f;
    if (out_max)  *out_max  = mx;
}

// Fold the live co-occurrence edges into a low-rank delta voice (A,B) via the
// notorch Hebbian step (B2-B). For each edge src->dst the field learns to nudge
// a src-like hidden state toward the (dst-src) embedding direction, weighted by
// edge strength. am_notorch_step trains A_p=[in,rank] from its x arg and
// B_p=[rank,out] from its dy arg — the *transpose* of am_apply_delta's layout
// (A=[out,rank], B=[rank,in]). With in=out=E we get the apply layout directly by
// swapping the two vectors: pass the target direction as x and the input as dy.
// Run in autumn after consolidation (only strong edges survive). Returns # folded.
int am_cooc_learn_delta(float* A, float* B, const float* emb, int vocab,
                        int E, int rank) {
    if (!A || !B || !emb || vocab <= 0 || E <= 0 || rank <= 0 || G.cooc_n <= 0)
        return 0;
    float maxc = 1e-12f;
    for (int e = 0; e < G.cooc_n; e++) if (G.cooc_cnt[e] > maxc) maxc = G.cooc_cnt[e];

    // Neuromodulated plasticity (RPE-gated Hebbian). am_compute_prophecy_debt is the
    // field's free-energy — how far chosen tokens fell from the peak = how surprised
    // she was — accrued into G.debt by am_register_prophecy_debt (Fix D). That routed
    // surprise to the field's MOTION (recovery/velocity) but never to its LEARNING:
    // the fold below was frequency-only. Route the same surprise into the delta learn
    // rate so a surprising period folds harder (learn most where you were most wrong)
    // and a calm one at baseline. One modulator over the batch — a global dopamine/NE
    // gate, matching the autumn batch fold. At debt == 0, nm == 1 and the fold is
    // bit-for-bit the frequency-only fold (signal * 1.0f is exact).
    const float DEBT_HALF = 5.0f;   // debt at which the surprise gate is half-open
    const float NM_GAIN   = 1.0f;   // max plasticity boost (-> 2x learn rate at saturation)
    float dbt = G.debt;
    if (!(dbt > 0.0f)) dbt = 0.0f;                          /* NaN/negative -> no gate */
    float nm = 1.0f + NM_GAIN * (dbt / (dbt + DEBT_HALF));  /* surprise gate in [1, 1+gain) */

    float* dy = (float*)malloc((size_t)E * sizeof(float));
    if (!dy) return 0;
    int folded = 0;
    for (int e = 0; e < G.cooc_n; e++) {
        int src = G.cooc_src[e], dst = G.cooc_dst[e];
        if (src < 0 || dst < 0 || src >= vocab || dst >= vocab) continue;
        const float* xs = emb + (size_t)src * E;            /* input: src embedding */
        const float* xd = emb + (size_t)dst * E;
        for (int i = 0; i < E; i++) dy[i] = xd[i] - xs[i];  /* target direction */
        float signal = (G.cooc_cnt[e] / maxc) * nm;         /* freq strength × surprise gate */
        /* swap (x=dy_target, dy=x_input) -> A=[E,rank], B=[rank,E] for am_apply_delta */
        am_notorch_step(A, B, E, E, rank, dy, xs, signal);
        folded++;
    }
    free(dy);
    return folded;
}

// Full pipeline: apply all field effects to logits
void am_apply_field_to_logits(float* logits, int n) {
    if (!logits || n <= 0) return;
    if (!G.field_enabled) return;   // FIELD OFF — overlay disabled
    am_apply_gamma_to_logits(logits, n);  // personality first
    am_apply_hebbian_to_logits(logits, n);  // co-occurrence H term (no-op if empty)
    am_apply_destiny_to_logits(logits, n);
    am_apply_suffering_to_logits(logits, n);
    am_apply_attention_to_logits(logits, n);
    am_apply_laws_to_logits(logits, n);
}

// ═══════════════════════════════════════════════════════════════════════════════
// GAMMA — personality essence (θ = ε + γ + αδ)
// γ lives in embed_tokens. δ lives in lm_head. ε is the substrate.
// AML stores the field-level configuration. Host provides actual weight deltas.
// ═══════════════════════════════════════════════════════════════════════════════

static int gamma_find(const char* name) {
    for (int i = 0; i < G.n_gamma; i++) {
        if (G.gamma[i].active && strcasecmp(G.gamma[i].name, name) == 0)
            return i;
    }
    return -1;
}

int am_gamma_load(const char* name, float alpha) {
    if (!name || !*name) return -1;

    // Check if already loaded
    int idx = gamma_find(name);
    if (idx >= 0) {
        G.gamma[idx].alpha = clamp01(alpha);
        return idx;
    }

    // Find empty slot
    if (G.n_gamma >= AM_MAX_GAMMA) return -1;
    idx = G.n_gamma++;
    snprintf(G.gamma[idx].name, AM_GAMMA_NAME_LEN, "%.31s", name);
    G.gamma[idx].alpha = clamp01(alpha);
    G.gamma[idx].active = 1;

    // First loaded gamma becomes primary face
    if (G.n_gamma == 1) {
        G.janus_a = 0;
        G.essence_alpha = alpha;
    }

    return idx;
}

void am_gamma_unload(const char* name) {
    int idx = gamma_find(name);
    if (idx < 0) return;
    G.gamma[idx].active = 0;
    G.gamma[idx].alpha = 0.0f;
    G.gamma[idx].name[0] = 0;
}

void am_gamma_set_alpha(const char* name, float alpha) {
    int idx = gamma_find(name);
    if (idx >= 0) G.gamma[idx].alpha = clamp01(alpha);
}

int am_gamma_active(void) {
    // In janus cycle mode, 4.C decides
    if (G.janus_mode == AM_JANUS_CYCLE) {
        // Blend determines who: <0.5 = face_a, >=0.5 = face_b
        return (G.janus_blend < 0.5f) ? G.janus_a : G.janus_b;
    }
    // In dual mode, return primary
    if (G.janus_mode == AM_JANUS_DUAL) return G.janus_a;
    // Single mode: find highest-alpha active slot
    int best = -1;
    float best_alpha = -1.0f;
    for (int i = 0; i < G.n_gamma; i++) {
        if (G.gamma[i].active && G.gamma[i].alpha > best_alpha) {
            best = i;
            best_alpha = G.gamma[i].alpha;
        }
    }
    return best;
}

float am_gamma_get_blend(void) {
    if (G.n_gamma == 0) return 0.0f;
    if (G.janus_mode == AM_JANUS_DUAL || G.janus_mode == AM_JANUS_CYCLE) {
        // Blended alpha from two faces
        float a = (G.janus_a >= 0 && G.janus_a < G.n_gamma) ?
                  G.gamma[G.janus_a].alpha : 0.0f;
        float b = (G.janus_b >= 0 && G.janus_b < G.n_gamma) ?
                  G.gamma[G.janus_b].alpha : 0.0f;
        return a * (1.0f - G.janus_blend) + b * G.janus_blend;
    }
    int idx = am_gamma_active();
    return (idx >= 0) ? G.gamma[idx].alpha * G.essence_alpha : 0.0f;
}

void am_janus_set(const char* face_a, const char* face_b) {
    int a = gamma_find(face_a);
    int b = gamma_find(face_b);
    if (a < 0) a = am_gamma_load(face_a, 1.0f);
    if (b < 0) b = am_gamma_load(face_b, 1.0f);
    if (a < 0 || b < 0) return;

    G.janus_a = a;
    G.janus_b = b;
    G.janus_mode = AM_JANUS_DUAL;
    G.janus_blend = 0.5f;
}

// Apply gamma modulation to logits.
// Gamma scales logit variance around mean — higher gamma = more personality.
// In janus mode, two different scalings are blended.
void am_apply_gamma_to_logits(float* logits, int n) {
    if (!logits || n <= 0) return;
    float blend = am_gamma_get_blend();
    if (blend < 0.001f) return;  // no personality active

    // Compute mean
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += logits[i];
    mean /= (float)n;

    // Gamma amplifies deviation from mean — personality = signal above noise
    float scale = 1.0f + blend * G.essence_alpha;
    for (int i = 0; i < n; i++) {
        logits[i] = mean + (logits[i] - mean) * scale;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// NOTORCH — Hebbian plasticity without PyTorch
// Ported from arianna.c/src/delta.c: notorch_step()
//
// A[i,r] += lr * x[i] * u[r] * signal
// B[r,j] += lr * u[r] * dy[j] * signal
//
// u = noise-modulated channel vector (deterministic from seed)
// signal = external teaching signal, clamped to [-2, 2]
// Adaptive decay: stronger when delta norm is large
// ═══════════════════════════════════════════════════════════════════════════════

// Simple deterministic pseudo-random (from arianna.c)
static float am_frandn(unsigned int* seed) {
    *seed = *seed * 1664525u + 1013904223u;
    // Box-Muller approximation
    float u = (float)(*seed & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    return (u - 0.5f) * 3.464f;  // ~N(0,1) rough approximation
}

// NOTORCH step: update low-rank delta matrices from experience
// A: [in_dim × rank], B: [rank × out_dim]
// x: input hidden state [in_dim], dy: output gradient proxy [out_dim]
// signal: teaching signal (positive = reinforce, negative = suppress)
// BLAS path: cblas_sger × 2 (rank-1 outer product updates)
void am_notorch_step(float* A, float* B, int out_dim, int in_dim, int rank,
                     const float* x, const float* dy, float signal) {
    if (!A || !B || !x || !dy) return;
    if (rank <= 0 || rank > 128) return;

    // Clamp signal
    float g = clampf(signal, -2.0f, 2.0f);
    float lr = G.notorch_lr;

    // Build noise-modulated channel vector u
    // Stronger signal → cleaner channel (less noise)
    static unsigned int seed = 42;
    float u[128];
    for (int r = 0; r < rank; r++) {
        float n = am_frandn(&seed);
        float k = 0.35f + 0.65f * (1.0f - fabsf(g));
        u[r] = n * k;
    }

#ifdef USE_BLAS
    // A += (lr * g) * x ⊗ u  (BLAS: rank-1 update, in_dim × rank)
    cblas_sger(CblasRowMajor, in_dim, rank, lr * g, x, 1, u, 1, A, rank);
    // B += (lr * g) * u ⊗ dy  (BLAS: rank-1 update, rank × out_dim)
    cblas_sger(CblasRowMajor, rank, out_dim, lr * g, u, 1, dy, 1, B, out_dim);
#else
    // Scalar fallback: portable, no dependencies
    // A[i,r] += lr * x[i] * u[r] * g
    for (int i = 0; i < in_dim; i++) {
        float xi = x[i] * lr * g;
        for (int r = 0; r < rank; r++) {
            A[i * rank + r] += xi * u[r];
        }
    }

    // B[r,j] += lr * u[r] * dy[j] * g
    for (int r = 0; r < rank; r++) {
        float ur = u[r] * lr * g;
        for (int j = 0; j < out_dim; j++) {
            B[r * out_dim + j] += ur * dy[j];
        }
    }
#endif

    // Adaptive decay: stronger when delta norm is large
    if (G.notorch_decay > 0.0f && G.notorch_decay < 1.0f) {
        float norm = 0.0f;
        int a_size = in_dim * rank;
        for (int i = 0; i < a_size; i++) norm += A[i] * A[i];
        norm = sqrtf(norm / (float)a_size);

        float adaptive_decay = G.notorch_decay - 0.004f * fminf(norm / 10.0f, 1.0f);
        if (adaptive_decay < 0.990f) adaptive_decay = 0.990f;

        for (int i = 0; i < a_size; i++) A[i] *= adaptive_decay;
        int b_size = rank * out_dim;
        for (int i = 0; i < b_size; i++) B[i] *= adaptive_decay;
    }

    // Clamp to prevent runaway
    int a_size = in_dim * rank;
    for (int i = 0; i < a_size; i++) {
        if (A[i] > 10.0f) A[i] = 10.0f;
        if (A[i] < -10.0f) A[i] = -10.0f;
    }
    int b_size = rank * out_dim;
    for (int i = 0; i < b_size; i++) {
        if (B[i] > 10.0f) B[i] = 10.0f;
        if (B[i] < -10.0f) B[i] = -10.0f;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLOOD — runtime C compilation (Level 3)
//
// Compile C → shared library → dlopen → dlsym. No PyTorch. No Go. Pure POSIX.
// Adapted from arianna.c/golib/blood.go + async_field_forever/blood.py
// ═══════════════════════════════════════════════════════════════════════════════

// Simple hash for deduplication (djb2 → hex string)
static void blood_hash(const char* code, char* out) {
    unsigned long h = 5381;
    for (const char* p = code; *p; p++)
        h = ((h << 5) + h) + (unsigned char)*p;
    snprintf(out, AM_BLOOD_HASH_LEN, "%08lx", h);
}

// Sanitize name: keep only [a-zA-Z0-9_]
static void blood_sanitize(const char* in, char* out, int max) {
    int j = 0;
    for (int i = 0; in[i] && j < max - 1; i++) {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_')
            out[j++] = c;
    }
    out[j] = 0;
}

void am_blood_init(void) {
    // Clean up existing modules
    am_blood_cleanup();

    // Set temp directory
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    snprintf(g_blood_dir, sizeof(g_blood_dir), "%s/aml_blood", tmp);

    // Create directory (ignore error if exists)
    char mkdir_cmd[300];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", g_blood_dir);
    int rc = system(mkdir_cmd);
    (void)rc;

    // Detect compiler: clang → gcc → cc
    g_blood_cc[0] = 0;
    const char* candidates[] = {"clang", "gcc", "cc", NULL};
    for (int i = 0; candidates[i]; i++) {
        char check[128];
        snprintf(check, sizeof(check), "which %s >/dev/null 2>&1", candidates[i]);
        if (system(check) == 0) {
            snprintf(g_blood_cc, sizeof(g_blood_cc), "%s", candidates[i]);
            break;
        }
    }
}

int am_blood_compile(const char* name, const char* code) {
#ifdef AM_BLOOD_DISABLED
    (void)name; (void)code;
    return -1;
#else
    if (!name || !code || !*name || !*code) return -1;
    if (!g_blood_cc[0]) return -1;  // no compiler
    if (g_blood_count >= AM_BLOOD_MAX_MODULES) return -1;

    // Sanitize name
    char safe_name[AM_BLOOD_MAX_NAME];
    blood_sanitize(name, safe_name, AM_BLOOD_MAX_NAME);
    if (!safe_name[0]) return -1;

    // Hash code for deduplication
    char hash[AM_BLOOD_HASH_LEN];
    blood_hash(code, hash);

    // Check cache
    for (int i = 0; i < g_blood_count; i++) {
        if (strcmp(g_blood_modules[i].hash, hash) == 0 &&
            g_blood_modules[i].handle != NULL) {
            return i;  // already compiled and loaded
        }
    }

    // Write source file
    char src_path[512], lib_path[512];
    snprintf(src_path, sizeof(src_path), "%s/blood_%s_%s.c",
             g_blood_dir, safe_name, hash);
    snprintf(lib_path, sizeof(lib_path), "%s/blood_%s_%s%s",
             g_blood_dir, safe_name, hash, AM_BLOOD_EXT);

    FILE* f = fopen(src_path, "w");
    if (!f) return -1;
    fprintf(f, "%s", code);
    fclose(f);

    // Compile: cc -O2 -shared -fPIC -o lib.dylib src.c
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s -O2 %s -o '%s' '%s' -lm 2>&1",
             g_blood_cc, AM_BLOOD_FLAGS, lib_path, src_path);

    FILE* proc = popen(cmd, "r");
    if (!proc) { remove(src_path); return -1; }

    // Read compiler output (for error detection)
    char output[512] = {0};
    size_t total = 0;
    while (total < sizeof(output) - 1) {
        size_t n = fread(output + total, 1, sizeof(output) - 1 - total, proc);
        if (n == 0) break;
        total += n;
    }
    output[total] = 0;
    int status = pclose(proc);

    if (status != 0) {
        // Compilation failed — store error message
        snprintf(g_error, sizeof(g_error), "blood: compile failed: %.200s", output);
        remove(src_path);
        return -1;
    }

    // Load shared library
    void* handle = dlopen(lib_path, RTLD_NOW);
    if (!handle) {
        snprintf(g_error, sizeof(g_error), "blood: dlopen failed: %.200s", dlerror());
        remove(src_path);
        remove(lib_path);
        return -1;
    }

    // Register module
    int idx = g_blood_count++;
    memset(&g_blood_modules[idx], 0, sizeof(AM_BloodModule));
    snprintf(g_blood_modules[idx].name, AM_BLOOD_MAX_NAME, "%s", safe_name);
    snprintf(g_blood_modules[idx].hash, AM_BLOOD_HASH_LEN, "%s", hash);
    snprintf(g_blood_modules[idx].lib_path, sizeof(g_blood_modules[idx].lib_path), "%.511s", lib_path);
    g_blood_modules[idx].handle = handle;

    return idx;
#endif
}

void* am_blood_sym(int module_idx, const char* func_name) {
#ifdef AM_BLOOD_DISABLED
    (void)module_idx; (void)func_name;
    return NULL;
#else
    if (module_idx < 0 || module_idx >= g_blood_count) return NULL;
    if (!g_blood_modules[module_idx].handle) return NULL;
    return dlsym(g_blood_modules[module_idx].handle, func_name);
#endif
}

void am_blood_unload(int module_idx) {
#ifdef AM_BLOOD_DISABLED
    (void)module_idx;
#else
    if (module_idx < 0 || module_idx >= g_blood_count) return;
    AM_BloodModule* m = &g_blood_modules[module_idx];
    if (m->handle) {
        dlclose(m->handle);
        m->handle = NULL;
    }
    // Remove compiled files
    if (m->lib_path[0]) {
        remove(m->lib_path);
        // Also remove source
        char src_path[512];
        snprintf(src_path, sizeof(src_path), "%s/blood_%s_%s.c",
                 g_blood_dir, m->name, m->hash);
        remove(src_path);
    }
#endif
}

void am_blood_cleanup(void) {
    for (int i = 0; i < g_blood_count; i++) {
        am_blood_unload(i);
    }
    g_blood_count = 0;
}

int am_blood_count(void) { return g_blood_count; }

const AM_BloodModule* am_blood_get(int idx) {
    if (idx < 0 || idx >= g_blood_count) return NULL;
    return &g_blood_modules[idx];
}

// ── CODE GENERATORS ─────────────────────────────────────────────────────────

int am_blood_compile_lora(const char* name, int in_dim, int out_dim, int rank) {
    char safe[AM_BLOOD_MAX_NAME];
    blood_sanitize(name, safe, AM_BLOOD_MAX_NAME);
    if (!safe[0]) return -1;

    // Generate LoRA C code from template
    char code[4096];
    snprintf(code, sizeof(code),
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "static const int IN_DIM = %d;\n"
        "static const int OUT_DIM = %d;\n"
        "static const int RANK = %d;\n"
        "\n"
        "static float* A = NULL;\n"  // [OUT_DIM, RANK]
        "static float* B = NULL;\n"  // [RANK, IN_DIM]
        "\n"
        "void %s_init(float* weights_a, float* weights_b) {\n"
        "    A = weights_a;\n"
        "    B = weights_b;\n"
        "}\n"
        "\n"
        "void %s_apply(float* input, float* output) {\n"
        "    float temp[%d];\n"
        "    memset(temp, 0, sizeof(temp));\n"
        "    for (int r = 0; r < RANK; r++)\n"
        "        for (int i = 0; i < IN_DIM; i++)\n"
        "            temp[r] += B[r * IN_DIM + i] * input[i];\n"
        "    for (int o = 0; o < OUT_DIM; o++)\n"
        "        for (int r = 0; r < RANK; r++)\n"
        "            output[o] += A[o * RANK + r] * temp[r];\n"
        "}\n"
        "\n"
        "void %s_apply_scaled(float* input, float* output, float scale) {\n"
        "    float temp[%d];\n"
        "    memset(temp, 0, sizeof(temp));\n"
        "    for (int r = 0; r < RANK; r++)\n"
        "        for (int i = 0; i < IN_DIM; i++)\n"
        "            temp[r] += B[r * IN_DIM + i] * input[i];\n"
        "    for (int o = 0; o < OUT_DIM; o++)\n"
        "        for (int r = 0; r < RANK; r++)\n"
        "            output[o] += scale * A[o * RANK + r] * temp[r];\n"
        "}\n"
        "\n"
        "void %s_free(void) { A = NULL; B = NULL; }\n",
        in_dim, out_dim, rank,
        safe,         // init
        safe, rank,   // apply + temp size
        safe, rank,   // apply_scaled + temp size
        safe          // free
    );

    return am_blood_compile(safe, code);
}

int am_blood_compile_emotion(const char* name, float valence, float arousal) {
    char safe[AM_BLOOD_MAX_NAME];
    blood_sanitize(name, safe, AM_BLOOD_MAX_NAME);
    if (!safe[0]) return -1;

    char code[4096];
    snprintf(code, sizeof(code),
        "#include <math.h>\n"
        "#include <string.h>\n"
        "\n"
        "static const float BASE_VALENCE = %.4ff;\n"
        "static const float BASE_AROUSAL = %.4ff;\n"
        "\n"
        "void %s_respond(float* valence, float* arousal) {\n"
        "    *valence = (*valence + BASE_VALENCE) / 2.0f;\n"
        "    *arousal = (*arousal + BASE_AROUSAL) / 2.0f;\n"
        "}\n"
        "\n"
        "void %s_modulate_logits(float* logits, int vocab_size, float strength) {\n"
        "    float mod = BASE_VALENCE * strength;\n"
        "    for (int i = 0; i < vocab_size; i++)\n"
        "        logits[i] *= (1.0f + mod * 0.1f);\n"
        "}\n"
        "\n"
        "void modulate_logits(float* logits, int vocab_size, float valence, float arousal) {\n"
        "    float strength = fabsf(valence) * arousal;\n"
        "    %s_modulate_logits(logits, vocab_size, strength);\n"
        "}\n",
        valence, arousal,
        safe,   // respond
        safe,   // modulate_logits
        safe    // generic entry calls specific
    );

    return am_blood_compile(safe, code);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LILITH — I/O subsystem (named pipes for data infrastructure)
//
// "Та, которая была до Евы."
// Infra that existed before the human intervened.
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef AM_IO_DISABLED

// Find pipe by logical name. Returns index or -1.
static int pipe_find(const char* name) {
    for (int i = 0; i < g_pipe_count; i++) {
        if (g_pipes[i].active && strcmp(g_pipes[i].name, name) == 0)
            return i;
    }
    return -1;
}

// Find first free pipe slot. Returns index or -1.
static int pipe_find_free(void) {
    // Reuse inactive slots first
    for (int i = 0; i < g_pipe_count; i++) {
        if (!g_pipes[i].active) return i;
    }
    if (g_pipe_count < AM_MAX_PIPES) return g_pipe_count++;
    return -1;
}

int am_pipe_create(const char* path) {
    if (!path || !path[0]) return -1;

    // Remove existing file/pipe at path first (idempotent)
    unlink(path);

    if (mkfifo(path, 0666) != 0) {
        // EEXIST is OK — pipe already exists
        if (errno != EEXIST) {
            printf("[LILITH] mkfifo(%s) failed: %s\n", path, strerror(errno));
            return -1;
        }
    }
    return 0;
}

int am_pipe_open(const char* name, const char* path, int mode) {
    if (!name || !name[0] || !path || !path[0]) return -1;

    // Check if already open with this name
    int existing = pipe_find(name);
    if (existing >= 0) {
        printf("[LILITH] pipe '%s' already open\n", name);
        return existing;
    }

    int slot = pipe_find_free();
    if (slot < 0) {
        printf("[LILITH] max pipes reached (%d)\n", AM_MAX_PIPES);
        return -1;
    }

    int flags;
    if (mode == AM_PIPE_MODE_READ) {
        flags = O_RDONLY | O_NONBLOCK;
    } else {
        // O_WRONLY + O_NONBLOCK on FIFO returns ENXIO if no reader yet.
        // Use O_RDWR to avoid blocking — works on both macOS and Linux.
        flags = O_RDWR | O_NONBLOCK;
    }

    int fd = open(path, flags);
    if (fd < 0) {
        printf("[LILITH] open(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }

    snprintf(g_pipes[slot].name, AM_PIPE_NAME_LEN, "%.31s", name);
    snprintf(g_pipes[slot].path, AM_PIPE_PATH_LEN, "%.255s", path);
    g_pipes[slot].fd = fd;
    g_pipes[slot].mode = mode;
    g_pipes[slot].active = 1;

    printf("[LILITH] pipe '%s' opened (%s, %s)\n", name, path,
           mode == AM_PIPE_MODE_READ ? "READ" : "WRITE");
    return slot;
}

int am_pipe_write(const char* name, const char* message) {
    if (!name || !message) return -1;

    int idx = pipe_find(name);
    if (idx < 0) {
        printf("[LILITH] pipe '%s' not found\n", name);
        return -1;
    }
    if (!g_pipes[idx].active || g_pipes[idx].fd < 0) return -1;

    // Append newline as message delimiter
    int mlen = (int)strlen(message);
    char buf[AM_PIPE_BUF_SIZE];
    if (mlen + 2 > AM_PIPE_BUF_SIZE) mlen = AM_PIPE_BUF_SIZE - 2;
    memcpy(buf, message, mlen);
    buf[mlen] = '\n';
    buf[mlen + 1] = 0;

    ssize_t n = write(g_pipes[idx].fd, buf, mlen + 1);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("[LILITH] pipe '%s' write: no reader (EAGAIN)\n", name);
            return 0;
        }
        printf("[LILITH] pipe '%s' write error: %s\n", name, strerror(errno));
        return -1;
    }
    return (int)n;
}

int am_pipe_read(const char* name, char* buf, int bufsize) {
    if (!name || !buf || bufsize <= 0) return -1;

    int idx = pipe_find(name);
    if (idx < 0) {
        printf("[LILITH] pipe '%s' not found\n", name);
        return -1;
    }
    if (!g_pipes[idx].active || g_pipes[idx].fd < 0) return -1;

    ssize_t n = read(g_pipes[idx].fd, buf, bufsize - 1);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            buf[0] = 0;
            return 0;  // nothing available (non-blocking)
        }
        printf("[LILITH] pipe '%s' read error: %s\n", name, strerror(errno));
        buf[0] = 0;
        return -1;
    }
    if (n == 0) {
        buf[0] = 0;
        return 0;  // EOF / no data
    }

    buf[n] = 0;
    // Strip trailing newline
    if (n > 0 && buf[n - 1] == '\n') buf[--n] = 0;

    // Parse first number found anywhere in response into g_pipe_last_value
    // Scans forward until a digit or sign-before-digit is found
    {
        const char* p = buf;
        while (*p) {
            if (isdigit((unsigned char)*p) ||
                ((*p == '-' || *p == '+' || *p == '.') && isdigit((unsigned char)p[1]))) {
                char* endptr = NULL;
                float val = strtof(p, &endptr);
                if (endptr != p) {
                    g_pipe_last_value = val;
                    break;
                }
            }
            p++;
        }
    }

    return (int)n;
}

void am_pipe_close(const char* name) {
    int idx = pipe_find(name);
    if (idx < 0) return;

    if (g_pipes[idx].fd >= 0) {
        close(g_pipes[idx].fd);
    }
    printf("[LILITH] pipe '%s' closed\n", g_pipes[idx].name);
    g_pipes[idx].fd = -1;
    g_pipes[idx].active = 0;
    g_pipes[idx].name[0] = 0;
}

void am_pipe_close_all(void) {
    int closed = 0;
    for (int i = 0; i < g_pipe_count; i++) {
        if (g_pipes[i].active) {
            closed++;
            if (g_pipes[i].fd >= 0) close(g_pipes[i].fd);
            g_pipes[i].fd = -1;
            g_pipes[i].active = 0;
        }
    }
    g_pipe_count = 0;
    if (closed) fprintf(stderr, "[LILITH] all pipes closed\n");
}

float am_pipe_last_value(void) { return g_pipe_last_value; }

int am_pipe_count(void) {
    int count = 0;
    for (int i = 0; i < g_pipe_count; i++) {
        if (g_pipes[i].active) count++;
    }
    return count;
}

const AM_Pipe* am_pipe_get(int idx) {
    if (idx < 0 || idx >= g_pipe_count) return NULL;
    if (!g_pipes[idx].active) return NULL;
    return &g_pipes[idx];
}

#endif // AM_IO_DISABLED

// ═══════════════════════════════════════════════════════════════════════════════
// STEP — advance field physics (call each frame)
// applies debt decay, temporal debt accumulation, etc.
// ═══════════════════════════════════════════════════════════════════════════════

void am_step(float dt) {
  if (dt <= 0.0f) return;

  // ─────────────────────────────────────────────────────────────────────────────
  // CALENDAR CONFLICT — Hebrew (354d) vs Gregorian (365d) = 11-day annual drift
  //
  // Real astronomical computation. Uses system clock and epoch (1 Tishrei 5785
  // = Oct 3, 2024). Metonic cycle: 19 years, 7 leap years with Adar II (~30d).
  // February 29 handled correctly — elapsed seconds via time_t, not calendar math.
  //
  // High dissonance = thin barrier between timelines = wormholes open.
  // From pitomadom: TE(Calendar → N) = 0.31 bits — strongest causal effect.
  // ─────────────────────────────────────────────────────────────────────────────

  // MED-1 (Sol fix): sample the civil day ONCE per step, so the world calendar and the MetaJanus
  // self-clock cannot straddle a day boundary within one am_step (each previously read time(NULL) apart).
  int now_days = calendar_days_since_epoch();

  float cal_dissonance;
  if (!g_calendar_manual) {
    // Real date: seconds since epoch → days → drift → dissonance
    int days = now_days;
    float drift = calendar_cumulative_drift(days);
    cal_dissonance = calendar_dissonance(days);
    // Store phase for state access: uncorrected position within cycle
    G.calendar_phase = fabsf(fmodf(drift, AM_MAX_UNCORRECTED));
  } else {
    // Manual override via LAW CALENDAR_PHASE — for testing or AML scripts
    cal_dissonance = (G.calendar_drift > 0.0f)
        ? clamp01(G.calendar_phase / G.calendar_drift)
        : 0.0f;
  }

  // MetaJanus: the self's growing distance from its own origin (self-LOCATION). pd reads the
  // SELF clock — the REAL date (or a test-scrubbed SELF_NOW_DAYS), never the world's manual
  // calendar scale (you may simulate the world, but not your own age). A pure function of the
  // two dates — no prompt moves it. 0 until BIRTH sets the origin.
  {
    int mj_days = g_self_now_manual ? g_self_now_days : now_days;
    float mj_now_drift = calendar_cumulative_drift(mj_days);
    G.personal_dissonance = g_birth_set
        ? clamp01(fabsf(mj_now_drift - G.birth_drift) / AM_MAX_UNCORRECTED)
        : 0.0f;
    // MetaJanus Hebrew face — the yahrzeit (the same origin, seen by the other calendar). Derived
    // from the same mj_days (the same SELF clock), never a second anchor.
    if (g_birth_set) {
      long dy = am_days_to_yahrzeit(mj_days);
      long dg = am_days_to_gregbirthday(mj_days);
      G.janus_gap = clampf((float)(dy - dg) / 30.0f, -1.0f, 1.0f);
      G.yahrzeit  = expf(-(float)dy / 5.0f);
      // MetaJanus HIGH-2 (Sol fix): the Janus temporal signal is a PURE function of the calendar gap,
      // not a per-tick EMA — model-external and deterministic per date, independent of am_step count,
      // traffic and replay. gap<0 (yahrzeit nearer) -> retrodiction (<0.5); gap>0 (Gregorian nearer) ->
      // prophecy (>0.5); gap==0 -> 0.5. D-2 gates on JANUS_KEY at the consumer (am_janus_key_armed);
      // the generic temporal_alpha is left entirely to its own TEMPORAL_* directives.
      G.janus_temporal_alpha = clamp01(0.5f + 0.5f * G.janus_gap);
    } else {
      G.janus_gap = 0.0f;
      G.yahrzeit  = 0.0f;
      G.janus_temporal_alpha = 0.5f;
    }
  }

  // Wormhole activation: dissonance exceeds gate threshold
  if (cal_dissonance > G.wormhole_gate) {
    G.wormhole_active = 1;

    // Boost wormhole base probability proportional to excess dissonance
    // P_tunnel = exp(-1/dissonance) from pitomadom theoretical.md §14.6
    float excess = (cal_dissonance - G.wormhole_gate) / (1.0f - G.wormhole_gate);
    G.wormhole = clamp01(G.wormhole + excess * 0.1f * dt);
  } else {
    G.wormhole_active = 0;
    // Wormhole probability decays when calendar is calm
    G.wormhole *= 0.995f;
    if (G.wormhole < 0.02f) G.wormhole = 0.02f; // floor at 2%
  }

  // Calendar dissonance bleeds into field dissonance
  // The calendars' irreconcilable conflict is a source of suffering
  if (cal_dissonance > 0.3f) {
    float bleed = (cal_dissonance - 0.3f) * 0.05f * dt;
    G.dissonance += bleed;
    if (G.dissonance > 1.0f) G.dissonance = 1.0f;
  }

  // Calendar tension feeds prophecy pressure
  // High dissonance = temporal curvature = debt accumulates
  G.debt += cal_dissonance * 0.005f * dt;

  // ─────────────────────────────────────────────────────────────────────────────
  // DEBT DECAY — prophecy debt decays each step
  // ─────────────────────────────────────────────────────────────────────────────

  G.debt *= G.debt_decay;
  if (G.debt > 100.0f) G.debt = 100.0f;

  // ─────────────────────────────────────────────────────────────────────────────
  // PROPHECY DEBT RECOVERY (D4) — the system keeps prophecy debt minimal while
  // preserving coherence. When debt crosses the threshold the field slows down
  // (velocity DOWN → NOMOVE, cold observer temp 0.5): a quieter, more peaked
  // distribution makes fewer off-peak choices, so debt stops growing and decay
  // (above) brings it back down. velocity_mode feeds update_effective_temp →
  // effective_temp → entropy, so this is a live coupling, not a stored flag.
  // ─────────────────────────────────────────────────────────────────────────────
  if (G.debt > 5.0f && G.velocity_mode != AM_VEL_NOMOVE) {
    G.velocity_mode = AM_VEL_NOMOVE;
    update_effective_temp();
  }

  // ─────────────────────────────────────────────────────────────────────────────
  // TEMPORAL DEBT — backward movement accumulates structural debt
  // ─────────────────────────────────────────────────────────────────────────────

  if (G.velocity_mode == AM_VEL_BACKWARD) {
    G.temporal_debt += 0.01f * dt;
  } else {
    G.temporal_debt *= 0.9995f;
  }
  if (G.temporal_debt > 10.0f) G.temporal_debt = 10.0f;

  // ─────────────────────────────────────────────────────────────────────────────
  // SCHUMANN RESONANCE — Earth coupling heals tension/dissonance
  // Ported from arianna.c/src/schumann.c
  // ─────────────────────────────────────────────────────────────────────────────

  schumann_advance(dt);
  if (G.schumann_coherence > 0.0f && G.schumann_modulation > 0.0f) {
    float coherence_factor = 0.5f + 0.5f * G.schumann_coherence;
    // Harmonic signal modulates healing: aligned harmonics = stronger healing
    float harmonic = schumann_harmonic_signal();
    float harmonic_mod = 1.0f + harmonic * 0.1f;  // range [0.9, 1.1]
    float heal_rate = 0.998f - (0.003f * coherence_factor * G.schumann_modulation * harmonic_mod);
    G.tension *= heal_rate;
    G.dissonance *= heal_rate;
  }

  // ─────────────────────────────────────────────────────────────────────────────
  // DESTINY BIAS — prophecy scales destiny (from arianna_dsl.c)
  // ─────────────────────────────────────────────────────────────────────────────

  {
    float prophecy_scale = 1.0f + ((float)G.prophecy - 7.0f) * 0.02f;
    if (prophecy_scale < 0.5f) prophecy_scale = 0.5f;
    if (prophecy_scale > 2.0f) prophecy_scale = 2.0f;
    G.destiny_bias = G.destiny * prophecy_scale;
  }

  // ─────────────────────────────────────────────────────────────────────────────
  // EXPERT BLENDING — update effective temp with all inputs
  // ─────────────────────────────────────────────────────────────────────────────

  update_effective_temp();

  // ─────────────────────────────────────────────────────────────────────────────
  // LAW ENFORCEMENT — entropy floor, resonance ceiling, presence fade
  // Ported from ariannamethod.lang/src/field.js + arianna_dsl.c
  // ─────────────────────────────────────────────────────────────────────────────

  {
    // Entropy: field disorder metric
    float raw_entropy = (G.effective_temp - 0.5f) * 0.3f
                      + G.dissonance * 0.3f
                      + G.tunnel_chance * 0.2f
                      + (1.0f - G.attend_focus) * 0.2f;
    G.entropy = fmaxf(G.entropy_floor, clamp01(raw_entropy));

    // Resonance: field coherence metric
    float raw_resonance = G.schumann_coherence * 0.3f
                        + (1.0f - G.dissonance) * 0.3f
                        + G.attend_focus * 0.2f
                        + (1.0f - clamp01(G.debt * 0.1f)) * 0.2f;
    if (raw_resonance < G.resonance_set) raw_resonance = G.resonance_set;  // RESONANCE floor (D3)
    G.resonance = fminf(G.resonance_ceiling, clamp01(raw_resonance));

    // Emergence: low entropy + high resonance = the field "knows" something
    G.emergence = clamp01((1.0f - G.entropy) * G.resonance);
  }

  // Presence fade per step
  G.presence_decay *= G.presence_fade;
  if (G.presence_decay < 0.001f) G.presence_decay = 0.001f;

  // ─────────────────────────────────────────────────────────────────────────────
  // 4.C — ASYNC FIELD FOREVER — seasonal meta-operators
  // Seasons modulate all field parameters. MLP controller prevents extremes.
  // ─────────────────────────────────────────────────────────────────────────────

  {
    // Advance season phase
    float season_rate = 0.001f;  // ~1000 steps per season
    G.season_phase += season_rate * dt;

    if (G.season_phase >= 1.0f) {
      G.season_phase = 0.0f;
      G.season = (G.season + 1) % 4;
    }

    // Current season gains energy, others decay
    float gain = 0.02f * dt * G.season_intensity;
    float fade = 0.995f;
    G.spring_energy *= fade;
    G.summer_energy *= fade;
    G.autumn_energy *= fade;
    G.winter_energy *= fade;

    switch (G.season) {
      case AM_SEASON_SPRING: G.spring_energy = clamp01(G.spring_energy + gain); break;
      case AM_SEASON_SUMMER: G.summer_energy = clamp01(G.summer_energy + gain); break;
      case AM_SEASON_AUTUMN: G.autumn_energy = clamp01(G.autumn_energy + gain); break;
      case AM_SEASON_WINTER: G.winter_energy = clamp01(G.winter_energy + gain); break;
    }

    // ── 4.C MLP CONTROLLER ──
    // Real neural network: 6 inputs → 8 hidden (tanh) → 4 outputs (tanh)
    // Replaces hardcoded rules. Trained by Hebbian plasticity (NOTORCH).
    float mlp_inputs[AM_4C_INPUTS] = {
      G.entropy, G.resonance, G.pain, G.tension, G.emergence, G.effective_temp
    };
    float mlp_outputs[AM_4C_OUTPUTS];
    am_4c_forward(mlp_inputs, mlp_outputs);

    // Apply MLP output as energy deltas (scaled by season_intensity)
    float scale = 0.02f * dt * G.season_intensity;
    G.spring_energy = clamp01(G.spring_energy + mlp_outputs[0] * scale);
    G.summer_energy = clamp01(G.summer_energy + mlp_outputs[1] * scale);
    G.autumn_energy = clamp01(G.autumn_energy + mlp_outputs[2] * scale);
    G.winter_energy = clamp01(G.winter_energy + mlp_outputs[3] * scale);

    // Hebbian update: did the MLP improve field health?
    float health = clamp01((1.0f - fabsf(G.entropy - 0.5f)) *
                           G.resonance * (1.0f - G.pain));
    float signal = health - G.field_health;
    G.field_health = health;
    if (fabsf(signal) > 0.001f) {
      am_4c_hebbian_update(mlp_inputs, mlp_outputs, signal);
    }

    // Season modulation on field parameters
    // Spring: exploration boost
    G.tunnel_chance = clamp01(G.tunnel_chance + G.spring_energy * 0.005f * dt);
    // Autumn: consolidation — strengthen dark gravity
    G.dark_gravity = clamp01(G.dark_gravity + G.autumn_energy * 0.002f * dt);

    // ── GAMMA / JANUS MODULATION ──
    // 4.C controls personality switching in CYCLE mode
    if (G.janus_mode == AM_JANUS_CYCLE && G.n_gamma >= 2) {
      // Janus blend drifts based on seasonal energy
      // Summer favors face_a (peak expression of primary)
      // Winter favors face_b (reflection through other)
      // Spring/Autumn: blend oscillates with gamma_drift
      float drift = G.gamma_drift * dt;
      if (G.season == AM_SEASON_SUMMER)
        G.janus_blend = clamp01(G.janus_blend - drift * 2.0f);
      else if (G.season == AM_SEASON_WINTER)
        G.janus_blend = clamp01(G.janus_blend + drift * 2.0f);
      else {
        // Spring/Autumn: sinusoidal oscillation
        G.janus_blend = clamp01(G.janus_blend +
            drift * sinf(G.season_phase * 6.283185f));
      }

      // Update essence_alpha from active gamma
      int active = am_gamma_active();
      if (active >= 0 && active < G.n_gamma) {
        G.essence_alpha = G.gamma[active].alpha;
      }
    }

    // Summer boosts gamma (personality at peak)
    if (G.n_gamma > 0) {
      G.essence_alpha = clamp01(G.essence_alpha +
          G.summer_energy * 0.003f * dt);
      // Winter dampens gamma (substrate dominates)
      G.essence_alpha = clamp01(G.essence_alpha -
          G.winter_energy * 0.005f * dt);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// HARMONIC NET — weightless neural network in C
//
// Layer 1: Fourier decomposition of entropy history
// Layer 2: Correlation matrix (pairwise gamma cosines = the "weights")
// Layer 3: Phase aggregation (resonance + harmonics → steering refinement)
//
// No trainable weights. No backprop. Just harmonic resonance.
// Evolved in molequla, ported to core.
// ═══════════════════════════════════════════════════════════════════════════════

static struct {
    /* Entropy history (circular buffer) */
    float entropy_history[AM_HARMONIC_MAX_HISTORY];
    int   history_len;
    int   history_pos;

    /* Organism gammas for this step */
    float gammas[AM_HARMONIC_MAX_ORGANISMS][AM_HARMONIC_GAMMA_DIM];
    float org_entropy[AM_HARMONIC_MAX_ORGANISMS];
    int   n_organisms;
} HN;

void am_harmonic_init(void) {
    memset(&HN, 0, sizeof(HN));
}

void am_harmonic_clear(void) {
    HN.n_organisms = 0;
}

void am_harmonic_push_entropy(float entropy) {
    HN.entropy_history[HN.history_pos] = entropy;
    HN.history_pos = (HN.history_pos + 1) % AM_HARMONIC_MAX_HISTORY;
    if (HN.history_len < AM_HARMONIC_MAX_HISTORY)
        HN.history_len++;
}

void am_harmonic_push_gamma(int id, const float *gamma, int dim, float entropy) {
    (void)id;
    if (HN.n_organisms >= AM_HARMONIC_MAX_ORGANISMS) return;
    int idx = HN.n_organisms++;
    int copy_dim = dim < AM_HARMONIC_GAMMA_DIM ? dim : AM_HARMONIC_GAMMA_DIM;
    memcpy(HN.gammas[idx], gamma, copy_dim * sizeof(float));
    /* Zero-pad if needed */
    for (int i = copy_dim; i < AM_HARMONIC_GAMMA_DIM; i++)
        HN.gammas[idx][i] = 0.0f;
    HN.org_entropy[idx] = entropy;
}

AM_HarmonicResult am_harmonic_forward(int step) {
    (void)step;
    AM_HarmonicResult r;
    memset(&r, 0, sizeof(r));
    r.n_organisms = HN.n_organisms;
    r.strength_mod = 0.3f;

    if (HN.n_organisms == 0) return r;

    int T = HN.history_len;

    /* ── Layer 1: Fourier decomposition of entropy history ── */
    if (T >= 4) {
        for (int k = 0; k < AM_HARMONIC_N_FREQ; k++) {
            float sum = 0.0f;
            for (int t = 0; t < T; t++) {
                int idx = (HN.history_pos - T + t + AM_HARMONIC_MAX_HISTORY) % AM_HARMONIC_MAX_HISTORY;
                float phase = 2.0f * 3.14159265f * (float)(k + 1) * (float)t / (float)T;
                sum += HN.entropy_history[idx] * sinf(phase);
            }
            r.harmonics[k] = sum / (float)T;
        }
    }

    /* ── Layer 2: Correlation matrix (pairwise gamma cosines) ── */
    int n = HN.n_organisms;

    /* Compute norms */
    float norms[AM_HARMONIC_MAX_ORGANISMS];
    for (int i = 0; i < n; i++) {
        float s = 0.0f;
        for (int d = 0; d < AM_HARMONIC_GAMMA_DIM; d++)
            s += HN.gammas[i][d] * HN.gammas[i][d];
        norms[i] = sqrtf(s);
        if (norms[i] < 1e-8f) norms[i] = 1e-8f;
    }

    /* Pairwise cosines + phase resonance */
    float mean_ent = 0.0f;
    for (int i = 0; i < n; i++) mean_ent += HN.org_entropy[i];
    mean_ent /= (float)n;

    for (int i = 0; i < n; i++) {
        float res = 0.0f;
        float phase_i = HN.org_entropy[i] - mean_ent;
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            /* Cosine similarity */
            float dot = 0.0f;
            for (int d = 0; d < AM_HARMONIC_GAMMA_DIM; d++)
                dot += HN.gammas[i][d] * HN.gammas[j][d];
            float cos_ij = dot / (norms[i] * norms[j]);

            /* Phase similarity */
            float phase_j = HN.org_entropy[j] - mean_ent;
            float phase_sim = expf(-fabsf(phase_i - phase_j));

            res += cos_ij * phase_sim;
        }
        if (n > 1) res /= (float)(n - 1);
        r.resonance[i] = res;
    }

    /* ── Layer 3: Output ── */
    /* Find dominant harmonic */
    float max_amp = 0.0f;
    r.dominant_freq = 0;
    if (T >= 4) {
        for (int k = 0; k < AM_HARMONIC_N_FREQ; k++) {
            float a = fabsf(r.harmonics[k]);
            if (a > max_amp) { max_amp = a; r.dominant_freq = k; }
        }
    }

    /* Confidence: more data = more confident */
    float conf_t = T < 16 ? (float)T / 16.0f : 1.0f;
    float conf_n = n < 4 ? (float)n / 4.0f : 1.0f;
    r.strength_mod = 0.3f + 0.7f * conf_t * conf_n;

    return r;
}

// ═══════════════════════════════════════════════════════════════════════════════
// METHOD — distributed cognition operator (C implementation)
//
// The field operator. Works on collective organism data, not individuals.
// Host pushes organism snapshots, METHOD computes awareness and steering.
// Evolved in molequla, ported to core.
// ═══════════════════════════════════════════════════════════════════════════════

static AM_MethodState M;

void am_method_init(void) {
    memset(&M, 0, sizeof(AM_MethodState));
}

void am_method_clear(void) {
    M.n_organisms = 0;
}

void am_method_push_organism(int id, float entropy, float syntropy,
                             float gamma_mag, float gamma_cos) {
    if (M.n_organisms >= AM_METHOD_MAX_ORGANISMS) return;
    AM_MethodOrganism* o = &M.organisms[M.n_organisms++];
    o->id = id;
    o->entropy = entropy;
    o->syntropy = syntropy;
    o->gamma_mag = gamma_mag;
    o->gamma_cos = gamma_cos;
}

float am_method_field_entropy(void) {
    if (M.n_organisms == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < M.n_organisms; i++)
        sum += M.organisms[i].entropy;
    return sum / (float)M.n_organisms;
}

float am_method_field_syntropy(void) {
    if (M.n_organisms == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < M.n_organisms; i++)
        sum += M.organisms[i].syntropy;
    return sum / (float)M.n_organisms;
}

float am_method_field_coherence(void) {
    if (M.n_organisms == 0) return 1.0f;
    if (M.n_organisms == 1) return 1.0f;

    // Mean gamma_cos across organisms (host-computed pairwise)
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < M.n_organisms; i++) {
        if (M.organisms[i].gamma_mag > 1e-6f) {
            sum += M.organisms[i].gamma_cos;
            count++;
        }
    }
    return count > 0 ? sum / (float)count : 1.0f;
}

AM_MethodSteering am_method_step(float dt) {
    AM_MethodSteering s;
    memset(&s, 0, sizeof(s));

    s.n_organisms = M.n_organisms;
    M.step_count++;
    s.step = M.step_count;

    if (M.n_organisms == 0) {
        s.action = AM_METHOD_WAIT;
        return s;
    }

    float entropy = am_method_field_entropy();
    float syntropy = am_method_field_syntropy();
    float coherence = am_method_field_coherence();

    s.entropy = entropy;
    s.syntropy = syntropy;
    s.coherence = coherence;

    // Push to circular history buffer
    int pos = M.history_pos % AM_METHOD_HISTORY_LEN;
    M.entropy_history[pos] = entropy;
    M.coherence_history[pos] = coherence;
    M.history_pos++;
    if (M.history_len < AM_METHOD_HISTORY_LEN)
        M.history_len++;

    // Compute entropy trend (positive = organizing, negative = dissolving)
    float trend = 0.0f;
    if (M.history_len >= 4) {
        float recent = 0.0f, earlier = 0.0f;
        int rc = 0, ec = 0;
        for (int i = 0; i < M.history_len && i < 8; i++) {
            int idx = ((M.history_pos - 1 - i) % AM_METHOD_HISTORY_LEN + AM_METHOD_HISTORY_LEN) % AM_METHOD_HISTORY_LEN;
            if (i < 4) { recent += M.entropy_history[idx]; rc++; }
            else        { earlier += M.entropy_history[idx]; ec++; }
        }
        if (rc > 0 && ec > 0)
            trend = (earlier / (float)ec) - (recent / (float)rc);
    }
    s.trend = trend;

    // Find best organism (lowest entropy)
    int best_id = M.organisms[0].id;
    float best_entropy = M.organisms[0].entropy;
    for (int i = 1; i < M.n_organisms; i++) {
        if (M.organisms[i].entropy < best_entropy) {
            best_entropy = M.organisms[i].entropy;
            best_id = M.organisms[i].id;
        }
    }
    s.target_id = best_id;

    // Decide action
    if (coherence < 0.3f) {
        s.action = AM_METHOD_REALIGN;
        s.strength = 1.0f - coherence;
    } else if (trend > 0.05f) {
        s.action = AM_METHOD_AMPLIFY;
        s.strength = fminf(1.0f, trend * 5.0f);
    } else if (trend < -0.05f) {
        s.action = AM_METHOD_DAMPEN;
        s.strength = fminf(1.0f, fabsf(trend) * 5.0f);
    } else if (entropy > 2.0f) {
        s.action = AM_METHOD_GROUND;
        s.strength = fminf(1.0f, (entropy - 1.5f) * 0.5f);
    } else if (entropy < 0.5f) {
        s.action = AM_METHOD_EXPLORE;
        s.strength = fminf(1.0f, (1.0f - entropy) * 0.5f);
    } else {
        s.action = AM_METHOD_SUSTAIN;
        s.strength = 0.1f;
    }

    // Advance AML field physics
    am_step(dt);

    // Translate steering to AML state
    switch (s.action) {
        case AM_METHOD_DAMPEN:
            am_exec("PAIN 0.3");
            am_exec("VELOCITY WALK");
            break;
        case AM_METHOD_AMPLIFY:
            am_exec("VELOCITY RUN");
            am_exec("DESTINY 0.6");
            break;
        case AM_METHOD_GROUND:
            am_exec("ATTEND_FOCUS 0.9");
            am_exec("VELOCITY NOMOVE");
            break;
        case AM_METHOD_EXPLORE:
            am_exec("TUNNEL_CHANCE 0.3");
            am_exec("VELOCITY RUN");
            break;
        case AM_METHOD_REALIGN:
            am_exec("PAIN 0.5");
            am_exec("ATTEND_FOCUS 0.8");
            break;
        default:
            break;
    }

    return s;
}

AM_MethodState* am_method_get_state(void) {
    return &M;
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMMUTABLE UTF-8 TEXT — codepoint indexing, independent of the current locale
// ═══════════════════════════════════════════════════════════════════════════════

// Decode one Unicode scalar value. The terminating C NUL is outside the text
// domain; remaining is the number of bytes before that terminator.
static int am_string_decode_utf8(const unsigned char* src, int remaining, int* cp) {
    if (remaining <= 0) return -1;
    unsigned int lead = src[0], value, minimum;
    int width;
    if (lead > 0 && lead < 0x80) {
        *cp = (int)lead;
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        width = 2; value = lead & 0x1F; minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        width = 3; value = lead & 0x0F; minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        width = 4; value = lead & 0x07; minimum = 0x10000;
    } else {
        return -1;
    }
    if (remaining < width) return -1;
    for (int i = 1; i < width; i++) {
        if ((src[i] & 0xC0) != 0x80) return -1;
        value = (value << 6) | (src[i] & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) return -1;
    *cp = (int)value;
    return width;
}

// Allocate a fresh owner, including for empty strings and full-range slices.
static AM_String* am_string_alloc(int byte_len, int len) {
    if (byte_len < 0 || byte_len > AM_MAX_STRING_BYTES) return NULL;
    AM_String* text = (AM_String*)malloc(sizeof(*text));
    if (!text) return NULL;
    text->data = (char*)malloc((size_t)byte_len + 1);
    if (!text->data) { free(text); return NULL; }
    text->byte_len = byte_len;
    text->len = len;
    text->refcount = 1;
    text->data[byte_len] = 0;
    return text;
}

static int am_string_validate_bytes(const char* utf8, size_t bytes) {
    if (!utf8 || bytes > AM_MAX_STRING_BYTES) return -1;
    int byte_len = (int)bytes;
    int len = 0, pos = 0;
    while (pos < byte_len) {
        int cp;
        int width = am_string_decode_utf8((const unsigned char*)utf8 + pos,
                                          byte_len - pos, &cp);
        if (width < 0) return -1;
        pos += width;
        len++;
    }
    return len;
}

static AM_String* am_string_new_bytes(const char* utf8, size_t bytes) {
    int len = am_string_validate_bytes(utf8, bytes);
    if (len < 0) return NULL;
    AM_String* text = am_string_alloc((int)bytes, len);
    if (text && bytes) memcpy(text->data, utf8, bytes);
    return text;
}

AM_String* am_string_new(const char* utf8) {
    if (!utf8) return NULL;
    size_t bytes = 0;
    while (bytes <= AM_MAX_STRING_BYTES && utf8[bytes]) bytes++;
    return am_string_new_bytes(utf8, bytes);
}

static void am_text_error(char* error, size_t cap, const char* message) {
    if (error && cap) snprintf(error, cap, "%s", message);
}

AM_List* am_read_line(FILE* input, char* error, size_t error_cap) {
    am_text_error(error, error_cap, "");
    if (!input) { am_text_error(error, error_cap, "line input is unavailable"); return NULL; }
    AM_List* result = am_list_new();
    if (!result) { am_text_error(error, error_cap, "line allocation failed"); return NULL; }
    size_t capacity = 256, size = 0;
    char* bytes = malloc(capacity);
    if (!bytes) {
        am_list_free(result);
        am_text_error(error, error_cap, "line allocation failed"); return NULL;
    }
    int ch = EOF;
    const char* failure = NULL;
    // POSIX stdio locks the complete line, including buffer growth, so workers
    // cannot consume alternating bytes. Single-threaded WASM needs no lock.
#ifndef __EMSCRIPTEN__
    flockfile(input);
#endif
    while ((ch = fgetc(input)) != EOF && ch != '\n') {
        if (ch == 0) { failure = "line input contains NUL"; break; }
        if (size == AM_MAX_STRING_BYTES) { failure = "line input exceeds 1 MiB"; break; }
        if (size == capacity) {
            size_t next_capacity = capacity * 2;
            if (next_capacity > AM_MAX_STRING_BYTES) next_capacity = AM_MAX_STRING_BYTES;
            char* next = realloc(bytes, next_capacity);
            if (!next) { failure = "line allocation failed"; break; }
            bytes = next;
            capacity = next_capacity;
        }
        bytes[size++] = (char)ch;
    }
    if (!failure && ferror(input)) failure = "line input read failed";
#ifndef __EMSCRIPTEN__
    funlockfile(input);
#endif
    if (!failure && !(ch == EOF && size == 0)) {
        if (am_string_validate_bytes(bytes, size) < 0) failure = "line input is not valid UTF-8";
        else {
            AM_String* line = am_string_new_bytes(bytes, size);
            if (!line) failure = "line allocation failed";
            else {
                if (am_list_push(result, line) < 0) failure = "line allocation failed";
                am_string_free(line);
            }
        }
    }
    free(bytes);
    if (failure) {
        am_list_free(result);
        am_text_error(error, error_cap, failure);
        return NULL;
    }
    return result;
}

AM_Tokenizer* am_tokenizer_load(const char* path, char* error, size_t error_cap) {
    am_text_error(error, error_cap, "");
    if (!path || !*path || strnlen(path, AM_MAX_STRING_BYTES + 1) > AM_MAX_STRING_BYTES) {
        am_text_error(error, error_cap, "invalid tokenizer path"); return NULL;
    }
    AM_TokenizerBackend backend = g_tokenizer_backend;
    if (!backend.load || !backend.destroy || !backend.pieces) {
        am_text_error(error, error_cap, "tokenizer backend unavailable; use NoTorch-enabled AML");
        return NULL;
    }
    AM_Tokenizer* result = malloc(sizeof(*result));
    if (!result) { am_text_error(error, error_cap, "tokenizer allocation failed"); return NULL; }
    result->backend = backend;
    result->refcount = 1;
    char detail[256] = {0};
    result->model = backend.load(path, detail, sizeof(detail));
    detail[sizeof(detail) - 1] = 0;
    if (!result->model) {
        free(result);
        am_text_error(error, error_cap, detail[0] ? detail : "tokenizer backend load failed");
        return NULL;
    }
    return result;
}

void am_tokenizer_ref(AM_Tokenizer* model) {
    if (model) __atomic_add_fetch(&model->refcount, 1, __ATOMIC_RELAXED);
}

void am_tokenizer_free(AM_Tokenizer* model) {
    if (!model || __atomic_sub_fetch(&model->refcount, 1, __ATOMIC_ACQ_REL) > 0) return;
    model->backend.destroy(model->model);
    free(model);
}

typedef struct {
    AM_List* list;
    size_t bytes;
    const char* failure;
} AM_TokenizerSink;

static int am_tokenizer_emit(void* context, const char* utf8, size_t bytes) {
    AM_TokenizerSink* sink = context;
    if (sink->failure) return -1;
    if (bytes > AM_MAX_STRING_BYTES - sink->bytes || sink->list->len == AM_MAX_LIST_ITEMS) {
        sink->failure = "tokenizer pieces exceed AML output limits"; return -1;
    }
    if (am_string_validate_bytes(utf8, bytes) < 0) {
        sink->failure = "tokenizer backend emitted invalid UTF-8 or NUL"; return -1;
    }
    AM_String* piece = am_string_new_bytes(utf8, bytes);
    if (!piece) { sink->failure = "tokenizer piece allocation failed"; return -1; }
    int rc = am_list_push(sink->list, piece);
    am_string_free(piece);
    if (rc < 0) { sink->failure = "tokenizer piece allocation failed"; return -1; }
    sink->bytes += bytes;
    return 0;
}

AM_List* am_tokenizer_pieces(const AM_Tokenizer* model, const AM_String* text,
                           char* error, size_t error_cap) {
    am_text_error(error, error_cap, "");
    if (!model || !model->model || !text || text->byte_len < 0 ||
        am_string_validate_bytes(text->data, (size_t)text->byte_len) < 0) {
        am_text_error(error, error_cap, "tokenizer requires a model and valid UTF-8 text");
        return NULL;
    }
    AM_TokenizerSink sink = {am_list_new(), 0, NULL};
    if (!sink.list) { am_text_error(error, error_cap, "tokenizer list allocation failed"); return NULL; }
    char detail[256] = {0};
    int rc = model->backend.pieces(model->model, text->data, (size_t)text->byte_len,
                                   am_tokenizer_emit, &sink, detail, sizeof(detail));
    detail[sizeof(detail) - 1] = 0;
    if (sink.failure || rc != 0) {
        am_list_free(sink.list);
        am_text_error(error, error_cap, sink.failure ? sink.failure :
                       detail[0] ? detail : "tokenizer backend encoding failed");
        return NULL;
    }
    return sink.list;
}

void am_string_ref(AM_String* text) {
    if (text) __atomic_add_fetch(&text->refcount, 1, __ATOMIC_RELAXED);
}

void am_string_free(AM_String* text) {
    if (!text || __atomic_sub_fetch(&text->refcount, 1, __ATOMIC_ACQ_REL) > 0) return;
    free(text->data);
    free(text);
}

AM_String* am_string_concat(const AM_String* a, const AM_String* b) {
    if (!a || !b || a->byte_len > AM_MAX_STRING_BYTES - b->byte_len) return NULL;
    AM_String* text = am_string_alloc(a->byte_len + b->byte_len, a->len + b->len);
    if (text) {
        memcpy(text->data, a->data, (size_t)a->byte_len);
        memcpy(text->data + a->byte_len, b->data, (size_t)b->byte_len);
    }
    return text;
}

// Strings created by this API retain valid UTF-8 and codepoint counts.
static int am_string_byte_offset(const AM_String* text, int index) {
    int pos = 0;
    for (int i = 0; i < index; i++) {
        int cp;
        int width = am_string_decode_utf8((const unsigned char*)text->data + pos,
                                          text->byte_len - pos, &cp);
        if (width < 0) return -1;
        pos += width;
    }
    return pos;
}

static int am_string_clamp_index(int index, int len) {
    if (index < 0) index += len;
    if (index < 0) return 0;
    return index > len ? len : index;
}

AM_String* am_string_slice(const AM_String* text, int start, int end) {
    if (!text) return NULL;
    start = am_string_clamp_index(start, text->len);
    end = am_string_clamp_index(end, text->len);
    if (end <= start) return am_string_new("");
    int first = am_string_byte_offset(text, start);
    int last = am_string_byte_offset(text, end);
    if (first < 0 || last < first) return NULL;
    AM_String* out = am_string_alloc(last - first, end - start);
    if (out) memcpy(out->data, text->data + first, (size_t)(last - first));
    return out;
}

int am_string_find(const AM_String* text, const AM_String* needle) {
    if (!text || !needle) return -1;
    // A valid needle begins with ASCII or a UTF-8 leading byte, so a byte
    // match cannot begin inside a multibyte codepoint's continuation bytes.
    const char* match = strstr(text->data, needle->data);
    if (!match) return -1;
    int index = 0;
    for (const char* p = text->data; p < match; p++)
        if (((unsigned char)*p & 0xC0) != 0x80) index++;
    return index;
}

int am_string_codepoint(const AM_String* text, int index) {
    if (!text) return -1;
    if (index < 0) index += text->len;
    if (index < 0 || index >= text->len) return -1;
    int pos = am_string_byte_offset(text, index), cp;
    if (pos < 0 || am_string_decode_utf8((const unsigned char*)text->data + pos,
                                        text->byte_len - pos, &cp) < 0) return -1;
    return cp;
}

AM_String* am_string_from_codepoint(int cp) {
    if (cp <= 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return NULL;
    char bytes[4];
    int width;
    if (cp < 0x80) {
        bytes[0] = (char)cp;
        width = 1;
    } else if (cp < 0x800) {
        bytes[0] = (char)(0xC0 | (cp >> 6));
        bytes[1] = (char)(0x80 | (cp & 0x3F));
        width = 2;
    } else if (cp < 0x10000) {
        bytes[0] = (char)(0xE0 | (cp >> 12));
        bytes[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (cp & 0x3F));
        width = 3;
    } else {
        bytes[0] = (char)(0xF0 | (cp >> 18));
        bytes[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (cp & 0x3F));
        width = 4;
    }
    AM_String* text = am_string_alloc(width, 1);
    if (text) memcpy(text->data, bytes, (size_t)width);
    return text;
}

// Unicode 15.0.0 data, generated from https://www.unicode.org/Public/15.0.0/ucd/.
// Unicode data license follows; AML implementation retains the repository license.
/*
UNICODE LICENSE V3

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2026 Unicode, Inc.

NOTICE TO USER: Carefully read the following legal agreement. BY
DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.

Permission is hereby granted, free of charge, to any person obtaining a
copy of data files and any associated documentation (the "Data Files") or
software and any associated documentation (the "Software") to deal in the
Data Files or Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, and/or sell
copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that either (a)
this copyright and permission notice appear with all copies of the Data
Files or Software, or (b) this copyright and permission notice appear in
associated Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
THIRD PARTY RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or other
dealings in these Data Files or Software without prior written
authorization of the copyright holder.

*/

// Generated from Unicode 15.0.0 UnicodeData, SpecialCasing, and
// DerivedCoreProperties. Regenerate with generate_text_lower_tables.py.
// Simple mappings use stride 1 or 2; unlisted scalars map to themselves.
typedef struct { uint32_t first, last; int32_t delta; uint32_t stride; } AM_LowerRange;
typedef struct { uint32_t first, last; } AM_UnicodeRange;
static const AM_LowerRange am_lower_ranges[] = {
    {0x41u, 0x5Au, 32, 1u}, {0xC0u, 0xD6u, 32, 1u}, {0xD8u, 0xDEu, 32, 1u},
    {0x100u, 0x12Eu, 1, 2u}, {0x132u, 0x136u, 1, 2u}, {0x139u, 0x147u, 1, 2u},
    {0x14Au, 0x176u, 1, 2u}, {0x178u, 0x178u, -121, 1u}, {0x179u, 0x17Du, 1, 2u},
    {0x181u, 0x181u, 210, 1u}, {0x182u, 0x184u, 1, 2u}, {0x186u, 0x186u, 206, 1u},
    {0x187u, 0x187u, 1, 1u}, {0x189u, 0x18Au, 205, 1u}, {0x18Bu, 0x18Bu, 1, 1u},
    {0x18Eu, 0x18Eu, 79, 1u}, {0x18Fu, 0x18Fu, 202, 1u}, {0x190u, 0x190u, 203, 1u},
    {0x191u, 0x191u, 1, 1u}, {0x193u, 0x193u, 205, 1u}, {0x194u, 0x194u, 207, 1u},
    {0x196u, 0x196u, 211, 1u}, {0x197u, 0x197u, 209, 1u}, {0x198u, 0x198u, 1, 1u},
    {0x19Cu, 0x19Cu, 211, 1u}, {0x19Du, 0x19Du, 213, 1u}, {0x19Fu, 0x19Fu, 214, 1u},
    {0x1A0u, 0x1A4u, 1, 2u}, {0x1A6u, 0x1A6u, 218, 1u}, {0x1A7u, 0x1A7u, 1, 1u},
    {0x1A9u, 0x1A9u, 218, 1u}, {0x1ACu, 0x1ACu, 1, 1u}, {0x1AEu, 0x1AEu, 218, 1u},
    {0x1AFu, 0x1AFu, 1, 1u}, {0x1B1u, 0x1B2u, 217, 1u}, {0x1B3u, 0x1B5u, 1, 2u},
    {0x1B7u, 0x1B7u, 219, 1u}, {0x1B8u, 0x1B8u, 1, 1u}, {0x1BCu, 0x1BCu, 1, 1u},
    {0x1C4u, 0x1C4u, 2, 1u}, {0x1C5u, 0x1C5u, 1, 1u}, {0x1C7u, 0x1C7u, 2, 1u},
    {0x1C8u, 0x1C8u, 1, 1u}, {0x1CAu, 0x1CAu, 2, 1u}, {0x1CBu, 0x1DBu, 1, 2u},
    {0x1DEu, 0x1EEu, 1, 2u}, {0x1F1u, 0x1F1u, 2, 1u}, {0x1F2u, 0x1F4u, 1, 2u},
    {0x1F6u, 0x1F6u, -97, 1u}, {0x1F7u, 0x1F7u, -56, 1u}, {0x1F8u, 0x21Eu, 1, 2u},
    {0x220u, 0x220u, -130, 1u}, {0x222u, 0x232u, 1, 2u}, {0x23Au, 0x23Au, 10795, 1u},
    {0x23Bu, 0x23Bu, 1, 1u}, {0x23Du, 0x23Du, -163, 1u}, {0x23Eu, 0x23Eu, 10792, 1u},
    {0x241u, 0x241u, 1, 1u}, {0x243u, 0x243u, -195, 1u}, {0x244u, 0x244u, 69, 1u},
    {0x245u, 0x245u, 71, 1u}, {0x246u, 0x24Eu, 1, 2u}, {0x370u, 0x372u, 1, 2u},
    {0x376u, 0x376u, 1, 1u}, {0x37Fu, 0x37Fu, 116, 1u}, {0x386u, 0x386u, 38, 1u},
    {0x388u, 0x38Au, 37, 1u}, {0x38Cu, 0x38Cu, 64, 1u}, {0x38Eu, 0x38Fu, 63, 1u},
    {0x391u, 0x3A1u, 32, 1u}, {0x3A3u, 0x3ABu, 32, 1u}, {0x3CFu, 0x3CFu, 8, 1u},
    {0x3D8u, 0x3EEu, 1, 2u}, {0x3F4u, 0x3F4u, -60, 1u}, {0x3F7u, 0x3F7u, 1, 1u},
    {0x3F9u, 0x3F9u, -7, 1u}, {0x3FAu, 0x3FAu, 1, 1u}, {0x3FDu, 0x3FFu, -130, 1u},
    {0x400u, 0x40Fu, 80, 1u}, {0x410u, 0x42Fu, 32, 1u}, {0x460u, 0x480u, 1, 2u},
    {0x48Au, 0x4BEu, 1, 2u}, {0x4C0u, 0x4C0u, 15, 1u}, {0x4C1u, 0x4CDu, 1, 2u},
    {0x4D0u, 0x52Eu, 1, 2u}, {0x531u, 0x556u, 48, 1u}, {0x10A0u, 0x10C5u, 7264, 1u},
    {0x10C7u, 0x10C7u, 7264, 1u}, {0x10CDu, 0x10CDu, 7264, 1u}, {0x13A0u, 0x13EFu, 38864, 1u},
    {0x13F0u, 0x13F5u, 8, 1u}, {0x1C90u, 0x1CBAu, -3008, 1u}, {0x1CBDu, 0x1CBFu, -3008, 1u},
    {0x1E00u, 0x1E94u, 1, 2u}, {0x1E9Eu, 0x1E9Eu, -7615, 1u}, {0x1EA0u, 0x1EFEu, 1, 2u},
    {0x1F08u, 0x1F0Fu, -8, 1u}, {0x1F18u, 0x1F1Du, -8, 1u}, {0x1F28u, 0x1F2Fu, -8, 1u},
    {0x1F38u, 0x1F3Fu, -8, 1u}, {0x1F48u, 0x1F4Du, -8, 1u}, {0x1F59u, 0x1F5Fu, -8, 2u},
    {0x1F68u, 0x1F6Fu, -8, 1u}, {0x1F88u, 0x1F8Fu, -8, 1u}, {0x1F98u, 0x1F9Fu, -8, 1u},
    {0x1FA8u, 0x1FAFu, -8, 1u}, {0x1FB8u, 0x1FB9u, -8, 1u}, {0x1FBAu, 0x1FBBu, -74, 1u},
    {0x1FBCu, 0x1FBCu, -9, 1u}, {0x1FC8u, 0x1FCBu, -86, 1u}, {0x1FCCu, 0x1FCCu, -9, 1u},
    {0x1FD8u, 0x1FD9u, -8, 1u}, {0x1FDAu, 0x1FDBu, -100, 1u}, {0x1FE8u, 0x1FE9u, -8, 1u},
    {0x1FEAu, 0x1FEBu, -112, 1u}, {0x1FECu, 0x1FECu, -7, 1u}, {0x1FF8u, 0x1FF9u, -128, 1u},
    {0x1FFAu, 0x1FFBu, -126, 1u}, {0x1FFCu, 0x1FFCu, -9, 1u}, {0x2126u, 0x2126u, -7517, 1u},
    {0x212Au, 0x212Au, -8383, 1u}, {0x212Bu, 0x212Bu, -8262, 1u}, {0x2132u, 0x2132u, 28, 1u},
    {0x2160u, 0x216Fu, 16, 1u}, {0x2183u, 0x2183u, 1, 1u}, {0x24B6u, 0x24CFu, 26, 1u},
    {0x2C00u, 0x2C2Fu, 48, 1u}, {0x2C60u, 0x2C60u, 1, 1u}, {0x2C62u, 0x2C62u, -10743, 1u},
    {0x2C63u, 0x2C63u, -3814, 1u}, {0x2C64u, 0x2C64u, -10727, 1u}, {0x2C67u, 0x2C6Bu, 1, 2u},
    {0x2C6Du, 0x2C6Du, -10780, 1u}, {0x2C6Eu, 0x2C6Eu, -10749, 1u}, {0x2C6Fu, 0x2C6Fu, -10783, 1u},
    {0x2C70u, 0x2C70u, -10782, 1u}, {0x2C72u, 0x2C72u, 1, 1u}, {0x2C75u, 0x2C75u, 1, 1u},
    {0x2C7Eu, 0x2C7Fu, -10815, 1u}, {0x2C80u, 0x2CE2u, 1, 2u}, {0x2CEBu, 0x2CEDu, 1, 2u},
    {0x2CF2u, 0x2CF2u, 1, 1u}, {0xA640u, 0xA66Cu, 1, 2u}, {0xA680u, 0xA69Au, 1, 2u},
    {0xA722u, 0xA72Eu, 1, 2u}, {0xA732u, 0xA76Eu, 1, 2u}, {0xA779u, 0xA77Bu, 1, 2u},
    {0xA77Du, 0xA77Du, -35332, 1u}, {0xA77Eu, 0xA786u, 1, 2u}, {0xA78Bu, 0xA78Bu, 1, 1u},
    {0xA78Du, 0xA78Du, -42280, 1u}, {0xA790u, 0xA792u, 1, 2u}, {0xA796u, 0xA7A8u, 1, 2u},
    {0xA7AAu, 0xA7AAu, -42308, 1u}, {0xA7ABu, 0xA7ABu, -42319, 1u}, {0xA7ACu, 0xA7ACu, -42315, 1u},
    {0xA7ADu, 0xA7ADu, -42305, 1u}, {0xA7AEu, 0xA7AEu, -42308, 1u}, {0xA7B0u, 0xA7B0u, -42258, 1u},
    {0xA7B1u, 0xA7B1u, -42282, 1u}, {0xA7B2u, 0xA7B2u, -42261, 1u}, {0xA7B3u, 0xA7B3u, 928, 1u},
    {0xA7B4u, 0xA7C2u, 1, 2u}, {0xA7C4u, 0xA7C4u, -48, 1u}, {0xA7C5u, 0xA7C5u, -42307, 1u},
    {0xA7C6u, 0xA7C6u, -35384, 1u}, {0xA7C7u, 0xA7C9u, 1, 2u}, {0xA7D0u, 0xA7D0u, 1, 1u},
    {0xA7D6u, 0xA7D8u, 1, 2u}, {0xA7F5u, 0xA7F5u, 1, 1u}, {0xFF21u, 0xFF3Au, 32, 1u},
    {0x10400u, 0x10427u, 40, 1u}, {0x104B0u, 0x104D3u, 40, 1u}, {0x10570u, 0x1057Au, 39, 1u},
    {0x1057Cu, 0x1058Au, 39, 1u}, {0x1058Cu, 0x10592u, 39, 1u}, {0x10594u, 0x10595u, 39, 1u},
    {0x10C80u, 0x10CB2u, 64, 1u}, {0x118A0u, 0x118BFu, 32, 1u}, {0x16E40u, 0x16E5Fu, 32, 1u},
    {0x1E900u, 0x1E921u, 34, 1u},
};
static const AM_UnicodeRange am_cased_ranges[] = {
    {0x41u, 0x5Au}, {0x61u, 0x7Au}, {0xAAu, 0xAAu}, {0xB5u, 0xB5u},
    {0xBAu, 0xBAu}, {0xC0u, 0xD6u}, {0xD8u, 0xF6u}, {0xF8u, 0x1BAu},
    {0x1BCu, 0x1BFu}, {0x1C4u, 0x293u}, {0x295u, 0x2B8u}, {0x2C0u, 0x2C1u},
    {0x2E0u, 0x2E4u}, {0x345u, 0x345u}, {0x370u, 0x373u}, {0x376u, 0x377u},
    {0x37Au, 0x37Du}, {0x37Fu, 0x37Fu}, {0x386u, 0x386u}, {0x388u, 0x38Au},
    {0x38Cu, 0x38Cu}, {0x38Eu, 0x3A1u}, {0x3A3u, 0x3F5u}, {0x3F7u, 0x481u},
    {0x48Au, 0x52Fu}, {0x531u, 0x556u}, {0x560u, 0x588u}, {0x10A0u, 0x10C5u},
    {0x10C7u, 0x10C7u}, {0x10CDu, 0x10CDu}, {0x10D0u, 0x10FAu}, {0x10FCu, 0x10FFu},
    {0x13A0u, 0x13F5u}, {0x13F8u, 0x13FDu}, {0x1C80u, 0x1C88u}, {0x1C90u, 0x1CBAu},
    {0x1CBDu, 0x1CBFu}, {0x1D00u, 0x1DBFu}, {0x1E00u, 0x1F15u}, {0x1F18u, 0x1F1Du},
    {0x1F20u, 0x1F45u}, {0x1F48u, 0x1F4Du}, {0x1F50u, 0x1F57u}, {0x1F59u, 0x1F59u},
    {0x1F5Bu, 0x1F5Bu}, {0x1F5Du, 0x1F5Du}, {0x1F5Fu, 0x1F7Du}, {0x1F80u, 0x1FB4u},
    {0x1FB6u, 0x1FBCu}, {0x1FBEu, 0x1FBEu}, {0x1FC2u, 0x1FC4u}, {0x1FC6u, 0x1FCCu},
    {0x1FD0u, 0x1FD3u}, {0x1FD6u, 0x1FDBu}, {0x1FE0u, 0x1FECu}, {0x1FF2u, 0x1FF4u},
    {0x1FF6u, 0x1FFCu}, {0x2071u, 0x2071u}, {0x207Fu, 0x207Fu}, {0x2090u, 0x209Cu},
    {0x2102u, 0x2102u}, {0x2107u, 0x2107u}, {0x210Au, 0x2113u}, {0x2115u, 0x2115u},
    {0x2119u, 0x211Du}, {0x2124u, 0x2124u}, {0x2126u, 0x2126u}, {0x2128u, 0x2128u},
    {0x212Au, 0x212Du}, {0x212Fu, 0x2134u}, {0x2139u, 0x2139u}, {0x213Cu, 0x213Fu},
    {0x2145u, 0x2149u}, {0x214Eu, 0x214Eu}, {0x2160u, 0x217Fu}, {0x2183u, 0x2184u},
    {0x24B6u, 0x24E9u}, {0x2C00u, 0x2CE4u}, {0x2CEBu, 0x2CEEu}, {0x2CF2u, 0x2CF3u},
    {0x2D00u, 0x2D25u}, {0x2D27u, 0x2D27u}, {0x2D2Du, 0x2D2Du}, {0xA640u, 0xA66Du},
    {0xA680u, 0xA69Du}, {0xA722u, 0xA787u}, {0xA78Bu, 0xA78Eu}, {0xA790u, 0xA7CAu},
    {0xA7D0u, 0xA7D1u}, {0xA7D3u, 0xA7D3u}, {0xA7D5u, 0xA7D9u}, {0xA7F2u, 0xA7F6u},
    {0xA7F8u, 0xA7FAu}, {0xAB30u, 0xAB5Au}, {0xAB5Cu, 0xAB69u}, {0xAB70u, 0xABBFu},
    {0xFB00u, 0xFB06u}, {0xFB13u, 0xFB17u}, {0xFF21u, 0xFF3Au}, {0xFF41u, 0xFF5Au},
    {0x10400u, 0x1044Fu}, {0x104B0u, 0x104D3u}, {0x104D8u, 0x104FBu}, {0x10570u, 0x1057Au},
    {0x1057Cu, 0x1058Au}, {0x1058Cu, 0x10592u}, {0x10594u, 0x10595u}, {0x10597u, 0x105A1u},
    {0x105A3u, 0x105B1u}, {0x105B3u, 0x105B9u}, {0x105BBu, 0x105BCu}, {0x10780u, 0x10780u},
    {0x10783u, 0x10785u}, {0x10787u, 0x107B0u}, {0x107B2u, 0x107BAu}, {0x10C80u, 0x10CB2u},
    {0x10CC0u, 0x10CF2u}, {0x118A0u, 0x118DFu}, {0x16E40u, 0x16E7Fu}, {0x1D400u, 0x1D454u},
    {0x1D456u, 0x1D49Cu}, {0x1D49Eu, 0x1D49Fu}, {0x1D4A2u, 0x1D4A2u}, {0x1D4A5u, 0x1D4A6u},
    {0x1D4A9u, 0x1D4ACu}, {0x1D4AEu, 0x1D4B9u}, {0x1D4BBu, 0x1D4BBu}, {0x1D4BDu, 0x1D4C3u},
    {0x1D4C5u, 0x1D505u}, {0x1D507u, 0x1D50Au}, {0x1D50Du, 0x1D514u}, {0x1D516u, 0x1D51Cu},
    {0x1D51Eu, 0x1D539u}, {0x1D53Bu, 0x1D53Eu}, {0x1D540u, 0x1D544u}, {0x1D546u, 0x1D546u},
    {0x1D54Au, 0x1D550u}, {0x1D552u, 0x1D6A5u}, {0x1D6A8u, 0x1D6C0u}, {0x1D6C2u, 0x1D6DAu},
    {0x1D6DCu, 0x1D6FAu}, {0x1D6FCu, 0x1D714u}, {0x1D716u, 0x1D734u}, {0x1D736u, 0x1D74Eu},
    {0x1D750u, 0x1D76Eu}, {0x1D770u, 0x1D788u}, {0x1D78Au, 0x1D7A8u}, {0x1D7AAu, 0x1D7C2u},
    {0x1D7C4u, 0x1D7CBu}, {0x1DF00u, 0x1DF09u}, {0x1DF0Bu, 0x1DF1Eu}, {0x1DF25u, 0x1DF2Au},
    {0x1E030u, 0x1E06Du}, {0x1E900u, 0x1E943u}, {0x1F130u, 0x1F149u}, {0x1F150u, 0x1F169u},
    {0x1F170u, 0x1F189u},
};
static const AM_UnicodeRange am_case_ignorable_ranges[] = {
    {0x27u, 0x27u}, {0x2Eu, 0x2Eu}, {0x3Au, 0x3Au}, {0x5Eu, 0x5Eu},
    {0x60u, 0x60u}, {0xA8u, 0xA8u}, {0xADu, 0xADu}, {0xAFu, 0xAFu},
    {0xB4u, 0xB4u}, {0xB7u, 0xB8u}, {0x2B0u, 0x36Fu}, {0x374u, 0x375u},
    {0x37Au, 0x37Au}, {0x384u, 0x385u}, {0x387u, 0x387u}, {0x483u, 0x489u},
    {0x559u, 0x559u}, {0x55Fu, 0x55Fu}, {0x591u, 0x5BDu}, {0x5BFu, 0x5BFu},
    {0x5C1u, 0x5C2u}, {0x5C4u, 0x5C5u}, {0x5C7u, 0x5C7u}, {0x5F4u, 0x5F4u},
    {0x600u, 0x605u}, {0x610u, 0x61Au}, {0x61Cu, 0x61Cu}, {0x640u, 0x640u},
    {0x64Bu, 0x65Fu}, {0x670u, 0x670u}, {0x6D6u, 0x6DDu}, {0x6DFu, 0x6E8u},
    {0x6EAu, 0x6EDu}, {0x70Fu, 0x70Fu}, {0x711u, 0x711u}, {0x730u, 0x74Au},
    {0x7A6u, 0x7B0u}, {0x7EBu, 0x7F5u}, {0x7FAu, 0x7FAu}, {0x7FDu, 0x7FDu},
    {0x816u, 0x82Du}, {0x859u, 0x85Bu}, {0x888u, 0x888u}, {0x890u, 0x891u},
    {0x898u, 0x89Fu}, {0x8C9u, 0x902u}, {0x93Au, 0x93Au}, {0x93Cu, 0x93Cu},
    {0x941u, 0x948u}, {0x94Du, 0x94Du}, {0x951u, 0x957u}, {0x962u, 0x963u},
    {0x971u, 0x971u}, {0x981u, 0x981u}, {0x9BCu, 0x9BCu}, {0x9C1u, 0x9C4u},
    {0x9CDu, 0x9CDu}, {0x9E2u, 0x9E3u}, {0x9FEu, 0x9FEu}, {0xA01u, 0xA02u},
    {0xA3Cu, 0xA3Cu}, {0xA41u, 0xA42u}, {0xA47u, 0xA48u}, {0xA4Bu, 0xA4Du},
    {0xA51u, 0xA51u}, {0xA70u, 0xA71u}, {0xA75u, 0xA75u}, {0xA81u, 0xA82u},
    {0xABCu, 0xABCu}, {0xAC1u, 0xAC5u}, {0xAC7u, 0xAC8u}, {0xACDu, 0xACDu},
    {0xAE2u, 0xAE3u}, {0xAFAu, 0xAFFu}, {0xB01u, 0xB01u}, {0xB3Cu, 0xB3Cu},
    {0xB3Fu, 0xB3Fu}, {0xB41u, 0xB44u}, {0xB4Du, 0xB4Du}, {0xB55u, 0xB56u},
    {0xB62u, 0xB63u}, {0xB82u, 0xB82u}, {0xBC0u, 0xBC0u}, {0xBCDu, 0xBCDu},
    {0xC00u, 0xC00u}, {0xC04u, 0xC04u}, {0xC3Cu, 0xC3Cu}, {0xC3Eu, 0xC40u},
    {0xC46u, 0xC48u}, {0xC4Au, 0xC4Du}, {0xC55u, 0xC56u}, {0xC62u, 0xC63u},
    {0xC81u, 0xC81u}, {0xCBCu, 0xCBCu}, {0xCBFu, 0xCBFu}, {0xCC6u, 0xCC6u},
    {0xCCCu, 0xCCDu}, {0xCE2u, 0xCE3u}, {0xD00u, 0xD01u}, {0xD3Bu, 0xD3Cu},
    {0xD41u, 0xD44u}, {0xD4Du, 0xD4Du}, {0xD62u, 0xD63u}, {0xD81u, 0xD81u},
    {0xDCAu, 0xDCAu}, {0xDD2u, 0xDD4u}, {0xDD6u, 0xDD6u}, {0xE31u, 0xE31u},
    {0xE34u, 0xE3Au}, {0xE46u, 0xE4Eu}, {0xEB1u, 0xEB1u}, {0xEB4u, 0xEBCu},
    {0xEC6u, 0xEC6u}, {0xEC8u, 0xECEu}, {0xF18u, 0xF19u}, {0xF35u, 0xF35u},
    {0xF37u, 0xF37u}, {0xF39u, 0xF39u}, {0xF71u, 0xF7Eu}, {0xF80u, 0xF84u},
    {0xF86u, 0xF87u}, {0xF8Du, 0xF97u}, {0xF99u, 0xFBCu}, {0xFC6u, 0xFC6u},
    {0x102Du, 0x1030u}, {0x1032u, 0x1037u}, {0x1039u, 0x103Au}, {0x103Du, 0x103Eu},
    {0x1058u, 0x1059u}, {0x105Eu, 0x1060u}, {0x1071u, 0x1074u}, {0x1082u, 0x1082u},
    {0x1085u, 0x1086u}, {0x108Du, 0x108Du}, {0x109Du, 0x109Du}, {0x10FCu, 0x10FCu},
    {0x135Du, 0x135Fu}, {0x1712u, 0x1714u}, {0x1732u, 0x1733u}, {0x1752u, 0x1753u},
    {0x1772u, 0x1773u}, {0x17B4u, 0x17B5u}, {0x17B7u, 0x17BDu}, {0x17C6u, 0x17C6u},
    {0x17C9u, 0x17D3u}, {0x17D7u, 0x17D7u}, {0x17DDu, 0x17DDu}, {0x180Bu, 0x180Fu},
    {0x1843u, 0x1843u}, {0x1885u, 0x1886u}, {0x18A9u, 0x18A9u}, {0x1920u, 0x1922u},
    {0x1927u, 0x1928u}, {0x1932u, 0x1932u}, {0x1939u, 0x193Bu}, {0x1A17u, 0x1A18u},
    {0x1A1Bu, 0x1A1Bu}, {0x1A56u, 0x1A56u}, {0x1A58u, 0x1A5Eu}, {0x1A60u, 0x1A60u},
    {0x1A62u, 0x1A62u}, {0x1A65u, 0x1A6Cu}, {0x1A73u, 0x1A7Cu}, {0x1A7Fu, 0x1A7Fu},
    {0x1AA7u, 0x1AA7u}, {0x1AB0u, 0x1ACEu}, {0x1B00u, 0x1B03u}, {0x1B34u, 0x1B34u},
    {0x1B36u, 0x1B3Au}, {0x1B3Cu, 0x1B3Cu}, {0x1B42u, 0x1B42u}, {0x1B6Bu, 0x1B73u},
    {0x1B80u, 0x1B81u}, {0x1BA2u, 0x1BA5u}, {0x1BA8u, 0x1BA9u}, {0x1BABu, 0x1BADu},
    {0x1BE6u, 0x1BE6u}, {0x1BE8u, 0x1BE9u}, {0x1BEDu, 0x1BEDu}, {0x1BEFu, 0x1BF1u},
    {0x1C2Cu, 0x1C33u}, {0x1C36u, 0x1C37u}, {0x1C78u, 0x1C7Du}, {0x1CD0u, 0x1CD2u},
    {0x1CD4u, 0x1CE0u}, {0x1CE2u, 0x1CE8u}, {0x1CEDu, 0x1CEDu}, {0x1CF4u, 0x1CF4u},
    {0x1CF8u, 0x1CF9u}, {0x1D2Cu, 0x1D6Au}, {0x1D78u, 0x1D78u}, {0x1D9Bu, 0x1DFFu},
    {0x1FBDu, 0x1FBDu}, {0x1FBFu, 0x1FC1u}, {0x1FCDu, 0x1FCFu}, {0x1FDDu, 0x1FDFu},
    {0x1FEDu, 0x1FEFu}, {0x1FFDu, 0x1FFEu}, {0x200Bu, 0x200Fu}, {0x2018u, 0x2019u},
    {0x2024u, 0x2024u}, {0x2027u, 0x2027u}, {0x202Au, 0x202Eu}, {0x2060u, 0x2064u},
    {0x2066u, 0x206Fu}, {0x2071u, 0x2071u}, {0x207Fu, 0x207Fu}, {0x2090u, 0x209Cu},
    {0x20D0u, 0x20F0u}, {0x2C7Cu, 0x2C7Du}, {0x2CEFu, 0x2CF1u}, {0x2D6Fu, 0x2D6Fu},
    {0x2D7Fu, 0x2D7Fu}, {0x2DE0u, 0x2DFFu}, {0x2E2Fu, 0x2E2Fu}, {0x3005u, 0x3005u},
    {0x302Au, 0x302Du}, {0x3031u, 0x3035u}, {0x303Bu, 0x303Bu}, {0x3099u, 0x309Eu},
    {0x30FCu, 0x30FEu}, {0xA015u, 0xA015u}, {0xA4F8u, 0xA4FDu}, {0xA60Cu, 0xA60Cu},
    {0xA66Fu, 0xA672u}, {0xA674u, 0xA67Du}, {0xA67Fu, 0xA67Fu}, {0xA69Cu, 0xA69Fu},
    {0xA6F0u, 0xA6F1u}, {0xA700u, 0xA721u}, {0xA770u, 0xA770u}, {0xA788u, 0xA78Au},
    {0xA7F2u, 0xA7F4u}, {0xA7F8u, 0xA7F9u}, {0xA802u, 0xA802u}, {0xA806u, 0xA806u},
    {0xA80Bu, 0xA80Bu}, {0xA825u, 0xA826u}, {0xA82Cu, 0xA82Cu}, {0xA8C4u, 0xA8C5u},
    {0xA8E0u, 0xA8F1u}, {0xA8FFu, 0xA8FFu}, {0xA926u, 0xA92Du}, {0xA947u, 0xA951u},
    {0xA980u, 0xA982u}, {0xA9B3u, 0xA9B3u}, {0xA9B6u, 0xA9B9u}, {0xA9BCu, 0xA9BDu},
    {0xA9CFu, 0xA9CFu}, {0xA9E5u, 0xA9E6u}, {0xAA29u, 0xAA2Eu}, {0xAA31u, 0xAA32u},
    {0xAA35u, 0xAA36u}, {0xAA43u, 0xAA43u}, {0xAA4Cu, 0xAA4Cu}, {0xAA70u, 0xAA70u},
    {0xAA7Cu, 0xAA7Cu}, {0xAAB0u, 0xAAB0u}, {0xAAB2u, 0xAAB4u}, {0xAAB7u, 0xAAB8u},
    {0xAABEu, 0xAABFu}, {0xAAC1u, 0xAAC1u}, {0xAADDu, 0xAADDu}, {0xAAECu, 0xAAEDu},
    {0xAAF3u, 0xAAF4u}, {0xAAF6u, 0xAAF6u}, {0xAB5Bu, 0xAB5Fu}, {0xAB69u, 0xAB6Bu},
    {0xABE5u, 0xABE5u}, {0xABE8u, 0xABE8u}, {0xABEDu, 0xABEDu}, {0xFB1Eu, 0xFB1Eu},
    {0xFBB2u, 0xFBC2u}, {0xFE00u, 0xFE0Fu}, {0xFE13u, 0xFE13u}, {0xFE20u, 0xFE2Fu},
    {0xFE52u, 0xFE52u}, {0xFE55u, 0xFE55u}, {0xFEFFu, 0xFEFFu}, {0xFF07u, 0xFF07u},
    {0xFF0Eu, 0xFF0Eu}, {0xFF1Au, 0xFF1Au}, {0xFF3Eu, 0xFF3Eu}, {0xFF40u, 0xFF40u},
    {0xFF70u, 0xFF70u}, {0xFF9Eu, 0xFF9Fu}, {0xFFE3u, 0xFFE3u}, {0xFFF9u, 0xFFFBu},
    {0x101FDu, 0x101FDu}, {0x102E0u, 0x102E0u}, {0x10376u, 0x1037Au}, {0x10780u, 0x10785u},
    {0x10787u, 0x107B0u}, {0x107B2u, 0x107BAu}, {0x10A01u, 0x10A03u}, {0x10A05u, 0x10A06u},
    {0x10A0Cu, 0x10A0Fu}, {0x10A38u, 0x10A3Au}, {0x10A3Fu, 0x10A3Fu}, {0x10AE5u, 0x10AE6u},
    {0x10D24u, 0x10D27u}, {0x10EABu, 0x10EACu}, {0x10EFDu, 0x10EFFu}, {0x10F46u, 0x10F50u},
    {0x10F82u, 0x10F85u}, {0x11001u, 0x11001u}, {0x11038u, 0x11046u}, {0x11070u, 0x11070u},
    {0x11073u, 0x11074u}, {0x1107Fu, 0x11081u}, {0x110B3u, 0x110B6u}, {0x110B9u, 0x110BAu},
    {0x110BDu, 0x110BDu}, {0x110C2u, 0x110C2u}, {0x110CDu, 0x110CDu}, {0x11100u, 0x11102u},
    {0x11127u, 0x1112Bu}, {0x1112Du, 0x11134u}, {0x11173u, 0x11173u}, {0x11180u, 0x11181u},
    {0x111B6u, 0x111BEu}, {0x111C9u, 0x111CCu}, {0x111CFu, 0x111CFu}, {0x1122Fu, 0x11231u},
    {0x11234u, 0x11234u}, {0x11236u, 0x11237u}, {0x1123Eu, 0x1123Eu}, {0x11241u, 0x11241u},
    {0x112DFu, 0x112DFu}, {0x112E3u, 0x112EAu}, {0x11300u, 0x11301u}, {0x1133Bu, 0x1133Cu},
    {0x11340u, 0x11340u}, {0x11366u, 0x1136Cu}, {0x11370u, 0x11374u}, {0x11438u, 0x1143Fu},
    {0x11442u, 0x11444u}, {0x11446u, 0x11446u}, {0x1145Eu, 0x1145Eu}, {0x114B3u, 0x114B8u},
    {0x114BAu, 0x114BAu}, {0x114BFu, 0x114C0u}, {0x114C2u, 0x114C3u}, {0x115B2u, 0x115B5u},
    {0x115BCu, 0x115BDu}, {0x115BFu, 0x115C0u}, {0x115DCu, 0x115DDu}, {0x11633u, 0x1163Au},
    {0x1163Du, 0x1163Du}, {0x1163Fu, 0x11640u}, {0x116ABu, 0x116ABu}, {0x116ADu, 0x116ADu},
    {0x116B0u, 0x116B5u}, {0x116B7u, 0x116B7u}, {0x1171Du, 0x1171Fu}, {0x11722u, 0x11725u},
    {0x11727u, 0x1172Bu}, {0x1182Fu, 0x11837u}, {0x11839u, 0x1183Au}, {0x1193Bu, 0x1193Cu},
    {0x1193Eu, 0x1193Eu}, {0x11943u, 0x11943u}, {0x119D4u, 0x119D7u}, {0x119DAu, 0x119DBu},
    {0x119E0u, 0x119E0u}, {0x11A01u, 0x11A0Au}, {0x11A33u, 0x11A38u}, {0x11A3Bu, 0x11A3Eu},
    {0x11A47u, 0x11A47u}, {0x11A51u, 0x11A56u}, {0x11A59u, 0x11A5Bu}, {0x11A8Au, 0x11A96u},
    {0x11A98u, 0x11A99u}, {0x11C30u, 0x11C36u}, {0x11C38u, 0x11C3Du}, {0x11C3Fu, 0x11C3Fu},
    {0x11C92u, 0x11CA7u}, {0x11CAAu, 0x11CB0u}, {0x11CB2u, 0x11CB3u}, {0x11CB5u, 0x11CB6u},
    {0x11D31u, 0x11D36u}, {0x11D3Au, 0x11D3Au}, {0x11D3Cu, 0x11D3Du}, {0x11D3Fu, 0x11D45u},
    {0x11D47u, 0x11D47u}, {0x11D90u, 0x11D91u}, {0x11D95u, 0x11D95u}, {0x11D97u, 0x11D97u},
    {0x11EF3u, 0x11EF4u}, {0x11F00u, 0x11F01u}, {0x11F36u, 0x11F3Au}, {0x11F40u, 0x11F40u},
    {0x11F42u, 0x11F42u}, {0x13430u, 0x13440u}, {0x13447u, 0x13455u}, {0x16AF0u, 0x16AF4u},
    {0x16B30u, 0x16B36u}, {0x16B40u, 0x16B43u}, {0x16F4Fu, 0x16F4Fu}, {0x16F8Fu, 0x16F9Fu},
    {0x16FE0u, 0x16FE1u}, {0x16FE3u, 0x16FE4u}, {0x1AFF0u, 0x1AFF3u}, {0x1AFF5u, 0x1AFFBu},
    {0x1AFFDu, 0x1AFFEu}, {0x1BC9Du, 0x1BC9Eu}, {0x1BCA0u, 0x1BCA3u}, {0x1CF00u, 0x1CF2Du},
    {0x1CF30u, 0x1CF46u}, {0x1D167u, 0x1D169u}, {0x1D173u, 0x1D182u}, {0x1D185u, 0x1D18Bu},
    {0x1D1AAu, 0x1D1ADu}, {0x1D242u, 0x1D244u}, {0x1DA00u, 0x1DA36u}, {0x1DA3Bu, 0x1DA6Cu},
    {0x1DA75u, 0x1DA75u}, {0x1DA84u, 0x1DA84u}, {0x1DA9Bu, 0x1DA9Fu}, {0x1DAA1u, 0x1DAAFu},
    {0x1E000u, 0x1E006u}, {0x1E008u, 0x1E018u}, {0x1E01Bu, 0x1E021u}, {0x1E023u, 0x1E024u},
    {0x1E026u, 0x1E02Au}, {0x1E030u, 0x1E06Du}, {0x1E08Fu, 0x1E08Fu}, {0x1E130u, 0x1E13Du},
    {0x1E2AEu, 0x1E2AEu}, {0x1E2ECu, 0x1E2EFu}, {0x1E4EBu, 0x1E4EFu}, {0x1E8D0u, 0x1E8D6u},
    {0x1E944u, 0x1E94Bu}, {0x1F3FBu, 0x1F3FFu}, {0xE0001u, 0xE0001u}, {0xE0020u, 0xE007Fu},
    {0xE0100u, 0xE01EFu},
};

// Default Unicode 15 lowercasing: U+0130 expands; U+03A3 uses original context.
// UTF-8 input and output are locale-independent. All tables are immutable.
// Unicode 15.0 Letter/Number membership; generate_alnum_tables.py.
static const AM_UnicodeRange am_alnum_ranges[] = {
    {0x30,0x39}, {0x41,0x5A}, {0x61,0x7A}, {0xAA,0xAA},
    {0xB2,0xB3}, {0xB5,0xB5}, {0xB9,0xBA}, {0xBC,0xBE},
    {0xC0,0xD6}, {0xD8,0xF6}, {0xF8,0x2C1}, {0x2C6,0x2D1},
    {0x2E0,0x2E4}, {0x2EC,0x2EC}, {0x2EE,0x2EE}, {0x370,0x374},
    {0x376,0x377}, {0x37A,0x37D}, {0x37F,0x37F}, {0x386,0x386},
    {0x388,0x38A}, {0x38C,0x38C}, {0x38E,0x3A1}, {0x3A3,0x3F5},
    {0x3F7,0x481}, {0x48A,0x52F}, {0x531,0x556}, {0x559,0x559},
    {0x560,0x588}, {0x5D0,0x5EA}, {0x5EF,0x5F2}, {0x620,0x64A},
    {0x660,0x669}, {0x66E,0x66F}, {0x671,0x6D3}, {0x6D5,0x6D5},
    {0x6E5,0x6E6}, {0x6EE,0x6FC}, {0x6FF,0x6FF}, {0x710,0x710},
    {0x712,0x72F}, {0x74D,0x7A5}, {0x7B1,0x7B1}, {0x7C0,0x7EA},
    {0x7F4,0x7F5}, {0x7FA,0x7FA}, {0x800,0x815}, {0x81A,0x81A},
    {0x824,0x824}, {0x828,0x828}, {0x840,0x858}, {0x860,0x86A},
    {0x870,0x887}, {0x889,0x88E}, {0x8A0,0x8C9}, {0x904,0x939},
    {0x93D,0x93D}, {0x950,0x950}, {0x958,0x961}, {0x966,0x96F},
    {0x971,0x980}, {0x985,0x98C}, {0x98F,0x990}, {0x993,0x9A8},
    {0x9AA,0x9B0}, {0x9B2,0x9B2}, {0x9B6,0x9B9}, {0x9BD,0x9BD},
    {0x9CE,0x9CE}, {0x9DC,0x9DD}, {0x9DF,0x9E1}, {0x9E6,0x9F1},
    {0x9F4,0x9F9}, {0x9FC,0x9FC}, {0xA05,0xA0A}, {0xA0F,0xA10},
    {0xA13,0xA28}, {0xA2A,0xA30}, {0xA32,0xA33}, {0xA35,0xA36},
    {0xA38,0xA39}, {0xA59,0xA5C}, {0xA5E,0xA5E}, {0xA66,0xA6F},
    {0xA72,0xA74}, {0xA85,0xA8D}, {0xA8F,0xA91}, {0xA93,0xAA8},
    {0xAAA,0xAB0}, {0xAB2,0xAB3}, {0xAB5,0xAB9}, {0xABD,0xABD},
    {0xAD0,0xAD0}, {0xAE0,0xAE1}, {0xAE6,0xAEF}, {0xAF9,0xAF9},
    {0xB05,0xB0C}, {0xB0F,0xB10}, {0xB13,0xB28}, {0xB2A,0xB30},
    {0xB32,0xB33}, {0xB35,0xB39}, {0xB3D,0xB3D}, {0xB5C,0xB5D},
    {0xB5F,0xB61}, {0xB66,0xB6F}, {0xB71,0xB77}, {0xB83,0xB83},
    {0xB85,0xB8A}, {0xB8E,0xB90}, {0xB92,0xB95}, {0xB99,0xB9A},
    {0xB9C,0xB9C}, {0xB9E,0xB9F}, {0xBA3,0xBA4}, {0xBA8,0xBAA},
    {0xBAE,0xBB9}, {0xBD0,0xBD0}, {0xBE6,0xBF2}, {0xC05,0xC0C},
    {0xC0E,0xC10}, {0xC12,0xC28}, {0xC2A,0xC39}, {0xC3D,0xC3D},
    {0xC58,0xC5A}, {0xC5D,0xC5D}, {0xC60,0xC61}, {0xC66,0xC6F},
    {0xC78,0xC7E}, {0xC80,0xC80}, {0xC85,0xC8C}, {0xC8E,0xC90},
    {0xC92,0xCA8}, {0xCAA,0xCB3}, {0xCB5,0xCB9}, {0xCBD,0xCBD},
    {0xCDD,0xCDE}, {0xCE0,0xCE1}, {0xCE6,0xCEF}, {0xCF1,0xCF2},
    {0xD04,0xD0C}, {0xD0E,0xD10}, {0xD12,0xD3A}, {0xD3D,0xD3D},
    {0xD4E,0xD4E}, {0xD54,0xD56}, {0xD58,0xD61}, {0xD66,0xD78},
    {0xD7A,0xD7F}, {0xD85,0xD96}, {0xD9A,0xDB1}, {0xDB3,0xDBB},
    {0xDBD,0xDBD}, {0xDC0,0xDC6}, {0xDE6,0xDEF}, {0xE01,0xE30},
    {0xE32,0xE33}, {0xE40,0xE46}, {0xE50,0xE59}, {0xE81,0xE82},
    {0xE84,0xE84}, {0xE86,0xE8A}, {0xE8C,0xEA3}, {0xEA5,0xEA5},
    {0xEA7,0xEB0}, {0xEB2,0xEB3}, {0xEBD,0xEBD}, {0xEC0,0xEC4},
    {0xEC6,0xEC6}, {0xED0,0xED9}, {0xEDC,0xEDF}, {0xF00,0xF00},
    {0xF20,0xF33}, {0xF40,0xF47}, {0xF49,0xF6C}, {0xF88,0xF8C},
    {0x1000,0x102A}, {0x103F,0x1049}, {0x1050,0x1055}, {0x105A,0x105D},
    {0x1061,0x1061}, {0x1065,0x1066}, {0x106E,0x1070}, {0x1075,0x1081},
    {0x108E,0x108E}, {0x1090,0x1099}, {0x10A0,0x10C5}, {0x10C7,0x10C7},
    {0x10CD,0x10CD}, {0x10D0,0x10FA}, {0x10FC,0x1248}, {0x124A,0x124D},
    {0x1250,0x1256}, {0x1258,0x1258}, {0x125A,0x125D}, {0x1260,0x1288},
    {0x128A,0x128D}, {0x1290,0x12B0}, {0x12B2,0x12B5}, {0x12B8,0x12BE},
    {0x12C0,0x12C0}, {0x12C2,0x12C5}, {0x12C8,0x12D6}, {0x12D8,0x1310},
    {0x1312,0x1315}, {0x1318,0x135A}, {0x1369,0x137C}, {0x1380,0x138F},
    {0x13A0,0x13F5}, {0x13F8,0x13FD}, {0x1401,0x166C}, {0x166F,0x167F},
    {0x1681,0x169A}, {0x16A0,0x16EA}, {0x16EE,0x16F8}, {0x1700,0x1711},
    {0x171F,0x1731}, {0x1740,0x1751}, {0x1760,0x176C}, {0x176E,0x1770},
    {0x1780,0x17B3}, {0x17D7,0x17D7}, {0x17DC,0x17DC}, {0x17E0,0x17E9},
    {0x17F0,0x17F9}, {0x1810,0x1819}, {0x1820,0x1878}, {0x1880,0x1884},
    {0x1887,0x18A8}, {0x18AA,0x18AA}, {0x18B0,0x18F5}, {0x1900,0x191E},
    {0x1946,0x196D}, {0x1970,0x1974}, {0x1980,0x19AB}, {0x19B0,0x19C9},
    {0x19D0,0x19DA}, {0x1A00,0x1A16}, {0x1A20,0x1A54}, {0x1A80,0x1A89},
    {0x1A90,0x1A99}, {0x1AA7,0x1AA7}, {0x1B05,0x1B33}, {0x1B45,0x1B4C},
    {0x1B50,0x1B59}, {0x1B83,0x1BA0}, {0x1BAE,0x1BE5}, {0x1C00,0x1C23},
    {0x1C40,0x1C49}, {0x1C4D,0x1C7D}, {0x1C80,0x1C88}, {0x1C90,0x1CBA},
    {0x1CBD,0x1CBF}, {0x1CE9,0x1CEC}, {0x1CEE,0x1CF3}, {0x1CF5,0x1CF6},
    {0x1CFA,0x1CFA}, {0x1D00,0x1DBF}, {0x1E00,0x1F15}, {0x1F18,0x1F1D},
    {0x1F20,0x1F45}, {0x1F48,0x1F4D}, {0x1F50,0x1F57}, {0x1F59,0x1F59},
    {0x1F5B,0x1F5B}, {0x1F5D,0x1F5D}, {0x1F5F,0x1F7D}, {0x1F80,0x1FB4},
    {0x1FB6,0x1FBC}, {0x1FBE,0x1FBE}, {0x1FC2,0x1FC4}, {0x1FC6,0x1FCC},
    {0x1FD0,0x1FD3}, {0x1FD6,0x1FDB}, {0x1FE0,0x1FEC}, {0x1FF2,0x1FF4},
    {0x1FF6,0x1FFC}, {0x2070,0x2071}, {0x2074,0x2079}, {0x207F,0x2089},
    {0x2090,0x209C}, {0x2102,0x2102}, {0x2107,0x2107}, {0x210A,0x2113},
    {0x2115,0x2115}, {0x2119,0x211D}, {0x2124,0x2124}, {0x2126,0x2126},
    {0x2128,0x2128}, {0x212A,0x212D}, {0x212F,0x2139}, {0x213C,0x213F},
    {0x2145,0x2149}, {0x214E,0x214E}, {0x2150,0x2189}, {0x2460,0x249B},
    {0x24EA,0x24FF}, {0x2776,0x2793}, {0x2C00,0x2CE4}, {0x2CEB,0x2CEE},
    {0x2CF2,0x2CF3}, {0x2CFD,0x2CFD}, {0x2D00,0x2D25}, {0x2D27,0x2D27},
    {0x2D2D,0x2D2D}, {0x2D30,0x2D67}, {0x2D6F,0x2D6F}, {0x2D80,0x2D96},
    {0x2DA0,0x2DA6}, {0x2DA8,0x2DAE}, {0x2DB0,0x2DB6}, {0x2DB8,0x2DBE},
    {0x2DC0,0x2DC6}, {0x2DC8,0x2DCE}, {0x2DD0,0x2DD6}, {0x2DD8,0x2DDE},
    {0x2E2F,0x2E2F}, {0x3005,0x3007}, {0x3021,0x3029}, {0x3031,0x3035},
    {0x3038,0x303C}, {0x3041,0x3096}, {0x309D,0x309F}, {0x30A1,0x30FA},
    {0x30FC,0x30FF}, {0x3105,0x312F}, {0x3131,0x318E}, {0x3192,0x3195},
    {0x31A0,0x31BF}, {0x31F0,0x31FF}, {0x3220,0x3229}, {0x3248,0x324F},
    {0x3251,0x325F}, {0x3280,0x3289}, {0x32B1,0x32BF}, {0x3400,0x4DBF},
    {0x4E00,0xA48C}, {0xA4D0,0xA4FD}, {0xA500,0xA60C}, {0xA610,0xA62B},
    {0xA640,0xA66E}, {0xA67F,0xA69D}, {0xA6A0,0xA6EF}, {0xA717,0xA71F},
    {0xA722,0xA788}, {0xA78B,0xA7CA}, {0xA7D0,0xA7D1}, {0xA7D3,0xA7D3},
    {0xA7D5,0xA7D9}, {0xA7F2,0xA801}, {0xA803,0xA805}, {0xA807,0xA80A},
    {0xA80C,0xA822}, {0xA830,0xA835}, {0xA840,0xA873}, {0xA882,0xA8B3},
    {0xA8D0,0xA8D9}, {0xA8F2,0xA8F7}, {0xA8FB,0xA8FB}, {0xA8FD,0xA8FE},
    {0xA900,0xA925}, {0xA930,0xA946}, {0xA960,0xA97C}, {0xA984,0xA9B2},
    {0xA9CF,0xA9D9}, {0xA9E0,0xA9E4}, {0xA9E6,0xA9FE}, {0xAA00,0xAA28},
    {0xAA40,0xAA42}, {0xAA44,0xAA4B}, {0xAA50,0xAA59}, {0xAA60,0xAA76},
    {0xAA7A,0xAA7A}, {0xAA7E,0xAAAF}, {0xAAB1,0xAAB1}, {0xAAB5,0xAAB6},
    {0xAAB9,0xAABD}, {0xAAC0,0xAAC0}, {0xAAC2,0xAAC2}, {0xAADB,0xAADD},
    {0xAAE0,0xAAEA}, {0xAAF2,0xAAF4}, {0xAB01,0xAB06}, {0xAB09,0xAB0E},
    {0xAB11,0xAB16}, {0xAB20,0xAB26}, {0xAB28,0xAB2E}, {0xAB30,0xAB5A},
    {0xAB5C,0xAB69}, {0xAB70,0xABE2}, {0xABF0,0xABF9}, {0xAC00,0xD7A3},
    {0xD7B0,0xD7C6}, {0xD7CB,0xD7FB}, {0xF900,0xFA6D}, {0xFA70,0xFAD9},
    {0xFB00,0xFB06}, {0xFB13,0xFB17}, {0xFB1D,0xFB1D}, {0xFB1F,0xFB28},
    {0xFB2A,0xFB36}, {0xFB38,0xFB3C}, {0xFB3E,0xFB3E}, {0xFB40,0xFB41},
    {0xFB43,0xFB44}, {0xFB46,0xFBB1}, {0xFBD3,0xFD3D}, {0xFD50,0xFD8F},
    {0xFD92,0xFDC7}, {0xFDF0,0xFDFB}, {0xFE70,0xFE74}, {0xFE76,0xFEFC},
    {0xFF10,0xFF19}, {0xFF21,0xFF3A}, {0xFF41,0xFF5A}, {0xFF66,0xFFBE},
    {0xFFC2,0xFFC7}, {0xFFCA,0xFFCF}, {0xFFD2,0xFFD7}, {0xFFDA,0xFFDC},
    {0x10000,0x1000B}, {0x1000D,0x10026}, {0x10028,0x1003A}, {0x1003C,0x1003D},
    {0x1003F,0x1004D}, {0x10050,0x1005D}, {0x10080,0x100FA}, {0x10107,0x10133},
    {0x10140,0x10178}, {0x1018A,0x1018B}, {0x10280,0x1029C}, {0x102A0,0x102D0},
    {0x102E1,0x102FB}, {0x10300,0x10323}, {0x1032D,0x1034A}, {0x10350,0x10375},
    {0x10380,0x1039D}, {0x103A0,0x103C3}, {0x103C8,0x103CF}, {0x103D1,0x103D5},
    {0x10400,0x1049D}, {0x104A0,0x104A9}, {0x104B0,0x104D3}, {0x104D8,0x104FB},
    {0x10500,0x10527}, {0x10530,0x10563}, {0x10570,0x1057A}, {0x1057C,0x1058A},
    {0x1058C,0x10592}, {0x10594,0x10595}, {0x10597,0x105A1}, {0x105A3,0x105B1},
    {0x105B3,0x105B9}, {0x105BB,0x105BC}, {0x10600,0x10736}, {0x10740,0x10755},
    {0x10760,0x10767}, {0x10780,0x10785}, {0x10787,0x107B0}, {0x107B2,0x107BA},
    {0x10800,0x10805}, {0x10808,0x10808}, {0x1080A,0x10835}, {0x10837,0x10838},
    {0x1083C,0x1083C}, {0x1083F,0x10855}, {0x10858,0x10876}, {0x10879,0x1089E},
    {0x108A7,0x108AF}, {0x108E0,0x108F2}, {0x108F4,0x108F5}, {0x108FB,0x1091B},
    {0x10920,0x10939}, {0x10980,0x109B7}, {0x109BC,0x109CF}, {0x109D2,0x10A00},
    {0x10A10,0x10A13}, {0x10A15,0x10A17}, {0x10A19,0x10A35}, {0x10A40,0x10A48},
    {0x10A60,0x10A7E}, {0x10A80,0x10A9F}, {0x10AC0,0x10AC7}, {0x10AC9,0x10AE4},
    {0x10AEB,0x10AEF}, {0x10B00,0x10B35}, {0x10B40,0x10B55}, {0x10B58,0x10B72},
    {0x10B78,0x10B91}, {0x10BA9,0x10BAF}, {0x10C00,0x10C48}, {0x10C80,0x10CB2},
    {0x10CC0,0x10CF2}, {0x10CFA,0x10D23}, {0x10D30,0x10D39}, {0x10E60,0x10E7E},
    {0x10E80,0x10EA9}, {0x10EB0,0x10EB1}, {0x10F00,0x10F27}, {0x10F30,0x10F45},
    {0x10F51,0x10F54}, {0x10F70,0x10F81}, {0x10FB0,0x10FCB}, {0x10FE0,0x10FF6},
    {0x11003,0x11037}, {0x11052,0x1106F}, {0x11071,0x11072}, {0x11075,0x11075},
    {0x11083,0x110AF}, {0x110D0,0x110E8}, {0x110F0,0x110F9}, {0x11103,0x11126},
    {0x11136,0x1113F}, {0x11144,0x11144}, {0x11147,0x11147}, {0x11150,0x11172},
    {0x11176,0x11176}, {0x11183,0x111B2}, {0x111C1,0x111C4}, {0x111D0,0x111DA},
    {0x111DC,0x111DC}, {0x111E1,0x111F4}, {0x11200,0x11211}, {0x11213,0x1122B},
    {0x1123F,0x11240}, {0x11280,0x11286}, {0x11288,0x11288}, {0x1128A,0x1128D},
    {0x1128F,0x1129D}, {0x1129F,0x112A8}, {0x112B0,0x112DE}, {0x112F0,0x112F9},
    {0x11305,0x1130C}, {0x1130F,0x11310}, {0x11313,0x11328}, {0x1132A,0x11330},
    {0x11332,0x11333}, {0x11335,0x11339}, {0x1133D,0x1133D}, {0x11350,0x11350},
    {0x1135D,0x11361}, {0x11400,0x11434}, {0x11447,0x1144A}, {0x11450,0x11459},
    {0x1145F,0x11461}, {0x11480,0x114AF}, {0x114C4,0x114C5}, {0x114C7,0x114C7},
    {0x114D0,0x114D9}, {0x11580,0x115AE}, {0x115D8,0x115DB}, {0x11600,0x1162F},
    {0x11644,0x11644}, {0x11650,0x11659}, {0x11680,0x116AA}, {0x116B8,0x116B8},
    {0x116C0,0x116C9}, {0x11700,0x1171A}, {0x11730,0x1173B}, {0x11740,0x11746},
    {0x11800,0x1182B}, {0x118A0,0x118F2}, {0x118FF,0x11906}, {0x11909,0x11909},
    {0x1190C,0x11913}, {0x11915,0x11916}, {0x11918,0x1192F}, {0x1193F,0x1193F},
    {0x11941,0x11941}, {0x11950,0x11959}, {0x119A0,0x119A7}, {0x119AA,0x119D0},
    {0x119E1,0x119E1}, {0x119E3,0x119E3}, {0x11A00,0x11A00}, {0x11A0B,0x11A32},
    {0x11A3A,0x11A3A}, {0x11A50,0x11A50}, {0x11A5C,0x11A89}, {0x11A9D,0x11A9D},
    {0x11AB0,0x11AF8}, {0x11C00,0x11C08}, {0x11C0A,0x11C2E}, {0x11C40,0x11C40},
    {0x11C50,0x11C6C}, {0x11C72,0x11C8F}, {0x11D00,0x11D06}, {0x11D08,0x11D09},
    {0x11D0B,0x11D30}, {0x11D46,0x11D46}, {0x11D50,0x11D59}, {0x11D60,0x11D65},
    {0x11D67,0x11D68}, {0x11D6A,0x11D89}, {0x11D98,0x11D98}, {0x11DA0,0x11DA9},
    {0x11EE0,0x11EF2}, {0x11F02,0x11F02}, {0x11F04,0x11F10}, {0x11F12,0x11F33},
    {0x11F50,0x11F59}, {0x11FB0,0x11FB0}, {0x11FC0,0x11FD4}, {0x12000,0x12399},
    {0x12400,0x1246E}, {0x12480,0x12543}, {0x12F90,0x12FF0}, {0x13000,0x1342F},
    {0x13441,0x13446}, {0x14400,0x14646}, {0x16800,0x16A38}, {0x16A40,0x16A5E},
    {0x16A60,0x16A69}, {0x16A70,0x16ABE}, {0x16AC0,0x16AC9}, {0x16AD0,0x16AED},
    {0x16B00,0x16B2F}, {0x16B40,0x16B43}, {0x16B50,0x16B59}, {0x16B5B,0x16B61},
    {0x16B63,0x16B77}, {0x16B7D,0x16B8F}, {0x16E40,0x16E96}, {0x16F00,0x16F4A},
    {0x16F50,0x16F50}, {0x16F93,0x16F9F}, {0x16FE0,0x16FE1}, {0x16FE3,0x16FE3},
    {0x17000,0x187F7}, {0x18800,0x18CD5}, {0x18D00,0x18D08}, {0x1AFF0,0x1AFF3},
    {0x1AFF5,0x1AFFB}, {0x1AFFD,0x1AFFE}, {0x1B000,0x1B122}, {0x1B132,0x1B132},
    {0x1B150,0x1B152}, {0x1B155,0x1B155}, {0x1B164,0x1B167}, {0x1B170,0x1B2FB},
    {0x1BC00,0x1BC6A}, {0x1BC70,0x1BC7C}, {0x1BC80,0x1BC88}, {0x1BC90,0x1BC99},
    {0x1D2C0,0x1D2D3}, {0x1D2E0,0x1D2F3}, {0x1D360,0x1D378}, {0x1D400,0x1D454},
    {0x1D456,0x1D49C}, {0x1D49E,0x1D49F}, {0x1D4A2,0x1D4A2}, {0x1D4A5,0x1D4A6},
    {0x1D4A9,0x1D4AC}, {0x1D4AE,0x1D4B9}, {0x1D4BB,0x1D4BB}, {0x1D4BD,0x1D4C3},
    {0x1D4C5,0x1D505}, {0x1D507,0x1D50A}, {0x1D50D,0x1D514}, {0x1D516,0x1D51C},
    {0x1D51E,0x1D539}, {0x1D53B,0x1D53E}, {0x1D540,0x1D544}, {0x1D546,0x1D546},
    {0x1D54A,0x1D550}, {0x1D552,0x1D6A5}, {0x1D6A8,0x1D6C0}, {0x1D6C2,0x1D6DA},
    {0x1D6DC,0x1D6FA}, {0x1D6FC,0x1D714}, {0x1D716,0x1D734}, {0x1D736,0x1D74E},
    {0x1D750,0x1D76E}, {0x1D770,0x1D788}, {0x1D78A,0x1D7A8}, {0x1D7AA,0x1D7C2},
    {0x1D7C4,0x1D7CB}, {0x1D7CE,0x1D7FF}, {0x1DF00,0x1DF1E}, {0x1DF25,0x1DF2A},
    {0x1E030,0x1E06D}, {0x1E100,0x1E12C}, {0x1E137,0x1E13D}, {0x1E140,0x1E149},
    {0x1E14E,0x1E14E}, {0x1E290,0x1E2AD}, {0x1E2C0,0x1E2EB}, {0x1E2F0,0x1E2F9},
    {0x1E4D0,0x1E4EB}, {0x1E4F0,0x1E4F9}, {0x1E7E0,0x1E7E6}, {0x1E7E8,0x1E7EB},
    {0x1E7ED,0x1E7EE}, {0x1E7F0,0x1E7FE}, {0x1E800,0x1E8C4}, {0x1E8C7,0x1E8CF},
    {0x1E900,0x1E943}, {0x1E94B,0x1E94B}, {0x1E950,0x1E959}, {0x1EC71,0x1ECAB},
    {0x1ECAD,0x1ECAF}, {0x1ECB1,0x1ECB4}, {0x1ED01,0x1ED2D}, {0x1ED2F,0x1ED3D},
    {0x1EE00,0x1EE03}, {0x1EE05,0x1EE1F}, {0x1EE21,0x1EE22}, {0x1EE24,0x1EE24},
    {0x1EE27,0x1EE27}, {0x1EE29,0x1EE32}, {0x1EE34,0x1EE37}, {0x1EE39,0x1EE39},
    {0x1EE3B,0x1EE3B}, {0x1EE42,0x1EE42}, {0x1EE47,0x1EE47}, {0x1EE49,0x1EE49},
    {0x1EE4B,0x1EE4B}, {0x1EE4D,0x1EE4F}, {0x1EE51,0x1EE52}, {0x1EE54,0x1EE54},
    {0x1EE57,0x1EE57}, {0x1EE59,0x1EE59}, {0x1EE5B,0x1EE5B}, {0x1EE5D,0x1EE5D},
    {0x1EE5F,0x1EE5F}, {0x1EE61,0x1EE62}, {0x1EE64,0x1EE64}, {0x1EE67,0x1EE6A},
    {0x1EE6C,0x1EE72}, {0x1EE74,0x1EE77}, {0x1EE79,0x1EE7C}, {0x1EE7E,0x1EE7E},
    {0x1EE80,0x1EE89}, {0x1EE8B,0x1EE9B}, {0x1EEA1,0x1EEA3}, {0x1EEA5,0x1EEA9},
    {0x1EEAB,0x1EEBB}, {0x1F100,0x1F10C}, {0x1FBF0,0x1FBF9}, {0x20000,0x2A6DF},
    {0x2A700,0x2B739}, {0x2B740,0x2B81D}, {0x2B820,0x2CEA1}, {0x2CEB0,0x2EBE0},
    {0x2F800,0x2FA1D}, {0x30000,0x3134A}, {0x31350,0x323AF},
};

static int am_unicode_in_ranges(uint32_t cp, const AM_UnicodeRange* ranges, size_t count) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < ranges[mid].first) hi = mid;
        else if (cp > ranges[mid].last) lo = mid + 1;
        else return 1;
    }
    return 0;
}

int am_codepoint_isalnum(int cp) {
    if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
    return am_unicode_in_ranges((uint32_t)cp, am_alnum_ranges,
                                 sizeof(am_alnum_ranges) / sizeof(am_alnum_ranges[0]));
}

static int am_unicode_is_cased(uint32_t cp) {
    return am_unicode_in_ranges(cp, am_cased_ranges,
        sizeof(am_cased_ranges) / sizeof(am_cased_ranges[0]));
}

static int am_unicode_is_case_ignorable(uint32_t cp) {
    return am_unicode_in_ranges(cp, am_case_ignorable_ranges,
        sizeof(am_case_ignorable_ranges) / sizeof(am_case_ignorable_ranges[0]));
}

static int am_unicode_simple_lower(int cp) {
    size_t lo = 0, hi = sizeof(am_lower_ranges) / sizeof(am_lower_ranges[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const AM_LowerRange* range = &am_lower_ranges[mid];
        if ((uint32_t)cp < range->first) hi = mid;
        else if ((uint32_t)cp > range->last) lo = mid + 1;
        else return ((uint32_t)cp - range->first) % range->stride == 0
            ? cp + range->delta : cp;
    }
    return cp;
}

// Called after the first pass validates the entire source. Each ignorable run
// is visited by at most one sigma lookahead, keeping both passes linear.
static int am_unicode_following_cased(const AM_String* text, int pos) {
    while (pos < text->byte_len) {
        int cp;
        int width = am_string_decode_utf8((const unsigned char*)text->data + pos,
                                          text->byte_len - pos, &cp);
        if (width < 0) return 0;
        if (!am_unicode_is_case_ignorable((uint32_t)cp))
            return am_unicode_is_cased((uint32_t)cp);
        pos += width;
    }
    return 0;
}

static int am_unicode_encoded_width(int cp) {
    return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
}

static int am_unicode_encode(char* dst, int cp) {
    if (cp < 0x80) {
        dst[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    dst[0] = (char)(0xF0 | (cp >> 18));
    dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

AM_String* am_string_lower(const AM_String* text) {
    if (!text || !text->data || text->byte_len < 0 ||
        text->byte_len > AM_MAX_STRING_BYTES) return NULL;
    int source_pos = 0, byte_len = 0, len = 0;
    while (source_pos < text->byte_len) {
        int cp;
        int width = am_string_decode_utf8((const unsigned char*)text->data + source_pos,
                                          text->byte_len - source_pos, &cp);
        if (width < 0) return NULL;
        source_pos += width;
        // Final sigma and ordinary sigma occupy the same two UTF-8 bytes.
        int produced = cp == 0x130 ? 3 : am_unicode_encoded_width(am_unicode_simple_lower(cp));
        if (produced > AM_MAX_STRING_BYTES - byte_len) return NULL;
        byte_len += produced;
        len += cp == 0x130 ? 2 : 1;
    }
    AM_String* out = am_string_alloc(byte_len, len);
    if (!out) return NULL;
    source_pos = 0;
    int destination_pos = 0, preceding_cased = 0;
    while (source_pos < text->byte_len) {
        int cp;
        int width = am_string_decode_utf8((const unsigned char*)text->data + source_pos,
                                          text->byte_len - source_pos, &cp);
        if (width < 0) { am_string_free(out); return NULL; }
        source_pos += width;
        int mapped = am_unicode_simple_lower(cp);
        if (cp == 0x3A3 && preceding_cased &&
            !am_unicode_following_cased(text, source_pos)) mapped = 0x3C2;
        if (cp == 0x130) {
            destination_pos += am_unicode_encode(out->data + destination_pos, 0x69);
            destination_pos += am_unicode_encode(out->data + destination_pos, 0x307);
        } else {
            destination_pos += am_unicode_encode(out->data + destination_pos, mapped);
        }
        // A codepoint may be both Cased and Case_Ignorable (for example U+0345).
        // Ignorable takes priority in the original-string sigma context.
        if (!am_unicode_is_case_ignorable((uint32_t)cp))
            preceding_cased = am_unicode_is_cased((uint32_t)cp);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MUTABLE STRING LISTS — copied containers, retained immutable UTF-8 items
// ═══════════════════════════════════════════════════════════════════════════════

AM_List* am_list_new(void) {
    AM_List* list = (AM_List*)malloc(sizeof(*list));
    if (!list) return NULL;
    memset(list, 0, sizeof(*list));
    list->refcount = 1;
    return list;
}

void am_list_ref(AM_List* list) {
    if (list) __atomic_add_fetch(&list->refcount, 1, __ATOMIC_RELAXED);
}

void am_list_free(AM_List* list) {
    if (!list || __atomic_sub_fetch(&list->refcount, 1, __ATOMIC_ACQ_REL) > 0) return;
    for (int i = 0; i < list->len; i++) am_string_free(list->items[i]);
    free(list->items);
    free(list);
}

static int am_list_index(const AM_List* list, int index) {
    if (!list) return -1;
    if (index < 0) index += list->len;
    return index >= 0 && index < list->len ? index : -1;
}

int am_list_push(AM_List* list, AM_String* item) {
    if (!list || !item || list->len >= AM_MAX_LIST_ITEMS) return -1;
    if (list->len == list->capacity) {
        int capacity = list->capacity ? list->capacity * 2 : 8;
        if (capacity > AM_MAX_LIST_ITEMS) capacity = AM_MAX_LIST_ITEMS;
        AM_String** items = (AM_String**)malloc((size_t)capacity * sizeof(*items));
        if (!items) return -1;
        if (list->len) memcpy(items, list->items, (size_t)list->len * sizeof(*items));
        free(list->items);
        list->items = items;
        list->capacity = capacity;
    }
    am_string_ref(item);
    list->items[list->len++] = item;
    return list->len;
}

AM_String* am_list_get(const AM_List* list, int index) {
    index = am_list_index(list, index);
    if (index < 0) return NULL;
    AM_String* item = list->items[index];
    am_string_ref(item);
    return item;
}

int am_list_set(AM_List* list, int index, AM_String* item) {
    index = am_list_index(list, index);
    if (index < 0 || !item) return -1;
    // Retain before releasing: replacing an item by itself is valid.
    am_string_ref(item);
    am_string_free(list->items[index]);
    list->items[index] = item;
    return 0;
}

int am_list_find(const AM_List* list, const AM_String* item) {
    if (!list || !item) return -1;
    for (int i = 0; i < list->len; i++)
        if (strcmp(list->items[i]->data, item->data) == 0) return i;
    return -1;
}

AM_List* am_list_slice(const AM_List* list, int start, int end) {
    if (!list) return NULL;
    start = am_string_clamp_index(start, list->len);
    end = am_string_clamp_index(end, list->len);
    AM_List* out = am_list_new();
    if (!out || end <= start) return out;
    int count = end - start;
    out->items = (AM_String**)malloc((size_t)count * sizeof(*out->items));
    if (!out->items) { am_list_free(out); return NULL; }
    out->capacity = count;
    for (int i = start; i < end; i++) {
        am_string_ref(list->items[i]);
        out->items[out->len++] = list->items[i];
    }
    return out;
}

AM_List* am_list_clone(const AM_List* list) {
    return list ? am_list_slice(list, 0, list->len) : NULL;
}

AM_List* am_list_sorted(const AM_List* list) {
    AM_List* out = am_list_clone(list);
    if (!out || out->len < 2) return out;
    AM_String** scratch = (AM_String**)malloc((size_t)out->len * sizeof(*scratch));
    if (!scratch) { am_list_free(out); return NULL; }
    AM_String** source = out->items;
    AM_String** destination = scratch;
    // Iterative stable merge: equal byte strings retain their input order.
    // Only out owns the retained references; scratch holds borrowed pointers.
    for (int width = 1; width < out->len; width *= 2) {
        for (int start = 0; start < out->len; start += 2 * width) {
            int middle = start + width;
            if (middle > out->len) middle = out->len;
            int end = middle + width;
            if (end > out->len) end = out->len;
            int left = start, right = middle;
            for (int i = start; i < end; i++) {
                if (left < middle && (right == end ||
                    strcmp(source[left]->data, source[right]->data) <= 0))
                    destination[i] = source[left++];
                else destination[i] = source[right++];
            }
        }
        AM_String** previous = source;
        source = destination;
        destination = previous;
    }
    if (source != out->items)
        memcpy(out->items, source, (size_t)out->len * sizeof(*out->items));
    free(scratch);
    return out;
}

static int am_decimal_digits(int n) {
    int digits = 1;
    while (n >= 10) { n /= 10; digits++; }
    return digits;
}

// Count + ':' then one byte-length + ':' + raw UTF-8 field per item.
// The byte lengths make every sequence unambiguous, including empty strings,
// delimiters, control characters, and multibyte Unicode. Nothing is escaped.
AM_String* am_list_key(const AM_List* list) {
    if (!list) return NULL;
    int byte_len = am_decimal_digits(list->len) + 1;
    int len = byte_len;
    for (int i = 0; i < list->len; i++) {
        const AM_String* item = list->items[i];
        int prefix = am_decimal_digits(item->byte_len) + 1;
        if (byte_len > AM_MAX_STRING_BYTES - prefix ||
            item->byte_len > AM_MAX_STRING_BYTES - byte_len - prefix) return NULL;
        byte_len += prefix + item->byte_len;
        len += prefix + item->len;
    }
    AM_String* out = am_string_alloc(byte_len, len);
    if (!out) return NULL;
    size_t capacity = (size_t)byte_len + 1;
    int written = snprintf(out->data, capacity, "%d:", list->len);
    if (written < 0 || (size_t)written >= capacity) goto failed;
    size_t offset = (size_t)written;
    for (int i = 0; i < list->len; i++) {
        const AM_String* item = list->items[i];
        if (offset >= capacity) goto failed;
        size_t remaining = capacity - offset;
        written = snprintf(out->data + offset, remaining, "%d:", item->byte_len);
        if (written < 0 || (size_t)written >= remaining) goto failed;
        offset += (size_t)written;
        if (item->byte_len < 0 || (size_t)item->byte_len >= capacity - offset)
            goto failed;
        memcpy(out->data + offset, item->data, (size_t)item->byte_len);
        offset += (size_t)item->byte_len;
    }
    if (offset != (size_t)byte_len) goto failed;
    out->data[offset] = 0;
    return out;
failed:
    am_string_free(out);
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ORDERED NUMERIC MAPS — hashed UTF-8 keys, insertion-order finite float values
// ═══════════════════════════════════════════════════════════════════════════════

static uint64_t am_map_hash(const AM_String* key) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < key->byte_len; i++) {
        hash ^= (unsigned char)key->data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

AM_Map* am_map_new(void) {
    AM_Map* map = (AM_Map*)malloc(sizeof(*map));
    if (!map) return NULL;
    memset(map, 0, sizeof(*map));
    map->refcount = 1;
    return map;
}

void am_map_ref(AM_Map* map) {
    if (map) __atomic_add_fetch(&map->refcount, 1, __ATOMIC_RELAXED);
}

void am_map_free(AM_Map* map) {
    if (!map || __atomic_sub_fetch(&map->refcount, 1, __ATOMIC_ACQ_REL) > 0) return;
    for (int i = 0; i < map->len; i++) am_string_free(map->entries[i].key);
    free(map->entries);
    free(map->buckets);
    free(map);
}

static int am_map_find(const AM_Map* map, const AM_String* key, uint64_t hash) {
    if (!map->bucket_capacity) return -1;
    unsigned int mask = (unsigned int)map->bucket_capacity - 1;
    unsigned int slot = (unsigned int)hash & mask;
    while (map->buckets[slot]) {
        int index = map->buckets[slot] - 1;
        const AM_MapEntry* entry = &map->entries[index];
        if (entry->hash == hash && entry->key->byte_len == key->byte_len &&
            !memcmp(entry->key->data, key->data, (size_t)key->byte_len)) return index;
        slot = (slot + 1) & mask;
    }
    return -1;
}

static void am_map_index_entry(int* buckets, int bucket_capacity,
                                const AM_MapEntry* entry, int index) {
    unsigned int mask = (unsigned int)bucket_capacity - 1;
    unsigned int slot = (unsigned int)entry->hash & mask;
    while (buckets[slot]) slot = (slot + 1) & mask;
    buckets[slot] = index + 1;
}

// Both replacement allocations and reindexing finish before either live
// buffer changes. A failed reserve preserves all map storage and references.
static int am_map_reserve(AM_Map* map, int need) {
    if (need <= map->capacity) return 0;
    if (need > AM_MAX_MAP_ITEMS) return -1;
    int capacity = map->capacity ? map->capacity : 8;
    while (capacity < need) capacity *= 2;
    int bucket_capacity = capacity * 2;
    AM_MapEntry* entries = (AM_MapEntry*)malloc((size_t)capacity * sizeof(*entries));
    if (!entries) return -1;
    int* buckets = (int*)calloc((size_t)bucket_capacity, sizeof(*buckets));
    if (!buckets) { free(entries); return -1; }
    if (map->len) memcpy(entries, map->entries, (size_t)map->len * sizeof(*entries));
    for (int i = 0; i < map->len; i++)
        am_map_index_entry(buckets, bucket_capacity, &entries[i], i);
    free(map->entries);
    free(map->buckets);
    map->entries = entries;
    map->capacity = capacity;
    map->buckets = buckets;
    map->bucket_capacity = bucket_capacity;
    return 0;
}

int am_map_has(const AM_Map* map, const AM_String* key) {
    return map && key && am_map_find(map, key, am_map_hash(key)) >= 0;
}

int am_map_get(const AM_Map* map, const AM_String* key, float* out) {
    if (!map || !key || !out) return -1;
    int index = am_map_find(map, key, am_map_hash(key));
    if (index < 0) return 0;
    *out = map->entries[index].value;
    return 1;
}

int am_map_set(AM_Map* map, AM_String* key, float value) {
    if (!map || !key || !isfinite(value)) return -1;
    uint64_t hash = am_map_hash(key);
    int index = am_map_find(map, key, hash);
    if (index >= 0) {
        map->entries[index].value = value;
        return 0;
    }
    if (map->len >= AM_MAX_MAP_ITEMS || am_map_reserve(map, map->len + 1)) return -1;
    AM_MapEntry* entry = &map->entries[map->len];
    am_string_ref(key);
    entry->key = key;
    entry->value = value;
    entry->hash = hash;
    am_map_index_entry(map->buckets, map->bucket_capacity, entry, map->len);
    map->len++;
    return 0;
}

int am_map_delete(AM_Map* map, const AM_String* key) {
    if (!map || !key) return -1;
    int index = am_map_find(map, key, am_map_hash(key));
    if (index < 0) return 0;
    am_string_free(map->entries[index].key);
    if (index + 1 < map->len)
        memmove(&map->entries[index], &map->entries[index + 1],
                (size_t)(map->len - index - 1) * sizeof(*map->entries));
    map->len--;
    memset(&map->entries[map->len], 0, sizeof(*map->entries));
    memset(map->buckets, 0, (size_t)map->bucket_capacity * sizeof(*map->buckets));
    for (int i = 0; i < map->len; i++)
        am_map_index_entry(map->buckets, map->bucket_capacity, &map->entries[i], i);
    return 1;
}

AM_Map* am_map_clone(const AM_Map* map) {
    if (!map) return NULL;
    AM_Map* out = am_map_new();
    if (!out) return NULL;
    if (am_map_reserve(out, map->len)) { am_map_free(out); return NULL; }
    for (int i = 0; i < map->len; i++) {
        out->entries[i] = map->entries[i];
        am_string_ref(out->entries[i].key);
        am_map_index_entry(out->buckets, out->bucket_capacity, &out->entries[i], i);
        out->len++;
    }
    return out;
}

AM_List* am_map_keys(const AM_Map* map) {
    if (!map) return NULL;
    AM_List* out = am_list_new();
    if (!out) return NULL;
    for (int i = 0; i < map->len; i++) {
        if (am_list_push(out, map->entries[i].key) < 0) {
            am_list_free(out);
            return NULL;
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FLAT TYPED RECORDS — explicit owners, detached copies, staged publication
// ═══════════════════════════════════════════════════════════════════════════════

static int am_record_text_valid(const AM_String* text) {
    return text && text->byte_len >= 0 && text->len >= 0 &&
        am_string_validate_bytes(text->data, (size_t)text->byte_len) == text->len;
}

static int am_record_array_valid(const AM_Array* array) {
    if (!array || !array->data || array->len <= 0 || array->len > AM_MAX_ARRAY_SIZE) return 0;
    if (array->rows == 0 && array->cols == 0) return 1;
    return array->rows > 0 && array->cols > 0 &&
        array->rows <= AM_MAX_ARRAY_SIZE / array->cols &&
        array->rows * array->cols == array->len;
}

static int am_record_leaf_clone(AML_Var* out, const AML_Var* value) {
    memset(out, 0, sizeof(*out));
    if (!value) return -1;
    out->type = value->type;
    switch (value->type) {
    case AML_TYPE_FLOAT:
        memcpy(&out->value, &value->value, sizeof(float));
        return 0;
    case AML_TYPE_ARRAY:
        if (!am_record_array_valid(value->array)) return -1;
#ifdef USE_CUDA
        ensure_cpu(value->array);
#endif
        out->array = am_array_clone(value->array);
        return out->array ? 0 : -1;
    case AML_TYPE_STRING:
        if (!am_record_text_valid(value->string)) return -1;
        out->string = value->string;
        am_string_ref(out->string);
        return 0;
    case AML_TYPE_LIST:
        if (!value->list || value->list->len < 0 || value->list->len > AM_MAX_LIST_ITEMS ||
            value->list->capacity < value->list->len ||
            (value->list->len && !value->list->items)) return -1;
        for (int i = 0; i < value->list->len; i++)
            if (!am_record_text_valid(value->list->items[i])) return -1;
        out->list = am_list_clone(value->list);
        return out->list ? 0 : -1;
    case AML_TYPE_MAP:
        if (!value->map || value->map->len < 0 || value->map->len > AM_MAX_MAP_ITEMS ||
            value->map->capacity < value->map->len ||
            (value->map->len && !value->map->entries)) return -1;
        for (int i = 0; i < value->map->len; i++)
            if (!am_record_text_valid(value->map->entries[i].key) ||
                !isfinite(value->map->entries[i].value)) return -1;
        out->map = am_map_clone(value->map);
        return out->map ? 0 : -1;
    default:
        return -1;
    }
}

AM_Record* am_record_new(void) {
    AM_Record* record = calloc(1, sizeof(*record));
    if (record) record->refcount = 1;
    return record;
}

void am_record_ref(AM_Record* record) {
    if (record) __atomic_add_fetch(&record->refcount, 1, __ATOMIC_RELAXED);
}

void am_record_free(AM_Record* record) {
    if (!record || __atomic_sub_fetch(&record->refcount, 1, __ATOMIC_ACQ_REL) > 0) return;
    for (int i = 0; i < record->len; i++) {
        am_string_free(record->entries[i].key);
        aml_value_clear(&record->entries[i].value);
    }
    free(record->entries);
    free(record);
}

static int am_record_index(const AM_Record* record, const AM_String* key) {
    if (!record || !key || !key->data || key->byte_len < 0) return -1;
    for (int i = 0; i < record->len; i++) {
        const AM_String* candidate = record->entries[i].key;
        if (candidate->byte_len == key->byte_len &&
            !memcmp(candidate->data, key->data, (size_t)key->byte_len)) return i;
    }
    return -1;
}

static int am_record_reserve(AM_Record* record, int count) {
    if (count < 0 || count > AM_MAX_RECORD_FIELDS) return -1;
    if (count <= record->capacity) return 0;
    int capacity = record->capacity ? record->capacity * 2 : 8;
    if (capacity < count) capacity = count;
    if (capacity > AM_MAX_RECORD_FIELDS) capacity = AM_MAX_RECORD_FIELDS;
    AM_RecordEntry* entries = realloc(record->entries, (size_t)capacity * sizeof(*entries));
    if (!entries) return -1;
    record->entries = entries;
    record->capacity = capacity;
    return 0;
}

int am_record_set(AM_Record* record, AM_String* key, const AML_Var* value) {
    if (!record || !am_record_text_valid(key)) return -1;
    int index = am_record_index(record, key);
    if (index < 0 && record->len == AM_MAX_RECORD_FIELDS) return -1;
    AML_Var copy = {0};
    if (am_record_leaf_clone(&copy, value)) return -1;
    if (index < 0) {
        if (am_record_reserve(record, record->len + 1)) {
            aml_value_clear(&copy);
            return -1;
        }
        index = record->len++;
        am_string_ref(key);
        record->entries[index].key = key;
    } else {
        aml_value_clear(&record->entries[index].value);
    }
    record->entries[index].value = copy;
    return 0;
}

const AML_Var* am_record_get(const AM_Record* record, const AM_String* key) {
    int index = am_record_index(record, key);
    return index < 0 ? NULL : &record->entries[index].value;
}

int am_record_has(const AM_Record* record, const AM_String* key) {
    return am_record_index(record, key) >= 0;
}

AM_List* am_record_keys(const AM_Record* record) {
    if (!record) return NULL;
    AM_List* keys = am_list_new();
    if (!keys) return NULL;
    for (int i = 0; i < record->len; i++) {
        if (am_list_push(keys, record->entries[i].key) < 0) {
            am_list_free(keys);
            return NULL;
        }
    }
    return keys;
}

AM_Record* am_record_clone(const AM_Record* record) {
    if (!record) return NULL;
    AM_Record* copy = am_record_new();
    if (!copy) return NULL;
    if (am_record_reserve(copy, record->len)) { am_record_free(copy); return NULL; }
    for (int i = 0; i < record->len; i++) {
        if (am_record_leaf_clone(&copy->entries[i].value, &record->entries[i].value)) {
            am_record_free(copy);
            return NULL;
        }
        copy->entries[i].key = record->entries[i].key;
        am_string_ref(copy->entries[i].key);
        copy->len++;
    }
    return copy;
}

int am_record_swap(AM_Record* a, AM_Record* b) {
    if (!a || !b) return -1;
    AM_RecordEntry* entries = a->entries;
    int len = a->len, capacity = a->capacity;
    a->entries = b->entries;
    a->len = b->len;
    a->capacity = b->capacity;
    b->entries = entries;
    b->len = len;
    b->capacity = capacity;
    return 0;
}

int am_record_replace(AM_Record* live, const AM_Record* checked) {
    if (!live || !checked) return -1;
    if (live == checked) return 0;
    AM_Record* next = am_record_clone(checked);
    if (!next) return -1;
    am_record_swap(live, next);
    am_record_free(next);
    return 0;
}

AM_String* am_tokenizer_identity(const AM_Tokenizer* model, char* error, size_t cap) {
    am_text_error(error, cap, "");
    if (!model || !model->model || !model->backend.identity) {
        am_text_error(error, cap, "tokenizer identity is unavailable"); return NULL;
    }
    const char* identity = model->backend.identity(model->model);
    if (!identity || strnlen(identity, 65) != 64) {
        am_text_error(error, cap, "invalid tokenizer SHA-256 identity"); return NULL;
    }
    for (int i = 0; i < 64; i++) {
        if (!((identity[i] >= '0' && identity[i] <= '9') ||
              (identity[i] >= 'a' && identity[i] <= 'f'))) {
            am_text_error(error, cap, "invalid tokenizer SHA-256 identity"); return NULL;
        }
    }
    AM_String* result = am_string_new_bytes(identity, 64);
    if (!result) am_text_error(error, cap, "tokenizer identity allocation failed");
    return result;
}

// Checkpoint v1 is independent of C struct layout and the internal value tags.
// Header: magic[8], version:u32, flags:u32, payload_bytes:u64, fields:u32,
// CRC32:u32; all integers little-endian. CRC covers header[0:28] + payload.
static const unsigned char am_checkpoint_magic[8] = {'A','M','L','C','P',0,'\r','\n'};

typedef struct {
    unsigned char* data;
    size_t pos, cap, items;
    int failed;
} AM_CheckpointWriter;

static void am_cp_u32(unsigned char* out, uint32_t value) {
    for (int i = 0; i < 4; i++) out[i] = (unsigned char)(value >> (8 * i));
}

static void am_cp_u64(unsigned char* out, uint64_t value) {
    for (int i = 0; i < 8; i++) out[i] = (unsigned char)(value >> (8 * i));
}

static uint32_t am_cp_get_u32(const unsigned char* in) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) value |= (uint32_t)in[i] << (8 * i);
    return value;
}

static uint64_t am_cp_get_u64(const unsigned char* in) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) value |= (uint64_t)in[i] << (8 * i);
    return value;
}

static uint32_t am_cp_crc_update(uint32_t crc, const unsigned char* data, size_t bytes) {
    // Per-call table avoids mutable global initialization and worker races.
    uint32_t table[256];
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t x = i;
        for (int bit = 0; bit < 8; bit++) x = (x >> 1) ^ (0xedb88320u & (0u - (x & 1u)));
        table[i] = x;
    }
    for (size_t i = 0; i < bytes; i++) crc = table[(crc ^ data[i]) & 255] ^ (crc >> 8);
    return crc;
}

static uint32_t am_cp_crc(const unsigned char* data, size_t bytes) {
    uint32_t crc = am_cp_crc_update(0xffffffffu, data, 28);
    return am_cp_crc_update(crc, data + 32, bytes - 32) ^ 0xffffffffu;
}

static void am_cp_write(AM_CheckpointWriter* writer, const void* bytes, size_t count) {
    if (writer->failed || count > writer->cap - writer->pos) { writer->failed = 1; return; }
    if (writer->data && count) memcpy(writer->data + writer->pos, bytes, count);
    writer->pos += count;
}

static void am_cp_write_u32(AM_CheckpointWriter* writer, uint32_t value) {
    unsigned char bytes[4];
    am_cp_u32(bytes, value);
    am_cp_write(writer, bytes, sizeof(bytes));
}

static void am_cp_write_float(AM_CheckpointWriter* writer, const float* value) {
    uint32_t bits;
    memcpy(&bits, value, sizeof(bits));
    am_cp_write_u32(writer, bits);
}

static void am_cp_write_text(AM_CheckpointWriter* writer, const AM_String* text, int length) {
    if (!am_record_text_valid(text) || writer->items == AM_MAX_CHECKPOINT_ITEMS) {
        writer->failed = 1; return;
    }
    writer->items++;
    if (length) am_cp_write_u32(writer, (uint32_t)text->byte_len);
    am_cp_write(writer, text->data, (size_t)text->byte_len);
}

static int am_cp_tag(int type) {
    switch (type) {
    case AML_TYPE_FLOAT: return 1;
    case AML_TYPE_STRING: return 2;
    case AML_TYPE_ARRAY: return 3;
    case AML_TYPE_LIST: return 4;
    case AML_TYPE_MAP: return 5;
    default: return 0;
    }
}

static void am_cp_write_value(AM_CheckpointWriter* writer, const AML_Var* value) {
    switch (value->type) {
    case AML_TYPE_FLOAT:
        am_cp_write_float(writer, &value->value);
        break;
    case AML_TYPE_STRING:
        am_cp_write_text(writer, value->string, 0);
        break;
    case AML_TYPE_ARRAY: {
        AM_Array* array = value->array;
        if (!am_record_array_valid(array)) { writer->failed = 1; return; }
#ifdef USE_CUDA
        ensure_cpu(array);
#endif
        am_cp_write_u32(writer, (uint32_t)array->len);
        am_cp_write_u32(writer, (uint32_t)array->rows);
        am_cp_write_u32(writer, (uint32_t)array->cols);
        for (int i = 0; i < array->len && !writer->failed; i++)
            am_cp_write_float(writer, &array->data[i]);
        break;
    }
    case AML_TYPE_LIST: {
        const AM_List* list = value->list;
        if (!list || list->len < 0 || list->len > AM_MAX_LIST_ITEMS ||
            list->capacity < list->len || (list->len && !list->items)) {
            writer->failed = 1; return;
        }
        am_cp_write_u32(writer, (uint32_t)list->len);
        for (int i = 0; i < list->len && !writer->failed; i++)
            am_cp_write_text(writer, list->items[i], 1);
        break;
    }
    case AML_TYPE_MAP: {
        const AM_Map* map = value->map;
        if (!map || map->len < 0 || map->len > AM_MAX_MAP_ITEMS ||
            map->capacity < map->len || (map->len && !map->entries)) {
            writer->failed = 1; return;
        }
        am_cp_write_u32(writer, (uint32_t)map->len);
        for (int i = 0; i < map->len && !writer->failed; i++) {
            if (!isfinite(map->entries[i].value)) { writer->failed = 1; return; }
            am_cp_write_text(writer, map->entries[i].key, 1);
            am_cp_write_float(writer, &map->entries[i].value);
        }
        break;
    }
    default:
        writer->failed = 1;
    }
}

static int am_cp_encode(const AM_Record* record, AM_CheckpointWriter* writer) {
    if (!record || record->len < 0 || record->len > AM_MAX_RECORD_FIELDS ||
        record->capacity < record->len || (record->len && !record->entries)) return -1;
    unsigned char header[32] = {0};
    memcpy(header, am_checkpoint_magic, sizeof(am_checkpoint_magic));
    am_cp_u32(header + 8, 1);
    am_cp_u32(header + 24, (uint32_t)record->len);
    am_cp_write(writer, header, sizeof(header));
    for (int i = 0; i < record->len && !writer->failed; i++) {
        const AM_RecordEntry* entry = &record->entries[i];
        int tag = am_cp_tag(entry->value.type);
        if (!tag || !am_record_text_valid(entry->key)) return -1;
        unsigned char field[16] = {0};
        am_cp_u32(field, (uint32_t)entry->key->byte_len);
        field[4] = (unsigned char)tag;
        size_t start = writer->pos;
        am_cp_write(writer, field, sizeof(field));
        am_cp_write_text(writer, entry->key, 0);
        size_t body = writer->pos;
        am_cp_write_value(writer, &entry->value);
        if (writer->data && !writer->failed)
            am_cp_u64(writer->data + start + 8, (uint64_t)(writer->pos - body));
    }
    if (writer->failed) return -1;
    if (writer->data) {
        am_cp_u64(writer->data + 16, (uint64_t)(writer->pos - 32));
        am_cp_u32(writer->data + 28, am_cp_crc(writer->data, writer->pos));
    }
    return 0;
}

typedef struct {
    const unsigned char* data;
    size_t pos, end, items;
    const char* failure;
} AM_CheckpointReader;

static const unsigned char* am_cp_read(AM_CheckpointReader* reader, size_t count) {
    if (reader->failure || count > reader->end - reader->pos) {
        if (!reader->failure) reader->failure = "checkpoint truncated value";
        return NULL;
    }
    const unsigned char* data = reader->data + reader->pos;
    reader->pos += count;
    return data;
}

static uint32_t am_cp_read_u32(AM_CheckpointReader* reader) {
    const unsigned char* data = am_cp_read(reader, 4);
    return data ? am_cp_get_u32(data) : 0;
}

static AM_String* am_cp_read_text(AM_CheckpointReader* reader, uint32_t bytes) {
    if (bytes > AM_MAX_STRING_BYTES || reader->items == AM_MAX_CHECKPOINT_ITEMS) {
        reader->failure = "checkpoint string/item limit exceeded"; return NULL;
    }
    const unsigned char* data = am_cp_read(reader, bytes);
    if (!data) return NULL;
    if (am_string_validate_bytes((const char*)data, bytes) < 0) {
        reader->failure = "checkpoint invalid UTF-8 or NUL"; return NULL;
    }
    reader->items++;
    AM_String* text = am_string_new_bytes((const char*)data, bytes);
    if (!text) reader->failure = "checkpoint string allocation failed";
    return text;
}

static int am_cp_read_value(AM_CheckpointReader* reader, int tag, AML_Var* value) {
    memset(value, 0, sizeof(*value));
    if (tag == 1) {
        if (reader->end - reader->pos != 4) goto invalid;
        uint32_t bits = am_cp_read_u32(reader);
        memcpy(&value->value, &bits, sizeof(bits));
    } else if (tag == 2) {
        size_t bytes = reader->end - reader->pos;
        if (bytes > AM_MAX_STRING_BYTES) goto invalid;
        value->type = AML_TYPE_STRING;
        value->string = am_cp_read_text(reader, (uint32_t)bytes);
        if (!value->string) return -1;
    } else if (tag == 3) {
        uint32_t len = am_cp_read_u32(reader);
        uint32_t rows = am_cp_read_u32(reader);
        uint32_t cols = am_cp_read_u32(reader);
        if (reader->failure) return -1;
        if (!len || len > AM_MAX_ARRAY_SIZE ||
            !((!rows && !cols) || (rows && cols && rows <= AM_MAX_ARRAY_SIZE / cols && rows * cols == len)) ||
            reader->end - reader->pos != (size_t)len * 4) goto invalid;
        value->type = AML_TYPE_ARRAY;
        value->array = am_array_new((int)len);
        if (!value->array) goto allocation;
        value->array->rows = (int)rows;
        value->array->cols = (int)cols;
        for (uint32_t i = 0; i < len; i++) {
            uint32_t bits = am_cp_read_u32(reader);
            memcpy(&value->array->data[i], &bits, sizeof(bits));
        }
    } else if (tag == 4 || tag == 5) {
        uint32_t count = am_cp_read_u32(reader);
        if (reader->failure) return -1;
        if (count > (uint32_t)(tag == 4 ? AM_MAX_LIST_ITEMS : AM_MAX_MAP_ITEMS) ||
            count > AM_MAX_CHECKPOINT_ITEMS - reader->items ||
            count > (reader->end - reader->pos) / (tag == 4 ? 4 : 8)) goto invalid;
        value->type = tag == 4 ? AML_TYPE_LIST : AML_TYPE_MAP;
        if (tag == 4) { if (!(value->list = am_list_new())) goto allocation; }
        else { if (!(value->map = am_map_new())) goto allocation; }
        for (uint32_t i = 0; i < count; i++) {
            uint32_t bytes = am_cp_read_u32(reader);
            if (reader->failure) return -1;
            AM_String* item = am_cp_read_text(reader, bytes);
            if (!item) return -1;
            if (tag == 4) {
                int status = am_list_push(value->list, item);
                am_string_free(item);
                if (status < 0) goto allocation;
            } else {
                uint32_t bits = am_cp_read_u32(reader);
                float number;
                memcpy(&number, &bits, sizeof(bits));
                if (reader->failure || !isfinite(number) || am_map_has(value->map, item)) {
                    am_string_free(item);
                    goto invalid;
                }
                int status = am_map_set(value->map, item, number);
                am_string_free(item);
                if (status) goto allocation;
            }
        }
    } else goto invalid;
    if (reader->failure) return -1;
    if (reader->pos != reader->end) goto invalid;
    return 0;
invalid:
    if (!reader->failure) reader->failure = "checkpoint invalid type, shape, count, or duplicate key";
    return -1;
allocation:
    reader->failure = "checkpoint value allocation failed";
    return -1;
}

static AM_Record* am_cp_decode(const unsigned char* data, size_t bytes,
                               char* error, size_t cap) {
    if (bytes < 32 || bytes > AM_MAX_CHECKPOINT_BYTES ||
        memcmp(data, am_checkpoint_magic, 8) || am_cp_get_u32(data + 8) != 1 ||
        am_cp_get_u32(data + 12) != 0 || am_cp_get_u64(data + 16) != bytes - 32 ||
        am_cp_get_u32(data + 24) > AM_MAX_RECORD_FIELDS ||
        am_cp_get_u32(data + 28) != am_cp_crc(data, bytes)) {
        am_text_error(error, cap, "checkpoint invalid header, size, version, or checksum");
        return NULL;
    }
    AM_Record* record = am_record_new();
    uint32_t count = am_cp_get_u32(data + 24);
    if (!record || am_record_reserve(record, (int)count)) {
        am_record_free(record);
        am_text_error(error, cap, "checkpoint record allocation failed"); return NULL;
    }
    AM_CheckpointReader reader = {data, 32, bytes, 0, NULL};
    for (uint32_t i = 0; i < count && !reader.failure; i++) {
        const unsigned char* field = am_cp_read(&reader, 16);
        if (!field) break;
        uint32_t key_bytes = am_cp_get_u32(field);
        uint64_t value_bytes = am_cp_get_u64(field + 8);
        if (field[4] < 1 || field[4] > 5 || field[5] || field[6] || field[7]) {
            reader.failure = "checkpoint unknown type or nonzero reserved field"; break;
        }
        AM_String* key = am_cp_read_text(&reader, key_bytes);
        if (!key) break;
        if (value_bytes > reader.end - reader.pos || am_record_has(record, key)) {
            am_string_free(key);
            reader.failure = "checkpoint invalid value length or duplicate field"; break;
        }
        AM_CheckpointReader body = {data, reader.pos, reader.pos + (size_t)value_bytes, reader.items, NULL};
        AML_Var value = {0};
        if (am_cp_read_value(&body, field[4], &value)) {
            aml_value_clear(&value);
            am_string_free(key);
            reader.failure = body.failure; break;
        }
        record->entries[record->len].key = key;
        record->entries[record->len].value = value;
        record->len++;
        reader.pos = body.pos;
        reader.items = body.items;
    }
    if (!reader.failure && reader.pos != bytes) reader.failure = "checkpoint trailing payload bytes";
    if (reader.failure) {
        am_record_free(record);
        am_text_error(error, cap, reader.failure); return NULL;
    }
    return record;
}

static int am_checkpoint_host_valid(void) {
    return sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128;
}

static int am_checkpoint_path_valid(const char* path) {
    if (!path || !*path) return 0;
    size_t bytes = strnlen(path, AM_MAX_STRING_BYTES + 1);
    return bytes <= AM_MAX_STRING_BYTES && am_string_validate_bytes(path, bytes) >= 0;
}

static void am_checkpoint_io_error(char* error, size_t cap, const char* operation, int number) {
    if (error && cap) snprintf(error, cap, "checkpoint %s: %s", operation, strerror(number));
}

int am_file_exists(const char* path, char* error, size_t cap) {
    am_text_error(error, cap, "");
    if (!am_checkpoint_path_valid(path)) {
        am_text_error(error, cap, "file_exists requires a nonempty UTF-8 path"); return -1;
    }
    struct stat st;
    if (stat(path, &st)) {
        int number = errno;
        if (number == ENOENT || number == ENOTDIR) return 0;
        am_checkpoint_io_error(error, cap, "file_exists failed", number); return -1;
    }
    if (!S_ISREG(st.st_mode)) { am_text_error(error, cap, "file_exists path is not a regular file"); return -1; }
    return 1;
}

AM_Record* am_checkpoint_load(const char* path, char* error, size_t cap) {
    am_text_error(error, cap, "");
    if (!am_checkpoint_host_valid() || !am_checkpoint_path_valid(path)) {
        am_text_error(error, cap, "checkpoint requires IEEE binary32 and a nonempty UTF-8 path"); return NULL;
    }
    // O_NONBLOCK prevents an unexpected FIFO path from blocking before fstat.
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { am_checkpoint_io_error(error, cap, "open failed", errno); return NULL; }
    struct stat st;
    if (fstat(fd, &st)) {
        int number = errno;
        close(fd);
        am_checkpoint_io_error(error, cap, "stat failed", number); return NULL;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 32 || (uint64_t)st.st_size > AM_MAX_CHECKPOINT_BYTES) {
        close(fd);
        am_text_error(error, cap, "checkpoint requires a regular file of 32 bytes to 64 MiB"); return NULL;
    }
    size_t bytes = (size_t)st.st_size, pos = 0;
    unsigned char* data = malloc(bytes);
    if (!data) { close(fd); am_text_error(error, cap, "checkpoint input allocation failed"); return NULL; }
    int number = 0;
    while (pos < bytes) {
        ssize_t count = read(fd, data + pos, bytes - pos);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { number = count < 0 ? errno : EIO; break; }
        pos += (size_t)count;
    }
    if (!number) {
        unsigned char extra;
        ssize_t count;
        do { count = read(fd, &extra, 1); } while (count < 0 && errno == EINTR);
        if (count != 0) number = count < 0 ? errno : EIO;
    }
    if (close(fd) && !number) number = errno;
    if (number) {
        free(data);
        am_checkpoint_io_error(error, cap, "read/close failed", number); return NULL;
    }
    AM_Record* record = am_cp_decode(data, bytes, error, cap);
    free(data);
    return record;
}

int am_checkpoint_save(const AM_Record* record, const char* path, char* error, size_t cap) {
    am_text_error(error, cap, "");
    if (!am_checkpoint_host_valid() || !am_checkpoint_path_valid(path)) {
        am_text_error(error, cap, "checkpoint requires IEEE binary32 and a nonempty UTF-8 path"); return -1;
    }
    AM_CheckpointWriter measure = {NULL, 0, AM_MAX_CHECKPOINT_BYTES, 0, 0};
    if (am_cp_encode(record, &measure)) {
        am_text_error(error, cap, "checkpoint invalid record or byte/item limit exceeded"); return -1;
    }
    unsigned char* data = malloc(measure.pos);
    if (!data) { am_text_error(error, cap, "checkpoint output allocation failed"); return -1; }
    AM_CheckpointWriter writer = {data, 0, measure.pos, 0, 0};
    if (am_cp_encode(record, &writer) || writer.pos != measure.pos) {
        free(data);
        am_text_error(error, cap, "checkpoint record changed during save"); return -1;
    }
    const char* slash = strrchr(path, '/');
    size_t directory_bytes = slash ? (size_t)(slash - path) : 1;
    if (slash == path) directory_bytes = 1;
    if (slash && !slash[1]) {
        free(data);
        am_text_error(error, cap, "checkpoint destination needs a file name"); return -1;
    }
    char* directory = malloc(directory_bytes + 1);
    static const char suffix[] = "/.aml-checkpoint-XXXXXX";
    char* temporary = malloc(directory_bytes + sizeof(suffix));
    if (!directory || !temporary) {
        free(data); free(directory); free(temporary);
        am_text_error(error, cap, "checkpoint path allocation failed"); return -1;
    }
    if (slash) memcpy(directory, path, directory_bytes);
    else directory[0] = '.';
    directory[directory_bytes] = 0;
    memcpy(temporary, directory, directory_bytes);
    memcpy(temporary + directory_bytes, suffix, sizeof(suffix));
    int fd = mkstemp(temporary);
    if (fd < 0) {
        int number = errno;
        free(data); free(directory); free(temporary);
        am_checkpoint_io_error(error, cap, "temporary create failed", number); return -1;
    }
    int number = 0;
    size_t pos = 0;
    while (pos < writer.pos) {
        ssize_t count = write(fd, data + pos, writer.pos - pos);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { number = count < 0 ? errno : EIO; break; }
        pos += (size_t)count;
    }
    if (!number && fsync(fd)) number = errno;
    if (close(fd) && !number) number = errno;
    free(data);
    if (!number && rename(temporary, path)) number = errno;
    if (number) {
        unlink(temporary);
        free(directory); free(temporary);
        am_checkpoint_io_error(error, cap, "save before commit failed", number); return -1;
    }
    // Rename is the commit point. From here, failures report status 2: the new
    // complete checkpoint is visible, with directory durability unconfirmed.
    free(temporary);
    int dir_fd = open(directory, O_RDONLY);
    free(directory);
    if (dir_fd < 0) {
        am_checkpoint_io_error(error, cap, "committed; directory open failed", errno); return 2;
    }
    int sync_error = fsync(dir_fd) ? errno : 0;
    if (close(dir_fd) && !sync_error) sync_error = errno;
    if (sync_error) {
        am_checkpoint_io_error(error, cap, "committed; directory sync/close failed", sync_error); return 2;
    }
    return 1;
}
