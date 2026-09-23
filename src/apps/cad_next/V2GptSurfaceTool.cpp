#include "V2GptSurfaceTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/app/GptSurface.h"
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctGptSurface.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"

#include <QComboBox>
#include <QDockWidget>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

using namespace kachakacha::v2;

V2GptSurfaceTool::V2GptSurfaceTool(V2MainWindow& window) : window_(window)
{
    dock_ = new QDockWidget(QStringLiteral("面生成 GPT版"), &window);
    dock_->setObjectName(QStringLiteral("gptSurfaceDock"));
    auto* panel = new QWidget(dock_);
    dock_->setWidget(panel);
    auto* layout = new QVBoxLayout(panel);
    BuildControls(layout);
    BuildActions(layout);
}

V2GptSurfaceTool::~V2GptSurfaceTool()
{
    if (preview_.has_value()) { kernel::ReleaseShape(preview_->handle); }
}

void V2GptSurfaceTool::BuildControls(QVBoxLayout* layout)
{
    auto* hint = new QLabel(QStringLiteral("線をクリックして追加 → プレビュー → 確定"), dock_->widget());
    hint->setWordWrap(true);
    layout->addWidget(hint);
    method_ = new QComboBox(dock_->widget());
    method_->setObjectName(QStringLiteral("gptSurfaceMethod"));
    method_->addItems({QStringLiteral("外周から張る（近似）"), QStringLiteral("断面をつなぐ")});
    layout->addWidget(method_);
    role_ = new QComboBox(dock_->widget());
    role_->setObjectName(QStringLiteral("gptSurfaceRole"));
    role_->addItems({QStringLiteral("外周へ追加"), QStringLiteral("内側の通る線へ追加"),
        QStringLiteral("新しい断面として追加"), QStringLiteral("一覧の選択行へ追加")});
    layout->addWidget(role_);
    list_ = new QTreeWidget(dock_->widget());
    list_->setObjectName(QStringLiteral("gptSurfaceInputs"));
    list_->setHeaderLabels({QStringLiteral("役割・順番"), QStringLiteral("ワイヤー")});
    list_->setMinimumHeight(130);
    layout->addWidget(list_);
    tolerance_ = new QDoubleSpinBox(dock_->widget());
    tolerance_->setObjectName(QStringLiteral("gptSurfaceTolerance"));
    tolerance_->setDecimals(6);
    tolerance_->setRange(0.000001, 1.0);
    tolerance_->setValue(0.01);
    tolerance_->setPrefix(QStringLiteral("許容偏差 "));
    tolerance_->setSuffix(QStringLiteral(" mm"));
    layout->addWidget(tolerance_);
    status_ = new QLabel(dock_->widget());
    status_->setObjectName(QStringLiteral("gptSurfaceStatus"));
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    QObject::connect(method_, &QComboBox::currentIndexChanged, dock_, [this](int index) { ChangeMode(index); });
    QObject::connect(tolerance_, &QDoubleSpinBox::valueChanged, dock_, [this](double value) {
        definition_.gptToleranceMm = value;
        Invalidate();
    });
}

void V2GptSurfaceTool::BuildActions(QVBoxLayout* layout)
{
    const auto button = [&](const char* name, const QString& label, auto handler) {
        auto* made = new QPushButton(label, dock_->widget());
        made->setObjectName(QString::fromLatin1(name));
        QObject::connect(made, &QPushButton::clicked, dock_, handler);
        layout->addWidget(made);
        return made;
    };
    button("gptSurfaceAddSelection", QStringLiteral("選択した線をまとめて追加"), [this] {
        Add(window_.viewport_->Selection().entityIds, true);
    });
    button("gptSurfaceRemove", QStringLiteral("選択行を外す"), [this] { EditRow(0); });
    button("gptSurfaceUp", QStringLiteral("選択行を上へ"), [this] { EditRow(-1); });
    button("gptSurfaceDown", QStringLiteral("選択行を下へ"), [this] { EditRow(1); });
    button("gptSurfaceReverse", QStringLiteral("選択行の向きを反転"), [this] { EditRow(2); });
    button("gptSurfaceApplyRole", QStringLiteral("選択行を上の役割に変更"), [this] { EditRow(3); });
    button("gptSurfaceReset", QStringLiteral("入力を空にする"), [this] {
        definition_.chains.clear(); definition_.roles.clear(); Invalidate(); RefreshList();
    });
    button("gptSurfacePreview", QStringLiteral("プレビュー"), [this] { Preview(); });
    confirm_ = button("gptSurfaceConfirm", QStringLiteral("この面を確定"), [this] { Confirm(); });
    button("gptSurfaceCancel", QStringLiteral("取消"), [this] { End(); });
    confirm_->setEnabled(false);
}

