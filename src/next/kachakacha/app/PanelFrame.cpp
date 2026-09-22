#include "kachakacha/app/PanelFrame.h"

#include <array>
#include <cstddef>

namespace kachakacha::v2::app {
namespace {

//! 先頭の「1. 」「12. 」を読み飛ばす(正本の番号つき見出し)。
[[nodiscard]] std::string_view WithoutNumber(std::string_view title)
{
    std::size_t at = 0;
    while (at < title.size() && title[at] >= '0' && title[at] <= '9') {
        ++at;
    }
    if (at > 0 && at + 1 < title.size() && title[at] == '.' && title[at + 1] == ' ') {
        return title.substr(at + 2);
    }
    return title;
}

[[nodiscard]] bool StartsWith(std::string_view text, std::string_view head)
{
    return text.substr(0, head.size()) == head;
}

//! 並びの順位。同じ順位どうしは並びを問わない(設定とオプションは正本でも入れ替わる)。
[[nodiscard]] int Rank(PanelSectionKind kind) noexcept
{
    switch (kind) {
    case PanelSectionKind::Method:   return 0;
    case PanelSectionKind::Input:    return 1;
    case PanelSectionKind::Settings: return 2;
    case PanelSectionKind::Common:   return 3;
    case PanelSectionKind::State:    return 4;
    case PanelSectionKind::Other:    return -1;
    }
    return -1;
}

} // namespace

PanelSectionKind PanelSectionKindOf(std::string_view titleJa)
{
    const std::string_view title = WithoutNumber(titleJa);
    struct Head {
        std::string_view text;
        PanelSectionKind kind;
    };
    static constexpr std::array<Head, 19> heads{{
        {"作り方", PanelSectionKind::Method},
        {"操作", PanelSectionKind::Method},
        {"入力", PanelSectionKind::Input},
        {"対象", PanelSectionKind::Input},
        {"設定", PanelSectionKind::Settings},
        {"オプション", PanelSectionKind::Settings},
        {"結果", PanelSectionKind::Settings},
        {"開始位置", PanelSectionKind::Settings},
        {"範囲", PanelSectionKind::Settings},
        {"演算", PanelSectionKind::Settings},
        {"出力", PanelSectionKind::Settings},
        {"断面順", PanelSectionKind::Settings},
        {"厚み", PanelSectionKind::Settings},
        {"相手", PanelSectionKind::Settings},
        {"近似条件", PanelSectionKind::Settings},
        {"共通", PanelSectionKind::Common},
        {"状態", PanelSectionKind::State},
        {"プレビュー", PanelSectionKind::State},
        {"Preview", PanelSectionKind::State},
    }};
    for (const Head& head : heads) {
        if (StartsWith(title, head.text)) {
            return head.kind;
        }
    }
    return PanelSectionKind::Other;
}

std::string_view PanelSectionKindNameJa(PanelSectionKind kind) noexcept
{
    switch (kind) {
    case PanelSectionKind::Method:   return "作り方";
    case PanelSectionKind::Input:    return "入力";
    case PanelSectionKind::Settings: return "設定";
    case PanelSectionKind::Common:   return "共通";
    case PanelSectionKind::State:    return "状態";
    case PanelSectionKind::Other:    return "その他";
    }
    return "その他";
}

std::string PanelSectionOrderProblemJa(const std::vector<std::string>& titlesJa)
{
    int highest = -1;
    std::string highestTitle;
    for (const std::string& title : titlesJa) {
        const int rank = Rank(PanelSectionKindOf(title));
        if (rank < 0) {
            continue;
        }
        if (rank < highest) {
            return "「" + title + "」が「" + highestTitle + "」より後ろにあります"
                   "(約束: 作り方 → 入力 → 設定 → 共通 → 状態)。";
        }
        if (rank > highest) {
            highest = rank;
            highestTitle = title;
        }
    }
    return {};
}

} // namespace kachakacha::v2::app
