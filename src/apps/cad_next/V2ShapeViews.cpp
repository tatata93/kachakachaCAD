//! 核が作った形を画面へ渡すところ(棚卸し A-1)。
//!
//! V2 は押し出しても面を作っても、3D 画面には何も出なかった。
//! 核の形は `partShapes_` / `guideShapes_` に持っていたが、
//! 書き出し(STL/STEP)にしか使っておらず、画面へ渡す道が無かった。
//!
//! ここがその道である。やることは3つ:
//!   1. 形の番号(handle)を核に渡して三角形と稜線にしてもらう
//!   2. 一度作った網は覚えておく(番号が同じなら作り直さない)
//!   3. 画面へ渡す
//!
//! **網を作り直すのは重い。** 選択が変わるたびに作り直すと、
//! 部品が10個あるだけで画面が固まる。番号で覚えるのはそのためである。

#include "V2MainWindow.h"

#include "kachakacha/domain/Entity.h"
#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/kernel/OcctTessellate.h"

#include <QStringList>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::modeling::ShapeMesh;

} // namespace

void V2MainWindow::RefreshShapeViews()
{
    if (viewport_ == nullptr) {
        return;
    }
    std::vector<V2Viewport::ShapeView> shapes;
    std::map<std::uint64_t, ShapeMesh> keep;
    const auto& snapshot = session_->GetDocument().Snapshot();

    const auto take = [&](const std::map<std::string, KernelShapeHandle>& source,
                          bool surface) {
        for (const auto& entry : source) {
            if (!entry.second.Valid()) {
                continue;
            }
            const auto* entity = FindEntityByIdText(snapshot, entry.first);
            if (entity == nullptr
                || entity->visibility != kachakacha::v2::domain::Visibility::Visible) {
                continue;
            }
            V2Viewport::ShapeView view;
            view.entityId = entity->id;
            view.surface = surface;
            const auto cached = shapeMeshes_.find(entry.second.value);
            if (cached != shapeMeshes_.end()) {
                view.mesh = cached->second;
            } else {
                // まだ網にしていない形。ここで1度だけ作る。
                auto made = kachakacha::v2::kernel::BuildShapeMesh(entry.second);
                if (!made.HasValue()) {
                    // 出せない形は黙って飛ばさない。知らせに出して、残りは出す。
                    ReportDiagnostics(made.Diagnostics());
                    continue;
                }
                view.mesh = made.Value();
            }
            keep.emplace(entry.second.value, view.mesh);
            shapes.push_back(std::move(view));
        }
    };
    take(guideShapes_, true);
    take(partShapes_, false);
    // もう文書にない形の網は捨てる。持ち続けると、開き直すたびに増える。
    shapeMeshes_ = std::move(keep);
    viewport_->SetShapeViews(std::move(shapes));
}

const kachakacha::v2::domain::Entity* V2MainWindow::FindEntityByIdText(
    const kachakacha::v2::document::DocumentSnapshot& snapshot, const std::string& idText)
{
    for (const auto& entity : snapshot.entities) {
        if (entity.id.ToString() == idText) {
            return &entity;
        }
    }
    return nullptr;
}

//! 部品の棚を、いまの数と選択に合わせて書き直す。
//!
//! 値は数の棚と同じものを映す。別に持つと、数の棚で変えたのに
//! 部品の棚が古いまま、ということが起きる。
void V2MainWindow::RefreshPartDock()
{
    using kachakacha::v2::app::ParameterId;
    if (partDock_ == nullptr || parameterDock_ == nullptr) {
        return;
    }
    const auto& values = parameterDock_->Values();
    for (const ParameterId id : {ParameterId::ExtrudeDistance, ParameterId::OffsetDistanceMm,
             ParameterId::RevolveAngleDeg, ParameterId::JigClearanceMm,
             ParameterId::JigThicknessMm}) {
        partDock_->SetParameterMm(id, kachakacha::v2::app::ParameterValueOf(values, id));
    }
    partDock_->SetPlacement(thicknessPlacement_);
    // いま何を選んでいるかを一言で出す。押す前に「足りない」と分かるようにする。
    const auto facts = BuildFactsForCommands();
    if (facts.wires + facts.parts + facts.guideSurfaces + facts.workPlanes == 0) {
        partDock_->SetSelectionText(QStringLiteral("選んでいるものはありません。"));
        return;
    }
    QStringList parts;
    if (facts.closedProfiles > 0) {
        parts << QStringLiteral("閉じた輪郭 %1").arg(facts.closedProfiles);
    }
    if (facts.wires > 0) {
        parts << QStringLiteral("線 %1").arg(facts.wires);
    }
    if (facts.guideSurfaces > 0) {
        parts << QStringLiteral("面 %1").arg(facts.guideSurfaces);
    }
    if (facts.parts > 0) {
        parts << QStringLiteral("部品 %1").arg(facts.parts);
    }
    if (facts.workPlanes > 0) {
        parts << QStringLiteral("作図面 %1").arg(facts.workPlanes);
    }
    partDock_->SetSelectionText(QStringLiteral("選択: %1").arg(parts.join(QStringLiteral(" / "))));
}
