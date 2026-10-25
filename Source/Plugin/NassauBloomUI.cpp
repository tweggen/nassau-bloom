// NassauBloom editor (docs/DESIGN.md §12, GATES G9). Five panels in signal
// order on two rows, INPUT / ROOM / OUTPUT over LATE / DAMPING, plus the
// factory preset selector in the header. The pattern, and both font lessons
// below, come from nassau-zermatt's editor.
//
// The ENTIRE body is inside #if IPLUG_EDITOR, so a headless build
// (NASSAU_FORCE_HEADLESS) compiles this file to nothing (G9.3).
#if IPLUG_EDITOR

#include "NassauBloomUI.h"
#include "NassauBloomPlugin.h"
#include "IGraphics.h"
#include "IControls.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

using namespace iplug;
using namespace iplug::igraphics;

namespace {

// `knobs` as evenly spaced knobs across `row`.
void KnobRow(IGraphics& ui, const IRECT& row, const IVStyle& style,
             std::initializer_list<std::pair<int, const char*>> knobs)
{
    const int n = static_cast<int>(knobs.size());
    int i = 0;
    for (const auto& kv : knobs) {
        const IRECT cell = row.GetGridCell(i, 1, n).GetPadded(-4.f);
        ui.AttachControl(new IVKnobControl(cell, kv.first, kv.second, style));
        ++i;
    }
}

} // namespace

