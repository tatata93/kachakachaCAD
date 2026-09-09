#pragma once

//! 押し出しで利用者が決めることを、1か所にまとめる(fabrication は工程3、ここは工程2)。
//!
//! core の `ExtrudeRequest` は、向き7通り・終端5通り(「ある面まで」を含む)・
//! 出力3通り・部品演算3通りを持っている。ところが画面はそれぞれ1通りに固定していて、
//! 「ワイヤーだけ作る」も「あの面まで押す」も選べなかった。
//! 工程の案内には「方向 → 終端(距離か、届かせる相手) → 出力」と書いてあるのに、
//! 実際には選べない、という食い違いになっていた。
//!
//! ここは **選んだ組み合わせが通るかどうか** だけを決める。OCCT も Qt も呼ばない。
//! 画面はこの答えを見て、欄を出したり断ったりするだけにする。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/modeling/ExtrudeInput.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 利用者が決めたひと組。`ExtrudeRequest` へ写す前の、画面に出す形。
struct ExtrudeChoice {
    modeling::ExtrudeDirectionMode direction =
        modeling::ExtrudeDirectionMode::WorkPlaneNormal;
    //! CustomXYZ / SelectedVector のとき。
    geometry::Vector3 customDirection{0.0, 0.0, 1.0};
    bool reversed = false;

    modeling::ExtrudeExtentMode extent = modeling::ExtrudeExtentMode::Distance;
    double distanceMm = 0.5;
    //! TwoDistances の逆側。
    double secondDistanceMm = 0.5;
    //! ToTarget の相手。作業平面か、部品の面の代わりに使う平面。
    std::optional<base::EntityId> targetEntityId;

    //! 何を作るか。1つも選ばないのは通さない。
    bool makePart = true;
    bool makeEndProfileWire = false;
    bool makeSideBoundaryWires = false;

    modeling::ExtrudeBooleanMode booleanMode = modeling::ExtrudeBooleanMode::NewPart;
    //! 足す/引く相手を明示して選んでいるか。近い部品を勝手に選ばない。
    bool hasSelectedPart = false;
    //! 距離0でワイヤーだけ作ることを、利用者が承知しているか。
    bool zeroDistanceConfirmed = false;
};

//! いま選んでいるものから分かる事実。通るかどうかの判断に要る。
struct ExtrudeFacts {
    int closedProfiles = 0;
    int openProfiles = 0;
    //! 「ある面まで」の相手にできるもの(作業平面)がいくつ選ばれているか。
    int targetPlanes = 0;
    //! 足す/引くの相手にできる部品がいくつ選ばれているか。
    int parts = 0;
};

//! 画面に出す名前。台帳ではなく、ここが唯一の出どころ。
[[nodiscard]] std::string_view ExtrudeDirectionNameJa(
    modeling::ExtrudeDirectionMode mode) noexcept;
[[nodiscard]] std::string_view ExtrudeExtentNameJa(
    modeling::ExtrudeExtentMode mode) noexcept;
[[nodiscard]] std::string_view ExtrudeBooleanNameJa(
    modeling::ExtrudeBooleanMode mode) noexcept;

//! 画面に並べる順。並びを2か所に書かないため、ここから配る。
[[nodiscard]] const std::vector<modeling::ExtrudeDirectionMode>& ExtrudeDirections();
[[nodiscard]] const std::vector<modeling::ExtrudeExtentMode>& ExtrudeExtents();
[[nodiscard]] const std::vector<modeling::ExtrudeBooleanMode>& ExtrudeBooleans();

//! その終端で、距離の欄を出すか。
[[nodiscard]] bool ExtentUsesDistance(modeling::ExtrudeExtentMode mode) noexcept;
//! その終端で、逆側の距離の欄を出すか。
[[nodiscard]] bool ExtentUsesSecondDistance(modeling::ExtrudeExtentMode mode) noexcept;
//! その終端で、届かせる相手を選ばせるか。
[[nodiscard]] bool ExtentUsesTarget(modeling::ExtrudeExtentMode mode) noexcept;

//! 選んだ組み合わせが通るか。通らないなら、なぜかを言う。
//!
//! - 出力を1つも選んでいない → EXT-U001
//! - 部品を作るのに閉じた輪郭が無い → EXT-U002
//! - 「ある面まで」なのに相手を選んでいない → EXT-U003
//! - 足す/引くなのに相手の部品を選んでいない → EXT-U004
//! - 距離が要るのに0以下 → EXT-U005(ただしワイヤーだけなら、承知の上で通す)
//! - 「全部貫く」は引くときだけ → EXT-U006
[[nodiscard]] base::Result<ExtrudeChoice> ValidateExtrudeChoice(
    const ExtrudeChoice& choice, const ExtrudeFacts& facts);

//! 決まったことを一文にする。押す前に、何が起きるかを見せるため。
[[nodiscard]] std::string ExtrudeSummaryJa(const ExtrudeChoice& choice);

//! 画面が決めたひと組を、core の要求へ写す。写し方を2か所に書かない。
[[nodiscard]] modeling::ExtrudeRequest ToExtrudeRequest(const ExtrudeChoice& choice,
    std::vector<modeling::ExtrudeProfile> profiles,
    const modeling::WorkPlaneFrame& workPlane,
    const std::optional<modeling::WorkPlaneFrame>& targetPlane);

} // namespace kachakacha::v2::app