void V2GptSurfaceTool::Begin()
{
    const auto selected = window_.viewport_->Selection().entityIds;
    if (active_) { return; }
    definition_ = {};
    definition_.gptBuilder = true;
    definition_.method = app::kGptBoundaryMethod;
    definition_.gptToleranceMm = tolerance_->value();
    active_ = true;
    method_->setCurrentIndex(0);
    role_->setCurrentIndex(0);
    window_.viewport_->SetToolPickActive(true);
    window_.viewport_->SetToolPickToggle(true);
    Add(selected, false);
    window_.RefreshRightShelves();
    window_.ShowToolFooter(QStringLiteral("面生成 GPT版 / 線を追加してプレビュー / Escで取消"));
}

void V2GptSurfaceTool::End()
{
    if (!active_) { return; }
    active_ = false;
    Invalidate();
    window_.viewport_->SetToolPickActive(false);
    window_.viewport_->SetToolPickToggle(false);
    window_.ShowToolFooter(QString());
    window_.RefreshRightShelves();
}

void V2GptSurfaceTool::ChangeMode(int mode)
{
    definition_.method = mode == 0 ? app::kGptBoundaryMethod : app::kGptSectionsMethod;
    for (auto& role : definition_.roles) {
        if (role == app::kGptBoundaryRole || role == app::kGptSectionRole) {
            role = mode == 0 ? app::kGptBoundaryRole : app::kGptSectionRole;
        }
    }
    role_->setCurrentIndex(mode == 0 ? 0 : 2);
    Invalidate();
    RefreshList();
}

void V2GptSurfaceTool::Add(const std::vector<base::EntityId>& ids, bool grouped)
{
    if (!active_) { return; }
    auto next = definition_;
    const int role = role_->currentIndex() == 0 ? app::kGptBoundaryRole
        : role_->currentIndex() == 1 ? app::kGptInteriorRole : app::kGptSectionRole;
    int row = role_->currentIndex() == 3 ? list_->indexOfTopLevelItem(list_->currentItem()) : -1;
    if (role_->currentIndex() == 3 && row < 0) {
        status_->setText(QStringLiteral("追加先の行を一覧で選んでください。")); return;
    }
    domain::WireChainRef added;
    for (const auto& id : ids) {
        const auto* entity = window_.session_->GetDocument().FindEntity(id);
        if (entity == nullptr || entity->kind != domain::EntityKind::Wire) {
            status_->setText(QStringLiteral("ワイヤー以外が含まれています。選択を直してください。何も追加していません。")); return;
        }
        for (const auto& chain : next.chains) {
            for (const auto& ref : chain.segments) {
                if (ref.entityId == id) {
                    status_->setText(QStringLiteral("このワイヤーは登録済みです。外す場合は一覧の行を選んでください。")); return;
                }
            }
        }
        if (!grouped && row < 0) {
            domain::WireChainRef one; one.segments.push_back({id}); one.reversed.push_back(false);
            next.chains.push_back(one); next.roles.push_back(role);
        } else {
            added.segments.push_back({id}); added.reversed.push_back(false);
        }
    }
    if (!added.segments.empty()) {
        if (row >= 0) {
            auto& chain = next.chains[static_cast<std::size_t>(row)];
            chain.segments.insert(chain.segments.end(), added.segments.begin(), added.segments.end());
            chain.reversed.insert(chain.reversed.end(), added.reversed.begin(), added.reversed.end());
        } else { next.chains.push_back(added); next.roles.push_back(role); }
    }
    definition_ = std::move(next);
    Invalidate();
    RefreshList();
}

void V2GptSurfaceTool::HandleSelectionChanged()
{
    if (!active_) { return; }
    if (const auto picked = window_.viewport_->TakeLastToolPick(); picked.has_value()) {
        Add({*picked}, false);
    }
}

