#pragma once

// Forward declarations only, no IGraphics include, so this header is harmless
// in any translation unit. NassauBloomPlugin.h includes it only under
// #if IPLUG_EDITOR, and the whole .cpp body is guarded the same way, so a
// headless build compiles it to nothing (GATES G9.3).
namespace iplug { namespace igraphics { class IGraphics; } }
class NassauBloomPlugin;

namespace NassauBloomUI {
    // Lay out the editor (DESIGN.md §12): all 16 params in five panels in
    // signal order, plus the factory preset selector. Every control binds to
    // its parameter by enum index, so iPlug2 does all UI <-> param sync and
    // this never writes BloomCore or plugin state (R9).
    void Layout(iplug::igraphics::IGraphics& ui, NassauBloomPlugin& plugin);
}
