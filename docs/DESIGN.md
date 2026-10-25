# NassauBloom — Design

**NassauBloom** is a **stereo** algorithmic reverb plugin. It ships as VST3 and
CLAP, plus AU on macOS. Like its siblings it is built on the Nassau plugin
stack, **IPlug2 + Steinberg VST3-SDK** (not JUCE), and consumes the shared
[`nassau-plugin-sdk`](../../nassau-plugin-sdk) through `NASSAU_SDK_DIR`.

It is meant to be a *simple* reverb, the one you reach for first. There are
two paths you can mix, a room's early reflections and a Lexicon-style
late-reverb tank, a short list of knobs a mixing engineer already knows, and
eight presets made for a particular job.

It follows the `nassau-eq` / `nassau-zermatt` house architecture exactly:

* `Source/DSP/` — **`BloomCore`**, a dependency-free C++17 class. It builds and
  unit-tests on its own, with **no** SDK, IPlug2 or framework dependency.
* `Source/Plugin/` — a thin IPlug2 wrapper: `config.h`, `NassauBloomPlugin.*`,
  `nassau_state.h`, `nassau_presets.h`, and later `NassauBloomUI.*`.
* `Tests/` — the hand-rolled harness (`int main` plus soft checks), a
  golden-parity fixture and a benchmark.

The non-negotiables are the same as in `nassau-eq`:

* **Lock-free params.** Every setter stores into a `std::atomic`; `process()`
  only loads them. There is no mutex, no allocation and no I/O in the audio
  path.
* **Block-based and safe in place**: `out` may alias `in`.
* Coefficient math is done in `double`. Filter and delay-line state is `float`
  (delay lines are large; see §9). Public I/O is `float`.
* **No `-ffast-math`.** Recursive feedback networks make it unsafe.
* **Append-only parameter discipline**, because state is serialized
  positionally.
* Create once, never copy or move. All buffers are allocated in `init()`.

The schedule is in [`GATES.md`](GATES.md). This file is the specification;
where the two disagree, this file wins.

---

## 1. Signal chain

```
            ┌───────────────────────────── dry (untouched) ─────────────────────────────┐
            │                                                                           │
 in L/R ────┤                                                                          Mix ──► Output ──► out L/R
            │                                  ┌─► Early reflections ─► × ER level ─┐  ▲
            └─► Input HPF ─► Input LPF ─► Pre-delay                                  ├─► Width ─┘
                                               └─► Late network ──────► × Late level┘
                                                   (Tank or Spring)
```

1. **Dry tap.** The input is kept, untouched, for the dry side of the mix.
2. **Input filters** (wet side only): a 12 dB/oct high-pass, then a
   12 dB/oct low-pass (§2).
3. **Pre-delay**, 0–250 ms, stereo (§3).
4. Then the signal **splits into two parallel paths**, both fed from the
   pre-delay output:
   * **Early reflections**: a computed tapped delay line (§4), scaled by
     `ER Level`, 0–200 %.
   * **Late network**: the *Tank* (§5) or the *Spring* (§6), chosen by
     `Algorithm` and scaled by `Late Level`, 0–200 %.
5. **Sum** the two paths, then apply **Width**, a mid/side stage on the wet
   signal only (§7).
6. **Mix**: an equal-power dry/wet crossfade (§8), then **Output** gain.

**"As designed" = 100 %.** Each level knob runs from 0 to twice its designed
gain. 100 % is the calibrated gain in §4.4 and §5.6, which the presets are
voiced against; 200 % is +6 dB on that path. It is a linear amplitude scale:
50 % is −6 dB and 0 % is silence.

The late network is fed from the pre-delay output, **not** from the early
reflections. Feeding the ER output into the tank would make the late tail
depend on `ER Level`, and "turn the ERs down" would then also shorten the
tail's build-up. Keeping the two paths independent means each knob does one
thing.

### 1.1 Latency

0 samples. Every stage is causal and unbuffered. `PLUG_LATENCY 0`.

### 1.2 Channel configuration

`PLUG_CHANNEL_IO "1-2 2-2"`.

* **2-2** is the normal case.
* **1-2** (mono in, stereo out) is the usual place for a reverb on a mono
  source. The wrapper copies the single input to both channels before calling
  the core, so the core is always stereo-in. A mono source then produces a
  *decorrelated* stereo tail; the Tank's L and R injections differ (§5.2), so
  the stereo comes from the network, not from the input.
* There is no 1-1. A mono reverb is `Width = 0` on a stereo instance, which is
  what the "60s Mono Plate" preset does.

---

## 2. Input filters

These filter the wet signal only, before the pre-delay. Each one is an RBJ
biquad (Q = 0.7071, Butterworth), coefficients in `double`, Direct Form I state
in `double`.

| Filter | Param | Range | Default | Bypassed at |
|---|---|---|---|---|
| High-pass | `kInputHpf` | 20 … 1000 Hz | 20 Hz | ≤ 20.0 Hz |
| Low-pass | `kInputLpf` | 1000 … 20000 Hz | 20000 Hz | ≥ 20000.0 Hz |

* **Bypass is exact.** At the bypass value the filter is skipped, not set to an
  extreme cutoff. With both filters bypassed, the signal reaching the pre-delay
  is bit-identical to the input.
* **The LPF cutoff is clamped to 0.45 · fs** whenever it is not bypassed. At
  44.1 kHz a 19 kHz LPF would otherwise put its pole at 0.43 · fs, where the
  RBJ design is still stable, but the clamp keeps the coefficient math away from
  the Nyquist singularity at any rate.
* **Crossing the bypass threshold** (for example, automation moving the HPF
  from 20 Hz to 25 Hz) switches the filter in with its state zeroed. The first
  few samples after that are a filter start-up transient on a *wet* signal
  that then goes through a reverb. That is inaudible, and cheaper than running
  a filter that does nothing. It is a deliberate choice; don't "fix" it with a
  crossfade.
* Coefficients are recomputed per **sub-block** (§9.2) from the smoothed
  cutoff, never per sample.

---

## 3. Pre-delay

`kPreDelay`: 0 … 250 ms, default 10 ms.

