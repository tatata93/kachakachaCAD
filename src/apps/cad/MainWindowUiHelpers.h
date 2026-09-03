#pragma once

// MainWindow の分割翻訳単位(MainWindow*.cpp)で共有する小さなUI・文字列
// ヘルパー(ADR 0018/0022: 逐語移動による分割)。MainWindow.cpp の無名名前空間から
// 逐語移動し、ヘッダー化のため inline を付与した。
// ここに置くのは「状態を持たない小物」だけ。パネル構築や操作本体は置かない。

#include "kachakacha/geometry/Vector2.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/io/NumericExpression.h"
#include "kachakacha/model/Project.h"
#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QValidator>
#include <QWidget>

#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mainwindow_helpers {

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;
using kachakacha::model::Project;
using kachakacha::model::ReferenceDimensionKind;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;
using kachakacha::model::WireMetadata;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;

inline constexpr int kSelectionKindRole = Qt::UserRole;
inline constexpr int kSelectionIndexRole = Qt::UserRole + 1;
inline constexpr int kDimensionNameRole = Qt::UserRole + 2;
inline constexpr int kSetNameRole = Qt::UserRole + 3;

//! 原点平面(初期の3基準面)。削除・グループ移動を禁止し、ツリー最上部に固定表示する。
inline bool IsOriginPlaneName(const std::string& name)
{
    return name == "top_XY" || name == "front_XZ" || name == "side_YZ";
}
inline constexpr double kPi = 3.14159265358979323846;

inline bool IsAutomationInvocation()
{
    const QStringList arguments = QApplication::arguments();
    return arguments.contains(QStringLiteral("--self-test"))
        || arguments.contains(QStringLiteral("--snapshot"))
        || arguments.contains(QStringLiteral("--manual-state"))
        || arguments.contains(QStringLiteral("--export-first-body-stl"))
        || arguments.contains(QStringLiteral("--export-first-body-step"));
}

inline QString FriendlyPlateCreationError(const std::exception& error)
{
    const QString technical = QString::fromUtf8(error.what());
    if (technical.contains(QStringLiteral("degenerate interior"))) {
        return QStringLiteral(
            "面の内部に幅または高さが0になる箇所があるため、板材化できません。\n"
            "断面の交差、重複、途中で一点に潰れる箇所を修正してください。\n"
            "端だけが一点に収束する形状には対応しています。");
    }
    if (technical.contains(QStringLiteral("fold or collapse"))) {
        return QStringLiteral(
            "指定した板厚では表面または裏面が折り返すため、板材化できません。\n"
            "板厚を小さくするか、厚み方向を反対側または中央に変更してください。");
    }
    if (technical.contains(QStringLiteral("normal is undefined"))) {
        return QStringLiteral(
            "面の向きを決められない領域があるため、板材化できません。\n"
            "外形線・断面線の重複や急なねじれを修正してください。");
    }
    if (technical.contains(QStringLiteral("non-finite"))) {
        return QStringLiteral(
            "面または厚みの計算結果が不正になったため、板材化できません。\n"
            "面の断面構成と板厚を確認してください。");
    }
    return technical;
}

inline QString FriendlySurfaceChainError(const std::exception& error)
{
    const QString technical = QString::fromUtf8(error.what());
    if (technical.contains(QStringLiteral("disconnected chain"))) {
        return QStringLiteral(
            "選んだ線が複数の離れた経路に分かれています。\n"
            "1つの輪郭・外形・断面として端点で続く線だけを選んでください。");
    }
    if (technical.contains(QStringLiteral("contain a branch"))) {
        return QStringLiteral(
            "選んだ線に枝分かれがあります。\n"
            "分岐点から先は、輪郭として使う側だけを選んでください。");
    }
    if (technical.contains(QStringLiteral("duplicate or overlapping"))) {
        return QStringLiteral(
            "同じ位置に重複する線が含まれています。\n"
            "重なった線の一方を外してから登録してください。");
    }
    if (technical.contains(QStringLiteral("crosses or overlaps itself"))) {
        return QStringLiteral(
            "閉じた輪郭が途中で交差または重なっています。\n"
            "交差点で線を分割し、面の外周になるループだけを登録してください。");
    }
    if (technical.contains(QStringLiteral("closes onto itself"))) {
        return QStringLiteral(
            "すでに閉じている線と別の線が同じ経路へ混在しています。\n"
            "閉じた線は単独の輪郭として登録してください。");
    }
    if (technical.contains(QStringLiteral("continuous chain"))
        || technical.contains(QStringLiteral("endpoint chain"))) {
        return QStringLiteral(
            "端点がつながる1本の経路として並べられません。\n"
            "端点の小さな四角を確認し、離れた箇所は端点一致で接続してください。");
    }
    return technical;
}

