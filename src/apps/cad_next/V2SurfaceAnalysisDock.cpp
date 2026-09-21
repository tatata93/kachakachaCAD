#include "V2SurfaceAnalysisDock.h"

#include <QDockWidget>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace {

using kachakacha::v2::app::SurfaceAnalysisMode;

[[nodiscard]] QString Text(std::string_view text)
{
    return QString::fromUtf8(std::string(text).c_str());
}

} // namespace

V2SurfaceAnalysisDock::V2SurfaceAnalysisDock(QWidget* parent)
    : QDockWidget(QStringLiteral("面の解析"), parent)
{
    setObjectName(QStringLiteral("surfaceAnalysisDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(new QLabel(QStringLiteral("1. 見るもの"), body));
    auto* grid = new QGridLayout();
    const auto& modes = kachakacha::v2::app::SurfaceAnalysisModes();
    for (std::size_t index = 0; index < modes.size() && index < modes_.size(); ++index) {
        const SurfaceAnalysisMode mode = modes[index];
        auto* button = new QPushButton(Text(kachakacha::v2::app::SurfaceAnalysisModeLabelJa(mode)), body);
        button->setCheckable(true);
        QObject::connect(button, &QPushButton::clicked, this, [this, mode] {
            if (!loading_ && modeHandler_) {
                modeHandler_(mode);
            }
        });
        modes_[index] = button;
        grid->addWidget(button, static_cast<int>(index / 2), static_cast<int>(index % 2));
    }
    layout->addLayout(grid);
    layout->addWidget(new QLabel(QStringLiteral("2. 対象"), body));
    target_ = new QLabel(body);
    target_->setWordWrap(true);
    layout->addWidget(target_);
    layout->addWidget(new QLabel(QStringLiteral("3. 読み方"), body));
    legend_ = new QLabel(body);
    legend_->setWordWrap(true);
    layout->addWidget(legend_);
    layout->addStretch(1);
    auto* actions = new QHBoxLayout();
    close_ = new QPushButton(QStringLiteral("閉じる(表示は残す)"), body);
    QObject::connect(close_, &QPushButton::clicked, this, [this] {
        if (closeHandler_) {
            closeHandler_();
        }
    });
    actions->addStretch(1);
    actions->addWidget(close_);
    layout->addLayout(actions);
}

void V2SurfaceAnalysisDock::ShowState(SurfaceAnalysisMode mode, const QString& targetJa,
    const std::vector<QString>& legendJa)
{
    loading_ = true;
    const auto& modes = kachakacha::v2::app::SurfaceAnalysisModes();
    for (std::size_t index = 0; index < modes.size() && index < modes_.size(); ++index) {
        modes_[index]->setChecked(modes[index] == mode);
    }
    target_->setText(targetJa);
    QString text;
    for (const QString& line : legendJa) {
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + line;
    }
    legend_->setText(text);
    loading_ = false;
}

void V2SurfaceAnalysisDock::SetModeHandler(std::function<void(SurfaceAnalysisMode)> handler)
{
    modeHandler_ = std::move(handler);
}

void V2SurfaceAnalysisDock::SetCloseHandler(std::function<void()> handler)
{
    closeHandler_ = std::move(handler);
}

bool V2SurfaceAnalysisDock::ClickMode(SurfaceAnalysisMode mode)
{
    const auto& modes = kachakacha::v2::app::SurfaceAnalysisModes();
    for (std::size_t index = 0; index < modes.size() && index < modes_.size(); ++index) {
        if (modes[index] == mode) {
            QPushButton* button = modes_[index];
            if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
                return false;
            }
            button->click();
            return true;
        }
    }
    return false;
}

bool V2SurfaceAnalysisDock::ClickClose()
{
    if (close_ == nullptr || !close_->isVisible()) {
        return false;
    }
    close_->click();
    return true;
}

QString V2SurfaceAnalysisDock::LegendTextJa() const
{
    return legend_->text();
}

QString V2SurfaceAnalysisDock::TargetTextJa() const
{
    return target_->text();
}
