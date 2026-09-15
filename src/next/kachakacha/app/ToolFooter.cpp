#include "kachakacha/app/ToolFooter.h"

namespace kachakacha::v2::app {
namespace {

[[nodiscard]] std::string BooleanToken(modeling::ExtrudeBooleanMode mode)
{
    switch (mode) {
    case modeling::ExtrudeBooleanMode::NewPart:      return "New";
    case modeling::ExtrudeBooleanMode::AddToPart:    return "Add";
    case modeling::ExtrudeBooleanMode::SubtractFromPart: return "Cut";
    }
    return "New";
}

[[nodiscard]] std::string OutputToken(const ExtrudeOutputs& outputs)
{
    switch (PresetForOutputs(outputs)) {
    case ExtrudeOutputPreset::SolidOnly:     return "Solid";
    case ExtrudeOutputPreset::WiresOnly:     return "Wires";
    case ExtrudeOutputPreset::WiresAndSolid: return "Solid+Wires";
    case ExtrudeOutputPreset::EndWireOnly:   return "EndWire";
    case ExtrudeOutputPreset::Custom:        break;
    }
    return "Custom";
}

[[nodiscard]] std::string MethodToken(modeling::GuideSurfaceMethod method)
{
    switch (method) {
    case modeling::GuideSurfaceMethod::PlanarBoundary: return "Plane";
    case modeling::GuideSurfaceMethod::RuledSections:  return "Ruled";
    case modeling::GuideSurfaceMethod::LoftSections:   return "Loft";
    case modeling::GuideSurfaceMethod::GuidedLoft:     return "GuidedLoft";
    case modeling::GuideSurfaceMethod::GordonNetwork:  return "Gordon";
    case modeling::GuideSurfaceMethod::BoundaryFill:   return "BoundaryFill";
    case modeling::GuideSurfaceMethod::OffsetGuide:    return "Offset";
    case modeling::GuideSurfaceMethod::Revolve:        return "Revolve";
    }
    return "Loft";
}

//! 小数第1位まで。正本の `18.0mm` に合わせる。
[[nodiscard]] std::string Millimetres(double value)
{
    const long long tenths = static_cast<long long>(value * 10.0 + (value < 0.0 ? -0.5 : 0.5));
    return std::to_string(tenths / 10) + "." + std::to_string(tenths < 0 ? -(tenths % 10)
                                                                        : tenths % 10)
        + "mm";
}

} // namespace

std::string ExtrudeFooterLine(const ExtrudeInputState& state, const std::string& targetName,
    const std::vector<std::string>& profileNames)
{
    std::string line = "押し出し: TARGET=";
    line += targetName.empty() ? std::string("(なし)") : targetName;
    line += " / PROFILE=";
    if (profileNames.empty()) {
        line += "(なし)";
    } else {
        for (std::size_t index = 0; index < profileNames.size(); ++index) {
            if (index > 0) {
                line += ",";
            }
            line += profileNames[index];
        }
    }
    line += " / " + Millimetres(state.distanceMm);
    line += " / " + BooleanToken(state.operation);
    line += " / OUTPUT=" + OutputToken(state.outputs);
    return line;
}

std::string SurfaceFooterLine(const SurfaceInputState& state, bool previewShown)
{
    std::string line = "面を作る: METHOD=" + MethodToken(state.method);
    line += " / SECTIONS=" + std::to_string(state.sections.size());
    line += " / GUIDES=" + std::to_string(state.guides.size());
    line += " / BOUNDARIES=" + std::to_string(state.boundaries.size());
    // **まだ文書に入っていないことを、ここでも言う。**
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

std::string ToolKeyHintJa()
{
    // 全角の空きで区切る。正本の footer の右側と同じ。
    return "Tab 候補切替　Ctrl 任意の複数選択　Esc 中止　Enter 確定";
}

} // namespace kachakacha::v2::app