* Stereo: one delay line per channel, of at least `ceil(0.250 · fs) + 4`
  samples, rounded up to a power of two like every delay line (G1's
  `DelayLine`).
* **Read with linear interpolation** at a fractional position. The delay time
  is smoothed with a 50 ms one-pole (§9.1), so automating it produces a short
  pitch glide, not a click. That is the expected behaviour of a pre-delay knob
  on hardware units.
* At 0 ms the read is at the write position with zero fractional offset. The
  output then equals the input exactly (an integer read, no interpolation
  arithmetic), which keeps the bypass-exact chain of §2 exact.

---

## 4. Early reflections — an image-source shoebox

The ER path is a **tapped delay line** whose taps come from an image-source
model of a rectangular room. It suggests a room size without the cost of a
real room simulation, and the taps are recomputed whenever `Size` changes.

### 4.1 Geometry

`kSize` is the room's **longest dimension** in metres: 2 … 40 m, default 12 m.

The room is `W × D × H` with

```
W = Size
D = 0.72 · Size
H = clamp(0.38 · Size, 2.4, 12)   // ceilings don't scale forever, nor drop below a person
```

These positions are fractions of the room (x along W, y along D, z along H):

| Point | x | y | z |
|---|---|---|---|
| Source | 0.42 W | 0.28 D | 0.40 H |
| Left mic | 0.50 W − s | 0.68 D | 0.45 H |
| Right mic | 0.50 W + s | 0.68 D | 0.45 H |

`s = clamp(0.06 · Size, 0.3, 1.2)` m is half the spacing of an AB pair. It
yields usable inter-channel delay differences without HRTFs, which a stereo
reverb does not need. It is not an ear model, on purpose.

The source sits off **every** centre line of the room, and in particular off
the mics' axis of symmetry x = 0.5 W. With the source at 0.5 W the whole setup
is mirror-symmetric in that plane, the L and R tap sets are the same multiset
of (delay, gain), and the ER path is mono for a mono source. A symmetric
placement also gives coincident images, and taps with equal delays are a comb
filter.

### 4.2 Taps

For each mic separately, enumerate the images `(nx, ny, nz)` with reflection
order `o = |nx| + |ny| + |nz|`, `1 ≤ o ≤ O`:

* `O = 2` in **Plain**: 6 + 18 = **24 taps per channel**.
* `O = 3` in **Dense**: 6 + 18 + 38 = **62 taps per channel**.

The direct path (`o = 0`) is excluded, because it is the dry signal and the mix
handles it.

**Which input each mic hears.** The left mic's taps read the left input and the
right mic's the right input, as if each channel were its own source at the
same position. For a mono source (L = R) that is exactly the one-source room,
and the two tap sets still differ (§4.1), so the ER is stereo.

For an image at distance `r`, with direct-path distance `r0` (to the same mic):

```
delay   = (r − r0) / c                 c = 343 m/s
gain    = (r0 / r) · β^o               β = 0.85  (amplitude reflection coefficient, ≈ α 0.28)
sign    = ±1                           // see below
```

* **The delay is relative to the direct path.** The first reflection therefore
  arrives at `PreDelay + (r_1 − r0)/c`, and `Pre-delay` keeps its studio
  meaning, "the gap before the reverb starts".
* **Sign.** A rigid wall reflects without inverting, so physics says every
  tap is positive. NassauBloom alternates the sign **by delay-sorted rank**
  instead: after sorting a channel's taps by delay, taps 0, 2, 4, … are
  positive and 1, 3, 5, … negative. This is not physics. It is a
  decorrelation device that removes the DC build-up and much of the
  comb-filter colouration of a dense tap set. Because the L and R delay
  patterns differ (the geometry is asymmetric), so do their sign patterns. A
  sign by image index parity, `(−1)^(nx+ny+nz)`, would **not** do this: it
  equals `(−1)^o`, one sign per reflection order. The golden fixture pins the
  choice.
* **Delays are rounded to the nearest integer sample.** At 48 kHz that is
  ±0.36 cm of path, far below anything audible in an ER pattern. Integer taps
  cost a load and a multiply-add each. Taps that round to the **same** delay
  are merged into one (their gains added) before signing and normalisation (§4.4),
  so the measured energy equals the computed one at every Size. Coincident
  taps would otherwise add coherently: at Size 3, order 3, unmerged, the
  impulse energy comes out 2.5 % low. (The tap *count* of §4.2 is the count
  before merging.)

### 4.3 Wall damping

