#include <stdint.h>
#include <stddef.h>

/*
 * InfiniD v0.1
 * Long-memory overdub delay / feedback looper for Korg NTS-1 digital mkI.
 * Target module: DELAY FX.
 *
 * TIME        -> LOOP:     physical circular-loop length (~0.125 ... 25 s)
 * DEPTH       -> OVERDUB:  amount of new input layered into the loop
 * SHIFT+DEPTH -> DECAY:    old-layer lifetime, up to true infinite feedback
 *
 * The buffer is mono int16 at 48 kHz. Long decay does not require storing
 * minutes of audio: each loop pass rewrites old * feedback + new * overdub.
 */

#define USER_API_VERSION 0x00010100u
#define USER_TARGET_PLATFORM_BYTE 0x03u
#define __sdram __attribute__((section(".sdram")))

#define SAMPLE_RATE 48000.0f
#define BUFFER_SAMPLES 1200000u  /* 2.4 MB => exactly 25 s at 48 kHz */
#define MIN_LOOP_SAMPLES 6000u   /* 125 ms */

#define k_user_delfx_param_time 0u
#define k_user_delfx_param_depth 1u
#define k_user_delfx_param_reserved0 2u
#define k_user_delfx_param_shift_depth 3u

typedef void (*UserDelFXFuncEntry)(uint32_t, uint32_t);
typedef void (*UserDelFXFuncProcess)(float *, uint32_t);
typedef void (*UserDelFXFuncVoid)(void);
typedef void (*UserDelFXFuncParam)(uint8_t, int32_t);

#pragma pack(push, 1)
typedef struct {
  uint8_t magic[4];
  uint32_t api;
  uint8_t platform;
  uint8_t reserved0[7];
  UserDelFXFuncEntry func_entry;
  UserDelFXFuncProcess func_process;
  UserDelFXFuncVoid func_suspend;
  UserDelFXFuncVoid func_resume;
  UserDelFXFuncParam func_param;
  UserDelFXFuncVoid reserved1[7];
} user_delfx_hook_table_t;
#pragma pack(pop)

extern uint8_t _bss_start;
extern uint8_t _bss_end;

void _entry(uint32_t platform, uint32_t api);
void _hook_init(uint32_t platform, uint32_t api);
void _hook_process(float *xn, uint32_t frames);
void _hook_suspend(void);
void _hook_resume(void);
void _hook_param(uint8_t index, int32_t value);

__attribute__((used, section(".hooks")))
static const user_delfx_hook_table_t s_hook_table = {
  .magic = {'U','D','E','L'},
  .api = USER_API_VERSION,
  .platform = USER_TARGET_PLATFORM_BYTE,
  .reserved0 = {0},
  .func_entry = _entry,
  .func_process = _hook_process,
  .func_suspend = _hook_suspend,
  .func_resume = _hook_resume,
  .func_param = _hook_param,
  .reserved1 = {0}
};

static int16_t g_loop[BUFFER_SAMPLES] __sdram;
static uint32_t g_write;
static uint32_t g_loop_samples;

static float g_loop_control;
static float g_overdub;
static float g_overdub_target;
static float g_decay_control;
static float g_feedback;

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline float q31_to_unit(int32_t v) {
  float x = (float)v * 4.656612873077392578125e-10f;
  return clamp01(x);
}

static inline float absf_fast(float x) {
  return (x < 0.0f) ? -x : x;
}

/* Linear below 0.80, then progressively bends toward +/-1.0. This leaves
 * ordinary layers clean and only protects the accumulator near overload. */
static inline float safety_limit(float x) {
  const float a = absf_fast(x);
  if (a <= 0.80f) return x;

  const float t = (a - 0.80f) * 5.0f;
  const float y = 0.80f + 0.20f * (t / (1.0f + t));
  return (x < 0.0f) ? -y : y;
}

static inline float i16_to_float(int16_t s) {
  return (float)s * (1.0f / 32768.0f);
}

static inline int16_t float_to_i16(float x) {
  if (x > 0.9999695f) x = 0.9999695f;
  if (x < -1.0f) x = -1.0f;
  return (int16_t)(x * 32767.0f);
}

/* exp(-x) approximation with range reduction. For the long decay region x is
 * tiny; for short decay/long loop it also behaves sensibly down toward zero. */
static float exp_neg_approx(float x) {
  if (x <= 0.0f) return 1.0f;
  if (x >= 16.0f) return 0.0f;

  const float y = x * 0.0625f; /* x / 16 */
  const float y2 = y * y;
  const float y3 = y2 * y;
  const float y4 = y2 * y2;
  const float denom = 1.0f + y + 0.5f * y2 + 0.16666667f * y3 + 0.04166667f * y4;
  float e = 1.0f / denom;
  e *= e;
  e *= e;
  e *= e;
  e *= e;
  return e;
}

