# GlitchB v0.2 — DELAY

Live buffer scrambler / crusher for the **Korg NTS-1 digital mkI**.

Controls:

- **TIME → CRUSH** — bit-depth reduction, sample-hold destruction, reverse fragments, wrong-speed playback and harder splice discontinuities.
- **DEPTH → SPEED** — slow roaming chunks → rapidly shuffled micro-fragments.
- **DELAY + B → HOLD** — gradually raises the chance of repeating the same captured fragment instead of choosing a new one. At 100% it becomes a real **FREEZE**: live-buffer writing stops and the captured region loops until HOLD is lowered. SPEED remains active while frozen and changes the loop length.

HOLD curve (approximately):

- 0%: original free-roaming GlitchB behaviour
- 25%: ~44% chance to repeat the current fragment
- 50%: ~75% chance to repeat
- 75%: ~94% chance to repeat
- 100%: full buffer freeze / locked fragment

Output remains intentionally **100% wet**.

The DELAY build keeps roughly **1.36 s** of stereo input history at 48 kHz.

Built for NTS-1 digital mkI / logue SDK API 1.1-0.