The ER sum per channel passes through a one-pole low-pass at `kDampHigh` (the
same knob as the late network's wall damping, §5.4) and a one-pole high-pass at
`kDampLow`. One knob describes "the walls" for both paths. A per-tap,
per-order filter would be more faithful, and with 62 taps it is 62 filters; the
single filter is the "simple reverb" trade.

### 4.4 Calibration ("as designed")

For each channel, in this order: compute the taps, **round** their delays,
**merge** taps that land on the same sample, **sign** them by delay-sorted rank
(§4.2), then scale all of them by a factor `k` so that the **tap energy** sums
to −6 dB. Signing after merging matters: adjacent-rank taps have opposite
signs and are the ones most likely to round together, so signing first would
make merged pairs nearly cancel.

```
Σ gain_i² · k² = 0.25           (per channel, before ER Level)
```

At `ER Level = 100 %` the ER cluster therefore carries a quarter of the input's
energy, regardless of `Size` or `Complexity`. Without this normalisation a
large room would be 20 dB quieter than a small one (the `r0/r` law), and every
`Size` move would also be a level move.

### 4.5 Changing Size

Taps are recomputed **on the audio thread, at a sub-block boundary**, when the
target `Size` has moved by more than 0.5 % since the last computation. That
costs ≤ 62 × 2 square roots and divisions, into two pre-allocated fixed arrays
(`kMaxTaps = 62` per channel, two sets for the crossfade). It allocates nothing
and takes no lock.

The new tap set is not swapped in directly; jumping between two tap sets
clicks. Instead there are **two tap sets, A and B**:

* The new taps go into the inactive set.
* The ER output crossfades from the old set to the new one, linearly, over
  **1024 samples** (21 ms at 48 kHz).
* While a crossfade runs, a further change is **deferred**, not stacked: it
  starts when the current crossfade finishes. Holding the `Size` knob steady
  converges within two crossfades.

The ER delay line is a single circular buffer per channel, shared by both tap
sets. Its length is the largest possible tap delay at `Size = 40 m`, order 3,
plus margin: `ceil(0.40 · fs)` samples, rounded up to a power of two. The
worst-case order-3 tap at Size 40 is on the **left** mic: a path difference
of 113.2 m (r0 = 11.71 m) = 0.330 s, 63 380 samples at 192 kHz. The right
mic's is 110.2 m, 61 685 samples. GATES G3.5 checks both; the buffer
(76 800 → 131 072 samples) is ample.

### 4.6 What `Size` does *not* do here

It does not change the ER *level* (§4.4) or the wall damping (§4.3). It changes
only the timing pattern, which is the cue the ear uses for room size.

---

## 5. Late network — the Tank (Lexicon-style figure-of-eight)

`kAlgorithm = Tank` (0). The topology is Jon Dattorro's plate reverberator
("Effect Design, Part 1", JAES 45(9), 1997, Fig. 1 and Table 1). That paper
documents a Lexicon-family "figure-of-eight" tank: two cross-coupled branches,
each made of an allpass, a delay, a damping filter, a decay gain, a second
allpass and a second delay. Each branch's output feeds the *other* branch's
input.

### 5.1 Allpass form

Every allpass in this document is the Schroeder lattice form with feedforward
coefficient `−g` and feedback `+g`:

```
v[n] = x[n] + g · v[n − M]
y[n] = −g · v[n] + v[n − M]
```

so that `g > 0` means the same thing everywhere. Dattorro's figure prints a
**negative** coefficient for the tank's first allpass in each branch (the
"decay diffusion 1" allpasses, 23–24 and 46–48). NassauBloom keeps that sign:
in the form above those two use `g = −0.70`. All other allpasses use the
positive coefficients listed.

### 5.2 Input diffusion — one chain per channel

Dattorro sums L+R to mono before four series allpasses. NassauBloom keeps
**one chain per channel** instead, so that a stereo source keeps some of its
image in the tail, and so that the 1-2 configuration (§1.2) still has two
distinct injection signals after the copy:

| Stage | L length @29761 Hz | R length @29761 Hz | Coefficient |
|---|---|---|---|
| AP1 | 142 | 151 | `0.75 · D` |
| AP2 | 107 | 113 | `0.75 · D` |
| AP3 | 379 | 401 | `0.625 · D` |
| AP4 | 277 | 293 | `0.625 · D` |

`D = kDiffusion / 100`, from 0 to 1, default 0.70. At `D = 0` the chain is a
pure delay (each allpass with `g = 0` is `y[n] = v[n − M]`), so the tail gets
the transient's full sharpness, as wanted on percussion with a slow decay.

The R lengths are the L lengths × ~1.06, rounded to a nearby **prime**. That
keeps the two chains from sharing a common period. With identical inputs (the
1-2 case) the two injections are equal only to the extent the chains are,
which is not at all after the first few milliseconds.

The **L chain's output feeds the left branch**, and the R chain's the right.

### 5.3 The tank

Lengths are Dattorro's (at 29761 Hz), scaled by `fs / 29761 · S`, where
`S = clamp(Size / 12, 0.25, 1.4)` is the tank's size scale (12 m is
Dattorro's nominal). Lengths are rounded to integers *after* scaling, with the
fractional part kept for the modulated reads.

**Why S is capped at 1.4.** One full figure-of-eight loop is 21 589 samples at
29761 Hz, 0.725 s at S = 1. An uncapped `Size/12` reaches S = 3.33 at 40 m, a
2.4 s loop: that is an echo, not a hall, and with a short decay the impulse
response is a single pass through the output taps. At the cap the loop is
1.0 s. Above about 17 m the tank stops growing, and the room-size cue is the
ER pattern (§4), which keeps scaling to 40 m. At the low end, S = 0.25 makes a
0.18 s loop, which is what a 0.35 s booth needs.

**Left branch** (input: L diffusion output + right branch output):

| Node | Element | Length | Coefficient |
|---|---|---|---|
| 23–24 | modulated allpass | 672 (+ excursion) | `g = −0.70` (decayDiff1) |
| 24–30 | delay | 4453 | — |
| 30 | damping LPF + HPF (§5.4) | — | — |
| 30 | decay gain `gL` (§5.5) | — | — |
| 31–33 | allpass | 1800 | `decayDiff2 = clamp(gL + 0.15, 0.25, 0.50)` |
| 33–39 | delay | 3720 | — |

**Right branch** (input: R diffusion output + left branch output):

| Node | Element | Length | Coefficient |
|---|---|---|---|
| 46–48 | modulated allpass | 908 (+ excursion) | `g = −0.70` (decayDiff1) |
| 48–54 | delay | 4217 | — |
| 54 | damping LPF + HPF | — | — |
| 54 | decay gain `gR` | — | — |
| 55–59 | allpass | 2656 | `decayDiff2 = clamp(gR + 0.15, 0.25, 0.50)` |
| 59–63 | delay | 3163 | — |

`decayDiff2` follows the decay, as in Dattorro (§1.3.4 of the paper), so that
short decays aren't smeared by a dense second allpass.

**Cross-feed.** The end of each branch (node 39, node 63) is summed into the
other branch's input **at unity gain**. That is the figure-of-eight: one full
loop runs through both branches.

This is a deliberate deviation from Dattorro's figure, which multiplies by
the decay gain a second time at the cross-feed. With two multiplies per
branch, the §5.5 formula would lose its 60 dB in half the time, and the
measured RT60 would be half the setting. One multiply per branch keeps the
formula exact (simulated at Size 12: 0.54, 2.15 and 8.8 s for settings of
0.5, 2 and 8 s).

**Dense (`kComplexity = 1`)** adds the following to each branch:

* The fixed allpass at 31–33 / 55–59 becomes **modulated** too, with its own
  LFO (§5.7).
