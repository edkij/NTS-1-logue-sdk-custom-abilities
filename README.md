# NTS-1 Custom Effects

A small vibecoded collection of custom effects for the **Korg NTS-1 digital mkI**, built with the Korg logue SDK.

Each effect is available in different slot variants (`MOD`, `DELAY`, and/or `REVERB`) so they can be combined more freely depending on your setup.

# Release:  
https://github.com/edkij/NTS-1-logue-sdk-custom-abilities/releases/tag/NTS-1

## OctaClean v0.1

A clean real-time pitch shifter / octaver.

**Parameters:**

* **Pitch:** -24 to +24 semitones
* **Mix:** Dry to Wet

Useful for octave-down effects, octave-up effects, harmonies, and full-range pitch shifting.

## AmpClean v0.2

A simple gain and saturation effect.

**Parameters:**

* **Gain:** -6 dB to +36 dB
* **Saturation:** Clean to heavily driven soft clipping

Can be used as a clean booster, preamp, or distortion stage.

## StereoCh v0.2

A wide multi-voice stereo chorus.

**Parameters:**

* **Depth:** Chorus modulation depth
* **Ensemble:** Stereo width and number/intensity of additional chorus voices

At higher settings it becomes a dense ensemble-style chorus with strong stereo movement.

## SwarmCh v0.1

A more extreme ensemble chorus built around multiple independently moving voices.

**Parameters:**

* **Depth:** Modulation depth
* **Swarm:** From a relatively simple chorus to a dense moving cloud of voices

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

The project is experimental and provided as-is.
