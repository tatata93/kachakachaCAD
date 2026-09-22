#pragma once

//! 「立体を作る」の道具(回転体・ロフト立体・スイープ、matrix P-08/P-09)。
//!
//! 道具から始める: 帯の 回転体 / ロフト立体 / スイープ を押すと棚(V2SolidDock)が出る →
//! 3D で線を押すと種類で欄に入る(閉じた線 → 輪郭・断面、直線 → 回転軸、開いた線 → 経路、
//! 足す・引くのときの部品 → 相手)。押し直すと外れる → そろえば **実際に核で作って**
//! 稜線を下見 → Enter で確定(下見に使った形をそのまま文書へ)/ Esc でやめる(文書は変わらない)。
//! 確定は 1 回の取り消しで全部戻る。開き直したら、同じ道(Build)で作り直す。
//!
//! 画面の窓(V2MainWindow)の中身を使うので、窓の友達にしてある。窓の頭(ヘッダ)を
//! 太らせないため、状態と手順はこちらに持つ(V2SurfaceEditTool と同じ形)。
//! 何がどの欄に入るかは core(app/SolidInputState)、入力の検査と予測は
//! core(modeling/SolidInput)、形は kernel(OcctSolid)が決める。

#include "V2SolidDock.h"

#include "kachakacha/app/SolidInputState.h"
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/kernel/OcctSolid.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class V2MainWindow;

class V2SolidTool final {
public:
    explicit V2SolidTool(V2MainWindow& window);

    [[nodiscard]] V2SolidDock* Dock() const noexcept { return dock_; }
    [[nodiscard]] bool Active() const noexcept { return active_; }
    //! この道具の命令(part.revolve / part.loft_solid / part.sweep)か。
    [[nodiscard]] static bool Handles(std::string_view commandId);
    //! 命令で始める。構えていれば、同じ作り方なら確定、違えば作り方だけ替える。
    void Begin(std::string_view commandId);
    void End();
    void Confirm();
    //! 3D の選択が変わった(窓の HandleSelectionChanged から)。
    void HandleSelectionChanged();
    //! Enter / Esc。引き受けたら真。
    [[nodiscard]] bool HandleKey(int key);
    //! 開き直したときの作り直し(CreateSolid の Feature)。下見・確定と同じ道を通す。
    [[nodiscard]] bool Rebuild(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);

    [[nodiscard]] const kachakacha::v2::app::SolidInputState& Input() const noexcept
    {
        return input_;
    }
    [[nodiscard]] const kachakacha::v2::app::SolidPreviewOutcome& Outcome() const noexcept
    {
        return outcome_;
    }
    //! 下見の形があるか(確定でそのまま文書へ入る形)。
    [[nodiscard]] bool PreviewBuilt() const noexcept { return built_.has_value(); }

private:
    void ChooseRevolveMode(kachakacha::v2::app::RevolveMode mode);
    void Refresh();
    void RefreshPreview();
    void RefreshDock();
    void MirrorToSelection();
    void ApplyPick(const kachakacha::v2::base::EntityId& id);
    [[nodiscard]] kachakacha::v2::app::SolidPickKind KindOf(
        const kachakacha::v2::base::EntityId& id) const;
    [[nodiscard]] std::vector<kachakacha::v2::geometry::CurveSegment> SegmentsOf(
        const kachakacha::v2::base::EntityId& id) const;
    [[nodiscard]] std::string NameOf(const kachakacha::v2::base::EntityId& id) const;
    [[nodiscard]] std::string NamesOf(const std::vector<kachakacha::v2::base::EntityId>& ids) const;
    //! いまの入力を、文書に残す作り方の形へ(下見・確定・開き直しで同じもの)。
    [[nodiscard]] kachakacha::v2::domain::CreateSolidDefinition DefinitionNow() const;
    //! 作り方から立体を作る(足す・引くまで)。**下見・確定・開き直しはこの 1 本だけを通す。**
    [[nodiscard]] kachakacha::v2::base::Result<kachakacha::v2::kernel::SolidBuildResult> Build(
        const kachakacha::v2::domain::CreateSolidDefinition& definition) const;
    //! 足す・引くの前の、作り方そのものの立体。
    [[nodiscard]] kachakacha::v2::base::Result<kachakacha::v2::kernel::SolidBuildResult> BuildShape(
        const kachakacha::v2::domain::CreateSolidDefinition& definition) const;

    V2MainWindow& window_;
    V2SolidDock* dock_ = nullptr;
    bool active_ = false;
    bool mirroring_ = false;
    kachakacha::v2::app::SolidInputState input_;
    kachakacha::v2::app::SolidPreviewOutcome outcome_;
    std::optional<kachakacha::v2::modeling::KernelShapeHandle> built_;
    //! 3D の選択に映した、欄の中身(差分を読むための前回の写し)。
    std::vector<kachakacha::v2::base::EntityId> mirror_;
};
