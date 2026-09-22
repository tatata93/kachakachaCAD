#pragma once

//! 立体を作る(部品モードの「作成」: 回転体・ロフト立体・スイープ、matrix P-08/P-09)の
//! 入力検査と予測。
//!
//! 押し出しと同じ考え方である。core が入力を調べ、作ってよいなら予測を出す
//! (回転体は Pappus の定理で体積を出す)。実際の形は OCCT 側(kernel/OcctSolid)が作り、
//! 予測と突き合わせる。合わなければ、その結果は捨てる。
//!
//! 輪郭(閉じた線、外周と穴)の読み方は押し出しと同じもの(AnalyzeExtrudeRequest)を使う。
//! 「閉じているか」「同じ平面か」を 2 か所で別々に決めない。

#include "kachakacha/modeling/ExtrudeInput.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::modeling {

//! 立体の作成の入力が揃っていない(数・閉じていない・種類が違う)。
inline constexpr const char* kSolidBadInput = "SOL-001";
//! 輪郭・断面が平面に載っていない。
inline constexpr const char* kSolidNotPlanar = "SOL-002";
//! 回転軸が輪郭の平面に載っていない。
inline constexpr const char* kSolidAxisOffPlane = "SOL-003";
//! 回転軸が輪郭の内側を通っている(回すと自分と重なる)。
inline constexpr const char* kSolidAxisCrossesProfile = "SOL-004";
//! 回す角度が 0° 以下か 360° より大きい。
inline constexpr const char* kSolidBadAngle = "SOL-005";
//! スイープの経路がつながっていない・枝分かれしている。
inline constexpr const char* kSolidPathBroken = "SOL-006";
//! ロフトの断面どうしが重なっている。
inline constexpr const char* kSolidSectionsOverlap = "SOL-007";
//! スイープの経路が輪郭の平面から始まっていない、または輪郭の面に沿っている。
inline constexpr const char* kSolidPathOffProfile = "SOL-008";

enum class SolidMethod {
    Revolve,   //!< 回転体(輪郭 + 軸 + 角度)
    Loft,      //!< ロフト立体(閉じた断面 2〜任意)
    Sweep,     //!< スイープ(輪郭 + 経路)
};

[[nodiscard]] std::string_view SolidMethodNameJa(SolidMethod method) noexcept;

// ---- 回転体 ----

struct RevolveSolidRequest {
    //! 閉じた輪郭(外周と穴。押し出しと同じ読み方)。同じ平面に載っていること。
    std::vector<ExtrudeProfile> profiles;
    Vector3 axisPoint{};
    Vector3 axisDirection{0.0, 0.0, 1.0};
    //! 回す角度(0 < 角度 ≤ 2π)。
    double angleRad = 6.283185307179586;
    //! 輪郭の面を中心に、両側へ半分ずつ回す(対称回転)。
    bool symmetric = false;
};

struct RevolveSolidAnalysis {
    //! 輪郭の平面・外周と穴(押し出しの読み方そのもの)。
    ExtrudeAnalysis profile;
    Vector3 axisPoint{};
    //! 正規化した軸の向き。
    Vector3 axisDirection{0.0, 0.0, 1.0};
    double angleRad = 0.0;
    //! 回し始めの角度(対称なら -角度/2、そうでなければ 0)。
    double startAngleRad = 0.0;
    //! 予測した体積(Pappus: 角度 × 面積 × 重心から軸までの距離)。
    double predictedVolumeMm3 = 0.0;
    //! 面積と重心を標本の多角形から出したので近似か(突き合わせの厳しさを変える)。
    bool volumeIsApproximate = true;
};

[[nodiscard]] base::Result<RevolveSolidAnalysis> AnalyzeRevolveSolid(
    const RevolveSolidRequest& request, const GeometryTolerance& tolerance);

// ---- ロフト立体 ----

struct LoftSolidRequest {
    //! 閉じた断面 2〜任意。並びは人が拾った順(面のロフトと同じく断面順の欄で入れ替える)。
    std::vector<ExtrudeProfile> sections;
};

struct LoftSolidAnalysis {
    //! 断面ごとの平面(両端の断面は平面でなければ蓋ができない)。
    std::vector<geometry::PlaneFit> planes;
};

[[nodiscard]] base::Result<LoftSolidAnalysis> AnalyzeLoftSolid(const LoftSolidRequest& request,
    const GeometryTolerance& tolerance);

// ---- スイープ ----

struct SweepSolidRequest {
    //! 閉じた輪郭(外周と穴)。同じ平面に載っていること。
    std::vector<ExtrudeProfile> profiles;
    //! 経路。何本の線でもよいが、1 本につながっていること(順と向きは検査が決める)。
    std::vector<CurveSegment> path;
};

struct SweepSolidAnalysis {
    ExtrudeAnalysis profile;
    //! つないだ経路(輪郭の平面から始まる向き)。
    std::vector<CurveSegment> orderedPath;
    bool pathClosed = false;
};

[[nodiscard]] base::Result<SweepSolidAnalysis> AnalyzeSweepSolid(
    const SweepSolidRequest& request, const GeometryTolerance& tolerance);

//! 出来た立体の体積が予測と合っているか(回転体)。近似の予測は 0.5%、厳密なら 1e-6。
[[nodiscard]] bool RevolveVolumeMatches(const RevolveSolidAnalysis& analysis,
    double actualVolumeMm3);

} // namespace kachakacha::v2::modeling
