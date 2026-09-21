#pragma once

//! 「面の編集」の道具(プロンプト additional_surface_tools)。
//!
//! 道具から始める: 押すと棚(V2SurfaceEditDock)が出る → 3D で面(縁は縁の近くを押す)や
//! 線を押すと欄へ入り、押し直すと外れる → そろえば **実際に核で作って** 下見 →
//! Enter で確定(下見に使った形をそのまま文書へ)/ Esc でやめる(文書は変わらない)。
//! 元の面は残す。確定は 1 回の取り消しで全部戻る(何枚でも 1 つのまとまり)。
//!
//! 画面の窓(V2MainWindow)の中身を使うので、窓の友達にしてある。窓の頭(ヘッダ)を
//! 太らせないため、状態と手順はこちらに持つ。何が欄に入るかは core が決める
//! (app/SurfaceEditInputState)。

#include "V2SurfaceEditDock.h"

#include "kachakacha/app/SurfaceEditInputState.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <QString>

#include <string>
#include <string_view>
#include <vector>

class V2MainWindow;

class V2SurfaceEditTool final {
public:
    explicit V2SurfaceEditTool(V2MainWindow& window);

    [[nodiscard]] V2SurfaceEditDock* Dock() const noexcept { return dock_; }
    [[nodiscard]] bool Active() const noexcept { return active_; }
    //! この道具の命令(surface.match など)か。
    [[nodiscard]] bool Handles(std::string_view commandId) const;
    //! 命令で始める(構えていれば作り方だけ替える)。
    void Begin(std::string_view commandId);
    void End();
    void Confirm();
    //! 3D の選択が変わった(窓の HandleSelectionChanged から)。
    void HandleSelectionChanged();
    //! Enter / Esc。引き受けたら真。
    [[nodiscard]] bool HandleKey(int key);
    //! 「曲面へ投影」(wire.project_surface、選んでから押す道)を核で厳密に落とす。
    //! 面の形があって落とせたら真(文書へ入れ、帯に言う)。偽なら窓が標本の折れ線で落とす。
    [[nodiscard]] bool ProjectSelectionExactly(const kachakacha::v2::base::EntityId& surfaceId);
    //! 開き直したときの作り直し(EditSurface の Feature)。
    [[nodiscard]] bool Rebuild(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);

    [[nodiscard]] const kachakacha::v2::app::SurfaceEditInputState& Input() const noexcept
    {
        return input_;
    }
    [[nodiscard]] const kachakacha::v2::app::SurfaceEditOutcome& Outcome() const noexcept
    {
        return outcome_;
    }
    //! 下見に作った面の形(面の解析が下見を塗るのに使う)。
    [[nodiscard]] std::vector<kachakacha::v2::modeling::KernelShapeHandle> PreviewSurfaces() const;

private:
    struct BuiltSurface {
        kachakacha::v2::modeling::GuideSurfaceResult surface;
        kachakacha::v2::domain::EditSurfaceDefinition definition;
        std::vector<kachakacha::v2::base::EntityId> inputs;
        std::string label;
    };

    void Choose(kachakacha::v2::app::SurfaceEditOperation operation);
    void Refresh();
    void RefreshPreview();
    void BuildSurfaces();
    void BuildWires();
    void RefreshDock();
    void MirrorToSelection();
    void ApplyPick(const kachakacha::v2::base::EntityId& id);
    [[nodiscard]] std::string NameOf(const kachakacha::v2::base::EntityId& id) const;
    [[nodiscard]] bool AddSurfaceFeature(const BuiltSurface& built);
    [[nodiscard]] bool AddWireFeature(const std::vector<kachakacha::v2::geometry::CurveSegment>& wire,
        const std::vector<kachakacha::v2::base::EntityId>& inputs, bool iso);

    V2MainWindow& window_;
    V2SurfaceEditDock* dock_ = nullptr;
    bool active_ = false;
    bool mirroring_ = false;
    kachakacha::v2::app::SurfaceEditInputState input_;
    kachakacha::v2::app::SurfaceEditOutcome outcome_;
    std::vector<BuiltSurface> surfaces_;
    std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> wires_;
};
