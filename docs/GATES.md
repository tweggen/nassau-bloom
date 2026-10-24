# NassauBloom — Implementation Gates

Each gate is a **self-contained work package**. A gate is *done* only when every
one of its acceptance criteria (AC) is objectively demonstrated. Nearly all of
them are `ctest` assertions, so "done" is a command, not an opinion.

Read [`DESIGN.md`](DESIGN.md) first. It is the specification; this file is the
schedule. Where the two disagree, `DESIGN.md` wins and you fix this file.

---

## Rules that bind every gate

These are `nassau-zermatt`'s rules R1–R11, which apply unchanged. They are
restated briefly here; the reasoning is in `nassau-zermatt/docs/GATES.md`.

* **R1 — No gate may regress an earlier gate.** The exit command for every gate
  is the full suite: `./ci/gates.sh` (and `./ci/gates.sh --sdk <dir>` from G0
  on, where the SDK is available).
* **R2 — The DSP core stays framework-free.** `Source/DSP/` may include only
  `<atomic> <cmath> <cstddef> <cstdint> <algorithm> <array> <vector>`, plus the
  compiler's own arch-intrinsic headers behind arch guards. `<vector>` is
  allowed here, unlike in Zermatt, **for `init()`-time allocation only** (the
  delay lines are megabytes and sample-rate dependent; R3 still holds).
* **R3 — No allocation, no locks, no I/O in `process()`.**
* **R4 — Append-only params.** `PLUG_N_PARAMS` is bumped in the same commit.
* **R5 — The audio thread never calls `std::sin`/`std::cos`/`std::exp`/`std::pow`
  per sample.** Per sub-block is fine (DESIGN §9.2). Smooth the *results*
  (linear gains), not the inputs to the transcendental (DESIGN §8).
* **R6 — Acceptance is measured.** Tests drive real audio through the public
  API and measure the result (RMS, decay curves, group delay). A test that
  inspects private state is not an acceptance test. The exception, stated per
  AC, is the **pure functions** the design exposes for testing: the ER tap
  computation (§4) and the dispersion group-delay formula (§6.1).
* **R7 — No `-ffast-math`, no `-march=native`.**
* **R8 — Determinism.** No `rand()` and no `time()`. Noise comes from the
  xorshift32 in `Tests/test_util.h`.
* **R9 — The plugin never writes its own parameters.**
* **R10 — Finish a gate by updating this file**: mark it DONE with a one-line
  summary of what was measured (numbers, not adjectives), then commit. One
  commit per gate.
* **R11 — Report honestly.** If an AC cannot be met, say so with the measured
  number and stop. Do not relax the AC on your own.

## Shared measurement helpers

`Tests/test_util.h` (G0 creates it; G1 completes it) holds everything gates
reuse:

```cpp
double rms(const std::vector<float>&, int from, int to);
double magDb(/*block callable*/, double freq, double fs, double amp);   // steady state
std::vector<double> schroederDb(const std::vector<float>& ir);         // backward-integrated energy decay, dB
double t30Seconds(const std::vector<float>& ir, double fs);            // RT60 from the −5…−35 dB fit
std::vector<float> octaveBand(const std::vector<float>&, double fc, double fs);  // IIR octave band (HP2+LP2), for T30 only
double octaveEnergyFft(const std::vector<float>&, double fc, double fs);        // Σ|FFT|² over the bins in [fc/√2, fc·√2)
double correlation(const std::vector<float>& a, const std::vector<float>& b, int from, int to);
std::vector<float> testSignal(TestSignal kind, double fs, double seconds);
```

`TestSignal` is a fixed, deterministic set: `Impulse`, `Sine1k`,
`PluckBurst` (Zermatt's definition), `SnareBurst` (a 5 ms xorshift-noise burst
with a 60 ms exponential decay), `Xorshift`.

**RT60 is always measured as T30 in the 1 kHz octave band**, with damping
bypassed unless the AC says otherwise. A broadband T30 is dominated by
whatever the damping does to the top octave, and is not what `Decay` promises
(DESIGN §5.4).