* A **short diffusing allpass** goes between the modulated allpass and the
  first delay (between 23–24 and 24–30, and between 46–48 and 48–54):
  **L 331, R 397** @29761 Hz, fixed coefficient **g = 0.65**.
  * *Where.* Before the long first delay, on purpose. After the damping,
    nothing would reach it until the first delay had passed (150 ms at
    Size 12), so it could not change the early tail at all. Before it, it
    smears the signal that the 24–30 and 48–54 output taps read on the first
    pass.
  * *Why short.* A long allpass (an earlier draft had 1410/1938 at
    `decayDiff2` ≈ 0.44) adds isolated echoes 47/65 ms apart and makes the
    early tail *peakier*, not denser. Simulated at Size 12, 20–100 ms,
    late only, Abel–Huang normalised echo density: Plain 0.257/0.282 (L/R),
    long allpass 0.246/0.259, the short one 0.393/0.441 (×1.53/×1.56).
* ER order 3 instead of 2 (§4.2).

Plain is Dattorro's tank with the two deviations stated above (a per-channel
input chain, unity cross-feed). It is the cheaper option and the more "60s"
one; the presets choose per use.

### 5.4 Wall damping (in the loop)

Each branch has, after its first delay:

* a one-pole **low-pass** at `kDampHigh`: 1000 … 20000 Hz, default 8000 Hz.
  Bypassed (exact) at ≥ 20000 Hz.
* a one-pole **high-pass** at `kDampLow`: 20 … 1000 Hz, default 20 Hz.
  Bypassed (exact) at ≤ 20 Hz.

These are filters **inside a feedback loop**, so their effect compounds with
every pass. A 6 kHz in-loop LPF does not mean "content above 6 kHz is
attenuated by 3 dB"; it means the **decay time** above about 6 kHz is shorter,
increasingly so with frequency. That is what wall absorption does physically,
and why the parameter is called damping and not EQ. The decay time a user sets
(§5.5) is accurate at frequencies where both filters are close to unity; the
gates measure it at 1 kHz with damping bypassed.

### 5.5 Decay

`kDecay` is the **RT60** in seconds: 0.2 … 20 s, default 2.0 s, with a
logarithmic-feeling taper (`ShapePowCurve(3)`).

The gain per branch follows from that branch's **total loop-segment length**
`Lb` in samples, including the allpasses' nominal lengths (a long allpass has a
group delay averaging its length):

```
Lb_L = len(23–24) + [len(extra AP), Dense only] + len(24–30) + len(31–33) + len(33–39)
gL   = 10^( −3 · Lb_L / (RT60 · fs) )
gR   = likewise with the right branch's lengths
```

`gL` is applied **once** per branch (at node 30, and `gR` at node 54; the
cross-feed is unity, §5.3), so a pass through a branch loses exactly
`60 dB · (Lb / (RT60 · fs))`. The
`Size` scale `S` enters through `Lb`, so **RT60 does not change with `Size`**:
a bigger room with the same decay is a sparser, longer-period tail with the
same 60 dB time. That holds while RT60 is long against the loop. A setting
shorter than about one loop (0.18 s at S = 0.25, 1.0 s at S = 1.4) is
dominated by the first pass through the output taps, and its measured T30 is
longer than the setting. The presets with short decays use small rooms.

**The measured T30 runs about 5–15 % long, systematically.** The in-loop
allpasses give the loop's modes different effective lengths (an allpass's
group delay lies between `M(1−g)/(1+g)` and `M(1+g)/(1−g)`, not at `M`), the
slow modes dominate the late slope of a band's decay curve, and the bias grows
with `decayDiff2`, which is largest at long decays. Simulated: +7 … +15 %
across Size 3–40 and RT60 0.5–8 s. The formula is kept clean (no fudge factor
on `Lb`), and the gates allow ±20 %.

The gains are recomputed per sub-block from the smoothed `Decay` and the
smoothed `Size`.

### 5.6 Output taps and calibration

Each output channel is the signed sum of **seven taps** read from the four
delay lines and two allpass interiors, at Dattorro's Table 2 positions (at
29761 Hz, scaled like everything else):

Node names follow §5.3:

```
yL = + d48_54[266]  + d48_54[2974] − ap55_59[1913] + d59_63[1996]
     − d24_30[1990] − ap31_33[187]  − d33_39[1066]
yR = + d24_30[353]  + d24_30[3627] − ap31_33[1228] + d33_39[2673]
     − d48_54[2111] − ap55_59[335]  − d59_63[121]
```

`d[k]` is the delay line's content `k` samples behind its write position;
`ap[k]` is the same for the allpass's internal delay line `v`.

**Calibration ("as designed").** The tap sum is multiplied by `kLateGain`,
a constant measured once (GATES G4) such that, at `Size = 12 m`,
`RT60 = 2.0 s`, damping bypassed, Plain, `Diffusion = 0.70`, Modulation 0,
for a unit impulse on **both** inputs (L = R, a mono source), with the energy
taken as the mean of the two output channels:

```
Σ (yL[n]² + yR[n]²) / 2 over the impulse response = 0.25   (−6 dB, the same as the ER path)
```

A longer decay holds more energy (energy ∝ RT60 for an exponential tail). If
that were left as it is, a 20 s setting would be +10 dB over a 2 s one, which
makes `Decay` a level control and clips easily. The output therefore also gets
a **partial compensation**:

```
gOut = kLateGain · (RT60 / 2.0)^(−1/4)        (before the Size term below)
```

This keeps tail *energy* ∝ √RT60 for long decays, so +5 dB at 20 s instead
of +10, and −2.6 dB at 0.6 s instead of −5. Long decays still sound bigger, as
they should, without running away. At decays not much longer than the loop
the raw energy grows more slowly than ∝ RT60 (simulated at Size 12: +5.1 dB
raw from 2 s to 8 s, +2.1 dB after the compensation). The exponent is a
voicing decision, pinned by the golden fixture.

**Size compensation.** The late energy also depends on `S`: a small tank packs
more passes into the same decay. Simulated at RT60 2 s (with the old,
uncapped scale), Size 3 held +5.1 dB more than Size 12. The output therefore
also gets `S^(0.42)` (energy ∝ S^−0.84 is the simulated slope), so that
`Size` is not also a level control on the late path:

```
gOut = kLateGain · (RT60 / 2.0)^(−1/4) · S^(0.42)
```

