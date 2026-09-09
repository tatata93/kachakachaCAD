#pragma once

//! 作業平面の作り方11通りを、画面から選べるようにするための材料。
//!
//! core は作り方を11通り持っていて、台帳の説明文にも「作り方は11通りあります」と
//! 書いてある。ところが画面は `Standard` しか作らず、押すたびに XY→YZ→ZX を
//! 順ぐりに切り替えるだけだった。
//!
//! そのため **原点を通らない平面が作れない**。船体や車体の「station ごとの断面」を
//! 描くには、平面から離した面や、曲線に直角な面が要る。
//! 工程1「断面をワイヤで描く」が、画面では始められない状態だった。
//!
//! ここは「どの作り方が何を要るか」と「いま選んでいるもので足りるか」だけを決める。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/WorkPlane.h"

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

} // namespace kachakacha::v2::app
