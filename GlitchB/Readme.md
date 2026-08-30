# GlitchB

A live buffer glitch effect for the Korg NTS-1 digital mkI.

GlitchB continuously records the incoming audio into a buffer and plays it back in unstable fragments: jumping between different positions, changing playback direction, repeating pieces and gradually destroying them with bit crushing and sample holding.

It can range from subtle broken-buffer textures to completely mangled digital noise.

## Controls

### CRUSH
Controls the amount of digital destruction.

Low values keep the buffered audio relatively clear.

Higher values introduce stronger bit crushing, sample holding and increasingly harsh digital artifacts.

### SPEED
Controls how quickly GlitchB changes and repeats fragments.

Lower values produce longer, slower-moving chunks.

Higher values create shorter fragments, faster repetitions and more aggressive glitching.

### HOLD
Hold **DELAY** and turn **B**.

Controls how strongly GlitchB holds onto the current fragment instead of selecting a new one.

- Low values — fragments change freely
- Medium values — the current fragment is repeated more often
- High values — GlitchB becomes increasingly stuck on the current piece
- 100% — full freeze: the audio buffer stops recording and the captured material is held indefinitely

CRUSH and SPEED remain active while the buffer is frozen, allowing the captured fragment to be reshaped and destroyed in real time.

## Notes

GlitchB is 100% wet.

The effect is designed for live input and works particularly well with vocals, percussion, synths and other changing audio sources.

At extreme settings it can easily turn the input into a wall of digital noise.

## Installation

Load `GlitchB_v0.2_DELAY.ntkdigunit` into a DELAY user slot using the Korg NTS-1 digital Librarian or `logue-cli`.

Built for the original Korg NTS-1 digital (mkI) using the logue SDK.
