#include "Win95Style.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionGroupBox>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QStyleOptionToolButton>
#include <QWidget>

#include <algorithm>

namespace {

// Windows 標準スキーム(Control Panel\Colors の既定値)。
const QColor kFace{0xC0, 0xC0, 0xC0};      // 3DFACE
const QColor kHighlight3d{0xFF, 0xFF, 0xFF}; // 3DHILIGHT
const QColor kLight{0xDF, 0xDF, 0xDF};     // 3DLIGHT
const QColor kShadow{0x80, 0x80, 0x80};    // 3DSHADOW
const QColor kDarkShadow{0x00, 0x00, 0x00}; // 3DDKSHADOW
const QColor kWindow{0xFF, 0xFF, 0xFF};
const QColor kText{0x00, 0x00, 0x00};
const QColor kSelection{0x00, 0x00, 0x80};
const QColor kSelectionText{0xFF, 0xFF, 0xFF};
const QColor kDisabledText{0x80, 0x80, 0x80};
const QColor kTooltip{0xFF, 0xFF, 0xE1};

enum class EdgeStyle {
    Raised,  //!< EDGE_RAISED: ボタン・浮いた面
    Sunken,  //!< EDGE_SUNKEN: 入力欄・一覧
    Etched,  //!< EDGE_ETCHED: グループ枠・区切り
    Pressed, //!< 押されたボタン(Raised の左上/右下を入れ替え)
};

//! 1pxの「左上」「右下」の線を引く(角は右下優先。Win95 と同じ重なり)。
void DrawEdgeLines(
    QPainter* painter, const QRect& rect, const QColor& topLeft, const QColor& bottomRight)
{
    if (rect.width() <= 0 || rect.height() <= 0) {
        return;
    }
    painter->setPen(topLeft);
    painter->drawLine(rect.left(), rect.top(), rect.right(), rect.top());
    painter->drawLine(rect.left(), rect.top(), rect.left(), rect.bottom());
    painter->setPen(bottomRight);
    painter->drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());
    painter->drawLine(rect.right(), rect.top(), rect.right(), rect.bottom());
}

//! DrawEdge 相当。2重の縁を描き、内側の矩形を返す。
QRect DrawWin95Edge(QPainter* painter, const QRect& rect, EdgeStyle style)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    QRect outer = rect.adjusted(0, 0, -1, -1);
    switch (style) {
    case EdgeStyle::Raised:
        DrawEdgeLines(painter, outer, kHighlight3d, kDarkShadow);
        DrawEdgeLines(painter, outer.adjusted(1, 1, -1, -1), kLight, kShadow);
        break;
    case EdgeStyle::Pressed:
        DrawEdgeLines(painter, outer, kShadow, kHighlight3d);
        DrawEdgeLines(painter, outer.adjusted(1, 1, -1, -1), kDarkShadow, kFace);
        break;
    case EdgeStyle::Sunken:
        DrawEdgeLines(painter, outer, kShadow, kHighlight3d);
        DrawEdgeLines(painter, outer.adjusted(1, 1, -1, -1), kDarkShadow, kLight);
        break;
    case EdgeStyle::Etched:
        DrawEdgeLines(painter, outer, kShadow, kHighlight3d);
        break;
    }
    painter->restore();
    return style == EdgeStyle::Etched ? rect.adjusted(1, 1, -1, -1)
                                      : rect.adjusted(2, 2, -2, -2);
}

