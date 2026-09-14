#pragma once

//! 立体の面を1枚取り出して、押し引きできる形にする(EX-02)。
//!
//! オーナーの手順の「立体の面をつまんで押す・引く」を通すために要る。
//! いまの押し出しは輪郭(ワイヤー)からしか始められない。
//! 面から始めるには、その面の縁を輪郭として取り出す道が要る。
//!
//! **新しい押し出しの仕掛けは作らない。** 面の縁を輪郭に直して、
//! いままでの押し出し(`BuildExtrude`)へそのまま渡す。
//! 押した先は立体に足し、引いた先は立体から削る。
//! こうしておくと、突き合わせ(体積・面数の予測)も今までのものがそのまま効く。
//!
//! 面の番号は `OcctTessellate` が三角形へ書き込むものと同じ順で数える
//! (`TopExp_Explorer(shape, TopAbs_FACE)` の順)。画面が拾った番号を
//! そのままここへ渡せる。順が食い違うと、押した面と別の面が動く。
//!
//! OCCT の型を外へ出さない。返すのは core の型だけ(AT-ARC-001)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::kernel {

//! 面を取り出す層の診断コード。
inline constexpr const char* kFaceSourceMissing = "KER-F001";
inline constexpr const char* kFaceIndexOutOfRange = "KER-F002";
inline constexpr const char* kFaceNotPlanar = "KER-F003";
inline constexpr const char* kFaceBoundaryUnsupported = "KER-F004";

//! 立体の面1枚の縁。
struct FaceBoundary {
    //! 外周。閉じている。
    std::vector<geometry::CurveSegment> outerLoop;
    //! 穴。1つも無いこともある。
    std::vector<std::vector<geometry::CurveSegment>> holeLoops;
    //! 面の上の1点(縁ではなく面そのものの基準)。
    geometry::Vector3 origin{};
    //! 立体の外を向く法線。押す向きの既定はこちら、引く向きはこの逆。
    geometry::Vector3 outwardNormal{0.0, 0.0, 1.0};
    double areaMm2 = 0.0;
};

//! 立体の face 番目の面の縁を取り出す。
//!
//! 平らな面だけを扱う。曲がった面は KER-F003 で断る。
//! 曲がった面の押し引きは、まっすぐ押しても元の面と辻褄が合わないためである。
//! 「できないことを、できたことにしない」。
[[nodiscard]] base::Result<FaceBoundary> FaceBoundaryOf(modeling::KernelShapeHandle handle,
    std::size_t faceIndex, const geometry::GeometryTolerance& tolerance);

//! 面1枚を格子状に標本化したもの。曲がり方を測るために要る。
struct FaceSamples {
    fabrication::SurfacePatchSamples samples;
    double areaMm2 = 0.0;
};

//! 面を格子状に標本化する。平らでない面も通る(測るためのものだから)。
//!
//! 標本の数は既定で 17×17。目標偏差に対して粗すぎる場合は呼ぶ側が増やす。
//! 面積も一緒に返す。分け方の判断で「大きい面を残す」に要る。
[[nodiscard]] base::Result<FaceSamples> FaceSamplesOf(modeling::KernelShapeHandle handle,
    std::size_t faceIndex, std::size_t rowCount = 17, std::size_t columnCount = 17);

//! 面の上の1点と、そこでの向き。「選択に正対」が要る(Q1)。
struct FacePose {
    //! 面の上の点。押した場所をいちばん近い面上の点へ寄せたもの。
    geometry::Vector3 point{};
    //! 立体の外を向く法線。この向きから見ると正対になる。
    geometry::Vector3 normal{0.0, 0.0, 1.0};
    //! 面の上の「横」の向き。画面の上下を決めるのに使う。
    geometry::Vector3 uAxis{1.0, 0.0, 0.0};
    //! 平らな面か。曲がっていれば偽。
    bool planar = false;
};

//! 面の、指定した点にいちばん近い場所での向きを返す。
//!
//! 曲面には1つの法線が無いので、**押した場所の近く** の法線を使う(オーナー指示 §4)。
//! 押した場所が分からないときは面の真ん中を使う。
//! 特異点などで向きが決まらないときは、面の真ん中、それも駄目なら
//! 標本から作った近似平面の法線へ落とす。**断って終わらない。**
[[nodiscard]] base::Result<FacePose> FacePoseNear(modeling::KernelShapeHandle handle,
    std::size_t faceIndex, const geometry::Vector3& nearPoint,
    const geometry::GeometryTolerance& tolerance);

//! 立体が持つ面の数。画面が拾った番号を渡す前に確かめるために使う。
[[nodiscard]] base::Result<std::size_t> ShapeFaceCount(modeling::KernelShapeHandle handle);

} // namespace kachakacha::v2::kernel
