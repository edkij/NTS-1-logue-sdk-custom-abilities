# InfiniD v0.1 — DELAY

An overdubbing long-memory delay / looper for the **Korg NTS-1 digital mkI**.

Incoming audio is continuously written into a circular loop. When the loop comes around again, the previous content is fed back and new audio is layered on top. The physical loop can be up to about **25 seconds**, while the stored layers can decay for **many minutes or indefinitely** because the long memory comes from feedback rather than from storing minutes of audio.

## Controls

- **TIME → LOOP** — physical loop length, approximately 0.125 s to 25 s. The scale is intentionally nonlinear to give more control over short loops.
- **DEPTH → OVERDUB** — how strongly new input is added to the existing loop. 0% stops adding new material; 100% records aggressively over the old layers.
- **DELAY + B → DECAY** — lifetime of old layers. The upper range is deliberately very long:
  - 0%: about 8 s T60
  - 25%: about 30 s
  - 50%: about 2 min
  - 75%: about 10 min
  - 90%: about 30 min
  - 98%: about 2 h
  - 100%: **INFINITE** (feedback = 100%)

A gentle safety limiter only acts when accumulated layers approach clipping. At infinite decay, repeated overdubbing can still deliberately drive the loop into saturation.

## Buffer format

The loop is stored as **mono 16-bit audio at 48 kHz**. This keeps the first version relatively clean and provides a maximum loop of roughly **25 seconds** within the NTS-1 mkI DELAY SDRAM.

Longer physical loops are possible in a future lo-fi build by reducing bit depth and/or effective sample rate; the 10–30 minute decay itself does **not** require lower sample quality.
