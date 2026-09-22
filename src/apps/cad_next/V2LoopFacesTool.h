#pragma once

//! 線から面(オーナー要望 2026-09-22)。線を選んで押すだけで、閉じた輪を全部面にする。
//!
//! 何を面にするか・作り方(平面 / 四辺面 / 境界面)は core(app/LoopFaces)が端点のつながりから
//! 決める。ここは、選んだ線を渡し、輪の下見(実線)とずれ(赤系の破線と × 印)を出し、
//! Enter で作り、Esc でやめるだけ。
//!   - ずれがあれば **黙って寄せない**。どこが何 mm 離れているかを言い、Enter で寄せてから作る、
//!     Esc でやめる、を人が選ぶ。寄せられるのは直線の端だけ(円弧の端は動かさない)。
//!   - 元の線は消さない。
//!   - 作るのは 1 回の取り消しで全部戻る(寄せた線も含めて)。

#include "kachakacha/app/LoopFaces.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QString>

#include <optional>
#include <vector>

class V2MainWindow;

class V2LoopFacesTool final {
public:
    explicit V2LoopFacesTool(V2MainWindow& window);

    //! 命令 surface.from_lines。いま選んでいる線から輪を探し、下見を出す。
    void Start();
    [[nodiscard]] bool Active() const noexcept { return plan_.has_value(); }
    //! Enter / Esc。引き受けたら真。
    [[nodiscard]] bool HandleKey(int key);
    //! 下見を片づけて構えを解く。
    void Clear();
    //! いまの計画(試験から見る)。
    [[nodiscard]] const std::optional<kachakacha::v2::app::LoopFacePlan>& Plan() const noexcept
    {
        return plan_;
    }

private:
    [[nodiscard]] bool Replan();
    void ShowPreview();
    //! ずれを寄せる(直線の端を動かす)。文書が変わったら真。失敗したら偽(理由は出す)。
    [[nodiscard]] bool CloseGaps();
    //! 輪を面にする。作れた数を返す。1 つでも作れなければ 0 で、文書は変えない。
    [[nodiscard]] int BuildFaces();
    //! 1 本の線を新しい線に置き換える(寄せた結果)。新しい線の id を返す。
    [[nodiscard]] kachakacha::v2::base::EntityId ReplaceWire(std::size_t selection,
        const kachakacha::v2::geometry::CurveSegment& segment, const std::string& label);

    V2MainWindow& window_;
    std::vector<kachakacha::v2::modeling::GuideTableSelection> selections_;
    std::optional<kachakacha::v2::app::LoopFacePlan> plan_;
};