//! 黒い塗り三角(スクロールバー・スピン・コンボの矢印)。
void DrawWin95Arrow(QPainter* painter, const QRect& rect, Qt::ArrowType arrow, bool enabled)
{
    const int size = std::min(rect.width(), rect.height());
    const int half = std::max(1, (size - 1) / 2);
    const QPoint center = rect.center();
    QPolygon polygon;
    switch (arrow) {
    case Qt::UpArrow:
        polygon << QPoint(center.x(), center.y() - half / 2 - 1)
                << QPoint(center.x() - half, center.y() + half / 2)
                << QPoint(center.x() + half, center.y() + half / 2);
        break;
    case Qt::DownArrow:
        polygon << QPoint(center.x(), center.y() + half / 2 + 1)
                << QPoint(center.x() - half, center.y() - half / 2)
                << QPoint(center.x() + half, center.y() - half / 2);
        break;
    case Qt::LeftArrow:
        polygon << QPoint(center.x() - half / 2 - 1, center.y())
                << QPoint(center.x() + half / 2, center.y() - half)
                << QPoint(center.x() + half / 2, center.y() + half);
        break;
    case Qt::RightArrow:
        polygon << QPoint(center.x() + half / 2 + 1, center.y())
                << QPoint(center.x() - half / 2, center.y() - half)
                << QPoint(center.x() - half / 2, center.y() + half);
        break;
    default:
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(Qt::NoPen);
    painter->setBrush(enabled ? kText : kShadow);
    painter->drawPolygon(polygon);
    painter->restore();
}

//! スクロールバーの溝(白と面色の市松)。
void DrawWin95Checker(QPainter* painter, const QRect& rect)
{
    QPixmap pattern(2, 2);
    pattern.fill(kFace);
    QPainter patternPainter(&pattern);
    patternPainter.setPen(kHighlight3d);
    patternPainter.drawPoint(0, 0);
    patternPainter.drawPoint(1, 1);
    patternPainter.end();
    painter->fillRect(rect, QBrush(pattern));
}

} // namespace

Win95Style::Win95Style()
    : QProxyStyle(QStringLiteral("fusion"))
{
}

QPalette Win95Style::Win95Palette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, kFace);
    palette.setColor(QPalette::WindowText, kText);
    palette.setColor(QPalette::Base, kWindow);
    palette.setColor(QPalette::AlternateBase, kFace);
    palette.setColor(QPalette::Text, kText);
    palette.setColor(QPalette::Button, kFace);
    palette.setColor(QPalette::ButtonText, kText);
    palette.setColor(QPalette::BrightText, kHighlight3d);
    palette.setColor(QPalette::Light, kHighlight3d);
    palette.setColor(QPalette::Midlight, kLight);
    palette.setColor(QPalette::Mid, kShadow);
    palette.setColor(QPalette::Dark, kShadow);
    palette.setColor(QPalette::Shadow, kDarkShadow);
    palette.setColor(QPalette::Highlight, kSelection);
    palette.setColor(QPalette::HighlightedText, kSelectionText);
    palette.setColor(QPalette::ToolTipBase, kTooltip);
    palette.setColor(QPalette::ToolTipText, kText);
    palette.setColor(QPalette::PlaceholderText, kDisabledText);
    palette.setColor(QPalette::Link, kSelection);
    palette.setColor(QPalette::Disabled, QPalette::Text, kDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, kDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, kDisabledText);
    return palette;
}

QFont Win95Style::Win95Font()
{
    // 日本語版 Windows 95 は MS UI Gothic 9pt。無ければ順に代替する。
    const QStringList candidates = {
        QStringLiteral("MS UI Gothic"),
        QStringLiteral("ＭＳ Ｐゴシック"),
        QStringLiteral("MS PGothic"),
        QStringLiteral("Meiryo UI"),
        QStringLiteral("MS Sans Serif"),
    };
    const QStringList families = QFontDatabase::families();
    for (const QString& candidate : candidates) {
        if (families.contains(candidate)) {
            QFont font(candidate, 9);
            font.setStyleStrategy(QFont::NoAntialias);
            return font;
        }
    }
    QFont fallback(QStringLiteral("sans-serif"), 9);
    fallback.setStyleStrategy(QFont::NoAntialias);
    return fallback;
}

void Win95Style::polish(QPalette& palette)
{
    palette = Win95Palette();
}

void Win95Style::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);
    if (auto* area = qobject_cast<QAbstractScrollArea*>(widget)) {
        area->setFrameShape(QFrame::StyledPanel);
    }
}