/* Piecewise curve chosen around useful musical landmarks. Values are T60,
 * i.e. time for a layer to fall by roughly 60 dB. */
static float decay_seconds(float d) {
  if (d < 0.25f) {
    const float t = d * 4.0f;
    return 8.0f + (30.0f - 8.0f) * t;
  }
  if (d < 0.50f) {
    const float t = (d - 0.25f) * 4.0f;
    return 30.0f + (120.0f - 30.0f) * t;
  }
  if (d < 0.75f) {
    const float t = (d - 0.50f) * 4.0f;
    return 120.0f + (600.0f - 120.0f) * t;
  }
  if (d < 0.90f) {
    const float t = (d - 0.75f) * (1.0f / 0.15f);
    return 600.0f + (1800.0f - 600.0f) * t;
  }
  if (d < 0.98f) {
    const float t = (d - 0.90f) * 12.5f;
    return 1800.0f + (7200.0f - 1800.0f) * t;
  }
  /* 98..99.95% stretches from ~2 h toward ~6 h before infinity. */
  {
    const float t = (d - 0.98f) * (1.0f / 0.0195f);
    const float tc = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
    return 7200.0f + (21600.0f - 7200.0f) * tc;
  }
}

static void update_feedback(void) {
  if (g_decay_control >= 0.9995f) {
    g_feedback = 1.0f;
    return;
  }

  const float loop_sec = (float)g_loop_samples * (1.0f / SAMPLE_RATE);
  const float t60 = decay_seconds(g_decay_control);
  const float exponent = 6.9077553f * loop_sec / t60;
  g_feedback = exp_neg_approx(exponent);

  if (g_feedback < 0.0f) g_feedback = 0.0f;
  if (g_feedback > 1.0f) g_feedback = 1.0f;
}

static void update_loop_length(float x) {
  /* Cubic control gives much more travel to sub-second / few-second loops. */
  const float x2 = x * x;
  const float x3 = x2 * x;
  const float span = (float)(BUFFER_SAMPLES - MIN_LOOP_SAMPLES);
  uint32_t n = MIN_LOOP_SAMPLES + (uint32_t)(span * x3);
  if (n < MIN_LOOP_SAMPLES) n = MIN_LOOP_SAMPLES;
  if (n > BUFFER_SAMPLES) n = BUFFER_SAMPLES;
  g_loop_samples = n;
  if (g_write >= g_loop_samples) g_write %= g_loop_samples;
  update_feedback();
}

__attribute__((used))
void _entry(uint32_t platform, uint32_t api) {
  volatile uint8_t *p = (volatile uint8_t *)&_bss_start;
  volatile uint8_t *e = (volatile uint8_t *)&_bss_end;
  while (p != e) *p++ = 0u;
  _hook_init(platform, api);
}

__attribute__((used))
void _hook_init(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;

  for (uint32_t i = 0; i < BUFFER_SAMPLES; ++i)
    g_loop[i] = 0;

  g_write = 0u;
  g_loop_control = 0.55f;
  g_overdub = g_overdub_target = 0.50f;
  g_decay_control = 0.75f;
  g_loop_samples = MIN_LOOP_SAMPLES;
  g_feedback = 0.95f;
  update_loop_length(g_loop_control);
}

__attribute__((used))
void _hook_process(float *xn, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    g_overdub += (g_overdub_target - g_overdub) * 0.0010f;

    const uint32_t k = i << 1;
    const float in_l = xn[k];
    const float in_r = xn[k + 1u];
    const float input = 0.5f * (in_l + in_r);

    const float old = i16_to_float(g_loop[g_write]);
    const float layered = old * g_feedback + input * g_overdub;
    const float stored = safety_limit(layered);

    g_loop[g_write] = float_to_i16(stored);

    /* Output the newly layered cell: new input is audible immediately, while
     * earlier material returns whenever the circular head reaches it again. */
    xn[k] = stored;
    xn[k + 1u] = stored;

    ++g_write;
    if (g_write >= g_loop_samples) g_write = 0u;
  }
}

__attribute__((used))
void _hook_suspend(void) {}

__attribute__((used))
void _hook_resume(void) {}

__attribute__((used))
void _hook_param(uint8_t index, int32_t value) {
  const float x = q31_to_unit(value);

  if (index == k_user_delfx_param_time) {
    g_loop_control = x;
    update_loop_length(x);
  } else if (index == k_user_delfx_param_depth) {
    g_overdub_target = x;
  } else if (index == k_user_delfx_param_shift_depth) {
    g_decay_control = x;
    update_feedback();
  }
}
