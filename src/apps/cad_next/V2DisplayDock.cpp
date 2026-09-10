#include "V2DisplayDock.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <string>

namespace {

using kachakacha::v2::app::DisplaySettings;
using kachakacha::v2::app::DisplayStage;
using kachakacha::v2::app::LineStyle;

constexpr std::array<DisplayStage, 4> kStages{DisplayStage::All, DisplayStage::NoGrid,
    DisplayStage::NoConstruction, DisplayStage::SelectionOnly};
constexpr std::array<LineStyle, 3> kStyles{LineStyle::Solid, LineStyle::Dashed,
    LineStyle::Dotted};

[[nodiscard]] QDoubleSpinBox* MakeWidth(QWidget* parent, double initial)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setRange(0.25, 12.0);
    field->setDecimals(2);
    field->setSingleStep(0.25);
    field->setSuffix(QStringLiteral(" px"));
    field->setValue(initial);
    return field;
}

[[nodiscard]] QComboBox* MakeStyle(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    for (const LineStyle style : kStyles) {
        combo->addItem(QString::fromUtf8(
            std::string(kachakacha::v2::app::LineStyleNameJa(style)).c_str()));
    }
    return combo;
}

} // namespace

V2DisplayDock::V2DisplayDock(QWidget* parent)
    : QDockWidget(QStringLiteral("表示"), parent)
{
    setObjectName(QStringLiteral("displayDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 段: 設計 / グリッド無し / 完成形 / 選択だけ(Ctrl+1 / - / Ctrl+2 / Ctrl+3)。
    auto* stageRow = new QWidget(body);
    auto* stageLayout = new QHBoxLayout(stageRow);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(2);
    for (std::size_t index = 0; index < kStages.size(); ++index) {
        auto* button = new QPushButton(QString::fromUtf8(std::string(
            kachakacha::v2::app::DisplayStageLabelJa(kStages[index])).c_str()), stageRow);
        button->setCheckable(true);
        const DisplayStage stage = kStages[index];
        QObject::connect(button, &QPushButton::clicked, this, [this, stage] { PressStage(stage); });
        stageLayout->addWidget(button);
        stageButtons_[index] = button;
    }
    layout->addWidget(stageRow);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(3);
    wireColor_ = new QPushButton(QStringLiteral("線の色"), body);
    form->addRow(QStringLiteral("線の色"), wireColor_);
    wireWidth_ = MakeWidth(body, settings_.wireWidthPx);
    form->addRow(QStringLiteral("線の太さ"), wireWidth_);
    wireStyle_ = MakeStyle(body);
    form->addRow(QStringLiteral("線の様式"), wireStyle_);
    constructionColor_ = new QPushButton(QStringLiteral("補助線の色"), body);
    form->addRow(QStringLiteral("補助線の色"), constructionColor_);
    constructionWidth_ = MakeWidth(body, settings_.constructionWidthPx);
    form->addRow(QStringLiteral("補助線の太さ"), constructionWidth_);
    constructionStyle_ = MakeStyle(body);
    constructionStyle_->setCurrentIndex(IndexOf(settings_.constructionStyle));
    form->addRow(QStringLiteral("補助線の様式"), constructionStyle_);
    backgroundColor_ = new QPushButton(QStringLiteral("背景色"), body);
    form->addRow(QStringLiteral("背景色"), backgroundColor_);
    layout->addLayout(form);

    note_ = new QLabel(QStringLiteral("見え方だけが変わります。形も文書も変わりません。"), body);
    note_->setWordWrap(true);
    layout->addWidget(note_);
    layout->addStretch(1);
    // 棚の中身は巻物にする。欄が多い棚の最小幅で右の棚全体が広がり、
    // 画面(作図の場所)が狭くなって入力列が画面の外へ寄っていた。
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);

    QObject::connect(wireWidth_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(wireStyle_, &QComboBox::currentIndexChanged, this, [this] { Emit(); });
    QObject::connect(constructionWidth_, &QDoubleSpinBox::valueChanged, this,
        [this] { Emit(); });
    QObject::connect(constructionStyle_, &QComboBox::currentIndexChanged, this,
        [this] { Emit(); });
    QObject::connect(wireColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(wireColor_, wire_, QStringLiteral("線の色")); });
    QObject::connect(constructionColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(constructionColor_, construction_, QStringLiteral("補助線の色")); });
    QObject::connect(backgroundColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(backgroundColor_, background_, QStringLiteral("背景色")); });
}