GATES G4.11 checks the result. The exponent is fitted, not derived: if the
gate measures a different slope, the gate's number wins and this line is
corrected.

### 5.7 Modulation

`kModulation`: 0 … 100 %, default 25 %.

Each modulated allpass's read position moves by
`± (kModulation/100) · 12 · (fs/29761)` samples. That is 12 samples at
29761 Hz, a little under Dattorro's 16, chosen so that 100 % does not
audibly detune piano; it does **not** scale with `Size`. The LFO is a sine
with these rates (Hz):

| Allpass | Plain | Dense |
|---|---|---|
| L 23–24 | 0.73 | 0.73 |
| R 46–48 | 0.89 | 0.89 |
| L 31–33 | — | 1.07 |
| R 55–59 | — | 0.61 |

Fixed initial phases are 0, ¼, ½ and ¾ of a cycle, in table order.

* **The LFO is computed by a recursive rotation** (a 2×2 rotation of a
  `(cos, sin)` pair per sample, renormalised once per sub-block), not
  `std::sin` per sample.
* **The read uses linear interpolation.** Its HF loss varies with the
  fractional position, and at excursions of a few samples that is a few tenths
  of a dB above 10 kHz, in a signal that is being damped anyway. Dattorro uses
  allpass interpolation; linear avoids allpass interpolation's own
  feedback-state transients when the rate changes, and is good enough.
* At `kModulation = 0` the read is integer (no interpolation), and the tank is
  time-invariant. The "60s Mono Plate" and "Spring" presets rely on that.

### 5.8 Stability

* Every in-loop element has magnitude response ≤ 1: allpasses with `|g| < 1`,
  one-pole filters with unity DC/HF gain, decay gains `g < 1` because
  `RT60 ≥ 0.2 s`. The loop gain is therefore < 1 at every frequency, for every
  parameter value. GATES G4 checks this by measurement too (the tail energy is
  finite and decreasing at the extremes).
* **Denormals.** The tank decays into the subnormal range on every note-off.
  `process()` sets FTZ/DAZ for its duration with a scoped guard (the
  `nassau-zermatt` `ScopedNoDenormals` pattern: `_mm_setcsr` on x64,
  `FPCR.FZ` on arm64, a no-op fallback elsewhere), restoring the caller's mode
  on exit.

---

## 6. Late network — the Spring

`kAlgorithm = Spring` (1). This replaces the Tank with a **two-spring
dispersive model**. Nothing a figure-of-eight tank can do sounds like a spring:
the defining sound is the *chirp*, a click smeared into a downward or upward
sweep because a helical spring is dispersive (its group delay depends on
frequency). That needs its own structure, and it gets one.

The model follows Välimäki, Parker & Abel, "Parametric Spring Reverberation
Effect" (JAES 58(7/8), 2010), simplified to a "low-chirp" path only.

### 6.1 One spring

```
x ─► (+) ─► Dispersion (M stretched allpasses) ─► Delay T ─► LPF fc ─► HPF ─► × gS ─┐
      ▲                                                                              │
      └──────────────────────────────────────────────────────────────────────────────┘
                                                              out = after the LPF pair
```

* **Dispersion.** A cascade of `M = 80` *stretched* first-order allpasses
  ```
  H(z) = (a + z^−K) / (1 + a · z^−K)
  ```
  with `K = max(1, round(fs / (2 · fc)))` and `fc = 4500 Hz`, the spring's
  transition frequency. For `a > 0` each section's group delay rises from
  `K(1−a)/(1+a)` at DC to `K(1+a)/(1−a)` at `fs/(2K)` ≈ fc. Over the
  cascade, **high frequencies arrive later than low ones**, which is the
  low-chirp of a real spring.
  * The chirp amount is `a = 0.30 + 0.45 · D`, where `D = kDiffusion/100`
    (the same knob as the Tank's diffusion, reinterpreted). At 48 kHz,
    `K = 5`, `a = 0.6`, so `M = 80` gives a group delay of 100 samples at DC,
    about 800 samples at 4 kHz and 1600 samples at fs/(2K) = 4.8 kHz: a
    ~30 ms chirp end to end, ~14.5 ms of it between 200 Hz and 4 kHz.
  * `M` is fixed, so the cost is fixed: 80 sections of a K-sample delay and
    two multiply-adds per spring, per sample.
* **Delay T** = `25 ms + Size · 1.0 ms`: 27 … 65 ms (`Size` reinterpreted as
  spring length). The R spring's T is the L spring's × 1.071 and its `a` is
  the L spring's − 0.02, so the two don't fall into lockstep.
* **LPF**, two **cascaded** RBJ biquads (Q = 0.7071 each, 24 dB/oct) at
  `min(kDampHigh, fc)`. A real spring passes very little above its transition
  frequency, and the damping knob can darken the spring but not brighten it
  past that. One biquad is not steep enough: in the closed loop the 8 kHz
  octave then holds −7.7 dB of the 1 kHz octave's energy (the 8 kHz octave is
  8× wider). Two give about −16.5 dB.
* **HPF**, one-pole at `kDampLow`, as in the Tank.
* **Feedback gain** `gS = 10^(−3 · Tloop / (RT60 · fs))`, with `Tloop` the
  loop's total length in samples: `T · fs` plus the cascade's group delay at
  1 kHz (computed in closed form per section, not measured), so that `Decay`
  means RT60 at 1 kHz here as well.
* **The measured T30 runs slightly short, increasingly with RT60.** Unlike the
  Tank's allpasses (§5.5), the stretched allpasses have a smooth group delay
  across the 1 kHz octave, so there is no mode-length bias. What there is
  comes from the LPF pair inside the loop: 0.02 dB per loop at 1 kHz and
  0.08 dB at the band's 1414 Hz edge, against only 0.45 dB per loop of
  designed decay at 5 s. Simulated: −1 % at 1 s, −3 % at 2.2 s, −6 % at 5 s,
  and roughly −15 … −20 % at 20 s. GATES G5.2 therefore stops at 5 s.

