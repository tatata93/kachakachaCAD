#include "kachakacha/app/FabricationOptions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kBadBoundaryText = "UI-F001";
constexpr const char* kBadPartCount = "UI-F002";
constexpr const char* kBadMinimumWidth = "UI-F003";
constexpr const char* kBadFidelity = "UI-F004";
constexpr int kMaxPartCount = 200;
constexpr int kMaxFidelity = 20;

[[nodiscard]] std::string Trim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

} // namespace

Result<std::vector<double>> ParseBoundaryList(std::string_view text)
{
    using Out = Result<std::vector<double>>;
    std::vector<double> boundaries;
    std::string rest(text);
    // 全角の読点・カンマも区切りとして読む。打ち分けを人に強いない。
    for (const char* wide : {"、", "，"}) {
        for (std::size_t at = rest.find(wide); at != std::string::npos; at = rest.find(wide)) {
            rest.replace(at, std::string(wide).size(), ",");
        }
    }
    std::size_t start = 0;
    while (start <= rest.size()) {
        const std::size_t comma = rest.find(',', start);
        const std::string piece = Trim(std::string_view(rest).substr(start,
            comma == std::string::npos ? std::string::npos : comma - start));
        if (!piece.empty()) {
            char* end = nullptr;
            const double value = std::strtod(piece.c_str(), &end);
            if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
                return Out::Failure(MakeError(kBadBoundaryText, "手動境界の書き方が読めません。",
                    "「" + piece + "」が数ではありません。0.3, 0.6 のように 0 と 1 の間の数を"
                    "カンマで区切ってください。"));
            }
            boundaries.push_back(value);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return Out::Success(std::move(boundaries));
}

std::string FormatBoundaryList(const std::vector<double>& boundaries)
{
    std::string text;
    for (const double value : boundaries) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", value);
        if (!text.empty()) {
            text += ", ";
        }
        text += buffer;
    }
    return text;
}

Result<FabricationChoice> CheckFabricationChoice(const FabricationChoice& choice)
{
    using Out = Result<FabricationChoice>;
    if (choice.maximumPartCount < 1 || choice.maximumPartCount > kMaxPartCount) {
        return Out::Failure(MakeError(kBadPartCount, "部材数の上限は 1〜200 にしてください。",
            std::to_string(choice.maximumPartCount) + " 枚"));
    }
    if (!(choice.minimumPartWidthMm > 0.0) || !std::isfinite(choice.minimumPartWidthMm)) {
        return Out::Failure(MakeError(kBadMinimumWidth,
            "部材の最小幅は 0 より大きい数にしてください。",
            std::to_string(choice.minimumPartWidthMm) + " mm"));
    }
    if (choice.fidelity < 1 || choice.fidelity > kMaxFidelity) {
        return Out::Failure(MakeError(kBadFidelity, "再現度は 1〜20 にしてください。",
            std::to_string(choice.fidelity)));
    }
    return Out::Success(choice);
}

void ApplyFabricationChoice(domain::CreateFabricationModelDefinition& definition,
    const FabricationChoice& choice)
{
    definition.method = static_cast<int>(choice.method);
    definition.splitAxis = choice.splitAxis;
    definition.automaticBoundaries = choice.automaticBoundaries;
    definition.maximumPartCount = choice.maximumPartCount;
    definition.minimumPartWidthMm = choice.minimumPartWidthMm;
    definition.fidelity = choice.fidelity;
    definition.manualBoundaries = choice.manualBoundaries;
}

FabricationChoice FabricationChoiceOf(const domain::CreateFabricationModelDefinition& definition)
{
    FabricationChoice choice;
    choice.method = FabricationMethodOf(definition);
    choice.splitAxis = definition.splitAxis;
    choice.automaticBoundaries = definition.automaticBoundaries;
    choice.maximumPartCount = definition.maximumPartCount;
    choice.minimumPartWidthMm = definition.minimumPartWidthMm;
    choice.fidelity = definition.fidelity;
    choice.manualBoundaries = definition.manualBoundaries;
    return choice;
}

std::string_view SplitAxisNameJa(int splitAxis) noexcept
{
    switch (splitAxis) {
    case 0:  return "U 方向で切る";
    case 1:  return "V 方向で切る";
    default: return "自動(曲がっている方向を横切る)";
    }
}

} // namespace kachakacha::v2::app