LineStyle V2DisplayDock::StyleAt(int index) noexcept
{
    return index >= 0 && index < static_cast<int>(kStyles.size())
        ? kStyles[static_cast<std::size_t>(index)]
        : LineStyle::Solid;
}

int V2DisplayDock::IndexOf(LineStyle style) noexcept
{
    for (std::size_t index = 0; index < kStyles.size(); ++index) {
        if (kStyles[index] == style) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

void V2DisplayDock::Apply()
{
    if (applyHandler_) {
        applyHandler_(Choice());
    }
}

void V2DisplayDock::Emit()
{
    if (!loading_ && applyHandler_) {
        applyHandler_(Choice());
    }
}

V2DisplayChoice V2DisplayDock::Choice() const
{
    V2DisplayChoice choice;
    choice.settings = settings_;
    choice.settings.wireWidthPx = kachakacha::v2::app::ClampLineWidthPx(wireWidth_->value());
    choice.settings.wireStyle = StyleAt(wireStyle_->currentIndex());
    choice.settings.constructionWidthPx =
        kachakacha::v2::app::ClampLineWidthPx(constructionWidth_->value());
    choice.settings.constructionStyle = StyleAt(constructionStyle_->currentIndex());
    choice.wireColor = wire_;
    choice.constructionColor = construction_;
    choice.backgroundColor = background_;
    return choice;
}

void V2DisplayDock::SetSettings(const DisplaySettings& settings, DisplayStage stage)
{
    loading_ = true;
    settings_ = settings;
    wireWidth_->setValue(settings.wireWidthPx);
    wireStyle_->setCurrentIndex(IndexOf(settings.wireStyle));
    constructionWidth_->setValue(settings.constructionWidthPx);
    constructionStyle_->setCurrentIndex(IndexOf(settings.constructionStyle));
    for (std::size_t index = 0; index < kStages.size(); ++index) {
        stageButtons_[index]->setChecked(kStages[index] == stage);
    }
    loading_ = false;
}

void V2DisplayDock::SetChoice(const V2DisplayChoice& choice, DisplayStage stage)
{
    SetSettings(choice.settings, stage);
    wire_ = choice.wireColor;
    construction_ = choice.constructionColor;
    background_ = choice.backgroundColor;
    PaintButton(wireColor_, wire_);
    PaintButton(constructionColor_, construction_);
    PaintButton(backgroundColor_, background_);
}

void V2DisplayDock::SetApplyHandler(std::function<void(const V2DisplayChoice&)> handler)
{
    applyHandler_ = std::move(handler);
}

void V2DisplayDock::SetStageHandler(std::function<void(DisplayStage)> handler)
{
    stageHandler_ = std::move(handler);
}

void V2DisplayDock::SetColorChooser(
    std::function<QColor(const QColor& initial, const QString& title)> chooser)
{
    colorChooser_ = std::move(chooser);
}

void V2DisplayDock::PressStage(DisplayStage stage)
{
    for (std::size_t index = 0; index < kStages.size(); ++index) {
        stageButtons_[index]->setChecked(kStages[index] == stage);
    }
    if (stageHandler_) {
        stageHandler_(stage);
    }
}

void V2DisplayDock::ChooseColor(QPushButton* button, QColor& color, const QString& title)
{
    const QColor chosen = colorChooser_ ? colorChooser_(color, title)
                                       : QColorDialog::getColor(color, this, title);
    if (!chosen.isValid()) {
        return;
    }
    color = chosen;
    PaintButton(button, color);
    Emit();
}

void V2DisplayDock::PaintButton(QPushButton* button, const QColor& color)
{
    if (!color.isValid()) {
        return;
    }
    const bool dark = (color.red() + color.green() + color.blue()) < 3 * 128;
    button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
            .arg(color.name(), dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000")));
}
