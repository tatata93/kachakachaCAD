#pragma once

//! 形状ガイドの役割テーブル(ui-workflows §9.3、AT-UIX-007)。
//!
//! 画面の右パネルに出る表そのものを、ここで作る。Qt は持たない。
//! 表が持つのは「どの役割の何番目の行に、どのワイヤーが、どの向きで入るか」だけで、
//! 面は作らない。面を作れるかどうかは AnalyzeGuideSurfaceRequest が決める。
//!
//! 1行 = 1本の論理 WireChain である。1行に複数のワイヤーを足せる。
//! V1 は「選んだ順が入力の順」だったので、選び直すたびに面が変わった。
//! V2 は行と番号を表に持ち、選択とは切り離す。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kachakacha::v2::modeling {

//! 3Dで行を色分けするための色。表と3Dで同じ値を使う(色同期)。
struct RowColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const RowColor& l, const RowColor& r) noexcept
    {
        return l.red == r.red && l.green == r.green && l.blue == r.blue;
    }
    friend bool operator!=(const RowColor& l, const RowColor& r) noexcept
    {
        return !(l == r);
    }
};

//! 表の1行。
struct GuideTableRow {
    ChainRole role = ChainRole::Section;
    //! この行に入っているワイヤー。足した順に並ぶ。表の「元ワイヤー」列に出る。
    std::vector<EntityId> sourceWireIds;
    std::vector<std::string> sourceLabels;
    std::vector<CurveSegment> segments;
    //! 表の「方向」列。true なら逆。
    bool reversed = false;
};

//! 表そのもの。
struct GuideTable {
    GuideSurfaceMethod method = GuideSurfaceMethod::LoftSections;
    std::vector<GuideTableRow> rows;
    //! OffsetGuide の離す距離。表から要求へそのまま渡る。0 は要求の側で断られる。
    double offsetDistanceMm = 0.0;
    //! Revolve の軸と角度。表から要求へそのまま渡る。
    geometry::Vector3 revolveAxisPoint{};
    geometry::Vector3 revolveAxisDirection{0.0, 0.0, 1.0};
    double revolveAngleRad = 0.0;
};

//! 選択した線の束。ボタンはこれを受け取る。
struct GuideTableSelection {
    EntityId sourceWireId;
    std::string label;
    std::vector<CurveSegment> segments;
};

//! その方法で使う役割。表はこれ以外の役割の行を作らせない。
[[nodiscard]] const std::vector<ChainRole>& RolesForMethod(GuideSurfaceMethod method);
[[nodiscard]] bool RoleUsedByMethod(GuideSurfaceMethod method, ChainRole role);

//! 役割の日本語。表の「役割」列に出る。
[[nodiscard]] std::string ChainRoleLabelJa(ChainRole role);

//! 外形の側の役割か。断面の両端がここへ届いているかを「接続」列に出す。
[[nodiscard]] bool IsBoundaryRole(ChainRole role) noexcept;

//! 画面に出す1行分。表示に必要なものが全部そろっている。
struct GuideTableRowView {
    std::size_t rowIndex = 0;
    ChainRole role = ChainRole::Section;
    std::string roleLabelJa;
    //! 役割ごとの通し番号。1始まり。行を動かすと詰め直す。
    int number = 1;
    std::size_t segmentCount = 0;
    //! 両端が外形へ届いているか。
    bool startConnected = false;
    bool endConnected = false;
    //! 表の「接続」列。両端とも届いていれば「有効」。
    std::string connectionLabelJa;
    bool reversed = false;
    std::string directionLabelJa;
    //! 「arc_1 + line_2 + arc_3」。
    std::string sourceLabelJa;
    RowColor color{};
    //! 3Dに出す進行矢印の向き(始点→終点)。
    Vector3 startPoint{};
    Vector3 endPoint{};
};

//! 表を画面に出せる形にする。接続は許容差で見る。
[[nodiscard]] std::vector<GuideTableRowView> BuildGuideTableView(const GuideTable& table,
    const GeometryTolerance& tolerance);

//! 足りない役割の案内。「断面が1つも入っていません」など。
[[nodiscard]] std::vector<std::string> MissingRoleGuidanceJa(const GuideTable& table);

//! 行の色。役割と番号だけで決まるので、表と3Dが必ず同じ色になる。
[[nodiscard]] RowColor ColorForRow(ChainRole role, int number) noexcept;

// ---- ボタン ----

//! 「選択を新しい外形へ」「選択を新しい断面へ」。
[[nodiscard]] base::Result<GuideTable> AddSelectionAsNewRow(const GuideTable& table,
    ChainRole role, const GuideTableSelection& selection);

//! 「選択した面を元の面へ」(OffsetGuide)。線ではなく、既にある形状ガイドを指す行。
//! 元の面は1つだけ。2つ目は断る。
[[nodiscard]] base::Result<GuideTable> AddSourceSurfaceRow(const GuideTable& table,
    const EntityId& surfaceId, const std::string& label);

//! 「選択を既存行へ追加」。行の端につながらない線は足さない。
[[nodiscard]] base::Result<GuideTable> AddSelectionToRow(const GuideTable& table,
    std::size_t rowIndex, const GuideTableSelection& selection,
    const GeometryTolerance& tolerance);

//! 「行を上へ / 下へ」。同じ役割の中だけで動く。
[[nodiscard]] base::Result<GuideTable> MoveRow(const GuideTable& table, std::size_t rowIndex,
    int delta);

//! 「行を削除」。
[[nodiscard]] base::Result<GuideTable> RemoveRow(const GuideTable& table,
    std::size_t rowIndex);

//! 「方向」列の反転。線の並びも各線の向きも逆にする。
[[nodiscard]] base::Result<GuideTable> ReverseRow(const GuideTable& table,
    std::size_t rowIndex);

//! 方法を変える。使わない役割の行が残っていたら断る(消して黙らない)。
[[nodiscard]] base::Result<GuideTable> SetGuideTableMethod(const GuideTable& table,
    GuideSurfaceMethod method);

//! 表を、面を作る要求へ変える。番号は表の並び順で振り直す。
[[nodiscard]] base::Result<GuideSurfaceRequest> ToGuideSurfaceRequest(const GuideTable& table,
    const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::modeling
