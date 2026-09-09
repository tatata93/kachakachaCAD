//! 書き出しのコマンド(WP-11)。棚を作り、中身を作り、ファイルへ置く。
//!
//! ここを V2MainWindow.cpp から分けたのは、
//! 「窓を組み立てる仕事」と「出すものを作る仕事」を混ぜないためである。
//!
//! 大事な約束は2つ。
//!   1. 出せない形は出さない。断るときに0バイトのファイルを残さない。
//!   2. 選んだものだけを出す。見えているだけのものを勝手に足さない。

#include "V2MainWindow.h"

#include "kachakacha/app/ExportContent.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/exporters/PdfWriter.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/domain/Entity.h"

#include <QString>

#include <string>
#include <string_view>
#include <vector>

void V2MainWindow::RunExportCommand(std::string_view id)
{
    using kachakacha::v2::app::ExportFormat;
    if (exportDock_ == nullptr) {
        return;
    }
    exportDock_->show();
    if (id == "export.validate") {
        // 出す前の検査。通っていれば、そのまま出せると言う。
        const QString reason = exportDock_->ReasonText();
        SetStatus(reason.isEmpty()
                ? QStringLiteral("%1 出せます。").arg(exportDock_->SummaryText())
                : QStringLiteral("%1 出せません。%2")
                      .arg(exportDock_->SummaryText(), reason));
        if (!reason.isEmpty()) {
            AddDiagnostic(reason);
        }
        return;
    }
    struct FormatBinding {
        std::string_view id;
        ExportFormat format;
    };
    const FormatBinding kBindings[] = {
        {"export.stl", ExportFormat::Stl},
        {"export.step", ExportFormat::Step},
        {"export.svg", ExportFormat::Svg},
        {"export.dxf", ExportFormat::Dxf},
    };
    for (const FormatBinding& binding : kBindings) {
        if (binding.id != id) {
            continue;
        }
        if (!exportDock_->ChooseFormat(binding.format)) {
            SetStatus(exportDock_->LastMessage());
            return;
        }
        SetStatus(QStringLiteral("%1 出す先を決めてください。")
                .arg(exportDock_->SummaryText()));
        return;
    }
}

void V2MainWindow::BuildExportDock()
{
    exportDock_ = new V2ExportDock(this);
    addDockWidget(Qt::RightDockWidgetArea, exportDock_);
    exportDock_->SetDiagnosticSink([this](const QString& text) { AddDiagnostic(text); });
    exportDock_->SetContentMaker(
        [this](const kachakacha::v2::app::ExportRequest& request) {
            return MakeExportContent(request);
        });
    RefreshExportCounts();
}

kachakacha::v2::base::Result<std::string> V2MainWindow::MakeExportContent(
    const kachakacha::v2::app::ExportRequest& request)
{
    using kachakacha::v2::app::ExportFormat;
    using kachakacha::v2::app::ExportTarget;
    using Out = kachakacha::v2::base::Result<std::string>;
    const auto& snapshot = session_->GetDocument().Snapshot();
    if (request.target == ExportTarget::Project) {
        kachakacha::v2::io::DocumentFile file;
        file.snapshot = snapshot;
        file.metadata.title = windowTitle().toStdString();
        return kachakacha::v2::app::MakeProjectContent(file);
    }
    if (request.target == ExportTarget::CurrentPattern) {
        if (patternPages_.empty()) {
            return Out::Failure(kachakacha::v2::base::MakeError("EXP-D001",
                "ページがありません。",
                "先に「製作」→「型紙を作る」で型紙を作ってください。"));
        }
        if (request.format == ExportFormat::Pdf) {
            kachakacha::v2::exporters::PdfMetadata metadata;
            metadata.title = "型紙";
            return kachakacha::v2::exporters::WritePatternPdf(patternPages_, metadata);
        }
        if (request.format == ExportFormat::Svg) {
            return kachakacha::v2::exporters::WritePatternSvg(patternPages_.front(), "型紙");
        }
        return kachakacha::v2::exporters::WritePatternDxf(patternPages_.front());
    }
    if (request.target == ExportTarget::SelectedWires) {
        kachakacha::v2::app::WirePatternRequest wires;
        wires.title = "ワイヤー";
        // 選んだものだけを出す。画面に出ているものを勝手に足さない。
        wires.segments = kachakacha::v2::app::SelectedCurves(viewport_->Selection(),
            session_->Scene());
        return kachakacha::v2::app::MakeWireContent(wires, request.format,
            snapshot.settings.tolerance.interactiveJoinMm);
    }
    if (request.target == ExportTarget::SelectedFabricationPanels) {
        // 部材の絵は型紙と同じものである。別に作ると食い違う。
        if (request.format == ExportFormat::Svg || request.format == ExportFormat::Dxf
            || request.format == ExportFormat::Pdf) {
            if (patternPages_.empty()) {
                return Out::Failure(kachakacha::v2::base::MakeError("EXP-D001",
                    "ページがありません。",
                    "先に「製作」→「型紙を作る」で型紙を作ってください。"));
            }
            if (request.format == ExportFormat::Pdf) {
                kachakacha::v2::exporters::PdfMetadata metadata;
                metadata.title = "部材";
                return kachakacha::v2::exporters::WritePatternPdf(patternPages_, metadata);
            }
            if (request.format == ExportFormat::Svg) {
                return kachakacha::v2::exporters::WritePatternSvg(patternPages_.front(),
                    "部材");
            }
            return kachakacha::v2::exporters::WritePatternDxf(patternPages_.front());
        }
        // 立体で出すときは、その部材のもとになった部品の形を出す。
        return MakeSolidContent(PanelSourceShapes(), request.format);
    }
    if (request.target == ExportTarget::SelectedParts
        || request.target == ExportTarget::VisibleParts) {
        return MakeSolidContent(
            PartShapesFor(request.target == ExportTarget::SelectedParts), request.format);
    }
    return Out::Failure(kachakacha::v2::base::MakeError("EXP-013",
        "書き出せませんでした。",
        std::string(kachakacha::v2::app::ExportTargetNameJa(request.target))
            + " はまだ出せません。"));
}

