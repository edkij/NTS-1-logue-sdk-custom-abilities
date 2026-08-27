#include <stdint.h>

#include "userdelfx.h"
#include "fixed_math.h"


#define BUF_FRAMES 4096u
#define BUF_MASK (BUF_FRAMES - 1u)
#define MIN_DELAY 64.0f
#define DELAY_RANGE 3072.0f
#define MAX_DELAY (MIN_DELAY + DELAY_RANGE)

/* 4096 stereo float frames = 32 KiB, placed in the NTS-1 effect SDRAM area. */
static float g_delay[BUF_FRAMES * 2u] __sdram;

static uint32_t g_write;
static float g_phase;
static float g_ratio;
static float g_ratio_target;
static float g_mix;
static float g_mix_target;
static float g_zero_blend;
static float g_zero_target;
static uint32_t g_filled;

/* 2^(semitones/12), semitones = -24 ... +24. */
static const float kPitchRatio[49] = {
  0.250000000f,0.264865774f,0.280615512f,0.297301779f,0.314980262f,0.333709964f,0.353553391f,
  0.374576769f,0.396850263f,0.420448208f,0.445449359f,0.471937156f,0.500000000f,0.529731547f,
  0.561231024f,0.594603558f,0.629960525f,0.667419927f,0.707106781f,0.749153538f,0.793700526f,
  0.840896415f,0.890898718f,0.943874313f,1.000000000f,1.059463094f,1.122462048f,1.189207115f,
  1.259921050f,1.334839854f,1.414213562f,1.498307077f,1.587401052f,1.681792831f,1.781797436f,
  1.887748625f,2.000000000f,2.118926188f,2.244924097f,2.378414230f,2.519842100f,2.669679708f,
  2.828427125f,2.996614154f,3.174802104f,3.363585661f,3.563594873f,3.775497251f,4.000000000f
};

/* sin(pi*x/2), x = 0..1 in 1/32 steps, for equal-power dry/wet mixing. */
static const float kMixSin[33] = {
  0.000000000f,0.049067674f,0.098017140f,0.146730474f,0.195090322f,0.242980180f,0.290284677f,
  0.336889853f,0.382683432f,0.427555093f,0.471396737f,0.514102744f,0.555570233f,0.595699304f,
  0.634393284f,0.671558955f,0.707106781f,0.740951125f,0.773010453f,0.803207531f,0.831469612f,
  0.857728610f,0.881921264f,0.903989293f,0.923879533f,0.941544065f,0.956940336f,0.970031253f,
  0.980785280f,0.989176510f,0.995184727f,0.998795456f,1.000000000f
};

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline float tri_window(float p) {
  float t = 2.0f * p - 1.0f;
  if (t < 0.0f) t = -t;
  return 1.0f - t;
}

static inline float read_delay(float delay, uint32_t ch) {
  float pos = (float)g_write - delay;
  if (pos < 0.0f) pos += (float)BUF_FRAMES;
  const uint32_t i0 = (uint32_t)pos;
  const float frac = pos - (float)i0;
  const uint32_t i1 = (i0 + 1u) & BUF_MASK;
  const float a = g_delay[(i0 << 1) + ch];
  const float b = g_delay[(i1 << 1) + ch];
  return a + (b - a) * frac;
}

static inline float mix_lut(float x) {
  x = clamp01(x);
  const float f = x * 32.0f;
  const uint32_t i = (uint32_t)f;
  if (i >= 32u) return 1.0f;
  const float frac = f - (float)i;
  return kMixSin[i] + (kMixSin[i + 1u] - kMixSin[i]) * frac;
}

void DELFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;

  for (uint32_t i = 0; i < BUF_FRAMES * 2u; ++i) {
    g_delay[i] = 0.0f;
  }

  g_write = 0u;
  g_phase = 0.0f;

  /* Useful fallback before the NTS-1 reports the physical knob positions. */
  g_ratio = 0.25f;
  g_ratio_target = 0.25f;  /* -24 semitones */
  g_mix = 1.0f;
  g_mix_target = 1.0f;    /* 100% wet */
  g_zero_blend = 0.0f;
  g_zero_target = 0.0f;
  g_filled = 0u;
}

void DELFX_PROCESS(float *xn, uint32_t frames) {

  for (uint32_t i = 0; i < frames; ++i) {
    const uint32_t s = i << 1;
    const float in_l = xn[s];
    const float in_r = xn[s + 1u];

    g_delay[g_write << 1] = in_l;
    g_delay[(g_write << 1) + 1u] = in_r;

    /* Smooth knob changes to reduce zipper noise and clicks. */
    g_ratio += (g_ratio_target - g_ratio) * 0.0015f;
    g_mix += (g_mix_target - g_mix) * 0.0020f;
    g_zero_blend += (g_zero_target - g_zero_blend) * 0.0020f;

    /* Two read heads half a cycle apart, crossfaded to hide wrap points. */
    const float p1 = g_phase;
    float p2 = p1 + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;

    const float d1 = MIN_DELAY + p1 * DELAY_RANGE;
    const float d2 = MIN_DELAY + p2 * DELAY_RANGE;
    const float w1 = tri_window(p1);
    const float w2 = tri_window(p2);

    float wet_l = read_delay(d1, 0u) * w1 + read_delay(d2, 0u) * w2;
    float wet_r = read_delay(d1, 1u) * w1 + read_delay(d2, 1u) * w2;

    /* At exactly 0 st, make the wet path direct to avoid comb filtering. */
    wet_l += (in_l - wet_l) * g_zero_blend;
    wet_r += (in_r - wet_r) * g_zero_blend;

    /* Fade in the shifted path while the delay line fills at startup. */
    const float startup = (g_filled >= (uint32_t)MAX_DELAY)
      ? 1.0f
      : ((float)g_filled / MAX_DELAY);
    wet_l = in_l + (wet_l - in_l) * startup;
    wet_r = in_r + (wet_r - in_r) * startup;

    const float wet_gain = mix_lut(g_mix);
    const float dry_gain = mix_lut(1.0f - g_mix);
    xn[s] = in_l * dry_gain + wet_l * wet_gain;
    xn[s + 1u] = in_r * dry_gain + wet_r * wet_gain;

    g_write = (g_write + 1u) & BUF_MASK;
    if (g_filled < (uint32_t)MAX_DELAY) ++g_filled;

    /* read-head speed = pitch ratio. */
    g_phase += (1.0f - g_ratio) * (1.0f / DELAY_RANGE);
    if (g_phase >= 1.0f) g_phase -= 1.0f;
    else if (g_phase < 0.0f) g_phase += 1.0f;
  }
}

void DELFX_SUSPEND(void) {}
void DELFX_RESUME(void) {}

void DELFX_PARAM(uint8_t index, int32_t value) {
  const float x = clamp01(q31_to_f32(value));

  switch (index) {
    case k_user_delfx_param_time: {
      int32_t step = (int32_t)(x * 48.0f + 0.5f); /* 0..48 => -24..+24 st */
      if (step < 0) step = 0;
      if (step > 48) step = 48;
      g_ratio_target = kPitchRatio[(uint32_t)step];
      g_zero_target = (step == 24) ? 1.0f : 0.0f;
      break;
    }

    case k_user_delfx_param_depth:
      g_mix_target = x; /* 0 = dry, 1 = wet */
      break;

    default:
      break;
  }
}
