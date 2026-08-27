#include <stdint.h>

#include "userdelfx.h"
#include "fixed_math.h"


/*
 * AmpClean v0.2
 * Stereo gain / saturation stage for the NTS-1 mkI DELAY slot.
 *
 * TIME  -> GAIN, -6 dB .. +36 dB
 * DEPTH -> SATURATION, clean .. strong driven soft clip
 */

static float g_gain;
static float g_gain_target;
static float g_sat;
static float g_sat_target;

/* -6 .. +36 dB in 0.5 dB LUT steps. Interpolation keeps the knob smooth.
 * 0 dB is LUT index 12, i.e. about 14.3% of the physical knob travel.
 */
static const float kGain[85] = {
  0.501187234f,0.530884444f,0.562341325f,0.595662144f,0.630957344f,0.668343918f,0.707945784f,
  0.749894209f,0.794328235f,0.841395142f,0.891250938f,0.944060876f,1.000000000f,1.059253725f,
  1.122018454f,1.188502227f,1.258925412f,1.333521432f,1.412537545f,1.496235656f,1.584893192f,
  1.678804018f,1.778279410f,1.883649089f,1.995262315f,2.113489040f,2.238721139f,2.371373706f,
  2.511886432f,2.660725060f,2.818382931f,2.985382619f,3.162277660f,3.349654392f,3.548133892f,
  3.758374043f,3.981071706f,4.216965034f,4.466835922f,4.731512590f,5.011872336f,5.308844442f,
  5.623413252f,5.956621435f,6.309573445f,6.683439176f,7.079457844f,7.498942093f,7.943282347f,
  8.413951416f,8.912509381f,9.440608763f,10.000000000f,10.592537252f,11.220184543f,11.885022274f,
  12.589254118f,13.335214322f,14.125375446f,14.962356561f,15.848931925f,16.788040181f,17.782794100f,
  18.836490895f,19.952623150f,21.134890398f,22.387211386f,23.713737057f,25.118864315f,26.607250598f,
  28.183829313f,29.853826189f,31.622776602f,33.496543916f,35.481338923f,37.583740429f,39.810717055f,
  42.169650343f,44.668359215f,47.315125896f,50.118723363f,53.088444423f,56.234132519f,59.566214353f,
  63.095734448f
};

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline float gain_lut(float x) {
  x = clamp01(x);
  const float f = x * 84.0f;
  const uint32_t i = (uint32_t)f;
  if (i >= 84u) return kGain[84];
  const float frac = f - (float)i;
  return kGain[i] + (kGain[i + 1u] - kGain[i]) * frac;
}

/*
 * Deliberately more audible than v0.1.
 *
 * SAT adds up to roughly +24 dB of internal drive (1x .. 16x) into a
 * bounded rational soft clipper, and progressively crossfades toward that
 * driven signal. SAT=0 remains a true bypass of the shaper.
 *
 * y = z / (1 + |z|) is cheap on Cortex-M4 and approaches +/-1 smoothly.
 */
static inline float saturate(float x, float sat) {
  if (sat <= 0.0001f) return x;

  const float s2 = sat * sat;
  const float drive = 1.0f + 15.0f * s2;  /* 1x .. 16x, ~+24.1 dB */
  const float z = x * drive;
  const float az = (z < 0.0f) ? -z : z;
  const float soft = z / (1.0f + az);

  /* Ease-out blend: the middle of the knob is already clearly audible. */
  const float blend = sat * (2.0f - sat);
  return x + (soft - x) * blend;
}

void DELFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;

  g_gain = 1.0f;
  g_gain_target = 1.0f;
  g_sat = 0.0f;
  g_sat_target = 0.0f;
}

void DELFX_PROCESS(float *xn, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    /* Smooth knob changes to avoid zipper noise. */
    g_gain += (g_gain_target - g_gain) * 0.0015f;
    g_sat += (g_sat_target - g_sat) * 0.0015f;

    const uint32_t s = i << 1;
    xn[s]      = saturate(xn[s]      * g_gain, g_sat);
    xn[s + 1u] = saturate(xn[s + 1u] * g_gain, g_sat);
  }
}

void DELFX_SUSPEND(void) {}
void DELFX_RESUME(void) {}

void DELFX_PARAM(uint8_t index, int32_t value) {
  const float x = clamp01(q31_to_f32(value));

  switch (index) {
    case k_user_delfx_param_time:
      g_gain_target = gain_lut(x);
      break;

    case k_user_delfx_param_depth:
      g_sat_target = x;
      break;

    default:
      break;
  }
}
