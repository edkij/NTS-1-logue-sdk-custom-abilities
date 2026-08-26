#include <stdint.h>
#include "userrevfx.h"
#include "fixed_math.h"

/*
 * StereoCh v0.2
 * Strong multi-voice stereo chorus for the NTS-1 mkI REVERB slot.
 *
 * TIME  -> DEPTH: much wider modulation excursion than v0.1
 * DEPTH -> ENSEMBLE: mono-ish single chorus -> dense 6-voice-per-channel stereo ensemble
 *
 * Rate is deliberately fixed. The ENSEMBLE control also increases the
 * prominence of the wet field a little, so the far-right end feels like
 * a genuinely larger ensemble rather than merely adding quiet taps.
 */

#define BUF_SIZE 4096u
#define BUF_MASK (BUF_SIZE - 1u)
#define NUM_PHASES 12u

static float g_buf_l[BUF_SIZE] __sdram;
static float g_buf_r[BUF_SIZE] __sdram;
static uint32_t g_write;

static float g_depth;
static float g_depth_target;
static float g_ensemble;
static float g_ensemble_target;

/* Independent slow LFOs, about 0.48 .. 0.97 Hz at 48 kHz. */
static uint32_t g_phase[NUM_PHASES];
static const uint32_t kPhaseInc[NUM_PHASES] = {
  42950u, 42950u, 54582u, 59951u,
  65319u, 70688u, 76951u, 82320u,
  51003u, 61740u, 73372u, 86794u
};

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline float smoothstep01(float x) {
  x = clamp01(x);
  return x * x * (3.0f - 2.0f * x);
}

/* Rounded triangle LFO in approximately [-1, 1]. */
static inline float lfo(uint32_t p) {
  const uint32_t q = p >> 16;
  const float t = (float)q * (1.0f / 65536.0f);
  float tri;
  if (t < 0.25f) {
    tri = t * 4.0f;
  } else if (t < 0.75f) {
    tri = 2.0f - t * 4.0f;
  } else {
    tri = t * 4.0f - 4.0f;
  }
  return tri * (1.5f - 0.5f * tri * tri);
}

static inline float read_delay(const float *buf, float delay_samples) {
  float rp = (float)g_write - delay_samples;
  if (rp < 0.0f) rp += (float)BUF_SIZE;

  const uint32_t i0 = ((uint32_t)rp) & BUF_MASK;
  const uint32_t i1 = (i0 + 1u) & BUF_MASK;
  const float frac = rp - (float)((uint32_t)rp);
  return buf[i0] + (buf[i1] - buf[i0]) * frac;
}

static inline float voice(const float *buf, float centre, float excursion,
                          uint32_t phase) {
  return read_delay(buf, centre + excursion * lfo(phase));
}

void REVFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;

  for (uint32_t i = 0; i < BUF_SIZE; ++i) {
    g_buf_l[i] = 0.0f;
    g_buf_r[i] = 0.0f;
  }

  g_write = 0u;
  g_depth = 0.40f;
  g_depth_target = 0.40f;
  g_ensemble = 0.62f;
  g_ensemble_target = 0.62f;

  /* Irregular phases make the high-ENSEMBLE end feel like independent voices. */
  g_phase[0]  = 0x00000000u;
  g_phase[1]  = 0x00000000u;
  g_phase[2]  = 0x21000000u;
  g_phase[3]  = 0xC9000000u;
  g_phase[4]  = 0x73000000u;
  g_phase[5]  = 0x16000000u;
  g_phase[6]  = 0xE6000000u;
  g_phase[7]  = 0x52000000u;
  g_phase[8]  = 0xA8000000u;
  g_phase[9]  = 0x36000000u;
  g_phase[10] = 0xD1000000u;
  g_phase[11] = 0x8B000000u;
}