**Two band methods, each for one job.**
* **Decay times** (T30 in any band, and every T30 ratio) use `octaveBand`, the
  IIR octave filter. A filter's skirts barely move a decay slope.
* **Band energies and energy ratios** use `octaveEnergyFft`: the sum of |FFT|²
  over the octave's bins, i.e. brick-wall edges. An IIR band filter's skirts
  leak a strong neighbouring octave into a weak one. On the spring's IR, the
  8 kHz / 1 kHz energy ratio reads −16.5 dB with FFT bins, −15.0 dB through an
  8th-order band filter and −12.8 dB through a 4th-order one; an AC on that
  ratio means nothing until the method is fixed.

---

# G0 — Scaffold and pipeline proof

**Goal:** an installable, loadable plugin that **passes the dry signal** with
the full parameter surface, presets and state. This proves the build and
packaging pipeline before any reverb exists.

### Deliverables

```
CMakeLists.txt, .gitignore, README.md          # nassau-eq pattern
cmake/NassauSDK.cmake                          # byte-identical to the siblings'
ci/{build,clean,gates,install,uninstall}.sh    # byte-identical to the siblings'
.github/workflows/ci.yml                       # nassau-eq's (branch main)
Source/DSP/CMakeLists.txt
Source/DSP/bloom_core.h                        # FULL final public API (DESIGN §10)
Source/DSP/bloom_core.cpp                      # G0 body: dry path, mix, output
Source/Plugin/CMakeLists.txt
Source/Plugin/config.h
Source/Plugin/NassauBloomPlugin.{h,cpp}
Source/Plugin/nassau_state.h                   # magic 'NsBl'
Source/Plugin/nassau_presets.h                 # the full table of DESIGN §11
Source/Plugin/resources/NassauBloom-{VST3,AU,CLAP}-Info.plist
Tests/CMakeLists.txt, Tests/test_util.h
Tests/core_tests.cpp, Tests/state_tests.cpp, Tests/preset_tests.cpp
```

In G0, `BloomCore::process()` computes `out = dry · cos θ · outGain`. That is
the mix law of DESIGN §8 with a wet signal of zero. **This is not a
passthrough at the default Mix of 25 %**, which is cos(π/8) = −0.69 dB. That is expected
and documented, and G2 replaces it.

### Acceptance criteria

| # | AC |
|---|---|
| G0.1 | `./ci/gates.sh` passes with **no SDK** (DSP core + tests only). |
| G0.2 | `./ci/gates.sh --sdk ../nassau-plugin-sdk` builds VST3 + CLAP on Windows (and AU on macOS) into `build-ci-plugin/out/`. |
| G0.3 | `kNumParams == 16 == PLUG_N_PARAMS == nassau_presets::kParamCount`; `kNumPresets == 9 == PLUG_N_PRESETS == nassau_presets::kPresetCount`. These are static_asserts plus a ctest check. |
| G0.4 | At `Mix = 0`, `Output = 0 dB`, the output equals the input **bit-exactly** for `Xorshift`, 10 s, block sizes 1, 31, 32, 33, 512 and 4096, in place and out of place. |
| G0.5 | Every preset value lies inside its parameter's range (the range table is in `nassau_presets.h`, and the plugin's `Init*` calls are generated from it, so there is one copy). Preset names are non-empty and unique. |
| G0.6 | The state round-trips: same-count, fewer-stored (defaults kept), more-stored (tail skipped, no overread), bad-magic legacy fallback. This is the nassau-eq `state_tests` battery. |
| G0.7 | `Config` passes `PLUG_WIDTH, PLUG_WIDTH, PLUG_HEIGHT, PLUG_HEIGHT` as the editor's min/max size, not `0, 0, 0, 0` (QBX-219: iPlug2 clamps every reopen-resize to the max size, so 0 makes every editor after the first 0×0). Checked by reading the source in review, and with an editor once G9 adds one. |
| G0.8 | Smaragd scans the installed CLAP and VST3, inserts it on a stereo track, and plays it with the dry signal unchanged at Mix 0. This is a hand check, recorded with the build's commit. |

---

