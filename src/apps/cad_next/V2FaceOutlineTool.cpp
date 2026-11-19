#include "V2FaceActions.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "V2OffsetTool.h"
#include "V2ShapeLifetime.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/geometry/CurveProjection.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/app/SurfacePreview.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QObject>
#include <QString>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>

using namespace kachakacha::v2;
namespace {
using Curves = std::vector<geometry::CurveSegment>;
using Groups = std::vector<Curves>;

base::Result<Groups> BoundaryGroups(const Curves& curves, const geometry::GeometryTolerance& tolerance)
{
    Groups groups;
    std::vector<bool> used(curves.size());
    // These deterministic IDs exist only inside the chain query, never in a document.
    base::DeterministicIdGenerator ids;
    for (std::size_t seed = 0; seed < curves.size(); ++seed) {
        if (used[seed]) continue;
        std::vector<std::size_t> component{seed}; used[seed] = true;
        for (std::size_t i = 0; i < component.size(); ++i) {
            const auto& a = curves[component[i]];
            for (std::size_t j = 0; j < curves.size(); ++j) if (!used[j]) {
                const auto& b = curves[j];
                const double distance = std::min({geometry::Distance(a.StartPoint(), b.StartPoint()),
                    geometry::Distance(a.StartPoint(), b.EndPoint()), geometry::Distance(a.EndPoint(), b.StartPoint()),
                    geometry::Distance(a.EndPoint(), b.EndPoint())});
                if (distance <= tolerance.modelLinearMm) { used[j] = true; component.push_back(j); }
            }
        }
        std::vector<geometry::ChainInput> inputs;
        for (const auto index : component)
            inputs.push_back({ids.NextTyped<base::IdKind::Entity>(), ids.NextTyped<base::IdKind::Segment>(), curves[index]});
        const auto chain = geometry::AnalyzeChain(inputs, tolerance);
        if (!chain.HasValue()) return base::Result<Groups>::Failure(chain.Diagnostics());
        Curves ordered;
        for (const auto& item : chain.Value().order.segments) {
            const auto at = std::find_if(inputs.begin(), inputs.end(), [&](const auto& input) { return input.segmentId == item.segmentId; });
            if (item.reversed) {
                const auto reversed = geometry::ReverseCurve(at->segment);
                if (!reversed.HasValue()) return base::Result<Groups>::Failure(reversed.Diagnostics());
                ordered.push_back(reversed.Value());
            } else ordered.push_back(at->segment);
        }
        groups.push_back(std::move(ordered));
    }
    return base::Result<Groups>::Success(std::move(groups));
}

class FaceOutlinePanel final : public QWidget {
public:
    using Commit = std::function<bool(const Groups&, bool)>;
    FaceOutlinePanel(V2MainWindow& window, Groups groups, QString name, std::function<bool()> valid, Commit commit)
        : QWidget(&window), window_(window), source_(std::move(groups)), valid_(std::move(valid)), commit_(std::move(commit))
    {
        setObjectName(QStringLiteral("faceOutlinePanel"));
        auto* layout = new QVBoxLayout(this);
        auto* source = new QLabel(QStringLiteral("元の面：%1\n元の面と線を残し、輪郭ごとに編集できる線を写します。").arg(name), this);
        source->setWordWrap(true); layout->addWidget(source);
        project_ = new QCheckBox(QStringLiteral("現在の作図面へ投影する"), this);
        project_->setObjectName(QStringLiteral("faceOutlineProject")); project_->setChecked(false); layout->addWidget(project_);
        status_ = new QLabel(this); status_->setObjectName(QStringLiteral("faceOutlineStatus"));
        status_->setWordWrap(true); layout->addWidget(status_); layout->addStretch();
        confirm_ = new QPushButton(QStringLiteral("輪郭を写す Enter"), this);
        confirm_->setObjectName(QStringLiteral("faceOutlineConfirm")); layout->addWidget(confirm_);
        offset_ = new QPushButton(QStringLiteral("写してオフセット"), this);
        offset_->setObjectName(QStringLiteral("faceOutlineOffset")); layout->addWidget(offset_);
        auto* cancel = new QPushButton(QStringLiteral("取消 Esc"), this); layout->addWidget(cancel);
        connect(project_, &QCheckBox::toggled, this, [this] { Preview(); });
        connect(confirm_, &QPushButton::clicked, this, [this] { Confirm(false); });
        connect(offset_, &QPushButton::clicked, this, [this] { Confirm(true); });
        connect(cancel, &QPushButton::clicked, this, [this] { window_.RunCommand("selection.activate"); });
        if (auto* plane = window.findChild<QComboBox*>(QStringLiteral("drawingPlane")))
            connect(plane, &QComboBox::currentIndexChanged, this, [this] { Preview(); });
        qApp->installEventFilter(this);
    }
    void Preview()
    {
        window_.Viewport().HideToolPreview(); ready_.clear(); confirm_->setEnabled(false); offset_->setEnabled(false);
        if (!valid_()) { status_->setText(QStringLiteral("元の面が変更されました。面を指定し直してください。")); return; }
        const auto& tolerance = window_.Session().GetDocument().Snapshot().settings.tolerance;
        const auto frame = window_.Viewport().WorkPlane();
        double maximum = 0, squares = 0; std::size_t count = 0, edges = 0;
        for (const auto& group : source_) {
            const auto projected = project_->isChecked()
                ? geometry::ProjectCurvesOntoPlane(group, {frame.origin, frame.normal}, tolerance)
                : base::Result<Curves>::Success(group);
            if (!projected.HasValue()) { status_->setText(QString::fromStdString(projected.FirstSummaryJa())); return; }
            ready_.push_back(projected.Value()); edges += group.size();
            if (project_->isChecked()) for (const auto& curve : group)
                for (const auto& point : geometry::SampleCurve(curve, tolerance.modelLinearMm, 256)) {
                    const double distance = std::abs(frame.SignedDistance(point.position));
                    maximum = std::max(maximum, distance); squares += distance * distance; ++count;
                }
        }
        std::vector<std::vector<geometry::Vector3>> lines;
        for (const auto& group : ready_) {
            const auto added = app::SurfaceBoundaryLines(group, 32); lines.insert(lines.end(), added.begin(), added.end());
        }
        window_.Viewport().ShowToolPreview(lines);
        status_->setText(QStringLiteral("輪郭 %1本 / 辺 %2本\n%3\n下見の位置・形を確認して確定してください。")
            .arg(ready_.size()).arg(edges).arg(project_->isChecked()
                ? QStringLiteral("投影距離（標本）：最大 %1 mm / RMS %2 mm").arg(maximum, 0, 'g', 8).arg(count ? std::sqrt(squares / count) : 0, 0, 'g', 8)
                : QStringLiteral("元の3D位置・形を保持します。")));
        confirm_->setEnabled(!ready_.empty()); offset_->setEnabled(!ready_.empty());
    }
private:
    void Confirm(bool offset)
    {
        Preview(); if (!confirm_->isEnabled()) return;
        // A successful commit closes this page. Keep callback and inputs off its lifetime.
        const auto commit = commit_; const auto groups = ready_;
        commit(groups, offset);
    }
    bool eventFilter(QObject* object, QEvent* event) override
    {
        const auto* widget = qobject_cast<QWidget*>(object);
        if (!isVisible() || !widget || widget->window() != window() || event->type() != QEvent::KeyPress) return false;
        if(qobject_cast<const QComboBox*>(widget)||qApp->activePopupWidget())return false;
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape) { window_.RunCommand("selection.activate"); return true; }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter)
            &&(widget==&window_.Viewport()||window_.Viewport().isAncestorOf(widget)||isAncestorOf(widget))) {
            Confirm(false); return true;
        }
        return false;
    }
    void closeEvent(QCloseEvent* event) override { window_.Viewport().HideToolPreview(); QWidget::closeEvent(event); }
    V2MainWindow& window_;
    Groups source_, ready_;
    std::function<bool()> valid_;
    Commit commit_;
    QCheckBox* project_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton *confirm_ = nullptr, *offset_ = nullptr;
};
}

