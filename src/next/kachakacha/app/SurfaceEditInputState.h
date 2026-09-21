#pragma once

//! 「面の編集」の入力の状態(プロンプト additional_surface_tools)。
//!
//!   面を合わせる   縁 2 本(直す面の縁 → 合わせ先の縁)+ G0/G1/G2
//!   面をつなぐ     縁 2 本(縁 A・縁 B)+ 両端の G0/G1/G2 + 張りの強さ
//!   面を整える     面 1〜任意 + 許容(1 枚ずつ整え直す)
//!   対称に写す     面 1〜任意 + 対称面(1 枚ずつ写す)
//!   U/V 線         面 1〜任意 + 向きと本数(線として取り出す)
//!   面へ投影       落とす先の面 1 + 線 1〜任意(既存の「面へ投影」を厳密にしたもの)
//!
//! 道具から始める: 押すと棚が出て、3D で面(縁は面の縁の近くを押す)や線を押すと入り、
//! 押し直すと外れる。何がどの欄に入るかはここが決める。画面は映して押すだけ。
//! **欄を「縁1」「縁2」のように固定で増やさない。** 本数の決まりは SurfaceEditSlotsFor。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class SurfaceEditOperation {
    Match,
    Bridge,
    Refit,
    Mirror,
    IsoCurve,
    CurveOnSurface,
};

[[nodiscard]] std::string_view SurfaceEditLabelJa(SurfaceEditOperation operation) noexcept;
//! 台帳の命令(`surface.match` など)。面へ投影は既存の `wire.project_surface`。
[[nodiscard]] std::string_view SurfaceEditCommandId(SurfaceEditOperation operation) noexcept;
//! 命令から作り方を引く。面の編集の命令でなければ偽。
[[nodiscard]] bool SurfaceEditOperationForCommand(std::string_view id,
    SurfaceEditOperation& operation) noexcept;
[[nodiscard]] const std::vector<SurfaceEditOperation>& SurfaceEditOperations();
//! 作り方の一言(棚のカードに出す)。
[[nodiscard]] std::string_view SurfaceEditHintJa(SurfaceEditOperation operation) noexcept;

//! 面の縁 1 本。縁の番号は、押した点に一番近い縁を核が決める(-1 = まだ決まっていない)。
struct SurfaceEdgePick {
    base::EntityId surface;
    int edgeIndex = -1;
    geometry::Vector3 hitPoint{};
};

enum class MirrorPlaneChoice {
    //! 車体の中心(Y = 0 の面、XZ 平面)。鉄道車両の左右対称に使う。
    CenterXZ,
    //! X = 0 の面(YZ 平面)。
    CenterYZ,
    //! Z = 0 の面(XY 平面)。
    CenterXY,
    //! いまの作業平面。
    WorkPlane,
};

[[nodiscard]] std::string_view MirrorPlaneLabelJa(MirrorPlaneChoice choice) noexcept;

struct SurfaceEditInputState {
    SurfaceEditOperation operation = SurfaceEditOperation::Match;
    //! 合わせる: [0] 直す面の縁、[1] 合わせ先の縁。つなぐ: [0] 縁 A、[1] 縁 B。
    std::vector<SurfaceEdgePick> edges;
    //! 整える・対称・U/V 線: 1〜任意。面へ投影: 落とす先の面 1 枚。
    std::vector<base::EntityId> surfaces;
    //! 面へ投影: 落とす線 1〜任意。
    std::vector<base::EntityId> wires;
    //! 合わせる: 合わせ方。つなぐ: 縁 A 側。
    modeling::SurfaceContinuity continuityA = modeling::SurfaceContinuity::G1;
    //! つなぐ: 縁 B 側。
    modeling::SurfaceContinuity continuityB = modeling::SurfaceContinuity::G1;
    //! 整える: 元の面からの許容(mm)。
    double toleranceMm = 0.01;
    //! つなぐ: 縁から出る向きの強さ(1 = 標準)。
    double tension = 1.0;
    //! U/V 線: 0 = U 方向の線、1 = V 方向の線、2 = 両方。
    int isoDirection = 2;
    //! U/V 線: 向きごとの本数。
    int isoCount = 5;
    MirrorPlaneChoice mirrorPlane = MirrorPlaneChoice::CenterXZ;
};