# G1 — DSP primitives (`Source/DSP/bloom_dsp.h`)

Header-only structs, each separately testable: `Biquad` (RBJ LP/HP, DF1,
double), `OnePole` (LP/HP, unity gain in its passband), `DelayLine` (power-of-two
circular buffer; integer and linear-interpolated reads), `Allpass` (the
DESIGN §5.1 form, with an optional modulated read), `StretchedAllpass` (§6.1),
`RotLfo` (a recursive rotation with per-sub-block renormalisation), `Smoother`
(one-pole, snap on reset), `ScopedNoDenormals`.

| # | AC |
|---|---|
| G1.1 | `Biquad` LP and HP at fc ∈ {100, 1000, 10000} Hz, fs ∈ {44.1, 48, 96} kHz: −3.01 ± 0.1 dB at fc; ≥ 12.0 dB rejection one octave into the stopband *for fc ≤ 0.2 fs* (a 2nd-order Butterworth gives 12.3 dB at 2·fc; the bilinear warp only adds to it on the LP side). |
| G1.2 | `OnePole` LP: 0.00 ± 0.01 dB at 20 Hz for fc = 1 kHz; −3.0 ± 0.2 dB at fc for fc ≤ 0.1 fs. HP mirrored. |
| G1.3 | `Allpass`, unmodulated, g ∈ {−0.7, 0.3, 0.5, 0.65, 0.7}: magnitude 0.00 ± 0.01 dB at 20 frequencies across the band. |
| G1.4 | `StretchedAllpass`: measured group delay (from the impulse response, τ(ω) = Re{Σ n·h[n]·e^−jωn / Σ h[n]·e^−jωn}, which is exact for an IR that has decayed within the window; *amended in G1*: phase differencing of sines needs sub-milliradian phase accuracy at 50 Hz, where the delay is ~1 sample) matches `K(1−a)/(1+a)` at 50 Hz and `K(1+a)/(1−a)` at `fs/(2K)` within ±3 %. The closed-form group delay function (used in §6.1's loop length) agrees with the measurement at 1 kHz within ±3 %. |
| G1.5 | `DelayLine` linear read at fractional delay 10.5 of a 1 kHz sine: the output phase matches 10.5 samples within ±0.01 samples. An integer read is bit-exact to the input delayed by n. |
| G1.6 | `RotLfo` over 10⁷ samples at 0.73 Hz, 48 kHz: amplitude stays in 1 ± 1e−6; frequency error < 1e−6 relative. |
| G1.7 | `ScopedNoDenormals`: a feedback one-pole fed an impulse then silence produces no subnormal samples after 10 s at 48 kHz (`std::fpclassify`). On a platform where the guard is the no-op fallback, the test records "skipped (no FTZ on this arch)" and does not fail. |

---

# G2 — The wet path without a reverb

Wire the input filters, pre-delay, width, mix and output, with a temporary
wet = `pre-delayed, filtered input` (i.e. both reverb paths replaced by a
wire). This proves the routing and every bit-exact claim before the reverb
hides them.

| # | AC |
|---|---|
| G2.1 | HPF/LPF at their bypass values, Pre-delay 0, Width 100 %: wet is bit-exact to the input (DESIGN §2, §3, §7). |
| G2.2 | Pre-delay 37.0 ms at 48 kHz: the wet `Impulse` peaks at sample 1776 exactly. At 37.01 ms it is split between 1776 and 1777 (linear read). |
| G2.3 | Equal-power mix: for uncorrelated dry and wet (`Xorshift` with two seeds, the wet injected through a test-only hook; R6 exception, stated), the output RMS at Mix 50 % is within ±0.1 dB of the RMS at Mix 0 and Mix 100. |
| G2.4 | Width 0 %: wet L == wet R bit-exactly. Width 200 % on a pure-side wet (L = −R): wet gain +6.02 ± 0.01 dB. Width on a pure-mid wet: unchanged at every setting. |
| G2.5 | Automating Pre-delay from 0 to 250 ms over 1 s, on `Sine1k`: no sample-to-sample step above 4× the largest step of the unmodulated sine (no click). |
| G2.6 | The input HPF crossing its bypass threshold (20 → 25 Hz step) produces no NaN/Inf, and output stays within ±1.0 of the input's peak. |

---

# G3 — Early reflections

Deliverable: a pure function `computeErTaps(size, order, fs, mic, out[])` in
`Source/DSP/bloom_er.h` (R6 exception: it is tested directly), and the ER path
in `BloomCore`.

| # | AC |
|---|---|
| G3.1 | Tap counts: 24 per channel at order 2, 62 at order 3 (`computeErTaps` returns them). |
| G3.2 | After normalisation, `Σ gain² = 0.25 ± 0.1 %` per channel, for Size ∈ {2, 5, 12, 25, 40} and both orders (DESIGN §4.4). |
| G3.3 | **Measured**: an `Impulse` on both inputs through the ER path only (Late Level 0, Mix 100 %, Width 100 %, damping bypassed): `Σ out²` per channel = 0.25 ± 1 %, for Size ∈ {3, 12, 40} and both orders (coincident taps are merged before normalisation, DESIGN §4.2, so this holds at every Size). |
| G3.4 | The earliest tap's delay equals `(r₁ − r₀)/c` computed independently in the test from the DESIGN §4.1 geometry, ±1 sample. |
| G3.5 | Every tap delay < the ER buffer length, for Size 40, order 3, at 192 kHz. The maximum tap delay is 63 380 ± 2 samples on L and 61 685 ± 2 on R (DESIGN §4.5). |
| G3.6 | L and R tap sets differ: sorting each channel's taps by delay, ≥ 50 % of rank-paired taps differ in delay by ≥ 1 sample, at 48 kHz, Size 12. **Measured** as well: for a mono `Impulse` the ER-only output has L ≠ R. (With the source on the mics' symmetry axis this measured 0 %: DESIGN §4.1.) |
| G3.7 | Size step 5 → 25 m on a running signal. (a) **The crossfade is a crossfade**: on `Xorshift`, over the 1024 samples after the change, the ER output equals `old + (k/1024)·(new − old)` on the k-th sample within 1e−5, where old/new are the outputs of cores held at Size 5 and Size 25. (b) On `Sine1k`, no sample-to-sample step greater than 3× the largest step in the 100 ms before the change. (c) The crossfade completes within 2 × 1024 samples: from then on the output equals a Size-25 core's, bit-exactly. *Amended in G3*: the original step-only criterion, on noise, passed with the crossfade **disabled** (noise already steps by ~0.8 per sample), and on a sine it passed too, by luck of the switching instant. (a) is the half that fails a hard switch. |
| G3.8 | Holding Size constant: no tap recompute happens (counted via `testErRecomputeCount()`, a test-only accessor, present in every build because it costs one int; R6 exception, stated). |