void REVFX_PROCESS(float *xn, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    /* Smooth controls so moving DEPTH does not zipper the delay taps. */
    g_depth += (g_depth_target - g_depth) * 0.0010f;
    g_ensemble += (g_ensemble_target - g_ensemble) * 0.0010f;

    const float d = g_depth;
    const float e = g_ensemble;

    /* v0.2: about 0.5 ms minimum excursion to ~15 ms maximum excursion.
     * The curve keeps the first half useful while making the far-right end
     * dramatically deeper than v0.1.
     */
    const float dcurve = d * (0.20f + 0.80f * d);
    const float excursion = 24.0f + 700.0f * dcurve;

    /* Six voices per channel at the right end. New layers arrive progressively. */
    const float w2 = 0.90f * smoothstep01((e - 0.04f) * 1.42f);
    const float w3 = 0.82f * smoothstep01((e - 0.18f) * 1.55f);
    const float w4 = 0.74f * smoothstep01((e - 0.34f) * 1.72f);
    const float w5 = 0.66f * smoothstep01((e - 0.52f) * 2.08f);
    const float w6 = 0.60f * smoothstep01((e - 0.70f) * 3.34f);
    const float invw = 1.0f / (1.0f + w2 + w3 + w4 + w5 + w6);

    const uint32_t s = i << 1;
    const float in_l = xn[s];
    const float in_r = xn[s + 1u];

    g_buf_l[g_write] = in_l;
    g_buf_r[g_write] = in_r;

    /* At ENSEMBLE=0 the main L/R voices are almost identical. At 100%,
     * they separate by a full half-cycle and their centre delays diverge,
     * giving a much wider stereo image even from a mono source.
     */
    const uint32_t stereo_off = (uint32_t)(e * 1073741824.0f); /* 0 .. 1/4 cycle */
    const float centre_l = 1024.0f - 104.0f * e;
    const float centre_r = 1024.0f + 104.0f * e;

    float wet_l = voice(g_buf_l, centre_l, excursion, g_phase[0] + stereo_off);
    float wet_r = voice(g_buf_r, centre_r, excursion, g_phase[1] - stereo_off);

    /* Extra voices: deliberately different rates, phases, centre delays and
     * excursion amounts in L and R. This is closer to a string-ensemble cloud
     * than a conventional single-tap chorus at high ENSEMBLE settings.
     */
    wet_l += w2 * voice(g_buf_l,  680.0f, excursion * 0.78f, g_phase[2]);
    wet_r += w2 * voice(g_buf_r,  760.0f, excursion * 0.74f, g_phase[3]);

    wet_l += w3 * voice(g_buf_l, 1180.0f, excursion * 0.70f, g_phase[4]);
    wet_r += w3 * voice(g_buf_r, 1320.0f, excursion * 0.67f, g_phase[5]);

    wet_l += w4 * voice(g_buf_l, 1540.0f, excursion * 0.60f, g_phase[6]);
    wet_r += w4 * voice(g_buf_r, 1430.0f, excursion * 0.63f, g_phase[7]);

    wet_l += w5 * voice(g_buf_l, 1880.0f, excursion * 0.50f, g_phase[8]);
    wet_r += w5 * voice(g_buf_r, 2030.0f, excursion * 0.47f, g_phase[9]);

    wet_l += w6 * voice(g_buf_l, 2350.0f, excursion * 0.42f, g_phase[10]);
    wet_r += w6 * voice(g_buf_r, 2200.0f, excursion * 0.45f, g_phase[11]);

    wet_l *= invw;
    wet_r *= invw;

    /* More ENSEMBLE also makes the chorus field somewhat more dominant.
     * This is intentional: the right side of the control should sound clearly
     * larger and denser, not merely wider on an analyser.
     */
    const float dry_gain = 0.62f - 0.17f * e;
    const float wet_gain = 0.72f + 0.23f * e;

    xn[s]      = in_l * dry_gain + wet_l * wet_gain;
    xn[s + 1u] = in_r * dry_gain + wet_r * wet_gain;

    g_write = (g_write + 1u) & BUF_MASK;

    for (uint32_t v = 0; v < NUM_PHASES; ++v) {
      g_phase[v] += kPhaseInc[v];
    }
  }
}

void REVFX_SUSPEND(void) {}
void REVFX_RESUME(void) {}

void REVFX_PARAM(uint8_t index, int32_t value) {
  const float x = clamp01(q31_to_f32(value));

  switch (index) {
    case k_user_revfx_param_time:
      g_depth_target = x;
      break;

    case k_user_revfx_param_depth:
      g_ensemble_target = x;
      break;

    default:
      break;
  }
}
