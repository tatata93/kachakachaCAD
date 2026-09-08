#pragma once

//! 型紙の書き出し(fabrication-contract.md §9、work-packages WP-11)。
//!
//! 決まりごと:
//!   - 曲線は曲線のまま出す。円弧を折れ線へ落とさない。
//!     SVG は円弧コマンド、DXF は ARC / SPLINE を使う。
//!   - 外周・折り線・切れ目・開口は別レイヤーにする。
//!   - 実寸で出す。ビューアの都合で勝手に縮めない。
//!   - 同じ入力からは同じバイト列が出る(版管理に載せられるように)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <string>
#include <vector>

namespace kachakacha::v2::exporters {

using geometry::CurveSegment;
using geometry::Point2;

enum class PatternLine {
    Outline,
    Fold,
    Cut,
    Opening,
    Annotation,
};

[[nodiscard]] std::string_view PatternLineLayerName(PatternLine line) noexcept;

//! 型紙の上の1本の線。曲線種類を保ったまま持つ。
//! 平面(型紙)の上なので、曲線は「XY平面に置いた CurveSegment」として渡す。
struct PatternCurve {
    PatternLine layer = PatternLine::Outline;
    CurveSegment segment;
    //! 折り線のとき、山折りなら true。
    bool mountainFold = false;
    std::string label;   //!< 部材番号、Fold番号、MatePair番号など
};

struct PatternPage {
    double widthMm = 210.0;
    double heightMm = 297.0;
    std::vector<PatternCurve> curves;
};

struct PatternDocument {
    std::string title;
    std::vector<PatternPage> pages;
    //! 縮尺の分母。1/87 なら 87。表示用の注記に使う。原寸で出すことは変えない。
    double referenceScaleDenominator = 0.0;
};

//! SVG。1ページぶん。単位は mm。
[[nodiscard]] base::Result<std::string> WritePatternSvg(const PatternPage& page,
    const std::string& title);

//! DXF(R12 相当の最小構成)。LINE / ARC / CIRCLE / LWPOLYLINE を使う。
[[nodiscard]] base::Result<std::string> WritePatternDxf(const PatternPage& page);

//! 三角形の集まりを STL(ASCII)で出す。
struct Triangle {
    geometry::Vector3 first{};
    geometry::Vector3 second{};
    geometry::Vector3 third{};
};

[[nodiscard]] base::Result<std::string> WriteAsciiStl(const std::vector<Triangle>& triangles,
    const std::string& name);

//! STL(バイナリ)。同じ入力からは同じバイト列。
[[nodiscard]] base::Result<std::string> WriteBinaryStl(
    const std::vector<Triangle>& triangles);

} // namespace kachakacha::v2::exporters
