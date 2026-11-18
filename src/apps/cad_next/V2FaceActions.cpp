#include "V2FaceActions.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Viewport.h"
#include "V2ToolSelection.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/document/Commands.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>
using namespace kachakacha::v2;

std::optional<modeling::KernelShapeHandle> V2FaceActions::DrawingShape(
    const V2MainWindow& window, const base::EntityId& id)
{
    const auto part = window.partShapes_.find(id.ToString());
    if (part != window.partShapes_.end()) return part->second;
    const auto guide = window.guideShapes_.find(id.ToString());
    if (guide != window.guideShapes_.end()) return guide->second;
    return std::nullopt;
}

std::vector<app::SelectionRef> V2FaceActions::SelectedDrawingFaces(
    const V2MainWindow& window, const app::SelectionSet& selection)
{
    std::vector<app::SelectionRef> faces;
    for (const auto& ref : selection.ordered) {
        if (ref.kind != app::SelectionElementKind::Face || !ref.pickedFaceIndex
            || !DrawingShape(window, ref.entityId)) continue;
        if (std::none_of(faces.begin(), faces.end(), [&](const auto& old) {
                return old.entityId == ref.entityId && old.pickedFaceIndex == ref.pickedFaceIndex;
            })) faces.push_back(ref);
    }
    for (const auto& id : selection.entityIds) {
        const auto* entity = window.session_->GetDocument().FindEntity(id);
        if (!entity || entity->kind != domain::EntityKind::GuideSurface
            || std::any_of(faces.begin(), faces.end(), [&](const auto& ref) { return ref.entityId == id; })) continue;
        // Only whole, single-face guides identify a unique face in the explorer.
        if (std::any_of(selection.ordered.begin(), selection.ordered.end(), [&](const auto& ref) {
                return ref.entityId == id && ref.kind != app::SelectionElementKind::Object;
            })) continue;
        const auto shape = DrawingShape(window, id);
        if (!shape) continue;
        const auto count = kernel::ShapeFaceCount(*shape);
        if (!count.HasValue() || count.Value() != 1) continue;
        app::SelectionRef ref;
        ref.entityId = id;
        ref.kind = app::SelectionElementKind::Face;
        ref.pickedFaceIndex = 0;
        for (const auto& view : window.viewport_->ShapeViews()) {
            if (view.entityId == id && !view.mesh.triangles.empty()) {
                ref.hitPoint = view.mesh.triangles.front().Center();
                break;
            }
        }
        const auto pose = kernel::FacePoseNear(*shape, 0, ref.hitPoint,
            window.session_->GetDocument().Snapshot().settings.tolerance);
        // A whole curved surface does not specify a contact point. Ask for a 3D click.
        if (!pose.HasValue() || !pose.Value().planar) continue;
        faces.push_back(ref);
    }
    return faces;
}

bool V2FaceActions::DrawingCandidate(const V2MainWindow& window, const app::PickCandidate& candidate)
{
    if (candidate.kind == app::SelectionElementKind::Object) return DrawingRefs(window, candidate).size() == 1;
    return candidate.kind == app::SelectionElementKind::Face && candidate.pickedFaceIndex
        && DrawingShape(window, candidate.entityId).has_value();
}

std::vector<app::SelectionRef> V2FaceActions::DrawingRefs(
    const V2MainWindow& window, const app::PickCandidate& candidate)
{
    app::SelectionSet selected;
    selected.entityIds = {candidate.entityId};
    if (candidate.kind != app::SelectionElementKind::Object) {
        app::SelectionRef ref;
        ref.entityId = candidate.entityId; ref.kind = candidate.kind;
        ref.pickedFaceIndex = candidate.pickedFaceIndex; ref.hitPoint = candidate.hitPoint;
        selected.ordered = {ref};
    }
    return SelectedDrawingFaces(window, selected);
}

void V2FaceActions::ResolvePlane(V2MainWindow& window, const app::SelectionRef& ref, const PlaneHandler& handler)
{
    const auto shape = DrawingShape(window, ref.entityId);
    if (!shape || !ref.pickedFaceIndex) { handler(std::nullopt, false); return; }
    const auto pose = kernel::FacePoseNear(*shape, *ref.pickedFaceIndex, ref.hitPoint,
        window.session_->GetDocument().Snapshot().settings.tolerance);
    if (!pose.HasValue()) { window.SetStatus(QString::fromStdString(pose.FirstSummaryJa())); handler(std::nullopt, false); return; }
    app::WorkPlaneChoice choice; choice.method = modeling::WorkPlaneMethod::PointNormal;
    choice.origin = pose.Value().point; choice.normal = pose.Value().normal; choice.uAxis = pose.Value().uAxis;
    handler(choice, pose.Value().planar);
}