int Win95Style::pixelMetric(
    PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    switch (metric) {
    case PM_ScrollBarExtent:
        return 16;
    case PM_ScrollBarSliderMin:
        return 12;
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
        return 13;
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return 12;
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical:
        return 1;
    case PM_DefaultFrameWidth:
        return 2;
    case PM_ButtonDefaultIndicator:
        return 1;
    case PM_MenuBarItemSpacing:
    case PM_MenuBarPanelWidth:
        return 0;
    case PM_SplitterWidth:
        return 4;
    case PM_ToolBarItemSpacing:
        return 1;
    case PM_TabBarTabVSpace:
        return 6;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int Win95Style::styleHint(
    StyleHint hint,
    const QStyleOption* option,
    const QWidget* widget,
    QStyleHintReturn* returnData) const
{
    switch (hint) {
    case SH_EtchDisabledText:
        return 1; // 無効文字は白で1pxずらした影を付ける(Win95の見た目)。
    case SH_DitherDisabledText:
        return 0;
    case SH_UnderlineShortcut:
        return 1;
    case SH_ComboBox_Popup:
        return 0;
    case SH_Menu_MouseTracking:
        return 1;
    default:
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

QSize Win95Style::sizeFromContents(
    ContentsType type,
    const QStyleOption* option,
    const QSize& contentsSize,
    const QWidget* widget) const
{
    QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    if (type == CT_PushButton) {
        size.setHeight(std::max(size.height(), 23)); // Win95 の標準ボタン高さ
        size.setWidth(std::max(size.width(), 75));
    } else if (type == CT_MenuBarItem) {
        size.setHeight(std::max(size.height(), 19));
    }
    return size;
}

void Win95Style::drawPrimitive(
    PrimitiveElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget) const
{
    const bool enabled = option->state.testFlag(State_Enabled);
    switch (element) {
    case PE_PanelButtonCommand:
    case PE_PanelButtonBevel:
    case PE_PanelButtonTool: {
        const bool pressed = option->state.testFlag(State_Sunken)
            || option->state.testFlag(State_On);
        painter->fillRect(option->rect, kFace);
        if (element == PE_PanelButtonTool && !pressed
            && !option->state.testFlag(State_MouseOver)) {
            return; // ツールバーは平ら。触れた時だけ浮き上がる。
        }
        DrawWin95Edge(painter, option->rect,
            pressed ? EdgeStyle::Pressed : EdgeStyle::Raised);
        return;
    }
    case PE_FrameDefaultButton:
        painter->save();
        painter->setPen(kDarkShadow);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    case PE_PanelLineEdit:
    case PE_FrameLineEdit:
    case PE_Frame:
    case PE_FrameDockWidget: {
        if (element == PE_PanelLineEdit) {
            painter->fillRect(option->rect, enabled ? kWindow : kFace);
        }
        DrawWin95Edge(painter, option->rect, EdgeStyle::Sunken);
        return;
    }
    case PE_FrameGroupBox:
        DrawWin95Edge(painter, option->rect, EdgeStyle::Etched);
        return;
    case PE_IndicatorCheckBox: {
        QRect box = option->rect;
        box.setSize(QSize(13, 13));
        box.moveCenter(option->rect.center());
        painter->fillRect(box, enabled ? kWindow : kFace);
        DrawWin95Edge(painter, box, EdgeStyle::Sunken);
        if (option->state.testFlag(State_NoChange)) {
            DrawWin95Checker(painter, box.adjusted(2, 2, -2, -2));
        }
        if (option->state.testFlag(State_On) || option->state.testFlag(State_NoChange)) {
            // 黒いレ点(Win95 は3pxの太さで折れ線)。
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setPen(QPen(enabled ? kText : kShadow, 1));
            const int x = box.left();
            const int y = box.top();
            for (int offset = 0; offset < 3; ++offset) {
                painter->drawLine(x + 3, y + 5 + offset, x + 5, y + 7 + offset);
                painter->drawLine(x + 5, y + 7 + offset, x + 9, y + 3 + offset);
            }
            painter->restore();
        }
        return;
    }
    case PE_IndicatorRadioButton: {
        QRect box = option->rect;
        box.setSize(QSize(12, 12));
        box.moveCenter(option->rect.center());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setBrush(enabled ? kWindow : kFace);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(box.adjusted(1, 1, -1, -1));
        // 左上=影、右下=光 の押し込み円。
        painter->setBrush(Qt::NoBrush);
        painter->setPen(kShadow);
        painter->drawArc(box, 45 * 16, 180 * 16);
        painter->setPen(kDarkShadow);
        painter->drawArc(box.adjusted(1, 1, -1, -1), 45 * 16, 180 * 16);
        painter->setPen(kHighlight3d);
        painter->drawArc(box, 225 * 16, 180 * 16);
        painter->setPen(kLight);
        painter->drawArc(box.adjusted(1, 1, -1, -1), 225 * 16, 180 * 16);
        if (option->state.testFlag(State_On)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(enabled ? kText : kShadow);
            QRect dot = box.adjusted(4, 4, -4, -4);
            painter->drawEllipse(dot);
        }
        painter->restore();
        return;
    }
    case PE_IndicatorArrowUp:
        DrawWin95Arrow(painter, option->rect, Qt::UpArrow, enabled);
        return;
    case PE_IndicatorArrowDown:
        DrawWin95Arrow(painter, option->rect, Qt::DownArrow, enabled);
        return;
    case PE_IndicatorArrowLeft:
        DrawWin95Arrow(painter, option->rect, Qt::LeftArrow, enabled);
        return;
    case PE_IndicatorArrowRight:
        DrawWin95Arrow(painter, option->rect, Qt::RightArrow, enabled);
        return;
    case PE_IndicatorSpinUp:
    case PE_IndicatorSpinPlus:
        DrawWin95Arrow(painter, option->rect, Qt::UpArrow, enabled);
        return;
    case PE_IndicatorSpinDown:
    case PE_IndicatorSpinMinus:
        DrawWin95Arrow(painter, option->rect, Qt::DownArrow, enabled);
        return;
    case PE_FrameFocusRect: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        QPen pen(kText, 1, Qt::DotLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }
    case PE_PanelMenuBar:
    case PE_PanelToolBar:
        painter->fillRect(option->rect, kFace);
        return;
    case PE_FrameMenu:
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect, EdgeStyle::Raised);
        return;
    case PE_IndicatorToolBarSeparator: {
        painter->save();
        painter->setPen(kShadow);
        const int x = option->rect.center().x();
        painter->drawLine(x, option->rect.top() + 2, x, option->rect.bottom() - 2);
        painter->setPen(kHighlight3d);
        painter->drawLine(x + 1, option->rect.top() + 2, x + 1, option->rect.bottom() - 2);
        painter->restore();
        return;
    }
    case PE_IndicatorToolBarHandle: {
        painter->save();
        painter->setPen(kHighlight3d);
        painter->drawLine(option->rect.left() + 2, option->rect.top() + 2,
            option->rect.left() + 2, option->rect.bottom() - 2);
        painter->setPen(kShadow);
        painter->drawLine(option->rect.left() + 3, option->rect.top() + 2,
            option->rect.left() + 3, option->rect.bottom() - 2);
        painter->restore();
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void Win95Style::drawControl(
    ControlElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget) const
{
    switch (element) {
    case CE_MenuBarItem: {
        const bool selected = option->state.testFlag(State_Selected)
            && option->state.testFlag(State_Enabled);
        painter->fillRect(option->rect, selected ? kSelection : kFace);
        if (const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
            painter->save();
            painter->setPen(selected ? kSelectionText : kText);
            painter->drawText(option->rect, Qt::AlignCenter | Qt::TextShowMnemonic,
                menuItem->text);
            painter->restore();
        }
        return;
    }
    case CE_MenuBarEmptyArea:
        painter->fillRect(option->rect, kFace);
        return;
    case CE_PushButtonLabel: {
        QStyleOptionButton shifted;
        if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option)) {
            shifted = *button;
            if (option->state.testFlag(State_Sunken) || option->state.testFlag(State_On)) {
                shifted.rect.translate(1, 1);
            }
            QProxyStyle::drawControl(element, &shifted, painter, widget);
            return;
        }
        break;
    }
    case CE_HeaderSection: {
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect,
            option->state.testFlag(State_Sunken) ? EdgeStyle::Pressed : EdgeStyle::Raised);
        return;
    }
    case CE_ProgressBarGroove:
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect, EdgeStyle::Sunken);
        return;
    case CE_ProgressBarContents: {
        // Win95 は塗りつぶしでなく細かいブロックの列。
        const auto* bar = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
        if (bar == nullptr) {
            break;
        }
        const QRect inner = option->rect.adjusted(3, 3, -3, -3);
        const double span = std::max(1, bar->maximum - bar->minimum);
        const double ratio = std::clamp(
            (bar->progress - bar->minimum) / span, 0.0, 1.0);
        const int filled = static_cast<int>(inner.width() * ratio);
        const int blockWidth = 8;
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(kSelection);
        for (int x = inner.left(); x + blockWidth - 2 <= inner.left() + filled;
             x += blockWidth) {
            painter->drawRect(QRect(x, inner.top(), blockWidth - 2, inner.height()));
        }
        painter->restore();
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void Win95Style::drawComplexControl(
    ComplexControl control,
    const QStyleOptionComplex* option,
    QPainter* painter,
    const QWidget* widget) const
{
    switch (control) {
    case CC_ScrollBar: {
        const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (slider == nullptr) {
            break;
        }
        const QRect groove = subControlRect(CC_ScrollBar, slider, SC_ScrollBarGroove, widget);
        DrawWin95Checker(painter, groove);
        const QRect subLine = subControlRect(CC_ScrollBar, slider, SC_ScrollBarSubLine, widget);
        const QRect addLine = subControlRect(CC_ScrollBar, slider, SC_ScrollBarAddLine, widget);
        const QRect handle = subControlRect(CC_ScrollBar, slider, SC_ScrollBarSlider, widget);
        const bool horizontal = slider->orientation == Qt::Horizontal;
        const auto drawButton = [&](const QRect& rect, Qt::ArrowType arrow, SubControl which) {
            painter->fillRect(rect, kFace);
            const bool pressed = slider->activeSubControls.testFlag(which)
                && slider->state.testFlag(State_Sunken);
            DrawWin95Edge(painter, rect, pressed ? EdgeStyle::Pressed : EdgeStyle::Raised);
            DrawWin95Arrow(painter, pressed ? rect.translated(1, 1) : rect, arrow,
                slider->state.testFlag(State_Enabled));
        };
        drawButton(subLine, horizontal ? Qt::LeftArrow : Qt::UpArrow, SC_ScrollBarSubLine);
        drawButton(addLine, horizontal ? Qt::RightArrow : Qt::DownArrow, SC_ScrollBarAddLine);
        if (handle.isValid() && !handle.isEmpty()) {
            painter->fillRect(handle, kFace);
            DrawWin95Edge(painter, handle, EdgeStyle::Raised);
        }
        return;
    }
    case CC_SpinBox: {
        const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
        if (spin == nullptr) {
            break;
        }
        const QRect frame = subControlRect(CC_SpinBox, spin, SC_SpinBoxFrame, widget);
        painter->fillRect(frame, kWindow);
        DrawWin95Edge(painter, frame, EdgeStyle::Sunken);
        const QRect up = subControlRect(CC_SpinBox, spin, SC_SpinBoxUp, widget);
        const QRect down = subControlRect(CC_SpinBox, spin, SC_SpinBoxDown, widget);
        const auto drawSpinButton = [&](const QRect& rect, Qt::ArrowType arrow, SubControl which) {
            const bool pressed = spin->activeSubControls.testFlag(which)
                && spin->state.testFlag(State_Sunken);
            painter->fillRect(rect, kFace);
            DrawWin95Edge(painter, rect, pressed ? EdgeStyle::Pressed : EdgeStyle::Raised);
            DrawWin95Arrow(painter, pressed ? rect.translated(1, 1) : rect, arrow,
                spin->state.testFlag(State_Enabled));
        };
        drawSpinButton(up, Qt::UpArrow, SC_SpinBoxUp);
        drawSpinButton(down, Qt::DownArrow, SC_SpinBoxDown);
        return;
    }
    case CC_ComboBox: {
        const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        if (combo == nullptr) {
            break;
        }
        const QRect frame = subControlRect(CC_ComboBox, combo, SC_ComboBoxFrame, widget);
        painter->fillRect(frame, combo->editable ? kWindow : kFace);
        DrawWin95Edge(painter, frame, EdgeStyle::Sunken);
        const QRect arrowRect = subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
        painter->fillRect(arrowRect, kFace);
        const bool pressed = combo->state.testFlag(State_Sunken);
        DrawWin95Edge(painter, arrowRect, pressed ? EdgeStyle::Pressed : EdgeStyle::Raised);
        DrawWin95Arrow(painter, pressed ? arrowRect.translated(1, 1) : arrowRect,
            Qt::DownArrow, combo->state.testFlag(State_Enabled));
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}