class ExpressionDoubleSpinBox final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    QValidator::State validate(QString& input, int& position) const override
    {
        Q_UNUSED(position);
        const QString expression = NormalizeExpression(input);
        if (expression.isEmpty()) {
            return QValidator::Intermediate;
        }
        static const QRegularExpression allowed(
            QStringLiteral("^[0-9eEpiPI+\\-*/().\\s]*$"));
        if (!allowed.match(expression).hasMatch()) {
            return QValidator::Invalid;
        }
        const std::optional<double> value = Evaluate(expression);
        if (!value.has_value()) {
            return QValidator::Intermediate;
        }
        return *value >= minimum() && *value <= maximum()
            ? QValidator::Acceptable
            : QValidator::Invalid;
    }

    double valueFromText(const QString& text) const override
    {
        const std::optional<double> result = Evaluate(NormalizeExpression(text));
        return result.has_value() ? *result : value();
    }

    void fixup(QString& input) const override
    {
        const std::optional<double> result = Evaluate(NormalizeExpression(input));
        if (result.has_value() && *result >= minimum() && *result <= maximum()) {
            input = prefix() + QDoubleSpinBox::textFromValue(*result) + suffix();
        }
    }

private:
    QString NormalizeExpression(QString text) const
    {
        if (!prefix().isEmpty() && text.startsWith(prefix())) {
            text.remove(0, prefix().size());
        }
        if (!suffix().isEmpty() && text.endsWith(suffix())) {
            text.chop(suffix().size());
        }
        text.replace(QChar(0x00d7), QLatin1Char('*'));
        text.replace(QChar(0x00f7), QLatin1Char('/'));
        text.replace(QChar(0x03c0), QStringLiteral("pi"));
        const QString decimalPoint = locale().decimalPoint();
        if (decimalPoint != QStringLiteral(".")) {
            text.replace(decimalPoint, QStringLiteral("."));
        }
        return text.trimmed();
    }

    static std::optional<double> Evaluate(const QString& expression)
    {
        const QByteArray utf8 = expression.toUtf8();
        return kachakacha::io::EvaluateNumericExpression(
            std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
    }
};

inline QDoubleSpinBox* MakeNumberField(double value = 0.0)
{
    auto* field = new ExpressionDoubleSpinBox;
    field->setRange(-1000000.0, 1000000.0);
    field->setDecimals(4);
    field->setSingleStep(0.5);
    field->setValue(value);
    field->setKeyboardTracking(false);
    field->setMinimumWidth(72);
    field->setToolTip(QStringLiteral("数値または計算式を入力できます。例: (180/2)*3"));
    return field;
}

inline QDoubleSpinBox* MakePositiveField(double value)
{
    QDoubleSpinBox* field = MakeNumberField(value);
    field->setRange(0.0001, 1000000.0);
    return field;
}

template <std::size_t Size>
QWidget* MakeCoordinateEditor(
    std::array<QDoubleSpinBox*, Size>& fields,
    const std::array<double, Size>& values,
    const std::array<QString, Size>& labels)
{
    auto* widget = new QWidget;
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    for (std::size_t index = 0; index < Size; ++index) {
        auto* label = new QLabel(labels[index]);
        label->setStyleSheet("color: #5c6670;");
        fields[index] = MakeNumberField(values[index]);
        layout->addWidget(label);
        layout->addWidget(fields[index], 1);
    }
    return widget;
}

inline QWidget* MakeVector3Editor(std::array<QDoubleSpinBox*, 3>& fields, Vector3 value = {})
{
    return MakeCoordinateEditor<3>(fields, {value.x, value.y, value.z}, {"X", "Y", "Z"});
}

