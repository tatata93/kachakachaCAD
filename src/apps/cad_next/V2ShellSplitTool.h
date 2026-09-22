#pragma once

//! 「シェル・分割」の道具(部品の形状編集、matrix P-13)。
//!
//! 道具から始める: 帯の シェル / 分割 を押すと棚(V2ShellSplitDock)が出る。
//!   シェル: 3D で部品の開けたい面を押す(何枚でも。入っている面を押すと外れる)→ 肉厚を決める
//!           → **実際に核でシェルにして** 稜線を下見 → Enter で確定(元の部品は隠す)。
//!   分割 : 3D で部品を押す → いまの作業平面(棚の「ずらす」で法線の向きへ動かせる)で
//!           **実際に核で分けて** 両側の稜線を下見 → Enter で両側を 2 つの部品として確定。
//! Esc でやめる。1 回の取り消しで全部戻り、開き直したら同じ入力で作り直す
//! (Rebuild は下見と同じ関数)。
//!
//! 窓の中身を使うので窓の友達にしてある(V2EdgeFinishTool と同じ形)。何が入るかは
//! core(app/ShellSplitInputState)、形は kernel(OcctShellSplit)が決める。

#include "V2ShellSplitDock.h"

#include "kachakacha/app/ShellSplitInputState.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <optional>
#include <string>
#include <string_view>

class V2MainWindow;

class V2ShellSplitTool final {
public:
    explicit V2ShellSplitTool(V2MainWindow& window);

    [[nodiscard]] V2ShellSplitDock* Dock() const noexcept { return dock_; }
    [[nodiscard]] bool Active() const noexcept { return active_; }
    //! この道具の命令(part.shell / part.split)か。
    [[nodiscard]] static bool Handles(std::string_view commandId);
    //! 命令で始める。構えていれば、同じ作り方なら確定、違えば作り方だけ替える。
    void Begin(std::string_view commandId);
    void End();
    void Confirm();
    void HandleSelectionChanged();
    [[nodiscard]] bool HandleKey(int key);
    //! 開き直したときの作り直し(ShellSplit の Feature)。
    [[nodiscard]] bool Rebuild(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);

    [[nodiscard]] const kachakacha::v2::app::ShellSplitInputState& Input() const noexcept
    {
        return input_;
    }
    [[nodiscard]] const kachakacha::v2::app::ShellSplitOutcome& Outcome() const noexcept
    {
        return outcome_;
    }

private:
    struct SplitPlane {
        kachakacha::v2::geometry::Vector3 origin{};
        kachakacha::v2::geometry::Vector3 normal{0.0, 0.0, 1.0};
    };

    void ApplyPick(const kachakacha::v2::base::EntityId& id);
    void MirrorToSelection();
    void Refresh();
    void RefreshPreview();
    void RefreshShellPreview(const kachakacha::v2::modeling::KernelShapeHandle& shape);
    void RefreshSplitPreview(const kachakacha::v2::modeling::KernelShapeHandle& shape);
    void RefreshDock();
    void ConfirmShell();
    void ConfirmSplit();
    [[nodiscard]] SplitPlane CurrentSplitPlane() const;
    [[nodiscard]] std::string PlaneTextJa() const;
    [[nodiscard]] std::string NameOf(const kachakacha::v2::base::EntityId& id) const;

    V2MainWindow& window_;
    V2ShellSplitDock* dock_ = nullptr;
    bool active_ = false;
    bool mirroring_ = false;
    kachakacha::v2::app::ShellSplitInputState input_;
    kachakacha::v2::app::ShellSplitOutcome outcome_;
    //! シェルの形 / 分割の法線の側。
    std::optional<kachakacha::v2::modeling::KernelShapeHandle> built_;
    //! 分割の反対の側。
    std::optional<kachakacha::v2::modeling::KernelShapeHandle> builtOther_;
    //! 下見を作ったときの平面(確定はこの平面を文書へ残す)。
    SplitPlane builtPlane_;
};