//! その作り方で使う欄と本数。0/0 はその欄を使わない。
struct SurfaceEditSlots {
    std::size_t edges = 0;
    std::size_t surfacesMinimum = 0;
    std::size_t surfacesMaximum = 0;
    std::size_t wiresMinimum = 0;
    std::size_t wiresMaximum = 0;
    bool continuityA = false;
    bool continuityB = false;
    //! 面が複数のとき、1 枚ずつ別に作る(まとめて 1 つの面にはしない)。
    bool batchPerSurface = false;
};

[[nodiscard]] SurfaceEditSlots SurfaceEditSlotsFor(SurfaceEditOperation operation) noexcept;

//! 縁の欄の名前(合わせる: 直す面の縁 / 合わせ先の縁、つなぐ: 縁 A / 縁 B)。
[[nodiscard]] std::string SurfaceEdgeSlotNameJa(SurfaceEditOperation operation, std::size_t index);

//! 3D で面を押した。縁を使う作り方なら縁の欄へ(同じ面を押せばその欄の縁を押した縁に替え、
//! 同じ縁なら外す)、面を使う作り方なら面の欄へ(押し直すと外れる)。
[[nodiscard]] SurfaceEditInputState WithSurfaceEditSurfacePick(const SurfaceEditInputState& state,
    const base::EntityId& surface, const geometry::Vector3& hitPoint, int edgeIndex);

//! 3D で線を押した(面へ投影だけが受ける)。押し直すと外れる。
[[nodiscard]] SurfaceEditInputState WithSurfaceEditWirePick(const SurfaceEditInputState& state,
    const base::EntityId& wire);

//! 一覧の ×。縁・面・線のどれに入っていても外す。
[[nodiscard]] SurfaceEditInputState WithoutSurfaceEditEntry(const SurfaceEditInputState& state,
    const base::EntityId& id);

//! 作り方を替える。入れたものは使える限り残す(縁の面 ↔ 面の欄)。
[[nodiscard]] SurfaceEditInputState WithSurfaceEditOperation(const SurfaceEditInputState& state,
    SurfaceEditOperation operation);

//! 足りないもの。足りていれば空。
[[nodiscard]] std::string SurfaceEditMissingJa(const SurfaceEditInputState& state);
[[nodiscard]] bool SurfaceEditReadyToBuild(const SurfaceEditInputState& state);

//! 下見の結果の要約。下見と確定は同じものを使う。
struct SurfaceEditOutcome {
    bool evaluated = false;
    bool available = false;
    std::string refusalJa;
    //! 測った値の言葉(滑らかさ・動いた量・制御点の数など)。
    std::string noteJa;
    //! 出来るものの数(面・線)。
    std::size_t outputs = 0;
};

//! 棚の「状態」に出す行。
[[nodiscard]] std::vector<std::string> SurfaceEditStatusLinesJa(const SurfaceEditInputState& state,
    const SurfaceEditOutcome& outcome, bool previewShown);

//! 一番下の一行。「面を合わせる: EDGES=2/2 / G1 / Preview only」。
[[nodiscard]] std::string SurfaceEditFooterLine(const SurfaceEditInputState& state,
    const SurfaceEditOutcome& outcome, bool previewShown);

//! 対称面(点と法線)。作業平面は呼び出し側が渡す。
struct MirrorPlane {
    geometry::Vector3 point{};
    geometry::Vector3 normal{0.0, 1.0, 0.0};
};

[[nodiscard]] MirrorPlane MirrorPlaneFor(MirrorPlaneChoice choice,
    const geometry::Vector3& workOrigin, const geometry::Vector3& workNormal) noexcept;

} // namespace kachakacha::v2::app