inline QWidget* MakeVector2Editor(std::array<QDoubleSpinBox*, 2>& fields, Vector2 value = {})
{
    return MakeCoordinateEditor<2>(fields, {value.x, value.y}, {"U", "V"});
}

inline Vector3 ReadVector3(const std::array<QDoubleSpinBox*, 3>& fields)
{
    return {fields[0]->value(), fields[1]->value(), fields[2]->value()};
}

inline Vector2 ReadVector2(const std::array<QDoubleSpinBox*, 2>& fields)
{
    return {fields[0]->value(), fields[1]->value()};
}

inline Vector3 ReadTablePoint(const QTableWidget* table, int row)
{
    std::array<double, 3> values{};
    for (int column = 0; column < 3; ++column) {
        const auto* field = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, column));
        if (field == nullptr) {
            throw std::logic_error("Wire point editor is incomplete.");
        }
        values[column] = field->value();
    }
    return {values[0], values[1], values[2]};
}

inline QWidget* MakeFormPage(QFormLayout*& form)
{
    auto* page = new QWidget;
    form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setContentsMargins(0, 6, 0, 6);
    form->setVerticalSpacing(8);
    return page;
}

inline QString ToQString(const std::string& text)
{
    return QString::fromUtf8(text);
}

inline std::string ToName(const QString& text)
{
    return text.trimmed().toUtf8().toStdString();
}

inline void ValidateObjectName(const QString& name)
{
    if (name.trimmed().isEmpty()) {
        throw std::invalid_argument("名前を入力してください。");
    }
    static const QRegularExpression invalidCharacters(QStringLiteral("[\\s#]"));
    if (name.contains(invalidCharacters)) {
        throw std::invalid_argument("名前には空白と # を使用できません。");
    }
}

inline QString Number(double value)
{
    return QString::number(value, 'f', 3);
}

inline QString ReferenceDimensionKindText(ReferenceDimensionKind kind)
{
    switch (kind) {
    case ReferenceDimensionKind::PointDistance:
        return QStringLiteral("2点間");
    case ReferenceDimensionKind::WireLength:
        return QStringLiteral("ワイヤー全長");
    case ReferenceDimensionKind::WireRadius:
        return QStringLiteral("半径");
    case ReferenceDimensionKind::WireDistance:
        return QStringLiteral("ワイヤー間距離");
    case ReferenceDimensionKind::WireAngle:
        return QStringLiteral("接線角");
    case ReferenceDimensionKind::PointWireDistance:
        return QStringLiteral("点・ワイヤー間");
    case ReferenceDimensionKind::PointPlaneDistance:
        return QStringLiteral("点・平面間");
    case ReferenceDimensionKind::WirePlaneAngle:
        return QStringLiteral("ワイヤー・平面角");
    case ReferenceDimensionKind::PlaneAngle:
        return QStringLiteral("平面角");
    case ReferenceDimensionKind::PlaneDistance:
        return QStringLiteral("平面間距離");
    }
    return QStringLiteral("参照寸法");
}

inline bool IsAngleDimension(ReferenceDimensionKind kind)
{
    return kind == ReferenceDimensionKind::WireAngle
        || kind == ReferenceDimensionKind::WirePlaneAngle
        || kind == ReferenceDimensionKind::PlaneAngle;
}

inline QString ReferenceDimensionValueText(ReferenceDimensionKind kind, double value)
{
    if (kind == ReferenceDimensionKind::WireRadius) {
        return QStringLiteral("R %1 mm").arg(Number(value));
    }
    if (IsAngleDimension(kind)) {
        return QStringLiteral("%1°").arg(Number(value));
    }
    return QStringLiteral("%1 mm").arg(Number(value));
}

inline QString VectorText(Vector3 value)
{
    return QStringLiteral("X %1   Y %2   Z %3").arg(Number(value.x), Number(value.y), Number(value.z));
}

inline QString WireKindText(WireKind kind)
{
    switch (kind) {
    case WireKind::Line:
        return QStringLiteral("直線");
    case WireKind::Polyline:
        return QStringLiteral("ポリライン");
    case WireKind::CubicBezier:
        return QStringLiteral("3次ベジェ曲線");
    case WireKind::CubicBSpline:
        return QStringLiteral("3次B-spline");
    case WireKind::Circle:
        return QStringLiteral("円");
    case WireKind::CircularArc:
        return QStringLiteral("円弧");
    }
    return QStringLiteral("ワイヤー");
}