---

# G4 — The Tank

| # | AC |
|---|---|
| G4.1 | **RT60.** T30 in the 1 kHz band, damping bypassed, Plain, Modulation 0, ER Level 0, Mix 100 %: within **±20 %** of the setting for RT60 ∈ {2.0, 8.0} s × Size ∈ {3, 12, 40} m, and for RT60 0.5 s at Size 3 and Size 6. Also Dense, at Size 12, RT60 2.0 s: ±20 %. (No 0.5 s cell at Size 12 or 40: the 0.725 s / 1.0 s loop is longer than the decay, DESIGN §5.5. The tolerance is ±20 % because a correct tank reads 5–15 % long, DESIGN §5.5; simulated worst cell +15.0 %.) |
| G4.2 | **Size invariance.** At RT60 4.0 s, T30 at Size 3 and at Size 40 differ by < 10 %. |
| G4.3 | **Calibration.** At the DESIGN §5.6 reference settings (impulse on both inputs, energy = mean of the two channels), the late-only impulse response (ER 0) holds 0.25 ± 3 %; `kLateGain` is a named constant in `bloom_core.cpp` whose derivation the commit message records. |
| G4.4 | **Energy vs Decay.** At Size 12, the late IR energy at RT60 8 s is +2.0 ± 1.0 dB over RT60 2 s. (The √RT60 law of DESIGN §5.6 would give +3.0 dB if the loop were short against the decay; at a 0.725 s loop the simulated raw ratio is +5.1 dB, and +2.1 dB after compensation.) |
| G4.5 | **Stability at the extremes.** RT60 20 s, Size 40, Diffusion 100 %, Modulation 100 %, Dense, damping bypassed: 60 s of `Xorshift` then 60 s of silence; all samples finite, and the last second's RMS is < −60 dB re the first second of silence. |
| G4.6 | **Damping works.** At RT60 4.0 s, Size 12, Plain, Mod 0: with Damp High 3 kHz, the 8 kHz octave band's T30 is < 55 % of the 1 kHz band's. With Damp Low 500 Hz, the 125 Hz band's T30 is < 60 % of the 1 kHz band's. (At RT60 2 s the simulated ratios are 0.63 and 0.61: the decay already takes most of each loop's loss, so damping shows less.) |
| G4.7 | **Decorrelation.** Mono input (L = R, `Xorshift` 2 s), Width 100 %: the late tail's L/R correlation over 0.2–1.0 s is in [−0.3, 0.3]. |
| G4.8 | **Time invariance at Mod 0.** Two runs of the same `PluckBurst`, the second preceded by 12 345 samples of silence: the outputs are equal after alignment, bit-exactly. At Mod 25 % they are not (that is the modulation working). |
| G4.9 | **Dense is denser.** Late only (ER 0), Size 12, RT60 2.0 s, Mod 0, impulse on both inputs: the Abel–Huang normalised echo density (20 ms window, divided by erfc(1/√2) = 0.3173), averaged over 20–100 ms, is ≥ 1.3 × Plain's, on each channel (simulated ×1.53 / ×1.56). The ER half of Dense is G3.1's tap count. *Do not* measure density as "samples above −40 dB re peak" on the full wet: the peak is then an ER tap and the metric falls when an allpass spreads energy. |
| G4.10 | **Size moves are glides, not clicks.** Size 6 → 16 over 50 ms on a running `Sine1k` tail: the content above 8 kHz (a 4th-order high-pass) during and after the glide stays below −30 dB re the output's peak before the change, and the 24–30 read position never moves backwards (its length grows by < 1 sample per sample; the 0.25 slew limit, +1 % for rounded endpoints, DESIGN §9.3; test-only probe, R6 exception, stated). *Amended in G4*: the original "no step above 4× the steady-state step" cannot work on a tank. A sine's steady amplitude through a reverb depends on how the modes fall around its frequency, which Size changes, so a correct glide read 2.83×. Measured: a correct glide −48.9 dB, every length jumping to its target at once +7.2 dB. |
| G4.11 | **Size is not a level control.** At RT60 2.0 s, late IR energy (as G4.3) at Size 3 and at Size 17 is within ±2 dB of Size 12's (DESIGN §5.6's `S^0.42` term). |

