# NTS-1 Custom Effects

A small vibecoded collection of custom effects for the **Korg NTS-1 digital mkI**, built with the Korg logue SDK.

Some effects are available in different slot variants (`MOD`, `DELAY`, and/or `REVERB`) so they can be combined more freely depending on your setup.

## [Latest releases for all effects](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/tag/NTS-1)

## GlitchB v0.2 - [DELAY](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/GlitchB_v0.2_DELAY.ntkdigunit)

A live buffer glitch effect that captures, repeats and destroys fragments of incoming audio.

**Parameters**
- **Crush** — digital degradation
- **Speed** — fragment length and repetition speed
- **Hold** — controls how strongly GlitchB holds onto the current fragment; at 100% the buffer freezes completely. To adjust it, hold the **DELAY** button and turn the **B** knob.

## InfiniD v0.1 - [DELAY](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/InfiniD_v0.1_DELAY.ntkdigunit)

A long-memory overdub delay that continuously layers incoming audio into a repeating loop.

**Parameters**
- **Loop** — loop length, from short repeats up to roughly 25 seconds
- **Overdub** — amount of new incoming audio added to the existing loop
- **Decay** — controls how long old layers remain, from a few seconds to effectively infinite feedback. To adjust it, hold the **DELAY** button and turn the **B** knob.

At high Decay settings, new sounds can gradually accumulate into a dense evolving wall of audio while older layers continue repeating underneath.

## OctaClean v0.1 - [MOD](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/OctaClean_v0.1_MOD.ntkdigunit) | [DELAY](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/OctaClean_v0.1_DELAY.ntkdigunit) | [REVERB](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/OctaClean_v0.1_REVERB.ntkdigunit)

A clean real-time pitch shifter / octaver.

**Parameters**
- **Pitch** — -24 to +24 semitones
- **Mix** — Dry to Wet

Useful for octave-down/octave-up effects, harmonies, and full-range pitch shifting.

## AmpClean v0.2 - [MOD](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/AmpClean_v0.2_MOD.ntkdigunit) | [DELAY](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/AmpClean_v0.2_DELAY.ntkdigunit) | [REVERB](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/AmpClean_v0.2_REVERB.ntkdigunit)

A simple gain and saturation effect.

**Parameters:**
- **Gain** — -6 dB to +36 dB
- **Saturation** — Clean to heavily driven soft clipping

Can be used as a clean booster, preamp, or distortion stage.

## StereoCh v0.4 - [MOD](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/StereoCh_v0.4_MOD.ntkdigunit) | [DELAY](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/StereoCh_v0.4_DELAY.ntkdigunit) | [REVERB](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/StereoCh_v0.4_REVERB.ntkdigunit)

An alternative version of StereoCh with asymmetric LFO movement.

**Parameters:**
- **Depth** — Chorus modulation depth
- **Ensemble** — Stereo width and number/intensity of additional chorus voices
- **Rate** — Modulation speed. Available in the DELAY and REVERB versions; hold the corresponding effect button and turn the **B** knob.

Unlike v0.2, the modulation in v0.4 is intentionally asymmetric. Upward pitch movement is softer and less prominent, while the downward movement is stronger. Additional ensemble voices become progressively more asymmetric as Ensemble is increased.

The result is a deeper and heavier stereo chorus with fewer prominent high-pitched voices, while keeping the wide moving ensemble character of the original StereoCh.

## SwarmCh v0.1 - [MOD](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/SwarmCh_v0.1_MOD.ntkdigunit) | [DELAY](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/SwarmCh_v0.1_DELAY.ntkdigunit) | [REVERB](https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/download/NTS-1/SwarmCh_v0.1_REVERB.ntkdigunit)

A more extreme ensemble chorus built around multiple independently moving voices.

**Parameters:**
- **Depth** — Modulation depth
- **Swarm** — From a relatively simple chorus to a dense moving cloud of voices

The voices use different modulation speeds, phases, and delay times, creating a constantly shifting stereo field.

Especially useful for vocals, pads, drones, and heavily processed sounds.

## Installation

Install the `.ntkdigunit` files using **Korg NTS-1 digital Librarian** or `logue-cli`.

On Linux, large user units may require a larger ALSA MIDI SysEx buffer. If transfers freeze or time out, increasing:

```bash
/sys/module/snd_seq_midi/parameters/output_buffer_size
```

to `65536` may help.

## Notes

These effects were made for the **original NTS-1 digital (mkI)**.

Different category versions of the same effect use the same DSP idea but are adapted for the corresponding logue SDK effect API.

The effects in this collection are vibecoded: the ideas, sound design, testing, and direction come from me, while AI is used to help implement them in code.