void NassauBloomUI::Layout(IGraphics& ui, NassauBloomPlugin& /*plugin*/)
{
    // The UI font, registered as "default" by FAMILY NAME on EVERY editor
    // open. nassau-zermatt shipped two bugs here (G9.4): "Helvetica" does not
    // exist on Windows, so nothing drew; and a `static bool` guard skipped the
    // load on every later open, although IGraphics (and its font storage) is
    // rebuilt each time. iPlug2 loads no default font of its own.
    static const char* const kFontFamilies[] = {
#ifdef OS_WIN
        "Segoe UI", "Arial", "Tahoma", "Verdana"
#else
        "Helvetica", "Arial"
#endif
    };
    bool fontLoaded = false;
    for (const char* family : kFontFamilies) {
        if (ui.LoadFont("default", family, ETextStyle::Normal)) {
            fontLoaded = true;
            break;
        }
    }
    assert(fontLoaded && "No UI font could be loaded: all text would be invisible");

    // A cool night palette: a reverb, not an amp.
    const IColor kBg(255, 18, 22, 30);
    const IColor kPanel(255, 30, 36, 48);
    const IColor kText(255, 214, 222, 236);
    const IColor kAccent(255, 120, 178, 230);
    const IColor kKnob(255, 50, 60, 80);     // knob face: dark, so the value reads

    // ---- Layout metrics ------------------------------------------------------
    // Every dimension derives from these and PLUG_WIDTH / PLUG_HEIGHT.
    const float kMargin     = 6.f;
    const float kHeaderH    = 30.f;
    const float kPanelTitle = 16.f;
    const float kRowH       = 84.f;   // label, knob, value below it
    const float kSwitchH    = 34.f;   // caption over the two-way switch
    const float kGap        = 4.f;
    const float kColGap     = 6.f;
    const float kRowGap     = 6.f;

    const IText header(16, kText, "default", EAlign::Near);
    const IText brand(11, kAccent, "default", EAlign::Far);
    const IText panelTitle(11, kAccent, "default", EAlign::Center);

    const IVStyle knobStyle = DEFAULT_STYLE
        .WithColor(kFG, kKnob)
        .WithColor(kX1, kAccent)
        .WithLabelText(IText(11, kText, "default"))
        .WithValueText(IText(10, kText, "default"))
        .WithDrawFrame(false)
        .WithShowValue(true);

    // IVTabSwitchControl fills the SELECTED segment with kPR (white by
    // default, which hid its label) and the others with kFG.
    const IVStyle switchStyle = DEFAULT_STYLE
        .WithColor(kFG, kKnob)
        .WithColor(kPR, kAccent)
        .WithColor(kBG, kPanel)
        .WithLabelText(IText(10, kText, "default"))
        .WithValueText(IText(10, kText, "default"))
        .WithDrawFrame(true);

    const IVStyle presetStyle = DEFAULT_STYLE
        .WithDrawShadows(false)
        .WithColor(kFG, kAccent)
        .WithColor(kBG, kPanel)
        .WithLabelText(IText(10, kText, "default", EAlign::Center, EVAlign::Middle))
        .WithValueText(IText(11, kText, "default"));

    const float W = static_cast<float>(PLUG_WIDTH);
    ui.AttachPanelBackground(kBg);

    // ---- Header: title, factory preset selector, brand -----------------------
    ui.AttachControl(new ITextControl(
        IRECT(kMargin + 2.f, kMargin, 120.f, kMargin + kHeaderH), "NassauBloom", header));
    // [<] preset menu [>], filled from the MakePresetFromChunk bank; choosing
    // one calls RestorePreset, the path G8.1 checks.
    ui.AttachControl(new IVBakedPresetManagerControl(
        IRECT(126.f, kMargin + 2.f, W - 66.f, kMargin + kHeaderH - 2.f), "", presetStyle));
    ui.AttachControl(new ITextControl(
        IRECT(W - 62.f, kMargin, W - kMargin, kMargin + kHeaderH), "Nassau", brand));

    // ---- Panels ----------------------------------------------------------------
    // Widths are weights (the knob count), normalised per row so both rows
    // span the same width.
    struct PanelSpec { const char* name; float weight; };
    const PanelSpec kRow1[] = { { "INPUT", 3.f }, { "ROOM", 2.f }, { "OUTPUT", 3.f } };
    const PanelSpec kRow2[] = { { "LATE", 4.f }, { "DAMPING", 2.f } };
    const float row1H = kPanelTitle + kRowH;
    const float row2H = kPanelTitle + kSwitchH + kGap + kRowH;

    IRECT panels[5];
    {
        const float contentW = W - 2.f * kMargin;
        auto placeRow = [&](const PanelSpec* row, int n, float y, float h, int outBase) {
            float wsum = 0.f;
            for (int i = 0; i < n; ++i) wsum += row[i].weight;
            const float usable = contentW - static_cast<float>(n - 1) * kColGap;
            float x = kMargin;
            for (int i = 0; i < n; ++i) {
                const float w = usable * row[i].weight / wsum;
                panels[outBase + i] = IRECT(x, y, x + w, y + h);
                x += w + kColGap;
            }
        };
        const float y1 = kMargin + kHeaderH + kRowGap;
        placeRow(kRow1, 3, y1, row1H, 0);
        placeRow(kRow2, 2, y1 + row1H + kRowGap, row2H, 3);
    }
    for (int i = 0; i < 5; ++i) {
        const PanelSpec& spec = i < 3 ? kRow1[i] : kRow2[i - 3];
        ui.AttachControl(new IPanelControl(panels[i].GetPadded(-2.f), kPanel));
        ui.AttachControl(new ITextControl(
            IRECT(panels[i].L, panels[i].T + 2.f, panels[i].R, panels[i].T + kPanelTitle), spec.name, panelTitle));
    }
    auto knobsAt = [&](const IRECT& p, float y) { return IRECT(p.L, y, p.R, y + kRowH); };

    // INPUT: what reaches the reverb (the wet path only, DESIGN.md §2-§3).
    KnobRow(ui, knobsAt(panels[0], panels[0].T + kPanelTitle), knobStyle,
            { { kInputHpf, "HPF" }, { kInputLpf, "LPF" }, { kPreDelay, "Pre-Delay" } });

    // ROOM: the space, and its early reflections (§4).
    KnobRow(ui, knobsAt(panels[1], panels[1].T + kPanelTitle), knobStyle,
            { { kSize, "Size" }, { kErLevel, "Early" } });

    // OUTPUT (§7-§8).
    KnobRow(ui, knobsAt(panels[2], panels[2].T + kPanelTitle), knobStyle,
            { { kMix, "Mix" }, { kWidth, "Width" }, { kOutputGain, "Output" } });

    // LATE: the two structure switches over the tail's knobs (§5-§6, §9.4).
    // An empty options list makes IVTabSwitchControl take its labels from the
    // param's own InitEnum, so the text cannot drift from the parameter.
    {
        const IRECT& p = panels[3];
        float y = p.T + kPanelTitle;
        const IRECT switches(p.L + 3.f, y, p.R - 3.f, y + kSwitchH);
        ui.AttachControl(new IVTabSwitchControl(switches.GetGridCell(0, 1, 2).GetPadded(-2.f, 0.f, -2.f, 0.f),
                                                kAlgorithm, std::vector<const char*>{}, "", switchStyle));
        ui.AttachControl(new IVTabSwitchControl(switches.GetGridCell(1, 1, 2).GetPadded(-2.f, 0.f, -2.f, 0.f),
                                                kComplexity, std::vector<const char*>{}, "", switchStyle));
        y += kSwitchH + kGap;
        KnobRow(ui, knobsAt(p, y), knobStyle,
                { { kLateLevel, "Late" }, { kDecay, "Decay" }, { kDiffusion, "Diffusion" },
                  { kModulation, "Modulation" } });
    }

    // DAMPING: in the loop, so it shapes how the tail decays (§5.4).
    {
        const IRECT& p = panels[4];
        KnobRow(ui, knobsAt(p, p.T + kPanelTitle + kSwitchH + kGap), knobStyle,
                { { kDampHigh, "High" }, { kDampLow, "Low" } });
    }

#ifndef NDEBUG
    // G9.1: every parameter has exactly one control, bound by its index.
    int bound[kNumParams] = {};
    for (int i = 0; i < ui.NControls(); ++i) {
        const int p = ui.GetControl(i)->GetParamIdx();
        if (p >= 0 && p < kNumParams) ++bound[p];
    }
    int wrong = 0;
    for (int p = 0; p < kNumParams; ++p) {
        if (bound[p] != 1) {
            std::fprintf(stderr, "NassauBloom G9.1: param %d (%s) has %d controls\n", p,
                         ui.GetDelegate()->GetParam(p)->GetName(), bound[p]);
            ++wrong;
        }
    }
    std::fprintf(stderr, "NassauBloom G9.1: %d params, %d controls, %d without exactly one control\n",
                 kNumParams, ui.NControls(), wrong);
    assert(wrong == 0 && "G9.1: a parameter has no control, or more than one");
#endif
}

#endif // IPLUG_EDITOR