* **Size glides.** `T` moves as the Tank's lengths do (§9.3): a one-pole
  smoother (150 ms) towards the *rounded* target, slew-limited to 0.25
  samples per sample, read with linear interpolation while it moves and with
  an integer tap once it has arrived. `gS` follows the moving `T` per
  sub-block. The smoother snaps within 10⁻⁶ samples: at `T` ≈ 2600 a step of
  the 10⁻⁹ snap that `Smoother` uses elsewhere falls below a double's
  resolution, and the glide would stall just short of the integer.

### 6.2 Stereo

The L spring is fed from the L input (after pre-delay) and the R spring from
R. In the 1-2 configuration both get the same signal, and the springs'
different T and `a` decorrelate them.

### 6.3 Calibration

`kSpringGain` is measured as in §5.6, at `Size = 10`, `RT60 = 2.0 s`,
`D = 0.65`, damping 4500 / 20 Hz, to the same −6 dB energy target. The same
`(RT60/2)^(−1/4)` compensation applies.

### 6.4 What the Spring ignores

`kModulation` and `kComplexity` have no effect in Spring mode. The **ER path
still runs** (it is independent of `kAlgorithm`, §1), and the Spring preset
sets `ER Level = 0` because a spring tank has no room.

---

## 7. Width

`kWidth`: 0 … 200 %, default 100 %. It applies to the **wet** sum (ER + late)
only, after the paths are summed and before the mix:

```
M = (L + R) / 2
S = (L − R) / 2 · w             w = kWidth / 100
L' = M + S
R' = M − S
```

* `w = 0` is mono wet, `w = 1` is unchanged (bit-exact: the stage is skipped),
  and `w = 2` doubles the side signal.
* The dry signal is never touched; a reverb's width knob describes the
  reverb.
* **Mono compatibility at w > 1.** Doubling S raises the side's energy
  relative to M; summed to mono the wet loses nothing it had (M is unchanged),
  which is the point of doing width in M/S. Nothing here phase-inverts M.

---

## 8. Mix and output

`kMix`: 0 … 100 %, default 25 %. It is an **equal-power** crossfade:

```
θ    = (kMix / 100) · π / 2
out  = dry · cos θ + wet · sin θ
```

* **The gains are computed off the per-sample path.** `cos θ`, `sin θ` and
  the output's `10^(dB/20)` are computed per sub-block (§9.2) from the
  parameter targets, and it is these **linear gains** that are smoothed per
  sample (§9.1). Nothing calls `std::cos` or `std::pow` per sample (GATES R5).
* At `kMix = 0` the output is the dry input, **bit-exact**: `cos 0 = 1`, the
  wet multiplier is exactly 0, and the implementation skips the wet path's
  *mix-in*. At `kMix = 100 %` it likewise skips the dry mix-in (`cos(π/2)` is
  6.1e−17 in double, not 0), so a wet-only output is exactly the wet. The wet path still runs, so its state stays current and turning
  the mix up never starts from a stale tank.
* At `kMix = 100 %` the output is wet only, which is the setting for a reverb
  on an aux send.
* 50 % is −3 dB on each side.

`kOutputGain`: −24 … +12 dB, default 0 dB, a final gain on the mixed signal.

---

## 9. Real-time structure

### 9.1 Smoothing

Every continuous parameter has a one-pole smoother, stepped **per sub-block**
(§9.2) for coefficient-type parameters and **per sample** for gain-type ones:

| Param(s) | Time constant | Stepped |
|---|---|---|
| Mix and Output (as linear gains, §8), ER Level, Late Level, Width | 20 ms | per sample |
| Input HPF/LPF, Damp High/Low, Decay, Diffusion | 30 ms | per sub-block |
| Pre-delay | 50 ms | per sample |
| Size (tank scale and spring T) | 150 ms | per sub-block |
| Size (ER taps) | — | thresholded recompute + crossfade (§4.5) |
| Modulation | 30 ms | per sub-block |

Enums (`Complexity`, `Algorithm`) are **not** smoothed. See §9.4.

`reset()` snaps every smoother to its target. It does not ramp from zero.

### 9.2 Sub-blocks

`process()` walks its block in **sub-blocks of 32 samples** (the last one may be
shorter). Coefficients (biquads, one-poles, decay gains, allpass `g`, tank
lengths) are recomputed once per sub-block from the smoothed values. A 32-sample
grid at 48 kHz is 0.67 ms, finer than any of the smoothing time constants
above, so the staircase is inaudible.

### 9.3 Size and the tank's delay lengths

The tank scale `S` moves the *read* positions of the tank's delay lines
(fractional, linear interpolation) while the write position advances by one
per sample. Moving `Size` therefore glides the pitch of the tail briefly. That
is the accepted behaviour of a size knob on Lexicon-family units, and it
avoids rebuilding the network. All buffers are sized for `S_max = 1.4`.

**Lengths glide to rounded targets.** At rest the tank reads with integer
taps, so every Size-scaled length (the five in-loop elements per branch and
the fourteen output taps) must be an integer at rest. During a glide each
length therefore moves linearly from where it is to its *rounded* target, in
step with the smoothed scale, and is exactly that integer when the scale
arrives; the switch back to integer reads is seamless. A new target mid-glide
re-anchors every length at its current value.

The smoothed length is also **slew-limited to 0.25 samples per sample**. The
150 ms smoother alone would move the 24–30 read position by up to about one
sample per sample on a large Size step, and at more than one the read runs
backwards: a reversed glide. With the limit, the read always moves forwards,
at between 0.75× and 1.25× speed (at most ±4 semitones, during the glide
only).

### 9.4 Switching `Algorithm` or `Complexity`

Both are **discontinuities by construction**: the tail of one structure cannot
be turned into the tail of another.

* On a change, the **late** output (only) crossfades over **2048 samples**
  (43 ms at 48 kHz) from the old structure's output to the new one's. The old
  structure keeps receiving input during the crossfade. The new one starts
  from its `reset()` state for the Spring/Tank switch, or from the current
  tank state for Plain/Dense (the Dense extras start at zero).
* After the crossfade the old structure is no longer processed, so a Tank
  instance does not also pay for the springs.
