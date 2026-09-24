#include "V2GptSurfaceTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/app/GptSurfaceAuto.h"
#include <QCheckBox>
#include <QColor>
#include <QObject>
#include <QString>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace kachakacha::v2;

void V2GptSurfaceTool::BuildAssist(QVBoxLayout* layout)
{
    automatic_ = new QCheckBox(QStringLiteral("外周と内部線を自動で判別"), dock_->widget());
    automatic_->setObjectName(QStringLiteral("gptSurfaceAutomatic"));
    automatic_->setChecked(true);
    layout->addWidget(automatic_);
    nextBoundary_ = new QPushButton(QStringLiteral("別の外周候補を見る"), dock_->widget());
    nextBoundary_->setObjectName(QStringLiteral("gptSurfaceNextBoundary"));
    nextBoundary_->setEnabled(false);
    layout->addWidget(nextBoundary_);
    QObject::connect(automatic_, &QCheckBox::toggled, dock_, [this](bool checked) {
        if (!active_) { return; }
        Invalidate();
        if (checked) { candidate_ = 0; AutoBoundary(); }
        else { nextBoundary_->setEnabled(false); }
        RefreshMarks();
    });
    QObject::connect(nextBoundary_, &QPushButton::clicked, dock_, [this] {
        ++candidate_;
        Preview();
    });
}

bool V2GptSurfaceTool::AutoBoundary()
{
    autoSummary_.clear();
    if (!automatic_->isChecked() || definition_.method != app::kGptBoundaryMethod) { return true; }
    const auto result = app::AutoGptSurfaceBoundary(window_.session_->Scene(), definition_,
        window_.session_->GetDocument().Snapshot().settings.tolerance, candidate_);
    nextBoundary_->setEnabled(result.HasValue() && result.Value().candidateCount > 1);
    if (!result.HasValue()) {
        status_->setText(QString::fromStdString(result.FirstSummaryJa() + "\n" + result.FirstDiagnostic().detailsJa));
        return false;
    }
    definition_ = result.Value().definition;
    candidate_ = result.Value().candidateIndex;
    autoSummary_ = QStringLiteral("自動判定: 外周 %1 辺 / 内部 %2 辺（候補 %3/%4）")
        .arg(result.Value().boundaryCount).arg(definition_.chains.size() - result.Value().boundaryCount)
        .arg(candidate_ + 1).arg(result.Value().candidateCount);
    status_->setText(autoSummary_ + QStringLiteral("\n紫＝外周、緑＝内部。違う場合は別候補か手動で修正できます。"));
    RefreshList();
    return true;
}

void V2GptSurfaceTool::RefreshMarks()
{
    if (!active_) { return; }
    std::vector<V2Viewport::PlacedRoleLabel> marks;
    std::vector<std::pair<base::EntityId, QColor>> colors;
    const int selected = list_->indexOfTopLevelItem(list_->currentItem());
    for (std::size_t row = 0; row < definition_.chains.size(); ++row) {
        const auto& chain = definition_.chains[row];
        const int role = definition_.roles[row];
        const bool emphasized = selected == static_cast<int>(row);
        const QColor color = emphasized ? QColor(255, 240, 100) : role == app::kGptBoundaryRole
            ? QColor(220, 150, 255) : role == app::kGptInteriorRole ? QColor(80, 230, 160) : QColor(255, 175, 70);
        const auto word = role == app::kGptBoundaryRole ? QStringLiteral("外周")
            : role == app::kGptInteriorRole ? QStringLiteral("内部") : QStringLiteral("断面");
        for (std::size_t index = 0; index < chain.segments.size(); ++index) {
            const auto& ref = chain.segments[index];
            colors.push_back({ref.entityId, color});
            for (const auto& source : window_.session_->Scene().curves) {
                if (source.entityId != ref.entityId || (!ref.segmentId.IsNil() && source.segmentId != ref.segmentId)) { continue; }
                const bool reverse = !chain.reversed.empty() && chain.reversed[index];
                marks.push_back({source.segment.Evaluate(.5), QStringLiteral("%1%2 %3")
                    .arg(emphasized ? QStringLiteral("● ") : QString()).arg(row + 1).arg(word), color,
                    source.segment.Evaluate(reverse ? .45 : .55), emphasized});
            }
        }
    }
    window_.viewport_->ShowToolRoleLabels(std::move(marks));
    window_.viewport_->SetRoleColors(std::move(colors));
}