void V2GptSurfaceTool::EditRow(int operation)
{
    const int index = list_->indexOfTopLevelItem(list_->currentItem());
    if (index < 0) { status_->setText(QStringLiteral("一覧の行を選んでください。")); return; }
    const auto at = static_cast<std::size_t>(index);
    if (operation == 0) {
        definition_.chains.erase(definition_.chains.begin() + index);
        definition_.roles.erase(definition_.roles.begin() + index);
    } else if (operation == 3) {
        if (role_->currentIndex() == 3) {
            status_->setText(QStringLiteral("外周・通る線・新しい断面のいずれかを上の欄で選んでください。")); return;
        }
        definition_.roles[at] = role_->currentIndex() == 0 ? app::kGptBoundaryRole
            : role_->currentIndex() == 1 ? app::kGptInteriorRole : app::kGptSectionRole;
    } else if (operation == 2) {
        auto& chain = definition_.chains[at];
        std::reverse(chain.segments.begin(), chain.segments.end());
        std::reverse(chain.reversed.begin(), chain.reversed.end());
        for (std::size_t item = 0; item < chain.reversed.size(); ++item) { chain.reversed[item] = !chain.reversed[item]; }
    } else {
        const int target = index + operation;
        if (target < 0 || target >= static_cast<int>(definition_.chains.size())) { return; }
        std::swap(definition_.chains[at], definition_.chains[static_cast<std::size_t>(target)]);
        std::swap(definition_.roles[at], definition_.roles[static_cast<std::size_t>(target)]);
    }
    Invalidate(); RefreshList();
}

void V2GptSurfaceTool::RefreshList()
{
    const int selectedRow = list_->indexOfTopLevelItem(list_->currentItem());
    list_->clear();
    for (std::size_t row = 0; row < definition_.chains.size(); ++row) {
        QStringList names;
        for (const auto& ref : definition_.chains[row].segments) {
            const auto* entity = window_.session_->GetDocument().FindEntity(ref.entityId);
            names.push_back(entity ? QString::fromStdString(entity->displayName) : QStringLiteral("参照切れ"));
        }
        const int role = definition_.roles[row];
        const auto label = role == app::kGptBoundaryRole ? QStringLiteral("外周")
            : role == app::kGptInteriorRole ? QStringLiteral("通る線") : QStringLiteral("断面");
        const bool reversed = !definition_.chains[row].reversed.empty() && definition_.chains[row].reversed.front();
        auto* item = new QTreeWidgetItem(list_, {QStringLiteral("%1 %2%3").arg(row + 1).arg(label)
            .arg(reversed ? QStringLiteral(" ←") : QStringLiteral(" →")), names.join(QStringLiteral(" + "))});
        item->setToolTip(1, names.join(QStringLiteral(" + ")));
    }
    if (selectedRow >= 0 && list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(std::min(selectedRow, list_->topLevelItemCount() - 1)));
    }
}

void V2GptSurfaceTool::Invalidate()
{
    if (preview_.has_value()) { kernel::ReleaseShape(preview_->handle); preview_.reset(); }
    confirm_->setEnabled(false);
    window_.viewport_->HideToolPreview();
    window_.viewport_->SetToolPreviewFaces({});
    status_->setText(QStringLiteral("入力 %1 行。プレビューで接続と全入力線からのずれを確認します。")
        .arg(definition_.chains.size()));
}

void V2GptSurfaceTool::Preview()
{
    Invalidate();
    const auto& document = window_.session_->GetDocument();
    const auto request = app::ResolveGptSurface(document, window_.session_->Scene(), definition_);
    if (!request.HasValue()) {
        status_->setText(QString::fromStdString(request.FirstSummaryJa() + "\n" + request.FirstDiagnostic().detailsJa)); return;
    }
    const auto made = kernel::BuildGptSurface(request.Value(), document.Snapshot().settings.tolerance);
    if (!made.HasValue()) {
        status_->setText(QString::fromStdString(made.FirstSummaryJa() + "\n" + made.FirstDiagnostic().detailsJa)); return;
    }
    preview_ = made.Value();
    previewRevision_ = document.Snapshot().revision;
    ShowResult(*preview_);
}