void V2FaceActions::PickWorkPlane(V2MainWindow& window, PlaneHandler handler)
{
    const auto receive = [&window, handler](std::optional<app::WorkPlaneChoice> choice, bool planar) {
        handler(choice, planar);
        if (choice) window.SetStatus(planar
            ? QStringLiteral("選んだ面の位置・向きを下見しています。「平面を作る」で確定します。")
            : QStringLiteral("選択位置の接平面を下見しています。「平面を作る」で確定します。"));
    };
    const auto selected = SelectedDrawingFaces(window, window.viewport_->Selection());
    window.viewport_->CancelPointPick();
    window.viewport_->CancelReferencePick();
    if (selected.size() == 1) { ResolvePlane(window, selected.front(), receive); return; }
    window.viewport_->BeginElementPick([&window](const app::PickCandidate& candidate) {
        return DrawingCandidate(window, candidate);
    }, [&window, receive](std::optional<app::PickCandidate> candidate) {
        if (!candidate) { receive(std::nullopt, false); return; }
        const auto refs = DrawingRefs(window, *candidate);
        if (refs.size() == 1) ResolvePlane(window, refs.front(), receive);
        else receive(std::nullopt, false);
    });
    const auto prompt = QStringLiteral("基準にする面をクリックしてください。重なる面はTabで切り替え、Escで取消。曲面は選択位置の接平面になります。");
    window.viewport_->setProperty("referencePickPrompt", prompt);
    window.SetStatus(prompt);
}

bool V2FaceActions::Run(V2MainWindow& window,std::string_view command)
{
    if(command!="fabrication.create" && command!="workplane.from_face" && command!="fabrication.from_face")return false;
    const auto selection=window.viewport_->Selection();
    std::vector<app::SelectionRef> faces = command == "workplane.from_face"
        ? SelectedDrawingFaces(window, selection) : std::vector<app::SelectionRef>{};
    if (command != "workplane.from_face") for(const auto& ref:selection.ordered) {
        if(ref.kind!=app::SelectionElementKind::Face || !ref.pickedFaceIndex
            || !window.partShapes_.contains(ref.entityId.ToString()))continue;
        if(std::none_of(faces.begin(),faces.end(),[&](const auto& old){
            return old.entityId==ref.entityId && old.pickedFaceIndex==ref.pickedFaceIndex;}))faces.push_back(ref);
    }
    if(command=="fabrication.create") {
        if(faces.empty())return false;
        command="fabrication.from_face";
    }
    if(command!="workplane.from_face" && command!="fabrication.from_face")return false;
    window.EndArmedTools();window.ClearPendingCommand();
    window.SelectTool(modeling::DrawingTool::Select);
    auto* panel=new V2FaceActions(window,std::string(command));panel->sources_=selection;
    window.operationHost_->ShowTemporaryPage(panel,command=="workplane.from_face"
        ? QStringLiteral("面で作図") : QStringLiteral("立体の面を近似"));
    if(!faces.empty())panel->Apply(faces);
    else panel->Pick();
    return true;
}
V2FaceActions::V2FaceActions(V2MainWindow& window,std::string command)
    :QWidget(&window),window_(window),command_(std::move(command))
{
    setObjectName(QStringLiteral("faceActionPanel"));auto* layout=new QVBoxLayout(this);
    status_=new QLabel(command_=="workplane.from_face"
        ? QStringLiteral("作図する面をクリックしてください。面だけを強調します。重なる面はTabで切り替え、Escで取消。")
        : QStringLiteral("近似する立体の面を選んでください。選んだ面を独立して取り出し、近似を始めます。"),this);
    status_->setObjectName(QStringLiteral("faceActionStatus"));status_->setWordWrap(true);layout->addWidget(status_);layout->addStretch();
    auto* cancel=new QPushButton(QStringLiteral("取消 Esc"),this);cancel->setObjectName(QStringLiteral("faceActionCancel"));layout->addWidget(cancel);
    QObject::connect(cancel,&QPushButton::clicked,this,[this]{window_.RunCommand("selection.activate");});
    qApp->installEventFilter(this);
}
void V2FaceActions::Pick()
{
    picking_=true;const QPointer<V2FaceActions> self(this);
    window_.viewport_->BeginElementPick([self](const app::PickCandidate& candidate){
        if (!self || !self->isVisible()) return false;
        if (self->command_ == "workplane.from_face") return DrawingCandidate(self->window_, candidate);
        return candidate.kind==app::SelectionElementKind::Face && candidate.pickedFaceIndex
            && self->window_.partShapes_.contains(candidate.entityId.ToString());
    },[self](std::optional<app::PickCandidate> candidate){
        if(!self)return;self->picking_=false;
        if(!candidate)return; // Tool switching owns the cancellation and its next page.
        if (self->command_ == "workplane.from_face") {
            self->Apply(DrawingRefs(self->window_, *candidate));
            return;
        }
        app::SelectionRef ref;ref.entityId=candidate->entityId;ref.kind=candidate->kind;
        ref.pickedFaceIndex=candidate->pickedFaceIndex;ref.hitPoint=candidate->hitPoint;
        self->Apply({ref});
    });
    if (command_ == "workplane.from_face") {
        window_.viewport_->setProperty("referencePickPrompt", status_->text());
        window_.SetStatus(status_->text());
    }
}
void V2FaceActions::closeEvent(QCloseEvent* event)
{
    if(picking_)window_.viewport_->CancelReferencePick();
    QWidget::closeEvent(event);
}
bool V2FaceActions::eventFilter(QObject* object,QEvent* event)
{
    if(!isVisible() || V2ToolSelection::Active(*window_.viewport_))return false;
    if(event->type()!=QEvent::KeyPress || static_cast<QKeyEvent*>(event)->key()!=Qt::Key_Escape)return false;
    const auto* widget=qobject_cast<QWidget*>(object);if(!widget || widget->window()!=window())return false;
    window_.RunCommand("selection.activate");return true;
}
void V2FaceActions::Apply(const std::vector<app::SelectionRef>& refs)
{
    if(refs.empty())return;
    if(command_!="workplane.from_face") {
        auto& window=window_;
        if(!Extract(refs)){Pick();return;}
        window.RunCommand("fabrication.create");return;
    }
    if(refs.size()!=1){status_->setText(QStringLiteral("作図する面を1つクリックしてください。"));Pick();return;}
    DrawOnFace(refs.front());
}

