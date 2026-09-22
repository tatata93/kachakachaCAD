#pragma once

//! 「辺の丸め・面取り」の道具(立体の辺のフィレット・面取り、matrix P-12)。
//!
//! 道具から始める: 帯の フィレット / 面取り を押すと棚(V2EdgeFinishDock)が出る → 3D で部品の
//! 辺の近くを押すと、その部品と一番近い辺が入る(同じ部品を押すたびに辺を足し引き)→
//! そろえば **実際に核で丸めて** 稜線を下見 → Enter で確定(下見の形をそのまま文書へ。
//! 元の部品は隠す)/ Esc でやめる。1 回の取り消しで全部戻り、開き直したら同じ辺
//! (真ん中の点が同じ辺)を同じ大きさで丸め直す(Rebuild は下見と同じ関数)。
//!
//! 窓の中身を使うので窓の友達にしてある(V2SolidTool と同じ形)。何が入るかは
//! core(app/EdgeFinishInputState)、形は kernel(OcctEdgeFinish)が決める。

#include "V2EdgeFinishDock.h"

#include "kachakacha/app/EdgeFinishInputState.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <optional>
#include <string>
#include <string_view>

class V2MainWindow;

class V2EdgeFinishTool final {
public:
    explicit V2EdgeFinishTool(V2MainWindow& window);

    [[nodiscard]] V2EdgeFinishDock* Dock() const noexcept { return dock_; }
    [[nodiscard]] bool Active() const noexcept { return active_; }
    //! この道具の命令(part.fillet / part.chamfer)か。
    [[nodiscard]] static bool Handles(std::string_view commandId);
    //! 命令で始める。構えていれば、同じ種類なら確定、違えば種類だけ替える。
    void Begin(std::string_view commandId);
    void End();
    void Confirm();
    void HandleSelectionChanged();
    [[nodiscard]] bool HandleKey(int key);
    //! 開き直したときの作り直し(EdgeFinish の Feature)。
    [[nodiscard]] bool Rebuild(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);

    [[nodiscard]] const kachakacha::v2::app::EdgeFinishInputState& Input() const noexcept
    {
        return input_;
    }
    [[nodiscard]] const kachakacha::v2::app::EdgeFinishOutcome& Outcome() const noexcept
    {
        return outcome_;
    }

private:
    void ApplyPick(const kachakacha::v2::base::EntityId& id);
    void MirrorToSelection();
    void Refresh();
    void RefreshPreview();
    void RefreshDock();
    [[nodiscard]] std::string NameOf(const kachakacha::v2::base::EntityId& id) const;
    [[nodiscard]] double JoinMm() const;

    V2MainWindow& window_;
    V2EdgeFinishDock* dock_ = nullptr;
    bool active_ = false;
    bool mirroring_ = false;
    kachakacha::v2::app::EdgeFinishInputState input_;
    kachakacha::v2::app::EdgeFinishOutcome outcome_;
    std::optional<kachakacha::v2::modeling::KernelShapeHandle> built_;
};
