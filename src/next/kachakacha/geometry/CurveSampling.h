#pragma once

//! 曲線を「調べるため」に点列へ落とす道具(geometry-contract §2、§6)。
//!
//! 大事な区別:
//!   ここで作る点列は *検査用* であって、正本ではない。
//!   同一平面か、鎖どうしが交差するか、面が断面を通っているかを数値で見るために使う。
//!   点列を保存したり、面の入力としてOCCTへ渡したりしてはならない。
//!   曲線は最後まで曲線のまま扱う(V1が折れ線へ落として形を崩した反省)。
//!
//! 分割の粗さは「弦の中点と曲線の実際の点との距離」で決める。
//! 一定分割にすると、半径の大きい円弧で粗く、小さい円弧で無駄に細かくなる。

#include "kachakacha/geometry/CurveSegment.h"

#include <vector>

namespace kachakacha::v2::geometry {

//! 曲線上の1点。どこの点かを後から言えるように、parameterも持つ。
struct CurvePoint {
    double parameter = 0.0;
    Vector3 position{};
};

//! 与えた許容差以内で曲線を近似する点列を作る。両端は必ず含む。
//! toleranceMm は「弦が曲線からどれだけ離れてよいか」。
[[nodiscard]] std::vector<CurvePoint> SampleCurve(const CurveSegment& segment,
    double toleranceMm, int maximumPoints = 4096);

//! 鎖(順に繋がった曲線の並び)をまとめて点列にする。継ぎ目の重複は取り除く。
[[nodiscard]] std::vector<Vector3> SampleChain(const std::vector<CurveSegment>& segments,
    double toleranceMm);

//! 閉じた鎖の点列は、最後の点が最初の点と同じになる。
//! そのまま多角形として扱うと、最初の辺と最後の辺が「隣どうし」でなくなり、
//! 角を共有しているだけの2辺を交差と誤判定する。閉じる前に必ず通すこと。
void RemoveClosingDuplicate(std::vector<Vector3>& points, double toleranceMm);

//! 点列の弧長を 0..1 へ正規化した値。断面どうしの対応付けに使う。
[[nodiscard]] std::vector<double> NormalizedArcLength(const std::vector<Vector3>& points);

//! 正規化弧長 t での位置。点列の間は直線で補間する(検査用なので十分)。
[[nodiscard]] Vector3 PointAtNormalizedArcLength(const std::vector<Vector3>& points,
    const std::vector<double>& parameters, double t);

//! 点列の重心。
[[nodiscard]] Vector3 Centroid(const std::vector<Vector3>& points);

//! 最小二乗で当てはめた平面と、そこからの最大距離。
struct PlaneFit {
    Vector3 origin{};
    Vector3 normal{0.0, 0.0, 1.0};
    double maximumDeviationMm = 0.0;
    bool valid = false;   //!< 点が3つ未満、または全部一直線なら false
};

[[nodiscard]] PlaneFit FitPlane(const std::vector<Vector3>& points);

//! 点列を平面の局所座標(u, v)へ落とす。
struct PlanarFrame {
    Vector3 origin{};
    Vector3 uDirection{1.0, 0.0, 0.0};
    Vector3 vDirection{0.0, 1.0, 0.0};
    Vector3 normal{0.0, 0.0, 1.0};
};

[[nodiscard]] PlanarFrame MakeFrame(const PlaneFit& fit);

struct Point2 {
    double u = 0.0;
    double v = 0.0;
};

[[nodiscard]] std::vector<Point2> ProjectToFrame(const std::vector<Vector3>& points,
    const PlanarFrame& frame);

// ---- 平面上の多角形として調べる ----

//! 符号つき面積。反時計回りなら正。
[[nodiscard]] double SignedArea(const std::vector<Point2>& loop);

//! 点が閉多角形の内側にあるか(交差数で判定)。
[[nodiscard]] bool ContainsPoint(const std::vector<Point2>& loop, const Point2& point);

//! 閉多角形が自分自身と交わっているか。隣り合う辺どうしは除く。
[[nodiscard]] bool HasSelfIntersection(const std::vector<Point2>& loop, double toleranceMm);

//! 2つの閉多角形の辺が交わっているか。
[[nodiscard]] bool LoopsIntersect(const std::vector<Point2>& first,
    const std::vector<Point2>& second, double toleranceMm);

//! 点列どうしの最も近づく場所。線分どうしで測るので、点の間で交差していても見つかる。
//! parameter は各点列の正規化弧長。
struct PolylineApproach {
    double distanceMm = 0.0;
    double firstParameter = 0.0;
    double secondParameter = 0.0;
    Vector3 firstPoint{};
    Vector3 secondPoint{};
    bool valid = false;
};

[[nodiscard]] PolylineApproach ClosestApproachBetween(const std::vector<Vector3>& first,
    const std::vector<Vector3>& second);

//! 点列どうしの最短距離。3D。線分どうしで測る。
[[nodiscard]] double MinimumDistanceBetween(const std::vector<Vector3>& first,
    const std::vector<Vector3>& second);

//! first の各点から second までの最大距離。面が断面を通っているかを見るのに使う。
[[nodiscard]] double MaximumDeviationTo(const std::vector<Vector3>& probe,
    const std::vector<Vector3>& reference);

} // namespace kachakacha::v2::geometry