void V2FaceActions::FrameDrawingFace(V2MainWindow& window, const app::SelectionRef& ref, const app::WorkPlaneChoice& choice)
{
    auto& viewport = *window.viewport_;
    double halfU = 0, halfV = 0;
    const auto v = geometry::Cross(choice.normal, choice.uAxis);
    for (const auto& shape : viewport.ShapeViews()) if (shape.entityId == ref.entityId)
        for (const auto& triangle : shape.mesh.triangles) if (triangle.faceIndex == ref.pickedFaceIndex)
            for (const auto& point : triangle.points) {
                halfU = std::max(halfU, std::abs(geometry::Dot(point - choice.origin, choice.uAxis)));
                halfV = std::max(halfV, std::abs(geometry::Dot(point - choice.origin, v)));
            }
    viewport.SetViewCenter(choice.origin);
    if (halfU + halfV > 0) {
        const double aspect = viewport.width() / double(std::max(1, viewport.height()));
        const double width = 1.15 * std::max({halfU * 2, halfV * 2 * aspect,
            window.session_->Scene().grid.majorSpacingMm * 6});
        // Preserve a user's close-up. Only enlarge a face that was small in the whole-model view.
        viewport.SetVisibleWidthMm(std::min(viewport.VisibleWidthMm(), width));
    }
}

void V2FaceActions::DrawOnFace(const app::SelectionRef& ref)
{
    ResolvePlane(window_, ref, [this, ref](std::optional<app::WorkPlaneChoice> choice, bool planar) {
        if (!choice || !RememberDrawingSupport(window_, ref, *choice, planar)) { Pick(); return; }
        auto& window = window_;
        window.operationHost_->SetShelves({});
        window.SetMode(app::UiMode::Drawing);window.AlignViewToActiveWorkPlane();window.RunCommand("draw.line");
        FrameDrawingFace(window, ref, *choice);
        if (auto* scope = window.findChild<QComboBox*>("drawingScope")) scope->setCurrentIndex(0);
        auto grid = window.CurrentGridChoice();
        grid.grid.visible = true;
        window.ApplyGridChoice(grid);
        if (!window.snapEnabled_) window.ToggleSnap();
        window.SetStatus(planar
            ? QStringLiteral("選んだ面で作図できます。境界線との交点・端点やグリッドを指定できます。")
            : QStringLiteral("選択位置の接平面で作図します。グリッドの交点や任意の位置をクリックしてください。"));
    });
}