std::vector<kachakacha::v2::modeling::KernelShapeHandle> V2MainWindow::PartShapesFor(
    bool selectedOnly) const
{
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> shapes;
    const auto& snapshot = session_->GetDocument().Snapshot();
    for (const auto& entity : snapshot.entities) {
        if (entity.kind != kachakacha::v2::domain::EntityKind::Part) {
            continue;
        }
        const std::string key = entity.id.ToString();
        if (selectedOnly) {
            // 選んだものだけ。見えているかどうかでは決めない。
            if (viewport_ == nullptr
                || !kachakacha::v2::app::IsSelected(viewport_->Selection(), entity.id)) {
                continue;
            }
        } else if (entity.visibility != kachakacha::v2::domain::Visibility::Visible) {
            continue;
        }
        const auto found = partShapes_.find(key);
        if (found != partShapes_.end()) {
            shapes.push_back(found->second);
        }
    }
    return shapes;
}

std::vector<kachakacha::v2::modeling::KernelShapeHandle>
V2MainWindow::PanelSourceShapes() const
{
    // 部材は部品から作った。もとの部品の形を出す。
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> shapes;
    const auto& snapshot = session_->GetDocument().Snapshot();
    for (const auto& panel : fabricationPanels_) {
        for (const auto& entity : snapshot.entities) {
            if (entity.kind != kachakacha::v2::domain::EntityKind::Part
                || entity.displayName != panel.panelId) {
                continue;
            }
            const auto found = partShapes_.find(entity.id.ToString());
            if (found != partShapes_.end()) {
                shapes.push_back(found->second);
            }
        }
    }
    return shapes;
}

kachakacha::v2::base::Result<std::string> V2MainWindow::MakeSolidContent(
    const std::vector<kachakacha::v2::modeling::KernelShapeHandle>& shapes,
    kachakacha::v2::app::ExportFormat format)
{
    using Out = kachakacha::v2::base::Result<std::string>;
    if (shapes.empty()) {
        // 形を持っていないものを、出せたことにしない。
        return Out::Failure(kachakacha::v2::base::MakeError("EXP-013",
            "書き出せませんでした。",
            "選んだ部品の立体がまだありません。先に「形」→「押し出し」などで作ってください。"));
    }
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    if (format == kachakacha::v2::app::ExportFormat::Step) {
        const auto built = kachakacha::v2::kernel::BuildStepForSelection(shapes, tolerance);
        if (!built.HasValue()) {
            return Out::Failure(built.Diagnostics());
        }
        return Out::Success(built.Value().content);
    }
    const auto built = kachakacha::v2::kernel::BuildBinaryStlForSelection(shapes, tolerance);
    if (!built.HasValue()) {
        return Out::Failure(built.Diagnostics());
    }
    return Out::Success(built.Value().content);
}

void V2MainWindow::RefreshExportCounts()
{
    if (exportDock_ == nullptr) {
        return;
    }
    const auto& snapshot = session_->GetDocument().Snapshot();
    int visibleParts = 0;
    for (const auto& entity : snapshot.entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++visibleParts;
        }
    }
    // 選んでいる数は画面が数え直さない。選択の側から取る。
    kachakacha::v2::app::ProcessContext context = processContext_;
    if (viewport_ != nullptr) {
        const auto& selection = viewport_->Selection();
        context.selectedWireCount = kachakacha::v2::app::SelectedCountOfKind(selection,
            snapshot, kachakacha::v2::domain::EntityKind::Wire);
        context.selectedPartCount = kachakacha::v2::app::SelectedCountOfKind(selection,
            snapshot, kachakacha::v2::domain::EntityKind::Part);
    }
    exportDock_->SetCounts(kachakacha::v2::app::ExportCountsFrom(context, visibleParts,
        true));
}
