#include <stdint.h>
#include <stddef.h>

/*
 * GlitchB v0.2
 * Chaotic live buffer scrambler / crusher for Korg NTS-1 digital mkI.
 * Target module: DELAY FX.
 *
 * TIME  -> CRUSH: clean buffer scrambling -> hard bit/sample-rate destruction
 * DEPTH -> SPEED: slow roaming chunks -> hyperactive micro-fragments
 * SHIFT+DEPTH -> HOLD: normal roaming -> repeated fragment -> full freeze
 *
 * HOLD is deliberately not a dry/wet mix. At intermediate values it raises
 * the probability of replaying the same captured fragment. At 100% it stops
 * writing the live buffer and locks playback to the currently captured region.
 * Output is intentionally 100% wet.
 */

#define USER_API_VERSION 0x00010100u
#define USER_TARGET_PLATFORM_BYTE 0x03u
#define __sdram __attribute__((section(".sdram")))

#define BUF_SIZE 65536u
#define BUF_MASK (BUF_SIZE - 1u)
#define MIN_HISTORY 192u

#define k_user_delfx_param_time 0u
#define k_user_delfx_param_depth 1u
#define k_user_delfx_param_reserved0 2u
#define k_user_delfx_param_shift_depth 3u

/* --- NTS-1 mkI delay hook ABI --- */
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

/* --- Audio state --- */
static float g_buf_l[BUF_SIZE] __sdram;
static float g_buf_r[BUF_SIZE] __sdram;
static uint32_t g_write;
static uint32_t g_filled;

static float g_crush;
static float g_crush_target;
static float g_speed;
static float g_speed_target;
static float g_hold_amount;
static float g_hold_target;

static uint32_t g_rng;
static float g_read_pos;
static float g_step;
static uint32_t g_grain_left;
static uint32_t g_grain_total;
static uint32_t g_xfade_left;
static uint32_t g_xfade_total;
static float g_prev_l;
static float g_prev_r;

/* HOLD state: the currently captured fragment can be replayed many times
 * without choosing a new region. Full HOLD additionally stops recording.
 */
static float g_fragment_anchor;
static float g_fragment_step;
static uint32_t g_fragment_len;
static uint8_t g_fragment_valid;
static uint8_t g_freeze_active;

static uint32_t g_hold_count;
static float g_hold_l;
static float g_hold_r;

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline float q31_to_unit(int32_t v) {
  float x = (float)v * 4.656612873077392578125e-10f; /* 1 / 2^31 */
  return clamp01(x);
}

static inline uint32_t rng32(void) {
  uint32_t x = g_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_rng = x;
  return x;
}

static inline float rand01(void) {
  return (float)(rng32() >> 8) * (1.0f / 16777216.0f);
}

static inline float wrap_pos(float p) {
  while (p >= (float)BUF_SIZE) p -= (float)BUF_SIZE;
  while (p < 0.0f) p += (float)BUF_SIZE;
  return p;
}

static inline float read_interp(const float *buf, float pos) {
  pos = wrap_pos(pos);
  const uint32_t i0 = ((uint32_t)pos) & BUF_MASK;
  const uint32_t i1 = (i0 + 1u) & BUF_MASK;
  const float frac = pos - (float)i0;
  return buf[i0] + (buf[i1] - buf[i0]) * frac;
}

static inline float hard_clip(float x) {
  if (x > 1.0f) return 1.0f;
  if (x < -1.0f) return -1.0f;
  return x;
}

/* Quantization is bypassed at the very bottom of CRUSH. Above that, the
 * number of amplitude steps falls continuously toward a brutal 2-3 bit zone.
 */
static inline float crush_sample(float x, float c) {
  if (c < 0.035f) return x;

  x = hard_clip(x);

  /* scale ~= 32768 at low crush, falling to 2 at full crush.  The polynomial
   * keeps the first half useful instead of instantly sounding like 8-bit.
   */
  /* Roughly: 16-bit-ish at the bottom, ~12 bit around the middle,
   * ~6 bit near 80%, and ~2-3 bit in the final part of the knob.
   */
  const float inv = 1.0f - c;
  const float inv2 = inv * inv;
  float scale_f = 32768.0f * inv2 * inv2 + 2.0f;
  if (scale_f < 2.0f) scale_f = 2.0f;

  const int32_t q = (int32_t)(x * scale_f);
  return (float)q / scale_f;
}

static inline uint32_t base_grain_length(float s) {
  /* Preserve the v0.1 SPEED response: about 0.65 s at the far left and a
   * couple of milliseconds at the far right. */
  const float inv = 1.0f - s;
  const float inv3 = inv * inv * inv;
  float base_len = 96.0f + 31200.0f * inv3;
  uint32_t grain = (uint32_t)base_len;
  if (grain < 64u) grain = 64u;
  if (grain > 36000u) grain = 36000u;
  return grain;
}