---

# G5 — The Spring

| # | AC |
|---|---|
| G5.1 | **Dispersion.** The cascade alone at 48 kHz, Diffusion 65 %: the measured group delay is monotonically increasing from 200 Hz to 4.5 kHz (sampled at 1/6 octave). The 4 kHz − 200 Hz difference is ≥ 12 ms (computed: 14.5 ms), and the 4.5 kHz − 200 Hz difference is ≥ 25 ms (computed: 26.5 ms). |
| G5.2 | **RT60.** T30 at 1 kHz within ±15 % of the setting for RT60 ∈ {1, 2.2, 5} s (simulated −1.0 / −3.1 / −5.7 %; the AC stops at 5 s for the reason in DESIGN §6.1). |
| G5.3 | **Band limit.** At DESIGN §6.3's reference but with Damp High 20 kHz (the knob open), the 8 kHz octave holds ≤ −15 dB of the 1 kHz octave's IR energy, both measured with `octaveEnergyFft` (bins 707–1414 Hz and 5657–11314 Hz) (the transition frequency caps the spring through the 24 dB/oct LPF pair; computed −16.5 dB, DESIGN §6.1). |
| G5.4 | **Calibration.** As G4.3, at DESIGN §6.3's reference: 0.25 ± 3 %. |
| G5.5 | Stability at the extremes, as G4.5, with Algorithm Spring. |
| G5.6 | L and R springs decorrelate a mono input as G4.7. |