* A change arriving during a running crossfade is deferred, as in §4.5.
* **How Plain/Dense gets two structures from one tail:** the core holds *two*
  Tank instances. A Plain/Dense switch copies the running tank's whole state
  (every delay line, filter, LFO and the Size glide) into the idle one, which
  then continues in the new mode while the old one fades out. The copy is
  `memcpy`-sized (≈ 1 MB at 48 kHz, ≈ 4 MB at 192 kHz, once per switch) and
  allocates nothing. Into Dense, the Dense-only allpasses start at zero.
  Tank/Spring needs no copy: the new structure starts from `reset()`.

### 9.5 Memory

All buffers are allocated in `init(fs)` for `fs ≤ 192 kHz` and `S = 1.4`, in
`float`, each delay line rounded up to a power of two. At 192 kHz that is
**4.14 MB** per instance, measured (GATES G6.5: 1.04 MB at 48 kHz, 2.07 MB
at 96 kHz). The two Tanks of §9.4 dominate; the springs' 160 short allpasses
and the pre-delay and ER lines are the rest. (The design estimated 3.7 MB
before the second Tank.) `init()`
is called from `OnReset()`, where allocation is allowed, as in `nassau-eq`.

### 9.6 Reset

`init(fs)` sizes the buffers and resets all state. `reset()` zeroes every delay
line, allpass and filter state, every LFO phase (back to its fixed initial
phase), ends any crossfade and snaps the smoothers, without touching
parameters. The wrapper calls `init()` from `OnReset()`.

---

## 10. Parameter surface (16 params, append-only)

| # | Enum | Type | Range | Default |
|---|---|---|---|---|
| 0 | `kMix` | double | 0 … 100 % | 25 |
| 1 | `kInputHpf` | frequency | 20 … 1000 Hz | 20 (bypass) |
| 2 | `kInputLpf` | frequency | 1000 … 20000 Hz | 20000 (bypass) |
| 3 | `kPreDelay` | double | 0 … 250 ms | 10 |
| 4 | `kSize` | double | 2 … 40 m | 12 |
| 5 | `kErLevel` | double | 0 … 200 % | 100 |
| 6 | `kLateLevel` | double | 0 … 200 % | 100 |
| 7 | `kDecay` | double, pow-curve 3 | 0.2 … 20 s | 2.0 |
| 8 | `kDampHigh` | frequency | 1000 … 20000 Hz | 8000 |
| 9 | `kDampLow` | frequency | 20 … 1000 Hz | 20 (bypass) |
| 10 | `kDiffusion` | double | 0 … 100 % | 70 |
| 11 | `kModulation` | double | 0 … 100 % | 25 |
| 12 | `kWidth` | double | 0 … 200 % | 100 |
| 13 | `kComplexity` | enum | Plain / Dense | Plain |
| 14 | `kAlgorithm` | enum | Tank / Spring | Tank |
| 15 | `kOutputGain` | double | −24 … +12 dB | 0 |

`kNumParams == 16 == PLUG_N_PARAMS` (`static_assert`).

The storage convention inside `BloomCore` follows `nassau-zermatt`: numeric
knobs are `std::atomic<float>` in **user units** (Hz, ms, m, s, %, dB), and
enums are `std::atomic<int>`. The core converts units; the wrapper passes
`IParam::Value()` through unchanged.

### 10.1 Why these 16 and not more

There is no tempo-synced pre-delay, no ER/late crossover EQ, no freeze, no
ducking and no gate. Each of these is a fine feature on someone else's reverb.
A "simple" reverb that grows them stops being simple. Requests for them go to
§13 and are not done in v1.

---

## 11. Factory presets (9)

Presets are a **full 16-value table** in `Source/Plugin/nassau_presets.h`, the
single source of truth used by both the plugin (`MakePresetFromChunk`) and
`Tests/preset_tests.cpp`, the `nassau-zermatt` pattern.

Columns: Mix %, In HPF Hz, In LPF Hz, Pre ms, Size m, ER %, Late %, Decay s,
Damp Hi Hz, Damp Lo Hz, Diff %, Mod %, Width %, Cx, Algo, Out dB.

| # | Name | Mix | HPF | LPF | Pre | Size | ER | Late | Decay | DHi | DLo | Diff | Mod | Width | Cx | Algo | Out |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Default | 25 | 20 | 20000 | 10 | 12 | 100 | 100 | 2.0 | 8000 | 20 | 70 | 25 | 100 | Plain | Tank | 0 |
| 1 | Guitar Amp Ambience | 18 | 120 | 9000 | 3 | 5 | 140 | 45 | 0.6 | 6000 | 100 | 60 | 10 | 110 | Plain | Tank | 0 |
| 2 | Vocal Booth | 12 | 150 | 12000 | 0 | 3 | 120 | 25 | 0.35 | 7000 | 150 | 50 | 0 | 80 | Plain | Tank | 0 |
| 3 | Vocal Ballad Long | 20 | 200 | 10000 | 60 | 25 | 40 | 100 | 3.8 | 6500 | 180 | 80 | 40 | 130 | Dense | Tank | 0 |
| 4 | Piano on Stage | 22 | 60 | 14000 | 20 | 18 | 110 | 90 | 1.9 | 7500 | 60 | 70 | 15 | 120 | Dense | Tank | 0 |
| 5 | 80s Bright Ballad Hall | 25 | 300 | 20000 | 80 | 30 | 30 | 120 | 4.5 | 16000 | 500 | 85 | 50 | 150 | Dense | Tank | 0 |
| 6 | 60s Mono Plate | 25 | 80 | 10000 | 0 | 8 | 0 | 100 | 2.4 | 9000 | 20 | 90 | 0 | 0 | Plain | Tank | 0 |
| 7 | Spring Tank | 30 | 150 | 5000 | 0 | 10 | 0 | 100 | 2.2 | 4500 | 80 | 65 | 0 | 60 | Plain | Spring | 0 |
| 8 | Pop Drum Verb | 20 | 150 | 12000 | 12 | 10 | 100 | 100 | 1.3 | 9000 | 200 | 95 | 20 | 140 | Dense | Tank | 0 |

What each one is for, and why the numbers:

