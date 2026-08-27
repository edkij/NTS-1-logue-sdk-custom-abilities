#include <stdint.h>
#include "userdelfx.h"
#include "fixed_math.h"


/*
 * SwarmCh v0.1
 * Multi-rate stereo "swarm" chorus for the Korg NTS-1 digital mkI DELAY slot.
 *
 * TIME  -> DEPTH: subtle chorus -> very deep modulation
 * DEPTH -> SWARM: one paired chorus -> dense multi-rate 6-voice-per-channel field
 *
 * Compared with StereoCh v0.2, the important change is not merely more taps.
 * The voices are split across slow, medium and fast LFO families, and each tap
 * gets a small amount of shared drift from another family.  At high SWARM this
 * makes the ensemble move in several time scales at once instead of sounding
 * like copies of one chorus oscillator.
 */

#define BUF_SIZE 4096u
#define BUF_MASK (BUF_SIZE - 1u)
#define NUM_PHASES 12u

static float g_buf_l[BUF_SIZE] __sdram;
static float g_buf_r[BUF_SIZE] __sdram;
static uint32_t g_write;

static float g_depth;
static float g_depth_target;
static float g_swarm;
static float g_swarm_target;

/* Three deliberately separated modulation-rate families at 48 kHz:
 * slow   ~0.21 .. 0.41 Hz
 * medium ~0.57 .. 1.07 Hz
 * fast   ~1.31 .. 2.13 Hz
 */
static uint32_t g_phase[NUM_PHASES];
static const uint32_t kPhaseInc[NUM_PHASES] = {
   18790u,  24159u,  30423u,  36686u,
   51003u,  63530u,  79636u,  95742u,
  117217u, 136902u, 160166u, 190589u
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

/* Rounded triangle in approximately [-1, 1]. It is cheap and avoids the
 * hard corners of a raw triangle LFO without needing libm/sinf.
 */
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

static inline float tap(const float *buf,
                        float centre,
                        float excursion,
                        float primary,
                        float drift,
                        float drift_amount) {
  /* Keep the composite modulator inside a sane range without an expensive
   * clamp.  Coefficients sum to 1.0 at maximum drift_amount.
   */
  const float mod = primary * (1.0f - drift_amount) + drift * drift_amount;
  return read_delay(buf, centre + excursion * mod);
}

void DELFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;

  for (uint32_t i = 0; i < BUF_SIZE; ++i) {
    g_buf_l[i] = 0.0f;
    g_buf_r[i] = 0.0f;
  }

  g_write = 0u;
  g_depth = 0.42f;
  g_depth_target = 0.42f;
  g_swarm = 0.64f;
  g_swarm_target = 0.64f;

  /* Irregular starting phases avoid an obvious repeated stereo sweep. */
  g_phase[0]  = 0x00000000u;
  g_phase[1]  = 0x89000000u;
  g_phase[2]  = 0x35000000u;
  g_phase[3]  = 0xD7000000u;
  g_phase[4]  = 0x5B000000u;
  g_phase[5]  = 0xEA000000u;
  g_phase[6]  = 0x17000000u;
  g_phase[7]  = 0xA4000000u;
  g_phase[8]  = 0x76000000u;
  g_phase[9]  = 0xC1000000u;
  g_phase[10] = 0x2D000000u;
  g_phase[11] = 0xF3000000u;
}

