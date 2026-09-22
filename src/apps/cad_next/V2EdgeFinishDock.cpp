#include "V2EdgeFinishDock.h"
#include "V2PanelFrame.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <utility>

namespace {

[[nodiscard]] bool ClickIfUsable(QPushButton* button)
{
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;
    }
    button->click();
    return true;
}

[[nodiscard]] QString Text(std::string_view text)
{
    return QString::fromUtf8(std::string(text).c_str());
}

} // namespace

V2EdgeFinishDock::V2EdgeFinishDock(QWidget* parent)
    : QDockWidget(QStringLiteral("辺の丸め・面取り"), parent)
{
    setObjectName(QStringLiteral("edgeFinishDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("1. 作り方")));
    auto* kindRow = new QHBoxLayout();
    const char* labels[2] = {"フィレット(R 丸め)", "面取り(C)"};
    for (std::size_t index = 0; index < kinds_.size(); ++index) {
        auto* button = new QPushButton(QString::fromUtf8(labels[index]), body);
        button->setCheckable(true);
        const int kind = static_cast<int>(index);
        QObject::connect(button, &QPushButton::clicked, this, [this, kind] {
            if (!loading_ && kindHandler_) {
                kindHandler_(kind);
            }
        });
        kindRow->addWidget(button);
        kinds_[index] = button;
    }
    layout->addLayout(kindRow);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("2. 入力")));
    const auto row = [this, body, layout](const QString& name, QLabel** value, QPushButton** clear,
                         std::function<void()>* handler) {
        auto* line = new QHBoxLayout();
        line->addWidget(new QLabel(name, body));
        *value = new QLabel(body);
        (*value)->setWordWrap(true);
        line->addWidget(*value, 1);
        *clear = new QPushButton(QStringLiteral("解除"), body);
        QObject::connect(*clear, &QPushButton::clicked, this, [this, handler] {
            if (!loading_ && *handler) {
                (*handler)();
            }
        });
        line->addWidget(*clear);
        layout->addLayout(line);
    };
    row(QStringLiteral("部品"), &partValue_, &clearPart_, &clearPartHandler_);
    row(QStringLiteral("辺"), &edgesValue_, &clearEdges_, &clearEdgesHandler_);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("3. 設定")));
    auto* sizeRow = new QHBoxLayout();
    sizeLabel_ = new QLabel(QStringLiteral("半径"), body);
    sizeRow->addWidget(sizeLabel_);
    size_ = new QDoubleSpinBox(body);
    size_->setObjectName(QStringLiteral("edgeFinishSize"));
    size_->setRange(0.001, 10000.0);
    size_->setDecimals(3);
    size_->setSuffix(QStringLiteral(" mm"));
    sizeRow->addWidget(size_, 1);
    layout->addLayout(sizeRow);
    QObject::connect(size_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && sizeHandler_) {
            sizeHandler_(value);
        }
    });

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("4. 状態")));
    status_ = new QLabel(body);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch(1);
    layout->addLayout(MakeCancelConfirmRow(body, &cancel_, &confirm_));
    QObject::connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelHandler_) {
            cancelHandler_();
        }
    });
    QObject::connect(confirm_, &QPushButton::clicked, this, [this] {
        if (confirmHandler_) {
            confirmHandler_();
        }
    });
}

void V2EdgeFinishDock::ShowInput(const kachakacha::v2::app::EdgeFinishInputState& state,
    const QString& partNameJa, const std::vector<QString>& statusLinesJa, bool canConfirm)
{
    loading_ = true;
    for (std::size_t index = 0; index < kinds_.size(); ++index) {
        kinds_[index]->setChecked(static_cast<int>(index) == state.kind);
    }
    partValue_->setText(state.part.IsNil() ? QStringLiteral("(選んでいません)") : partNameJa);
    clearPart_->setEnabled(!state.part.IsNil());
    edgesValue_->setText(state.edges.empty() ? QStringLiteral("(選んでいません)")
                                             : QStringLiteral("%1 本").arg(static_cast<int>(state.edges.size())));
    clearEdges_->setEnabled(!state.edges.empty());
    sizeLabel_->setText(Text(kachakacha::v2::app::EdgeFinishSizeNameJa(state.kind)));
    size_->setValue(state.sizeMm);
    QString status;
    for (const QString& line : statusLinesJa) {
        status += (status.isEmpty() ? QString() : QStringLiteral("\n")) + line;
    }
    status_->setText(status);
    confirm_->setEnabled(canConfirm);
    loading_ = false;
}

void V2EdgeFinishDock::SetKindHandler(std::function<void(int)> handler)
{
    kindHandler_ = std::move(handler);
}

void V2EdgeFinishDock::SetSizeHandler(std::function<void(double)> handler)
{
    sizeHandler_ = std::move(handler);
}

void V2EdgeFinishDock::SetClearHandlers(std::function<void()> part, std::function<void()> edges)
{
    clearPartHandler_ = std::move(part);
    clearEdgesHandler_ = std::move(edges);
}

void V2EdgeFinishDock::SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

bool V2EdgeFinishDock::ClickKind(int kind)
{
    return kind >= 0 && kind < static_cast<int>(kinds_.size())
        && ClickIfUsable(kinds_[static_cast<std::size_t>(kind)]);
}

bool V2EdgeFinishDock::ClickClearEdges()
{
    return ClickIfUsable(clearEdges_);
}

bool V2EdgeFinishDock::ClickConfirm()
{
    return ClickIfUsable(confirm_);
}

bool V2EdgeFinishDock::TypeSize(double sizeMm)
{
    if (size_ == nullptr || !size_->isVisible() || !size_->isEnabled()) {
        return false;
    }
    size_->setValue(sizeMm);
    return true;
}

QString V2EdgeFinishDock::PartTextJa() const
{
    return partValue_->text();
}

QString V2EdgeFinishDock::EdgesTextJa() const
{
    return edgesValue_->text();
}

QString V2EdgeFinishDock::SizeLabelJa() const
{
    return sizeLabel_->text();
}

QString V2EdgeFinishDock::StatusTextJa() const
{
    return status_->text();
}

bool V2EdgeFinishDock::ConfirmEnabled() const
{
    return confirm_ != nullptr && confirm_->isEnabled();
}

int V2EdgeFinishDock::KindShown() const
{
    return kinds_[1]->isChecked() ? 1 : 0;
}
