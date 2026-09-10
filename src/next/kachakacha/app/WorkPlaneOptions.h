#pragma once

//! 作業平面の作り方12通りを、画面から選べるようにするための材料。
//!
//! core は作り方を12通り持っていて、台帳の説明文にも「作り方は12通りあります」と
//! 書いてある。ところが画面は `Standard` しか作らず、押すたびに XY→YZ→ZX を
//! 順ぐりに切り替えるだけだった。
//!
//! そのため **原点を通らない平面が作れない**。船体や車体の「station ごとの断面」を
//! 描くには、平面から離した面や、曲線に直角な面が要る。
//! 工程1「断面をワイヤで描く」が、画面では始められない状態だった。
//!
//! ここは「どの作り方が何を要るか」と「いま選んでいるもので足りるか」だけを決める。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! その作り方に要る材料。
struct WorkPlaneNeeds {
    int points = 0;
    int edges = 0;
    //! 元にする平面(作業平面)の数。
    int planes = 0;
    bool usesStandardKind = false;
    bool usesOffset = false;
    bool usesAngle = false;
};

//! いま選んでいるものから分かる事実。
struct WorkPlaneFacts {
    int points = 0;
    int edges = 0;
    int planes = 0;
};

[[nodiscard]] WorkPlaneNeeds NeedsOf(modeling::WorkPlaneMethod method) noexcept;

//! 画面に出す「何を選べばよいか」の一文。
[[nodiscard]] std::string WorkPlaneNeedsJa(modeling::WorkPlaneMethod method);

//! 画面に並べる順。並びを2か所に書かないため、ここから配る。
[[nodiscard]] const std::vector<modeling::WorkPlaneMethod>& WorkPlaneMethods();

//! いま選んでいるもので、その作り方が使えるか。使えないなら理由を言う。
//! 足りないものを黙って補わない。補うと、思っていない平面が出来る。
[[nodiscard]] base::Result<modeling::WorkPlaneMethod> ValidateWorkPlaneChoice(
    modeling::WorkPlaneMethod method, const WorkPlaneFacts& facts);

//! 画面(右パネル)で決めたこと。V1 の平面パネルと同じ欄を持つ。
//! 選択で与える材料(点・線・平面)と、数で与える材料の両方を持つ。
struct WorkPlaneChoice {
    modeling::WorkPlaneMethod method = modeling::WorkPlaneMethod::Standard;
    modeling::StandardPlaneKind standard = modeling::StandardPlaneKind::XY;
    //! 平面の名前。空なら作り方の名前で付ける。
    std::string name;
    double offsetMm = 0.0;
    double angleDeg = 0.0;
    //! PointNormal: 通過点・法線・横方向。
    geometry::Vector3 origin{};
    geometry::Vector3 normal{0.0, 0.0, 1.0};
    geometry::Vector3 uAxis{1.0, 0.0, 0.0};
    //! ThreePoints: 点を3つ選んでいなければ、この数を使う。
    std::array<geometry::Vector3, 3> threePoints{geometry::Vector3{0.0, 0.0, 0.0},
        geometry::Vector3{10.0, 0.0, 0.0}, geometry::Vector3{0.0, 10.0, 0.0}};
    //! AngleAboutEdge: 線を選んでいなければ、この軸(点と向き)を使う。
    geometry::Vector3 axisPoint{};
    geometry::Vector3 axisDirection{1.0, 0.0, 0.0};
    //! 基準平面・相手の平面。コンボで選んだもの。無ければ選択から取る。
    std::optional<base::EntityId> referencePlaneId;
    std::optional<base::EntityId> secondPlaneId;
    //! NormalToCurveAtPoint: 線上の位置(0〜1)。点を選んでいなければこれを使う。
    double curveParameter = 0.5;
};

//! 選択から集めた材料。画面が集めて渡す。選んだ順を保つ。
struct WorkPlaneMaterials {
    std::vector<geometry::Vector3> points;
    std::vector<geometry::CurveSegment> edges;
    //! 選んだ作業平面(順)。
    std::vector<modeling::WorkPlaneFrame> selectedPlanes;
    //! コンボで選んだ基準平面・相手の平面の枠(画面が id から引く)。
    std::optional<modeling::WorkPlaneFrame> referencePlane;
    std::optional<modeling::WorkPlaneFrame> secondPlane;
};

//! 選んでいるものと数の欄から、core の要求を組み立てる。
//! 足りないものがあれば UI-W001 で断る(黙って補わない)。
//! ただし 3点・軸・線上位置は「選んでいなければ数の欄」で、これは補いではなく指定である。
[[nodiscard]] base::Result<modeling::WorkPlaneRequest> BuildWorkPlaneRequest(
    const WorkPlaneChoice& choice, const WorkPlaneMaterials& materials);

//! 平面に付ける名前。空なら作り方の名前。
[[nodiscard]] std::string WorkPlaneDisplayName(const WorkPlaneChoice& choice);

} // namespace kachakacha::v2::app
