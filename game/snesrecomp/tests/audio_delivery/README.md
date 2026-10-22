# Host audio delivery regression

Run `powershell -File tests/audio_delivery/run.ps1` from an engine checkout.
The default compiler is native MinGW GCC; `-Compiler` overrides its path.
No ROM, SDL audio device, or wall-clock timing is needed. Whole-program LTO
links the production resampler and DSP FIFO while omitting unrelated host APIs.

The test covers startup, replacement queues after snapshot loads, 32040/44100/
48000 Hz output, and forced starvation with callbacks shorter than the fade.
It checks continuous PCM ramps, one underflow per starvation episode, a rebuilt
four-block cushion, and no guest sample production by the consumer.

Reset and both snapshot-load paths discard host resampler phase and occupancy
history. Delivery fades to silence, waits for guest execution to refill the
existing ~67 ms target cushion, then fades back in. The SPC/DSP clock, synthesis
and ordinary drift servo are unchanged. `SNESRECOMP_AUDIO_STATS` appends a
`priming` count so intentional recovery is distinguishable from underflows;
the debug endpoint exposes the same count as `output_priming`.

For real-time regressions, run the game at normal pacing with a device and the
audio stats enabled. Benchmark-audio modes deliberately disable pacing and
cannot establish that rendering keeps up with the audio clock. SMW's adaptive
renderer has a separate copied-save exit/pipe replay for this check.
