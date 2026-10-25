// NassauBloom editor (docs/DESIGN.md §12, GATES G9). Five panels in signal
// order on two rows, INPUT / ROOM / OUTPUT over LATE / DAMPING, plus the
// factory preset selector in the header.
//
// Built from nassau-plugin-sdk's shared editor style (include/nassau/
// ui_style.h, docs/UI_STYLE.md): the metrics, text, control styles and
// helpers are the SDK's; the palette and the panels are this plugin's.
//
// The ENTIRE body is inside #if IPLUG_EDITOR, so a headless build
// (NASSAU_FORCE_HEADLESS) compiles this file to nothing (G9.3).
#if IPLUG_EDITOR

#include "NassauBloomUI.h"
#include "NassauBloomPlugin.h"
#include "nassau/ui_style.h"

using namespace iplug::igraphics;
namespace nui = nassau::ui;

void NassauBloomUI::Layout(IGraphics& ui, NassauBloomPlugin& /*plugin*/)
{
    nui::LoadFont(ui);

    // A cool night palette: a reverb, not an amp.
    const nui::Palette pal{
        IColor(255, 18, 22, 30),     // bg
        IColor(255, 30, 36, 48),     // panel
        IColor(255, 214, 222, 236),  // text
        IColor(255, 130, 140, 158),  // textDim
        IColor(255, 120, 178, 230),  // accent
        IColor(255, 50, 60, 80),     // knobFace
    };

    // ---- Size: derived, and checked against config.h ---------------------------
    // Row 1 is the widest in knob columns: INPUT 3 + ROOM 2 + OUTPUT 3.
    constexpr float row1H = nui::kPanelTitleH + nui::kKnobRowH;
    constexpr float row2H = nui::kPanelTitleH + nui::kSwitchH + nui::kGap + nui::kKnobRowH;
    constexpr float kEditorW = 8.f * nui::kKnobColW + 2.f * nui::kColGap + 2.f * nui::kMargin;
    constexpr float kEditorH = 2.f * nui::kMargin + nui::kHeaderH + nui::kRowGap + row1H + nui::kRowGap + row2H;
    static_assert(kEditorW == static_cast<float>(PLUG_WIDTH), "PLUG_WIDTH does not match the layout");
    static_assert(kEditorH == static_cast<float>(PLUG_HEIGHT), "PLUG_HEIGHT does not match the layout");

    const IVStyle knob = nui::KnobStyle(pal);
    const IVStyle sw = nui::SwitchStyle(pal);

    ui.AttachPanelBackground(pal.bg);
    // The preset selector calls RestorePreset, the path G8.1 checks.
    nui::Header(ui, pal, "NassauBloom", kEditorW);

    const float x0 = nui::kMargin, x1 = kEditorW - nui::kMargin;
    const float y1 = nui::kMargin + nui::kHeaderH + nui::kRowGap;
    const float y2 = y1 + row1H + nui::kRowGap;
    const auto top = nui::PanelRow(IRECT(x0, y1, x1, y1 + row1H), {3.f, 2.f, 3.f});
    const auto bottom = nui::PanelRow(IRECT(x0, y2, x1, y2 + row2H), {4.f, 2.f});
    auto knobRow = [](const IRECT& area, float y) { return IRECT(area.L, y, area.R, y + nui::kKnobRowH); };

    // INPUT: what reaches the reverb (the wet path only, DESIGN.md §2-§3).
    {
        const IRECT a = nui::Panel(ui, pal, top[0], "INPUT");
        nui::KnobRow(ui, knobRow(a, a.T), knob,
                     { { kInputHpf, "HPF" }, { kInputLpf, "LPF" }, { kPreDelay, "Pre-Delay" } });
    }
    // ROOM: the space, and its early reflections (§4).
    {
        const IRECT a = nui::Panel(ui, pal, top[1], "ROOM");
        nui::KnobRow(ui, knobRow(a, a.T), knob, { { kSize, "Size" }, { kErLevel, "Early" } });
    }
    // OUTPUT (§7-§8).
    {
        const IRECT a = nui::Panel(ui, pal, top[2], "OUTPUT");
        nui::KnobRow(ui, knobRow(a, a.T), knob,
                     { { kMix, "Mix" }, { kWidth, "Width" }, { kOutputGain, "Output" } });
    }
    // LATE: the two structure switches over the tail's knobs (§5-§6, §9.4).
    {
        const IRECT a = nui::Panel(ui, pal, bottom[0], "LATE");
        const IRECT switches(a.L, a.T, a.R, a.T + nui::kSwitchH);
        nui::Switch(ui, switches.GetGridCell(0, 1, 2), kAlgorithm, "Algorithm", sw);
        nui::Switch(ui, switches.GetGridCell(1, 1, 2), kComplexity, "Complexity", sw);
        nui::KnobRow(ui, knobRow(a, switches.B + nui::kGap), knob,
                     { { kLateLevel, "Late" }, { kDecay, "Decay" }, { kDiffusion, "Diffusion" },
                       { kModulation, "Modulation" } });
    }
    // DAMPING: in the loop, so it shapes how the tail decays (§5.4). Aligned
    // with LATE's knobs, under the switch row it does not have.
    {
        const IRECT a = nui::Panel(ui, pal, bottom[1], "DAMPING");
        nui::KnobRow(ui, knobRow(a, a.T + nui::kSwitchH + nui::kGap), knob,
                     { { kDampHigh, "High" }, { kDampLow, "Low" } });
    }

#ifndef NDEBUG
    nui::CheckBindings(ui, kNumParams, "NassauBloom");  // G9.1
#endif
}

#endif // IPLUG_EDITOR