inline QString PolicyText(WirePlanePolicy policy)
{
    switch (policy) {
    case WirePlanePolicy::Free3D:
        return QStringLiteral("平面拘束なし");
    case WirePlanePolicy::ReferenceOnly:
        return QStringLiteral("平面を編集基準に使用");
    case WirePlanePolicy::LockedToPlane:
        return QStringLiteral("作業平面に固定");
    }
    return {};
}

inline bool WireLiesOnPlane(const Wire& wire, const WorkPlane& plane, double tolerance = 1.0e-7)
{
    const int samples = wire.Kind() == WireKind::Line ? 1 : 32;
    for (int sample = 0; sample <= samples; ++sample) {
        if (std::abs(plane.Project(wire.Evaluate(static_cast<double>(sample) / samples)).w) > tolerance) {
            return false;
        }
    }
    return true;
}

inline WireMetadata RetargetLineConstraints(
    const Project& project,
    WireMetadata metadata,
    const Wire& wire,
    bool updateLength)
{
    if (metadata.lineConstraints.Empty()) {
        return metadata;
    }
    if (wire.Kind() != WireKind::Line) {
        metadata.lineConstraints = {};
        return metadata;
    }
    if (updateLength && metadata.lineConstraints.lengthMillimeters.has_value()) {
        metadata.lineConstraints.lengthMillimeters = (wire.End() - wire.Start()).Length();
    }
    if (!metadata.lineConstraints.angleDegrees.has_value()) {
        return metadata;
    }
    if (!metadata.sourcePlaneName.has_value()) {
        throw std::invalid_argument("角度拘束の基準作業平面がありません。");
    }
    const auto plane = project.FindWorkPlane(*metadata.sourcePlaneName);
    if (!plane.has_value() || !WireLiesOnPlane(wire, *plane)) {
        throw std::invalid_argument("角度拘束された直線は基準作業平面の外へ移動できません。");
    }
    const auto start = plane->Project(wire.Start());
    const auto end = plane->Project(wire.End());
    metadata.lineConstraints.angleDegrees =
        std::atan2(end.v - start.v, end.u - start.u) * 180.0 / kPi;
    return metadata;
}

//! 縦レイアウトを見出し(QLabelの文字列またはQGroupBoxのタイトル)ごとの
//! コンテナへ束ね直す。モードのツール選択で1セクションだけ表示するために使う
//! (ADR 0025: 右パネルの単純化)。戻り値は (見出し, コンテナ) を並び順で返す。
inline std::vector<std::pair<QString, QWidget*>> SectionizeVerticalLayout(
    QVBoxLayout* source, const QStringList& sectionTitles)
{
    std::vector<QLayoutItem*> items;
    while (QLayoutItem* item = source->takeAt(0)) {
        items.push_back(item);
    }
    std::vector<std::pair<QString, QWidget*>> sections;
    QVBoxLayout* currentLayout = nullptr;
    for (QLayoutItem* item : items) {
        QWidget* widget = item->widget();
        QString startsTitle;
        if (widget != nullptr) {
            if (auto* box = qobject_cast<QGroupBox*>(widget);
                box != nullptr && sectionTitles.contains(box->title())) {
                startsTitle = box->title();
            } else if (auto* label = qobject_cast<QLabel*>(widget);
                label != nullptr && sectionTitles.contains(label->text())) {
                startsTitle = label->text();
            }
        }
        if (!startsTitle.isEmpty() || currentLayout == nullptr) {
            auto* container = new QWidget;
            currentLayout = new QVBoxLayout(container);
            currentLayout->setContentsMargins(0, 0, 0, 0);
            currentLayout->setSpacing(source->spacing());
            sections.emplace_back(startsTitle, container);
            source->addWidget(container);
        }
        if (widget != nullptr) {
            currentLayout->addWidget(widget);
            delete item;
        } else if (QLayout* sublayout = item->layout()) {
            currentLayout->addLayout(sublayout);
        } else {
            currentLayout->addItem(item);
        }
    }
    return sections;
}

} // namespace mainwindow_helpers