1. **Guitar Amp Ambience.** This is the air around a mic'd amp, not a reverb you
   hear as one. It uses a 5 m room, ERs above design (140 %) and the late tail
   nearly gone (45 %, 0.6 s). The HPF at 120 Hz keeps the amp's low end out of
   the room, so palm mutes stay tight. Diffusion is moderate so picking
   transients survive.
2. **Vocal Booth.** This is a small, deadened room for a dry vocal that should
   not sound like it was recorded in a cupboard. A 3 m room, zero pre-delay,
   the late tail at 25 % and 0.35 s. The width is *under* 100 % so the ambience
   stays behind a centred voice. Modulation is off; a short tail has nothing to
   chorus.
3. **Vocal Ballad Long.** This is the "nice long low-level" vocal reverb. Mix
   at 20 % (meant to sit under the voice), 3.8 s, 60 ms pre-delay so
   consonants land before the tail, and an HPF at 200 Hz plus damping at
   6.5 kHz so the tail's top decays first: the tail turns warm as it fades
   and sibilance does not hang. (Damping shortens the top's decay; it does not
   make the tail's first second darker, §5.4.) Dense, 40 % modulation for
   a lush, Lexicon-style chorus, and width at 130 %.
4. **Piano on Stage.** This is a concert stage: a medium-large room (18 m), ERs
   at 110 % for the stage's sense of place, a moderate 1.9 s tail. The HPF is
   low (60 Hz) because a piano's low end belongs to it, and modulation is kept
   low (15 %) because chorusing a piano detunes it.
5. **80s Bright Ballad Hall.** This is the bright, long hall of an 80s
   saxophone-and-vocal ballad: a big space (30 m), long (4.5 s) and *bright*.
   Damping is barely engaged (16 kHz) and the input LPF is open, but the HPF is
   high (300 Hz) and the in-loop low cut higher still (500 Hz; a one-pole at
   300 Hz left the 125 Hz band at 73 % of the 1 kHz decay), so the long tail
   carries shimmer, not mud. 80 ms pre-delay, ERs low (30 %) so the hall reads
   as a tail rather than a room, late at 120 %, width at 150 %.
6. **60s Mono Plate.** This is an EMT-140-style plate: no room (ER 0), zero
   pre-delay, high diffusion (90 %), no modulation (a steel plate does not
   chorus), 2.4 s, darkened top (input LPF 10 kHz, damping 9 kHz), and width
   **0 %**, mono, as plates of that era were mostly used. Plain tank: Dattorro's
   tank *is* a plate model.
7. **Spring Tank.** This is a guitar amp's spring: the Spring algorithm, input
   band-limited to 150 Hz–5 kHz, damping at the spring's own 4.5 kHz, 2.2 s.
   Diffusion 65 % sets the chirp's strength. Width 60 %: two springs, close
   together.
8. **Pop Drum Verb.** This is a snare-and-tom verb: a 10 m room with ERs at
   design for punch, a short 1.3 s tail, very high diffusion (95 %) so the
   tail is smooth and not a flutter echo of the hit, a 12 ms pre-delay to keep
   the transient dry and in front. The HPF at 150 Hz and in-loop low cut at
   200 Hz keep kick bleed from blooming. Width 140 %.

**Names.** The preset names describe a sound or a use. They name no artist or
product. ("The George Michael reverb" is what preset 5 was voiced towards;
`80s Bright Ballad Hall` is what it is called.)

**Mix values are for an insert.** On an aux send, set Mix to 100 %. Every
preset leaves `Output` at 0 dB, and no preset sets anything the gates do not
measure (GATES G7).

---

## 12. Editor

`Source/Plugin/NassauBloomUI.cpp`, an iPlug2 IGraphics editor (NanoVG / GL2 on
Windows), 544 × 270, built from nassau-plugin-sdk's shared editor style
(`include/nassau/ui_style.h`, specified in its `docs/UI_STYLE.md`): the
metrics, text, control styles and layout helpers are the SDK's, the palette
(a cool night blue) and the panels below are this plugin's. Every control binds to its
parameter by enum index, so iPlug2 does all the UI ↔ parameter sync and the
editor never writes the core (R9).

```
 NassauBloom   [<] [>] [ factory preset menu ]                      Nassau
 ┌ INPUT ─────────────────┐ ┌ ROOM ─────────┐ ┌ OUTPUT ────────────────┐
 │  HPF    LPF  Pre-Delay │ │ Size   Early  │ │  Mix   Width   Output  │
 └────────────────────────┘ └───────────────┘ └────────────────────────┘
 ┌ LATE ───────────────────────────────────────┐ ┌ DAMPING ───────────┐
 │ Algorithm [Tank|Spring]  [Plain|Dense]      │ │                    │
 │  Late     Decay     Diffusion   Modulation  │ │   High      Low    │
 └─────────────────────────────────────────────┘ └────────────────────┘
```

* **Signal order, left to right and top to bottom:** what reaches the reverb
  (§2–§3), the room and its early reflections (§4), the output stage
  (§7–§8); then the tail (§5–§6) with the two structure switches over it, and
  the in-loop damping that shapes how it decays (§5.4).
* **The two enums are two-way switches**, not knobs; their labels come from
  the parameters' own `InitEnum` strings, so they cannot drift.
* **The preset selector** is iPlug2's baked preset manager over the
  `MakePresetFromChunk` bank: choosing one calls `RestorePreset`, the path
  GATES G8.1 checks.
* **Font, readability, size check:** as UI_STYLE.md requires: the font
  registered by family name on every open (GATES G9.4); knob values below dark
  knob faces; the selected switch segment in the accent; the window size
  derived from the metrics and `static_assert`ed against `config.h`.
* **Fixed size.** `PLUG_HOST_RESIZE 0`, and the Config passes the size as both
  min and max (G0.7, QBX-219).

---

## 13. Non-goals for v1

* A convolution mode, IR loading.
* Tempo-synced pre-delay; freeze; ducking; gating.
* Per-tap filtering in the ER path (§4.3).
* Oversampling. Nothing in the chain is nonlinear.
* A resizable editor, or one that shows the signal (meters, a decay
  display). The editor is knobs and switches (§12).
* A third algorithm. If a "Plate" algorithm distinct from the Tank is ever
  wanted, it is appended to `kAlgorithm`'s enum, never inserted.
