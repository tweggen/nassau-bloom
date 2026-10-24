# NassauBloom

A simple stereo reverb plugin (VST3 + CLAP, plus AU on macOS) built on the
Nassau plugin stack: **IPlug2 + Steinberg VST3-SDK** (not JUCE).

Two paths you mix: a room's **early reflections**, computed from an
image-source model of a shoebox room, and a **late network**, either a
Lexicon-style figure-of-eight tank (Dattorro) or a dispersive spring model.
Around them are the knobs a mixing engineer expects: dry/wet, input HPF/LPF,
pre-delay, wall damping, diffusion, modulation, stereo width, and a
Plain/Dense complexity switch. It comes with eight factory presets, each made
for a particular job: guitar amp ambience, vocal booth, a long vocal-ballad
tail, piano on a stage, a bright 80s ballad hall, a 60s mono plate, a spring,
and a pop drum verb.

The DSP heart (`BloomCore`, in `Source/DSP/`) is a dependency-free C++ class
kept strictly separate from a thin IPlug2 wrapper (`Source/Plugin/`). It
builds and is unit-tested standalone.

* [`docs/DESIGN.md`](docs/DESIGN.md) is the specification: the signal chain,
  every algorithm, the 16 parameters and the presets with their reasons.
* [`docs/GATES.md`](docs/GATES.md) is the schedule: gates G0–G9, each with
  measured acceptance criteria.

**Status: G0 (scaffold).** The plugin builds, loads, and carries the full
parameter surface, the presets and versioned state, but the core passes only
the dry side of the mix. The reverb arrives in gates G2–G5.

## Dependencies

NassauBloom consumes the shared **[nassau-plugin-sdk](../nassau-plugin-sdk)**
through `cmake/NassauSDK.cmake`, a verbatim copy of the SDK's canonical
locator (re-copy it rather than editing this one). It resolves the SDK in a
fixed order:

1. an explicit `-DNASSAU_SDK_DIR=...`;
2. a sibling checkout `../nassau-plugin-sdk`;
3. `FetchContent` from the SDK's git remote (disable with
   `-DNASSAU_SDK_ALLOW_FETCH=OFF`).

The DSP core and tests build even without the SDK; only the plugin targets
require it.

## Platform support

| | DSP core + tests | Plugin formats |
|---|---|---|
| macOS | ✅ | VST3 + AU + CLAP |
| Windows | ✅ | VST3 + CLAP |
| Linux | ✅ | ❌, skipped at configure time |

There is no editor yet (`PLUG_HAS_UI 0`, GATES G9); hosts show their generic
parameter UI.

## Build

```sh
./ci/build.sh            # configure + build into build/; installs nothing
./ci/build.sh --test     # ...and run ctest afterwards
./ci/install.sh          # install the result into this user's plug-in folders
./ci/uninstall.sh        # take it back out
./ci/clean.sh            # drop build/, keep the checkout
./ci/gates.sh            # the gate: DSP core + tests, no SDK needed
./ci/gates.sh --sdk ../nassau-plugin-sdk   # ...plus the plugin bundles
```

The `ci/` scripts and `cmake/NassauSDK.cmake` are **byte-identical** across
the Nassau plugin repos. Fix one, then copy it across all of them. Their
header comments list the four repos that existed when they were written;
this is the fifth.

`ci/build.sh` configures with `-DIPLUG_DEPLOY_PLUGINS=OFF`, so a build never
writes outside `build/`. A **raw `cmake` build auto-installs** the bundles,
because iPlug2 defaults `IPLUG_DEPLOY_PLUGINS=ON`. Pass `OFF` yourself if you
drive CMake directly.

Windows (Visual Studio is a multi-config generator):

```pwsh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

macOS / Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Built plugins appear under `build/out/`:

```
build/out/NassauBloom.vst3                                       # macOS bundle
build/out/NassauBloom.vst3/Contents/x86_64-win/NassauBloom.vst3  # Windows
build/out/NassauBloom.clap
```
