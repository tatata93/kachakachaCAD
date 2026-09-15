#pragma once

//! 押し出しの下見を、塗れる面にする(オーナー指示 2026-09-15 §8)。
//!
//! これまでの下見は線だけだった。線だけだと
//!   - どちらが「押し出した先」なのかが、輪郭の重なり方でしか分からない
//!   - 引いている最中に、厚みがついているのかどうかが読めない
//! ので、うすく塗った面を下に敷く。**線は今までどおり上に出す。**
//!
//! ここは点を並べるだけ。塗りの色も濃さも画面の仕事。

#include "kachakacha/geometry/Vector3.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::app {

using geometry::Vector3;

//! うすく塗る面。1枚が閉じた多角形。
using PreviewFaces = std::vector<std::vector<Vector3>>;

//! 輪郭を `offset` だけ押したときの、側面と押し出し先のふた。
//!
//! 側面は輪郭の1区間ごとに1枚。ふたは押し出し先の輪郭そのもの。
//! 始めの輪郭は塗らない。**そこにはもう線がある**ので、塗ると元の図が沈む。
//! 距離が 0 に近いときは側面もふたも返さない(厚みが無いのに厚く見せない)。
[[nodiscard]] PreviewFaces ExtrudeSweptFaces(const std::vector<Vector3>& outline,
    const Vector3& offset);

} // namespace kachakacha::v2::app
