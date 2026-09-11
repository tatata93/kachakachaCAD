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
#include "kachakacha/kernel/OcctTessellate.h"

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