void DELFX_PROCESS(float *xn, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    /* Smooth parameter motion so delay taps do not zipper when knobs move. */
    g_depth += (g_depth_target - g_depth) * 0.0010f;
    g_swarm += (g_swarm_target - g_swarm) * 0.0010f;

    const float d = g_depth;
    const float e = g_swarm;

    /* Same deliberately wide useful range as the liked StereoCh v0.2:
     * roughly 0.5 ms -> 15 ms excursion at 48 kHz.
     */
    const float dcurve = d * (0.20f + 0.80f * d);
    const float excursion = 24.0f + 700.0f * dcurve;

    /* Extra layers enter progressively. At the far right there are six taps
     * per channel, but unlike v0.2 they belong to very different LFO speeds.
     */
    const float w2 = 0.88f * smoothstep01((e - 0.03f) * 1.45f);
    const float w3 = 0.82f * smoothstep01((e - 0.16f) * 1.58f);
    const float w4 = 0.76f * smoothstep01((e - 0.31f) * 1.78f);
    const float w5 = 0.70f * smoothstep01((e - 0.48f) * 2.10f);
    const float w6 = 0.66f * smoothstep01((e - 0.66f) * 2.95f);
    const float sumw = w2 + w3 + w4 + w5 + w6;

    /* Do not average the swarm all the way down by 1/N. A slightly looser
     * normalization keeps the cloud physically present. It can run hotter
     * than StereoCh v0.2 at the extreme end, intentionally.
     */
    const float invw = 1.0f / (1.0f + 0.72f * sumw);

    const uint32_t s = i << 1;
    const float in_l = xn[s];
    const float in_r = xn[s + 1u];

    g_buf_l[g_write] = in_l;
    g_buf_r[g_write] = in_r;

    /* Evaluate each LFO only once per sample, then reuse them for both the
     * primary movement and cross-family drift. This gives complex motion
     * without doubling the LFO CPU cost.
     */
    float m[NUM_PHASES];
    for (uint32_t v = 0; v < NUM_PHASES; ++v) {
      m[v] = lfo(g_phase[v]);
    }

    /* SWARM also increases the amount of cross-family drift. */
    const float drift = 0.08f + 0.24f * e;

    /* Main pair: medium-speed movement, almost mono at the left, increasingly
     * dissimilar at the right. Separate centre delays make mono sources widen.
     */
    const float centre_l = 1010.0f - 125.0f * e;
    const float centre_r = 1010.0f + 125.0f * e;

    const float main_r = m[4] * (1.0f - e) + m[6] * e;
    const float drift_r = m[1] * (1.0f - e) + m[2] * e;

    float wet_l = tap(g_buf_l, centre_l, excursion,
                      m[4], m[1], drift);
    float wet_r = tap(g_buf_r, centre_r, excursion * (0.98f - 0.06f * e),
                      main_r, drift_r, drift);

    /* Layer 2: slow, broad drift. */
    wet_l += w2 * tap(g_buf_l,  650.0f, excursion * 0.80f,
                      m[0], m[8], drift * 0.70f);
    wet_r += w2 * tap(g_buf_r,  735.0f, excursion * 0.76f,
                      m[3], m[9], drift * 0.70f);

    /* Layer 3: medium-rate voice pair. */
    wet_l += w3 * tap(g_buf_l, 1190.0f, excursion * 0.72f,
                      m[5], m[0], drift);
    wet_r += w3 * tap(g_buf_r, 1325.0f, excursion * 0.69f,
                      m[7], m[3], drift);

    /* Layer 4: first fast family - this adds the string-machine shimmer. */
    wet_l += w4 * tap(g_buf_l, 1510.0f, excursion * 0.50f,
                      m[8], m[5], drift * 0.80f);
    wet_r += w4 * tap(g_buf_r, 1405.0f, excursion * 0.53f,
                      m[9], m[4], drift * 0.80f);

    /* Layer 5: very slow centre movement with a little fast instability. */
    wet_l += w5 * tap(g_buf_l, 1865.0f, excursion * 0.58f,
                      m[2], m[10], drift * 0.62f);
    wet_r += w5 * tap(g_buf_r, 2035.0f, excursion * 0.55f,
                      m[1], m[11], drift * 0.62f);

    /* Layer 6: fastest, shallower pair. At high SWARM these voices supply
     * the continuously moving 'insect cloud' around the slower chorus body.
     */
    wet_l += w6 * tap(g_buf_l, 2330.0f, excursion * 0.36f,
                      m[10], m[6], drift * 0.78f);
    wet_r += w6 * tap(g_buf_r, 2190.0f, excursion * 0.39f,
                      m[11], m[7], drift * 0.78f);

    wet_l *= invw;
    wet_r *= invw;

    /* Keep a strong direct vocal centre while letting the swarm become large.
     * This mix is slightly hotter than v0.2 so the many voices do not disappear
     * perceptually after normalization/cancellation. AmpClean in the DELAY slot
     * can still be used before this effect when more vocal level is desired.
     */
    const float dry_gain = 0.62f - 0.12f * e;
    const float wet_gain = 0.74f + 0.34f * e;

    xn[s]      = in_l * dry_gain + wet_l * wet_gain;
    xn[s + 1u] = in_r * dry_gain + wet_r * wet_gain;

    g_write = (g_write + 1u) & BUF_MASK;

    for (uint32_t v = 0; v < NUM_PHASES; ++v) {
      g_phase[v] += kPhaseInc[v];
    }
  }
}

void DELFX_SUSPEND(void) {}
void DELFX_RESUME(void) {}

void DELFX_PARAM(uint8_t index, int32_t value) {
  const float x = clamp01(q31_to_f32(value));

  switch (index) {
    case k_user_delfx_param_time:
      g_depth_target = x;
      break;

    case k_user_delfx_param_depth:
      g_swarm_target = x;
      break;

    default:
      break;
  }
}