static void setup_join(float c, uint32_t grain) {
  /* Soft joins at low CRUSH; almost hard discontinuities at high CRUSH. */
  float xf = (1.0f - c);
  xf = xf * xf;
  uint32_t xfade = (uint32_t)(2.0f + 150.0f * xf);
  if (xfade > grain / 3u) xfade = grain / 3u;
  g_xfade_total = xfade;
  g_xfade_left = xfade;
}

static void choose_new_fragment(float c, float s) {
  uint32_t grain = base_grain_length(s);
  const float jitter = 0.55f + 0.90f * rand01();
  grain = (uint32_t)((float)grain * jitter);
  if (grain < 64u) grain = 64u;
  if (grain > 36000u) grain = 36000u;

  /* At low SPEED the head performs a semi-local random walk. At high SPEED it
   * is increasingly likely to teleport anywhere in the captured history. */
  uint32_t history = g_filled;
  if (history > (BUF_SIZE - MIN_HISTORY - 1u))
    history = BUF_SIZE - MIN_HISTORY - 1u;

  if (history < 512u) {
    g_read_pos = (float)((g_write - MIN_HISTORY) & BUF_MASK);
  } else {
    const float global_chance = 0.10f + 0.82f * s + 0.08f * c;
    if (rand01() < global_chance) {
      const uint32_t age = MIN_HISTORY + ((rng32() & 0xFFFFu) * (history - MIN_HISTORY) >> 16);
      g_read_pos = (float)((g_write - age) & BUF_MASK);
    } else {
      const float radius = 384.0f + 11600.0f * s * s;
      const float delta = (rand01() * 2.0f - 1.0f) * radius;
      g_read_pos = wrap_pos(g_read_pos + delta);
    }
  }

  /* CRUSH increasingly allows wrong-speed and reverse fragments. */
  float mag = 1.0f;
  const float rate_roll = rand01();
  if (rate_roll < c * 0.18f) {
    mag = 0.50f;
  } else if (rate_roll < c * 0.38f) {
    mag = 0.75f;
  } else if (rate_roll < c * 0.60f) {
    mag = 1.50f;
  } else if (rate_roll < c * 0.76f) {
    mag = 2.00f;
  }

  const float reverse_chance = 0.04f + 0.54f * c;
  g_step = (rand01() < reverse_chance) ? -mag : mag;

  g_grain_total = grain;
  g_grain_left = grain;
  setup_join(c, grain);

  /* Store the complete identity of this fragment. HOLD can replay it exactly
   * rather than merely making another nearby random jump. */
  g_fragment_anchor = g_read_pos;
  g_fragment_step = g_step;
  g_fragment_len = grain;
  g_fragment_valid = 1u;
}

static void repeat_fragment(float c, float s, uint8_t frozen) {
  if (!g_fragment_valid) {
    choose_new_fragment(c, s);
    return;
  }

  g_read_pos = g_fragment_anchor;
  g_step = g_fragment_step;

  /* In full FREEZE, SPEED remains performable: it changes the length of the
   * locked loop while the source material and start point stay frozen. */
  uint32_t grain = frozen ? base_grain_length(s) : g_fragment_len;
  g_fragment_len = grain;
  g_grain_total = grain;
  g_grain_left = grain;
  setup_join(c, grain);
}

static void choose_grain(float c, float s, float h, uint8_t frozen) {
  if (frozen) {
    repeat_fragment(c, s, 1u);
    return;
  }

  /* HOLD is a curved probability, not a second speed control:
   *   0%  ->  0% repeats (original GlitchB behaviour)
   *  25%  -> ~44% repeats
   *  50%  -> ~75% repeats
   *  75%  -> ~94% repeats
   * 100%  -> handled as hard FREEZE above
   */
  const float repeat_probability = h * (2.0f - h);
  if (g_fragment_valid && h > 0.001f && rand01() < repeat_probability) {
    repeat_fragment(c, s, 0u);
  } else {
    choose_new_fragment(c, s);
  }
}

__attribute__((used))
void _entry(uint32_t platform, uint32_t api) {
  /* The runtime clears normal BSS in Korg's template. Do it explicitly here. */
  volatile uint8_t *p = (volatile uint8_t *)&_bss_start;
  volatile uint8_t *e = (volatile uint8_t *)&_bss_end;
  while (p != e) *p++ = 0u;
  _hook_init(platform, api);
}

