#pragma once

//! 切れ目(fabrication-contract.md §7)。
//!
//! 二重曲率を1枚のまま吸収するための切れ目。入れ方を間違えると、
//! 部材が千切れたり、小片が脱落したり、組んだときに合わなくなる。
//! ここは「入れてよいかどうか」を全部数えて確かめる層。
//! 条件を満たせないなら、無理に入れず、別の分割を勧める。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Point2;

//! 型紙の上での1本の切れ目。
struct ReliefCut {
    std::string cutId;
    //! 入口から先端までの中心線。型紙の座標。
    std::vector<Point2> centerPath;
    //! VNotch のとき、左右の側辺。StraightSlit なら空。
    std::vector<Point2> leftSide;
    std::vector<Point2> rightSide;
    ReliefShape shape = ReliefShape::StraightSlit;
    //! 左右の側辺が組立後にどれだけ離れるか。左右を勝手に詰めて合わせない。
    double mateGapMm = 0.0;
};

//! 切れ目を入れる先の部材。
struct ReliefPanel {
    std::string panelId;
    //! 型紙の上の外周。閉じた並び。
    std::vector<Point2> outline;
    //! 開口。ここへ切れ目を入れてはならない。
    std::vector<std::vector<Point2>> openings;
};

struct ReliefValidation {
    //! 通った切れ目。
    std::vector<std::string> acceptedCutIds;
    //! 一番深い切れ目の、部材幅に対する比。
    double maximumDepthRatio = 0.0;
    //! 先端から向こう側の縁までの、いちばん狭いところ。
    double minimumLigamentMm = 0.0;
    double maximumMateGapMm = 0.0;
};

//! 切れ目を検査する。§7.5 の条件をすべて見る。
//! 満たせない切れ目があれば、FAB-C001〜C006 で断る。
[[nodiscard]] base::Result<ReliefValidation> ValidateReliefCuts(const ReliefPanel& panel,
    const std::vector<ReliefCut>& cuts, const FabricationSettings& settings,
    double targetMaxDeviationMm);

//! 切れ目が必要なのに、設定で切れ目を止めている場合を見つける(FAB-C001)。
[[nodiscard]] std::optional<base::Diagnostic> CheckReliefRequired(
    const FabricationSettings& settings, bool reliefWouldBeNeeded);

// ---- 向きの選び方(§7.4、AT-FAB-004) ----

//! 部材の曲がり具合。向きを選ぶのに使う。
//!
//! U方向とV方向で、どちらがどれだけ曲がっているかを持つ。
//! 二重曲率(両方向に曲がっている)なら、切れ目は曲率の大きい方向へ *直交* して
//! 進める。曲がっている向きに沿って切っても、そこは開かない。
struct PanelCurvature {
    //! U方向の曲率の大きさ(1/mm)。
    double alongUPerMm = 0.0;
    //! V方向の曲率の大きさ(1/mm)。
    double alongVPerMm = 0.0;
    //! 部材の大きさ。細い方向へ深く切ると千切れるので、これも見る。
    double widthUMm = 0.0;
    double widthVMm = 0.0;
};

//! 選ばれた向きと、その理由。
//!
//! 理由を必ず付ける。「なぜかUになった」では利用者が直せない(§7.4)。
struct ReliefDirectionChoice {
    //! 実際に使う向き。Auto は返らない(必ず U / V / Both のどれかに決まる)。
    BendDirection direction = BendDirection::U;
    std::string reasonJa;
    //! U と V の両方を評価したか(Both のとき真)。
    bool evaluatesBoth = false;
};

// ---- 左右の辺の対応(§7.5、AT-FAB-008) ----

//! 切れ目の左右の辺が、どの点どうしで合わさるか。
//!
//! 対応は「正規化した弧長」で取る。端から測った長さの割合が同じ点どうしが合わさる。
//! こうすると、切れ目の形(直線・V・曲がったV)が変わっても同じ決め方で済む。
//!
//! **片方の辺だけを縮めて合わせてはならない。** 縮めると型紙は閉じるが、
//! 組んだ実物は歪む。合わないなら合わないと言う(FAB-C006)。
struct ReliefMateCorrespondence {
    //! 対応する点の、左右それぞれの正規化弧長(0〜1)。同じ数だけ並ぶ。
    std::vector<double> leftParameters;
    std::vector<double> rightParameters;
    //! 左右それぞれの実長。合わせるために縮めたりしない。
    double leftLengthMm = 0.0;
    double rightLengthMm = 0.0;
    //! 組立100%のときに、対応する点どうしが離れる最大量。
    //! 左右の長さが同じなら0になる。
    double maximumGapMm = 0.0;
    ReliefShape shape = ReliefShape::StraightSlit;
};

//! 左右の辺を対応させる。目標の隙間を超えたら FAB-C006 で断る。
[[nodiscard]] base::Result<ReliefMateCorrespondence> CorrespondReliefSides(
    const ReliefCut& cut, double targetMaxGapMm);

//! 向きを決める。Auto のときだけ曲率から選び、それ以外は指定をそのまま使う。
//!
//! 縦だけに固定してはならない、という契約をここで守る。
//! Both のときは U と V の両方を候補にし、片方へ勝手に寄せない。
[[nodiscard]] base::Result<ReliefDirectionChoice> ChooseReliefDirection(
    const FabricationSettings& settings, const PanelCurvature& curvature);

//! その向きで作る切れ目の進行方向(型紙の上の単位ベクトル)。
//! Both のときは2本返る。U / V のときは1本。
[[nodiscard]] base::Result<std::vector<Point2>> ReliefAdvanceDirections(
    const ReliefDirectionChoice& choice);

} // namespace kachakacha::v2::fabrication