bool V2FaceActions::OpenDrawingOutline(V2MainWindow& window, bool explicitRequest)
{
    const auto& selection = window.viewport_->Selection();
    if (!explicitRequest) for (const auto id : selection.entityIds) {
        const auto* entity = window.session_->GetDocument().FindEntity(id);
        if (entity && entity->kind == domain::EntityKind::Wire) return false;
    }
    auto selected = SelectedDrawingFaces(window, selection);
    const auto active = ActiveDrawingFace(window);
    if (explicitRequest && active) selected = {*active};
    if (selected.empty() && active) selected = {*active};
    if (selected.size() != 1) {
        if (explicitRequest) window.SetStatus(QStringLiteral("先に「面で作図」で使う面を指定してください。"));
        return explicitRequest;
    }
    const auto ref = selected.front(); const auto shape = DrawingShape(window, ref.entityId);
    if (!shape || !ref.pickedFaceIndex) return explicitRequest;
    const auto face = kernel::OutputFace(*shape, *ref.pickedFaceIndex);
    if (!face.HasValue()) { window.ReportDiagnostics(face.Diagnostics()); return true; }
    const v2ui::ShapeListOwner owner({face.Value()});
    const auto edges = kernel::OutputEdges(face.Value(), window.session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm);
    if (!edges.HasValue()) { window.ReportDiagnostics(edges.Diagnostics()); return true; }
    const auto groups = BoundaryGroups(edges.Value(), window.session_->GetDocument().Snapshot().settings.tolerance);
    if (!groups.HasValue()) { window.ReportDiagnostics(groups.Diagnostics()); return true; }
    const auto documentId = window.session_->GetDocument().Snapshot().id;
    const auto valid = [&window, documentId, id = ref.entityId, original = shape->value] {
        const auto current = DrawingShape(window, id);
        return window.session_->GetDocument().Snapshot().id == documentId
            && window.session_->GetDocument().FindEntity(id) && current && current->value == original;
    };
    const auto* entity = window.session_->GetDocument().FindEntity(ref.entityId);
    auto name = entity ? QString::fromStdString(entity->displayName) : QStringLiteral("面");
    if (!edges.Diagnostics().empty()) name += "\n" + QString::fromStdString(edges.FirstSummaryJa());
    window.EndArmedTools(); window.ClearPendingCommand(); window.SelectTool(modeling::DrawingTool::Select);
    auto* panel = new FaceOutlinePanel(window, groups.Value(), name, valid,
        [&window, valid](const Groups& copies, bool offset) {
            if (!valid()) return false;
            app::SelectionSet created;
            document::Document::Transaction transaction(window.session_->GetDocument(), "輪郭を写す");
            for (const auto& group : copies) {
                const auto id = window.AddPlainWire(group, "輪郭の写し", false);
                if (id.IsNil()) return false;
                created.entityIds.push_back(id);
            }
            if (!transaction.Commit()) return false;
            window.operationHost_->SetShelves({}); window.AdoptCurrentDocument(); window.viewport_->SetSelection(created);
            window.SetStatus(QStringLiteral("輪郭を%1本の線として写しました。元の面・線は残しています。").arg(created.entityIds.size()));
            if (offset) V2OffsetTool::Open(window);
            return true;
        });
    window.operationHost_->ShowTemporaryPage(panel, QStringLiteral("面の輪郭を写す")); panel->Preview();
    return true;
}