__attribute__((used))
void _hook_init(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;

  /* SDRAM is NOLOAD, so clear the live buffer ourselves. */
  for (uint32_t i = 0; i < BUF_SIZE; ++i) {
    g_buf_l[i] = 0.0f;
    g_buf_r[i] = 0.0f;
  }

  g_write = 0u;
  g_filled = 0u;
  g_crush = g_crush_target = 0.58f;
  g_speed = g_speed_target = 0.42f;
  g_hold_amount = g_hold_target = 0.0f;
  g_rng = 0xA53C9E17u;
  g_read_pos = 0.0f;
  g_step = 1.0f;
  g_grain_left = 0u;
  g_grain_total = 0u;
  g_xfade_left = 0u;
  g_xfade_total = 0u;
  g_prev_l = g_prev_r = 0.0f;
  g_fragment_anchor = 0.0f;
  g_fragment_step = 1.0f;
  g_fragment_len = 0u;
  g_fragment_valid = 0u;
  g_freeze_active = 0u;
  g_hold_count = 0u;
  g_hold_l = g_hold_r = 0.0f;
}

__attribute__((used))
void _hook_process(float *xn, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    g_crush += (g_crush_target - g_crush) * 0.0012f;
    g_speed += (g_speed_target - g_speed) * 0.0012f;
    g_hold_amount += (g_hold_target - g_hold_amount) * 0.0012f;

    const float c = g_crush;
    const float s = g_speed;
    const float h = g_hold_amount;

    const uint32_t k = i << 1;
    const float in_l = xn[k];
    const float in_r = xn[k + 1u];

    /* 100% HOLD is a real freeze: stop advancing/writing the live buffer.
     * If HOLD is engaged before startup history exists, keep recording just
     * long enough to make a valid fragment.
     */
    const uint8_t want_freeze =
        (g_hold_target >= 0.995f && g_filled >= (MIN_HISTORY + 64u)) ? 1u : 0u;

    if (want_freeze && !g_freeze_active) {
      g_freeze_active = 1u;
      g_grain_left = 0u; /* latch/restart the current fragment immediately */
    } else if (!want_freeze && g_freeze_active) {
      g_freeze_active = 0u;
      g_grain_left = 0u; /* resume with a fresh decision */
    }

    if (!g_freeze_active) {
      g_buf_l[g_write] = in_l;
      g_buf_r[g_write] = in_r;
      if (g_filled < BUF_SIZE - 1u) ++g_filled;
    }

    if (g_filled < (MIN_HISTORY + 64u)) {
      /* Very short startup while enough past audio accumulates. */
      xn[k] = 0.0f;
      xn[k + 1u] = 0.0f;
    } else {
      if (g_grain_left == 0u)
        choose_grain(c, s, h, g_freeze_active);

      float raw_l = read_interp(g_buf_l, g_read_pos);
      float raw_r = read_interp(g_buf_r, g_read_pos);

      /* A tiny channel skew appears only deep into CRUSH, widening the debris
       * without turning low settings into a stereo chorus.
       */
      if (c > 0.70f) {
        const float skew = 2.0f + 46.0f * (c - 0.70f) * 3.3333333f;
        raw_r = read_interp(g_buf_r, g_read_pos + skew);
      }

      g_read_pos = wrap_pos(g_read_pos + g_step);
      --g_grain_left;

      /* Sample-and-hold decimation: 1x at low CRUSH, up to 64 samples held. */
      float cc = c * c * c;
      uint32_t hold_n = 1u + (uint32_t)(63.0f * cc);
      if (hold_n > 64u) hold_n = 64u;

      if (g_hold_count == 0u) {
        g_hold_l = crush_sample(raw_l, c);
        g_hold_r = crush_sample(raw_r, c);
        g_hold_count = hold_n;
      }
      --g_hold_count;

      float out_l = g_hold_l;
      float out_r = g_hold_r;

      if (g_xfade_left != 0u && g_xfade_total != 0u) {
        const float t = 1.0f - (float)g_xfade_left / (float)g_xfade_total;
        out_l = g_prev_l + (out_l - g_prev_l) * t;
        out_r = g_prev_r + (out_r - g_prev_r) * t;
        --g_xfade_left;
      }

      xn[k] = out_l;
      xn[k + 1u] = out_r;
      g_prev_l = out_l;
      g_prev_r = out_r;
    }

    if (!g_freeze_active)
      g_write = (g_write + 1u) & BUF_MASK;
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
    g_crush_target = x;
  } else if (index == k_user_delfx_param_depth) {
    g_speed_target = x;
  } else if (index == k_user_delfx_param_shift_depth) {
    g_hold_target = x;
  }
}