---

# G6 — Switching, performance, golden

| # | AC |
|---|---|
| G6.1 | Algorithm Tank → Spring mid-tail, and back: no step above 4× the steady-state maximum; the crossfade takes 2048 ± 32 samples (rounded to sub-blocks). |
| G6.2 | Complexity Plain → Dense mid-tail, as G6.1. |
| G6.3 | After a switch completes, the old structure is not processed (bench: the Tank preset's CPU is within 5 % of the same settings on a fresh instance). |
| G6.4 | `bloom_golden generate|verify` (the nassau-eq pattern): every preset × `PluckBurst` + `SnareBurst`, 4 s, at 48 kHz, frozen to `Tests/fixtures/golden.bin`; verify tolerance 1e−5 absolute. |
| G6.5 | `bloom_bench` (not a ctest) prints the CPU per preset at 48 kHz, 512-sample blocks, and the per-instance memory at 192 kHz. Both numbers go into R10's summary. Not a pass/fail, but DESIGN §9.5's "about 3.7 MB" is corrected if it is wrong. |

---

# G7 — The presets do their job

Each preset is driven with the signal it was made for, and its claim is
measured. `PluckBurst` stands for guitar and piano, `SnareBurst` for drums,
and `Xorshift` shaped by a 300 Hz–4 kHz bandpass (one 2nd-order RBJ HPF at 300 Hz and one 2nd-order RBJ LPF at 4 kHz, Q 0.7071) stands for voice.

| # | AC |
|---|---|
| G7.1 | Every preset: all samples finite; output peak ≤ 0 dBFS for a −6 dBFS-peak input of its signal. |
| G7.2 | Vocal Booth: wet T30 (1 kHz) < 0.5 s. Guitar Amp Ambience: < 0.8 s. |
| G7.3 | Vocal Ballad Long: T30 (1 kHz) in 3.8 ± 15 %; and T30 in the 8 kHz octave ≤ 85 % of T30 at 1 kHz, both by `octaveBand` (warm: the top decays first; simulated 73 / 74 %). Not an energy ratio: in-loop damping shortens the top's decay and barely tilts the spectrum (simulated wet re dry at 6 kHz: −0.6 dB), DESIGN §5.4. |
| G7.4 | 80s Bright Ballad Hall: T30 at 8 kHz ≥ 60 % of T30 at 1 kHz (bright), and T30 in the 125 Hz band ≤ 70 % of the 1 kHz band (no mud; Damp Low 500 Hz, DESIGN §11). |
| G7.5 | 60s Mono Plate, at Mix 100 % (wet only, DESIGN §8): L == R bit-exactly; and with Late Level also 0, every output sample `== 0.0f` (no ER energy; compare with `==`, which treats −0.0 as zero). |
| G7.6 | Spring Tank: G5.1's monotonic group delay and its ≥ 12 ms (4 kHz) spread hold for the preset's own parameters. |
| G7.7 | Pop Drum Verb: on `SnareBurst`, the output's first 12 ms is the dry hit (wet contributes < −40 dB re the dry in 0–11 ms, because of the pre-delay). |
| G7.8 | Piano on Stage: on `PluckBurst` at 220 Hz, the wet's spectral peak stays at 220 Hz ± 1 Hz (modulation low enough not to detune). |
| G7.9 | Hand check in Smaragd: each preset on its intended source material. Write down what you heard, one line per preset. |

---

# G8 — Plugin-level checks

| # | AC |
|---|---|
| G8.1 | Preset recall through the IPlug2 mechanism (`MakePresetFromChunk`) reproduces the table: in a debug build, recalling each preset and reading back every `GetParam(i)->Value()` equals the table value. |
| G8.2 | The CLAP passes `clap-validator` (if it is available on the dev box); the VST3 passes the SDK's `validator`. |
| G8.3 | 1-2 configuration: Smaragd on a mono track gives a stereo, decorrelated tail. Hand check. |

---

# G9 — Editor (not yet scheduled)

`PLUG_HAS_UI 0` until this gate is written. When it is, G0.7 becomes a gate
AC: open → close → open the editor three times in Smaragd and it is full size
each time (Smaragd's `qxa.plugin_native_editor_reopen` covers the host side).

---

## Gate status

| Gate | Status |
|---|---|
| G0 | DONE except G0.8 (the hand check in Smaragd): `ci/gates.sh` 3/3 on Windows/MSVC and Ubuntu CI; CLAP + VST3 build with the SDK. |
| G1 | DONE: `dsp_tests` 81/81 (Windows/MSVC). Biquad −3.010 dB at fc in all 18 cells, worst stopband −12.30 dB; OnePole −3.010 dB at fc, worst passband −0.0023 dB; Allpass worst 0.0001 dB over 20 freqs × 5 g; StretchedAllpass group delay within 0.03 % of both closed forms (K = 5, a = 0.3/0.6/0.75); linear read 10.50000 samples; RotLfo amplitude error 2.9e−15, frequency error 1.2e−16 over 1e7 samples; ScopedNoDenormals 426 215 subnormal samples without the guard, 0 with it. |
| G2 | DONE: `wetpath_tests` 18/18 (Windows/MSVC). Neutral wet path bit-exact at block sizes 1/32/33/512; 37.0 ms puts the impulse at sample 1776 exactly (1.0, all else exactly 0), 37.01 ms splits 0.5201/0.4799; Mix 50 % within 0.023 dB of Mix 0 and 0.038 dB of Mix 100; Width 200 % on pure side +6.0206 dB, pure mid bit-exact at every width; automated pre-delay largest step 0.0653 against a 0.2611 limit; HPF switch-in peak 0.550 for a 0.500 input. Watched failing: inexact mix end points fail G2.2; a pre-delay read without its one-sample offset fails G2.2 and G2.5. |
| G3 | DONE: `er_tests` 32/32 (Windows/MSVC). 24 / 62 taps enumerated; Σ gain² exact to 0.0000 % at every Size; measured impulse energy 0.25000 per channel at Size 3/12/40, both orders; first tap within 0.43 samples of independent geometry; longest tap at 192 kHz 63 380 (L) / 61 685 (R), buffer 76 800; all 24 rank-paired L/R taps differ, and a mono impulse gives L ≠ R; Size-step crossfade matches the linear blend to 2.8e−8 and is bit-exact to a Size-25 core after 2048 samples; one recompute per change, none while Size holds or moves < 0.5 %. Watched failing: crossfade disabled fails G3.7(a) (error 0.5); the original step-only G3.7 did not fail it. |
| G4 | DONE: `tank_tests` 25/25 (Windows/MSVC). `kLateGain` = 0.261721 (raw reference energy 3.6497 → 0.25). T30 at 1 kHz +7.1 … +15.0 % across the G4.1 cells, worst +15.0 % (RT 8 s, Size 3); Size 3 vs 40 at RT 4 s 5.5 %; RT 8 s vs 2 s +2.08 dB; Size 3 / 17 vs 12: +0.04 / +0.72 dB; damping ratios 0.44 / 0.44; L/R correlation +0.008; bit-exact time invariance at Mod 0; echo density ×1.53 / ×1.56; glide −48.9 dB above 8 kHz, read growth ≤ 0.25005 samples/sample; extremes finite, −158 dB after 60 s. These match the DESIGN.md simulations to the millisecond. Watched failing: the double cross-feed gain halves every RT60 (2 s → 1.10 s); an instant length jump +7.2 dB. |
| G5–G8 | not started |
| G9 | not scheduled |

## Dependency graph

```
G0 ─► G1 ─► G2 ─► G3 ─┐
             │        ├─► G6 ─► G7 ─► G8
             └─► G4 ─► G5 ┘
```

G3 (ER) and G4/G5 (late) are independent of each other once G2 has
the wet path. G6's golden fixture is generated only after **all** of G3–G5,
because any voicing change invalidates it.