void V2GptSurfaceTool::ShowResult(const modeling::GuideSurfaceResult& result)
{
    const auto mesh = kernel::BuildShapeMesh(result.handle);
    if (!mesh.HasValue()) { status_->setText(QString::fromStdString(mesh.FirstSummaryJa())); return; }
    std::vector<std::vector<geometry::Vector3>> faces;
    for (const auto& triangle : mesh.Value().triangles) {
        faces.push_back({triangle.points[0], triangle.points[1], triangle.points[2]});
    }
    window_.viewport_->ShowToolPreview(mesh.Value().edges);
    window_.viewport_->SetToolPreviewFaces(std::move(faces));
    status_->setText(QStringLiteral("%1\n全入力線の標本偏差: 最大 %2 mm / RMS %3 mm\n面積 %4 mm²。確定すると元の線を残して面を作ります。")
        .arg(definition_.method == app::kGptBoundaryMethod ? QStringLiteral("外周と通る線から張った面（近似）")
            : QStringLiteral("一覧順の断面をつないだ面"))
        .arg(result.maximumDeviationMm, 0, 'g', 7).arg(result.rmsDeviationMm, 0, 'g', 7).arg(result.areaMm2, 0, 'g', 7));
    confirm_->setEnabled(true);
}

void V2GptSurfaceTool::Store(const base::EntityId& id, const modeling::GuideSurfaceResult& result)
{
    window_.guideShapes_[id.ToString()] = result.handle;
    window_.guideEdges_[id.ToString()] = result.boundary;
    window_.guideSamples_[id.ToString()] = result.samples;
}

void V2GptSurfaceTool::Confirm()
{
    auto& document = window_.session_->GetDocument();
    if (!preview_.has_value() || !confirm_->isEnabled()) { Preview(); return; }
    if (document.Snapshot().revision != previewRevision_) {
        Invalidate(); status_->setText(QStringLiteral("元の文書が変わりました。プレビューを更新してください。")); return;
    }
    domain::Feature feature;
    feature.id = window_.ids_->NextTyped<base::IdKind::Feature>();
    feature.type = domain::FeatureType::CreateGuideSurface;
    feature.displayName = "面生成 GPT版";
    feature.definition = definition_;
    for (const auto& chain : definition_.chains) {
        for (const auto& ref : chain.segments) {
            if (std::find(feature.inputEntityIds.begin(), feature.inputEntityIds.end(), ref.entityId) == feature.inputEntityIds.end()) {
                feature.inputEntityIds.push_back(ref.entityId);
            }
        }
    }
    domain::Entity entity;
    entity.id = window_.ids_->NextTyped<base::IdKind::Entity>();
    entity.kind = domain::EntityKind::GuideSurface;
    entity.displayName = app::UniqueDisplayName(document.Snapshot(), entity.kind, "面（GPT版）");
    entity.createdBy = feature.id;
    entity.groupId = document.Snapshot().settings.activeGroupId;
    feature.outputs.push_back({"surface", entity.id, entity.kind});
    const auto added = document.Run(document::AddFeatureCommand(feature, {entity}, "面生成 GPT版"));
    if (!added.committed) { window_.ReportDiagnostics(added.diagnostics); return; }
    Store(entity.id, *preview_);
    preview_.reset(); // 文書側へ所有権を渡したため解放しない。
    End();
    window_.AdoptCurrentDocument();
    window_.SetStatus(QStringLiteral("GPT版の面を作りました。元のワイヤーはそのままです。"));
}

bool V2GptSurfaceTool::HandleKey(int key)
{
    if (!active_) { return false; }
    if (key == Qt::Key_Escape) { End(); return true; }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) { Confirm(); return true; }
    return false;
}

bool V2GptSurfaceTool::Rebuild(const domain::Feature& feature, const base::EntityId& output)
{
    const auto* definition = std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition);
    if (definition == nullptr) { return false; }
    const auto& document = window_.session_->GetDocument();
    const auto request = app::ResolveGptSurface(document, window_.session_->Scene(), *definition);
    if (!request.HasValue()) { window_.ReportDiagnostics(request.Diagnostics()); return false; }
    const auto made = kernel::BuildGptSurface(request.Value(), document.Snapshot().settings.tolerance);
    if (!made.HasValue()) { window_.ReportDiagnostics(made.Diagnostics()); return false; }
    Store(output, made.Value());
    return true;
}
