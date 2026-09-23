#pragma once

//! 面にする(線から面。オーナー要望 2026-09-22、UI 設計 2026-09-23 `考察-面作成UIの設計案`)。
//! 線を選んで押すだけで、閉じた輪を全部面にする。
//!
//! 何を面にするか・作り方(平面 / 四辺面 / 境界面 / ロフト)・ずれ・T 字は core(app/LoopFaces)が
//! 端点のつながりから決める。ここは、選んだ線を渡し、輪の下見(実線)とずれ(赤系の破線と × 印)を
//! 3D に出し、右の棚(V2LoopFacesDock)に輪の表を映し、Enter で作り、Esc でやめるだけ。
//!   - 輪ごとに作り方を変えられる(平面 ↔ 境界面、四辺面 ↔ 境界面)。作らない輪は外せる。
//!   - ずれは **黙って寄せない**。どこが何 mm 離れているかを言い、[寄せる](直線の端だけ動かす)か
//!     [そのまま] を人が選ぶ。Enter は「寄せると決めたものを寄せてから作る」。
//!   - T 字(線の端が別の線の途中に乗る)は、Enter のときにその線を分けてから作る(形は変わらない)。
//!   - 許容(端)は棚の数で変えられ、変えると輪の判定が変わる。
//!   - 元の線は残す。作るのは 1 回の取り消しで全部戻る(寄せた線・分けた線も含めて)。
//! 自分の棚を持つ道具(立体を作る・辺の丸め面取りと同じ組み立て)。

#include "V2LoopFacesDock.h"

#include "kachakacha/app/LoopFaces.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class V2MainWindow;

class V2LoopFacesTool final {
public:
    explicit V2LoopFacesTool(V2MainWindow& window);

    //! 命令 surface.from_lines(面にする)。いま選んでいる線から輪を探し、下見と棚を出す。
    void Start();
    [[nodiscard]] bool Active() const noexcept { return plan_.has_value(); }
    //! Enter / Esc。引き受けたら真。
    [[nodiscard]] bool HandleKey(int key);
    //! 下見を片づけて構えを解く。
    void Clear();
    //! 別の道具を持った: 構えも「直前の操作」も片づける。
    void End();
    //! 直前の操作(作ったあと)。棚に「直前: 面にする(n 枚)[開いて直す]」を出している間だけ真。
    [[nodiscard]] bool HasRecent() const noexcept { return recent_.has_value(); }
    //! 直前の操作を開く: 1 回の取り消しで元に戻し、同じ線・同じ選択で構え直す。開けたら真。
    [[nodiscard]] bool ReopenRecent();
    //! 棚(自分の棚を持つ道具)。
    [[nodiscard]] V2LoopFacesDock* Dock() const noexcept { return dock_; }
    //! いまの計画(試験から見る)。
    [[nodiscard]] const std::optional<kachakacha::v2::app::LoopFacePlan>& Plan() const noexcept
    {
        return plan_;
    }
    //! 輪 face のいまの作り方(上書きがあればそれ)。
    [[nodiscard]] kachakacha::v2::app::LoopFaceMethod MethodOf(std::size_t face) const;
    //! 輪 face の辺 edge の連続(隣が無い辺・平面の輪は G0)。
    [[nodiscard]] kachakacha::v2::modeling::SurfaceContinuity ContinuityOf(std::size_t face,
        std::size_t edge) const;
    //! 辺の連続を G0 → G1 → G2 → G0 と回す(棚の欄、3D の Tab)。回せたら真。
    [[nodiscard]] bool CycleContinuity(std::size_t face, std::size_t edge);

private:
    [[nodiscard]] bool Replan();
    void ShowPreview();
    void ShowDock();
    [[nodiscard]] kachakacha::v2::geometry::GeometryTolerance ToleranceNow() const;
    //! T 字で線を分ける(新しい線に置き換える)。文書が変わったら真。
    [[nodiscard]] bool ApplySplits();
    //! 寄せると決めたずれを寄せる(直線の端を動かす)。失敗したら偽(理由は出す)。
    [[nodiscard]] bool CloseGaps();
    //! 輪を面にする。作れた数を返す。1 つでも作れなければ 0 で、文書は変えない。
    [[nodiscard]] int BuildFaces();
    //! 1 本の線を新しい線(片)に置き換える。新しい線の id。
    [[nodiscard]] std::vector<kachakacha::v2::base::EntityId> ReplaceWire(std::size_t selection,
        const std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>>& parts,
        const std::string& label);
    [[nodiscard]] bool Confirm();
    void CloseOneGap(int index);
    //! すでにある面の縁(核の外周)を折れ線にして渡す(辺の連続の相手を探す)。
    [[nodiscard]] std::vector<kachakacha::v2::app::LoopNeighborCurve> Neighbors() const;
    //! 3D で置いている辺(隣のある辺のうち、いちばん近いもの)の連続を回す(Tab)。
    [[nodiscard]] bool CycleContinuityAtHover();
    //! 作る順(同じ計画の輪を支持面にする辺は、その輪を先に作る)。
    [[nodiscard]] std::vector<std::size_t> BuildOrder() const;
    //! 輪 face の辺ごとの連続と支持面(builtIds: 同じ計画で先に作った輪の面)。
    void EdgeSupports(std::size_t face, const std::vector<kachakacha::v2::base::EntityId>& builtIds,
        std::vector<kachakacha::v2::modeling::SurfaceContinuity>& continuity,
        std::vector<kachakacha::v2::base::EntityId>& supports) const;

    V2MainWindow& window_;
    V2LoopFacesDock* dock_ = nullptr;
    std::vector<kachakacha::v2::modeling::GuideTableSelection> selections_;
    std::optional<kachakacha::v2::app::LoopFacePlan> plan_;
    //! 輪ごとの上書き(作り方・作るか)。計画し直すと輪の数に合わせて作り直す。
    std::vector<std::optional<kachakacha::v2::app::LoopFaceMethod>> methodOverride_;
    std::vector<bool> make_;
    //! 輪ごと・辺ごとの連続(G0/G1/G2)。隣のある辺だけ意味を持つ。
    std::vector<std::vector<kachakacha::v2::modeling::SurfaceContinuity>> continuity_;
    //! ずれごとの「そのまま」(寄せない)。
    std::vector<bool> leaveGap_;
    //! 許容(端)の上書き(棚で変えたとき)。
    std::optional<double> joinMm_;

    //! 直前の操作(UI 設計 2-6)。作った直後の入力と選択。文書の版が変わったら開けない。
    struct Recent {
        std::vector<kachakacha::v2::base::EntityId> wireIds;
        std::vector<std::optional<kachakacha::v2::app::LoopFaceMethod>> methods;
        std::vector<bool> make;
        std::vector<std::vector<kachakacha::v2::modeling::SurfaceContinuity>> continuity;
        std::vector<bool> leaveGap;
        std::optional<double> joinMm;
        int made = 0;
        std::uint64_t revision = 0;
    };
    std::optional<Recent> recent_;
    void ShowRecent();
};
