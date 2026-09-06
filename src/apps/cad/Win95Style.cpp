#include "Win95Style.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QFontDatabase>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionDockWidget>
#include <QStyleOptionGroupBox>
#include <QStyleOptionMenuItem>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QStyleOptionTab>
#include <QStyleOptionToolButton>
#include <QTabBar>
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
const QColor kActiveCaption{0x00, 0x00, 0x80};
const QColor kInactiveCaption{0x80, 0x80, 0x80};
const QColor kFolder{0xFF, 0xFF, 0x00};
const QColor kFolderShadow{0x80, 0x80, 0x00};

constexpr auto kSavedStyleSheetProperty = "_kachakacha_win95_saved_stylesheet";

enum class EdgeStyle {
    ButtonRaised, //!< コマンドボタンの通常状態
    ButtonSunken, //!< コマンドボタンの押下状態
    FieldSunken,  //!< 入力欄・一覧
    WindowRaised, //!< メニュー・ウィンドウ枠
    Grouping,     //!< グループ枠・区切り
    StatusSunken, //!< 状態欄の1段だけの沈み枠
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
    case EdgeStyle::ButtonRaised:
        // Button border: raised outer/inner の左上色を入れ替える。
        DrawEdgeLines(painter, outer, kHighlight3d, kDarkShadow);
        DrawEdgeLines(painter, outer.adjusted(1, 1, -1, -1), kFace, kShadow);
        break;
    case EdgeStyle::ButtonSunken:
    case EdgeStyle::FieldSunken:
        DrawEdgeLines(painter, outer, kShadow, kHighlight3d);
        DrawEdgeLines(painter, outer.adjusted(1, 1, -1, -1), kDarkShadow, kFace);
        break;
    case EdgeStyle::WindowRaised:
        DrawEdgeLines(painter, outer, kFace, kDarkShadow);
        DrawEdgeLines(painter, outer.adjusted(1, 1, -1, -1), kHighlight3d, kShadow);
        break;
    case EdgeStyle::Grouping:
        DrawEdgeLines(painter, outer, kShadow, kHighlight3d);
        DrawEdgeLines(painter, outer.adjusted(1, 1, -1, -1), kHighlight3d, kShadow);
        break;
    case EdgeStyle::StatusSunken:
        DrawEdgeLines(painter, outer, kShadow, kHighlight3d);
        break;
    }
    painter->restore();
    return style == EdgeStyle::StatusSunken ? rect.adjusted(1, 1, -1, -1)
                                            : rect.adjusted(2, 2, -2, -2);
}

//! 黒い塗り三角(スクロールバー・スピン・コンボの矢印)。
//!
//! 一次資料どおり「決まった大きさの絵」として描く。Windows 95 の矢印は
//! ビットマップで、枠の大きさに合わせて伸び縮みしない。標準は幅7px・高さ4px
//! (行ごとに 7,5,3,1 と細る三角)。枠が狭いときだけ小さくする。
//! 枠いっぱいに引き伸ばすと、実機と似ても似つかない大きな三角になる。
void DrawWin95Arrow(QPainter* painter, const QRect& rect, Qt::ArrowType arrow, bool enabled)
{
    const bool vertical = arrow == Qt::UpArrow || arrow == Qt::DownArrow;
    // 三角の「底辺」の長さ。奇数にして中心が1pxに乗るようにする。
    const int across = vertical ? rect.width() : rect.height();
    int base = std::min(7, across - 2);
    if (base % 2 == 0) {
        --base;
    }
    if (base < 3) {
        base = 3;
    }
    const int depth = (base + 1) / 2; // 7→4, 5→3, 3→2
    const QPoint center = rect.center();
    const auto drawGlyph = [&](QColor color, QPoint offset) {
        painter->setPen(color);
        const int left = center.x() - (vertical ? base : depth) / 2 + offset.x();
        const int top = center.y() - (vertical ? depth : base) / 2 + offset.y();
        for (int step = 0; step < depth; ++step) {
            const int run = 1 + step * 2;
            switch (arrow) {
            case Qt::DownArrow: {
                const int width = base - step * 2;
                painter->drawLine(left + step, top + step,
                    left + step + width - 1, top + step);
                break;
            }
            case Qt::UpArrow:
                painter->drawLine(center.x() - run / 2 + offset.x(), top + step,
                    center.x() + run / 2 + offset.x(), top + step);
                break;
            case Qt::RightArrow: {
                const int height = base - step * 2;
                painter->drawLine(left + step, top + step,
                    left + step, top + step + height - 1);
                break;
            }
            case Qt::LeftArrow:
                painter->drawLine(left + step, center.y() - run / 2 + offset.y(),
                    left + step, center.y() + run / 2 + offset.y());
                break;
            default:
                return;
            }
        }
    };

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (!enabled) {
        drawGlyph(kHighlight3d, {1, 1});
    }
    drawGlyph(enabled ? kText : kShadow, {});
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

void DrawDisabledText(
    QPainter* painter, const QRect& rect, int flags, const QString& text, bool enabled,
    const QColor& normalColor = kText)
{
    painter->save();
    if (!enabled) {
        painter->setPen(kHighlight3d);
        painter->drawText(rect.translated(1, 1), flags, text);
        painter->setPen(kDisabledText);
    } else {
        painter->setPen(normalColor);
    }
    painter->drawText(rect, flags, text);
    painter->restore();
}

void DrawCheckMark(QPainter* painter, const QRect& rect, const QColor& color)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(color, 1));
    const QPoint center = rect.center();
    for (int offset = 0; offset < 2; ++offset) {
        painter->drawLine(center.x() - 4, center.y() + offset,
            center.x() - 1, center.y() + 3 + offset);
        painter->drawLine(center.x() - 1, center.y() + 3 + offset,
            center.x() + 5, center.y() - 3 + offset);
    }
    painter->restore();
}

bool KeepsOwnStyleSheet(const QWidget* widget)
{
    // 表示色ボタンは「色そのもの」が入力値なので、テーマ色で塗りつぶさない。
    return widget != nullptr && widget->property("displayColor").isValid();
}

QPixmap ClassicStandardPixmap(QStyle::StandardPixmap standardPixmap)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(kDarkShadow, 1));

    switch (standardPixmap) {
    case QStyle::SP_FileIcon: {
        const QPolygon page{{3, 1}, {10, 1}, {14, 5}, {14, 15}, {3, 15}};
        painter.setBrush(kWindow);
        painter.drawPolygon(page);
        painter.drawLine(10, 1, 10, 5);
        painter.drawLine(10, 5, 14, 5);
        painter.setPen(kShadow);
        painter.drawLine(5, 8, 12, 8);
        painter.drawLine(5, 10, 12, 10);
        painter.drawLine(5, 12, 10, 12);
        break;
    }
    case QStyle::SP_DirIcon:
    case QStyle::SP_DirOpenIcon:
    case QStyle::SP_DialogOpenButton: {
        painter.setBrush(kFolderShadow);
        painter.drawRect(1, 4, 13, 10);
        painter.setBrush(kFolder);
        painter.drawPolygon(QPolygon{{1, 5}, {6, 5}, {7, 3}, {14, 3}, {14, 12}, {1, 12}});
        if (standardPixmap != QStyle::SP_DirIcon) {
            painter.setBrush(QColor(0xFF, 0xFF, 0x80));
            painter.drawPolygon(QPolygon{{2, 7}, {15, 7}, {12, 14}, {0, 14}});
        }
        break;
    }
    case QStyle::SP_DialogSaveButton: {
        painter.setBrush(kSelection);
        painter.drawRect(1, 1, 14, 14);
        painter.setBrush(kWindow);
        painter.drawRect(4, 2, 7, 4);
        painter.setBrush(kFace);
        painter.drawRect(4, 9, 8, 6);
        painter.setBrush(kDarkShadow);
        painter.drawRect(9, 2, 2, 3);
        break;
    }
    case QStyle::SP_TrashIcon: {
        painter.setBrush(kLight);
        painter.drawPolygon(QPolygon{{4, 5}, {13, 5}, {12, 15}, {5, 15}});
        painter.setBrush(kFace);
        painter.drawRect(3, 3, 11, 2);
        painter.drawRect(6, 1, 5, 2);
        painter.drawLine(7, 7, 7, 13);
        painter.drawLine(10, 7, 10, 13);
        break;
    }
    case QStyle::SP_ArrowBack:
    case QStyle::SP_ArrowForward: {
        const bool back = standardPixmap == QStyle::SP_ArrowBack;
        painter.setPen(QPen(QColor(0x00, 0x80, 0x00), 2));
        painter.drawArc(QRect(3, 3, 10, 10), back ? 20 * 16 : 160 * 16, 220 * 16);
        painter.setBrush(QColor(0x00, 0x80, 0x00));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(back
                ? QPolygon{{1, 7}, {6, 3}, {6, 11}}
                : QPolygon{{15, 7}, {10, 3}, {10, 11}});
        break;
    }
    case QStyle::SP_DialogApplyButton:
        DrawCheckMark(&painter, pixmap.rect(), QColor(0x00, 0x80, 0x00));
        break;
    case QStyle::SP_DialogCancelButton:
    case QStyle::SP_TitleBarCloseButton:
        painter.setPen(QPen(kDarkShadow, 2));
        painter.drawLine(4, 4, 11, 11);
        painter.drawLine(11, 4, 4, 11);
        break;
    case QStyle::SP_DialogHelpButton:
        painter.setPen(QPen(kSelection, 2));
        painter.drawArc(QRect(4, 2, 8, 8), 0, 220 * 16);
        painter.drawLine(8, 9, 8, 11);
        painter.drawPoint(8, 14);
        break;
    case QStyle::SP_TitleBarNormalButton:
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(3, 5, 8, 7);
        painter.drawRect(6, 3, 7, 7);
        break;
    case QStyle::SP_TitleBarMinButton:
    case QStyle::SP_TitleBarShadeButton:
        painter.drawLine(4, 11, 12, 11);
        painter.drawLine(4, 12, 12, 12);
        break;
    case QStyle::SP_TitleBarMaxButton:
    case QStyle::SP_TitleBarUnshadeButton:
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(3, 3, 10, 10);
        painter.drawLine(4, 5, 12, 5);
        break;
    case QStyle::SP_ComputerIcon:
        painter.setBrush(QColor(0x00, 0x80, 0x80));
        painter.drawRect(1, 2, 13, 9);
        painter.setBrush(kFace);
        painter.drawRect(3, 4, 9, 5);
        painter.drawLine(7, 11, 7, 13);
        painter.drawLine(4, 14, 11, 14);
        break;
    default:
        return {};
    }
    return pixmap;
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
    // Windows 95 の一覧は縞表示ではない。通常行も交互行も COLOR_WINDOW。
    palette.setColor(QPalette::AlternateBase, kWindow);
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
    palette.setColor(QPalette::Disabled, QPalette::Base, kFace);
    palette.setColor(QPalette::Inactive, QPalette::Highlight, kShadow);
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, kWindow);
    return palette;
}

QFont Win95Style::Win95Font()
{
    // MS UI Gothic は日本語版 Windows 98 のシェルフォント。Windows 95 では
    // MS P Gothic が同時代の日本語UI用なので、こちらを先に選ぶ。
    const QStringList candidates = {
        QStringLiteral("ＭＳ Ｐゴシック"),
        QStringLiteral("MS PGothic"),
        QStringLiteral("ＭＳ ゴシック"),
        QStringLiteral("MS Gothic"),
        QStringLiteral("MS UI Gothic"),
        QStringLiteral("MS Sans Serif"),
        QStringLiteral("Meiryo UI"),
    };
    const QStringList families = QFontDatabase::families();
    for (const QString& candidate : candidates) {
        if (families.contains(candidate)) {
            QFont font(candidate, 9);
            font.setStyleStrategy(static_cast<QFont::StyleStrategy>(
                QFont::PreferBitmap | QFont::NoAntialias));
            return font;
        }
    }
    QFont fallback(QStringLiteral("sans-serif"), 9);
    fallback.setStyleStrategy(QFont::NoAntialias);
    return fallback;
}

void Win95Style::SuspendApplicationStyleSheets()
{
    for (QWidget* widget : QApplication::allWidgets()) {
        if (widget == nullptr || KeepsOwnStyleSheet(widget) || widget->styleSheet().isEmpty()) {
            continue;
        }
        if (!widget->property(kSavedStyleSheetProperty).isValid()) {
            widget->setProperty(kSavedStyleSheetProperty, widget->styleSheet());
        }
        widget->setStyleSheet({});
    }
}

void Win95Style::RestoreApplicationStyleSheets()
{
    for (QWidget* widget : QApplication::allWidgets()) {
        if (widget == nullptr) {
            continue;
        }
        const QVariant saved = widget->property(kSavedStyleSheetProperty);
        if (!saved.isValid()) {
            continue;
        }
        widget->setProperty(kSavedStyleSheetProperty, {});
        widget->setStyleSheet(saved.toString());
    }
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
    // 一覧・表・ツリーの地は白(COLOR_WINDOW)。Win95のエクスプローラと同じ。
    if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
        view->viewport()->setBackgroundRole(QPalette::Base);
        view->viewport()->setAutoFillBackground(true);
    }
}

QRect Win95Style::subControlRect(
    ComplexControl control,
    const QStyleOptionComplex* option,
    SubControl subControl,
    const QWidget* widget) const
{
    // Windows 95 では、スピンの上下ボタンもコンボの▼ボタンも幅は
    // SM_CXVSCROLL(=16px)で、沈んだ枠の内側いっぱいの高さに収まる。
    // 基底スタイル(Fusion)の配置のままだと、ボタンが低すぎて矢印がはみ出す。
    constexpr int kButtonWidth = 16;
    if (option == nullptr) {
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }
    const QRect inner = option->rect.adjusted(2, 2, -2, -2);
    if (inner.width() <= kButtonWidth + 4 || inner.height() < 6) {
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }
    const QRect buttons(
        inner.right() - kButtonWidth + 1, inner.top(), kButtonWidth, inner.height());
    switch (control) {
    case CC_SpinBox: {
        const int upHeight = inner.height() / 2;
        switch (subControl) {
        case SC_SpinBoxFrame:
            return option->rect;
        case SC_SpinBoxUp:
            return QRect(buttons.left(), buttons.top(), kButtonWidth, upHeight);
        case SC_SpinBoxDown:
            return QRect(buttons.left(), buttons.top() + upHeight, kButtonWidth,
                inner.height() - upHeight);
        case SC_SpinBoxEditField:
            return QRect(inner.left() + 1, inner.top(),
                buttons.left() - inner.left() - 2, inner.height());
        default:
            break;
        }
        break;
    }
    case CC_ComboBox: {
        switch (subControl) {
        case SC_ComboBoxFrame:
            return option->rect;
        case SC_ComboBoxArrow:
            return buttons;
        case SC_ComboBoxEditField:
            return QRect(inner.left() + 1, inner.top(),
                buttons.left() - inner.left() - 2, inner.height());
        case SC_ComboBoxListBoxPopup:
            return QRect(option->rect.left(), option->rect.bottom() + 1,
                option->rect.width(), 1);
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
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
    case PM_ButtonMargin:
        return 6;
    case PM_ButtonDefaultIndicator:
        return 1;
    case PM_MenuBarItemSpacing:
    case PM_MenuBarPanelWidth:
        return 0;
    case PM_MenuBarHMargin:
        return 0;
    case PM_MenuBarVMargin:
        return 1;
    case PM_MenuHMargin:
    case PM_MenuVMargin:
        return 2;
    case PM_MenuPanelWidth:
        return 2;
    case PM_SplitterWidth:
        return 4;
    case PM_ToolBarIconSize:
    case PM_SmallIconSize:
        return 16;
    case PM_ToolBarFrameWidth:
        return 2;
    case PM_ToolBarItemMargin:
        return 1;
    case PM_ToolBarItemSpacing:
        return 1;
    case PM_ToolBarSeparatorExtent:
        return 8;
    case PM_TabBarTabHSpace:
        return 12;
    case PM_TabBarTabVSpace:
        return 5;
    case PM_TabBarBaseOverlap:
    case PM_TabBarBaseHeight:
        return 2;
    case PM_ProgressBarChunkWidth:
        return 6;
    case PM_SliderLength:
        return 11;
    case PM_SliderThickness:
        return 16;
    case PM_DockWidgetTitleMargin:
    case PM_DockWidgetTitleBarButtonMargin:
        return 2;
    case PM_TitleBarHeight:
        return 18;
    case PM_ToolTipLabelFrameWidth:
        return 1;
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
    case SH_Menu_SubMenuPopupDelay:
        return 400;
    case SH_ItemView_ShowDecorationSelected:
        return 1;
    case SH_ScrollBar_MiddleClickAbsolutePosition:
        return 0;
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
    } else if (type == CT_ToolButton) {
        size.setHeight(std::max(size.height(), 22));
    } else if (type == CT_MenuBarItem) {
        size.setHeight(std::max(size.height(), 19));
    } else if (type == CT_MenuItem) {
        const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (menuItem != nullptr && menuItem->menuItemType == QStyleOptionMenuItem::Separator) {
            return QSize(std::max(size.width(), 10), 7);
        }
        size.setHeight(std::max(size.height(), 19));
    } else if (type == CT_TabBarTab) {
        size.setHeight(std::max(size.height(), 22));
    } else if (type == CT_ComboBox || type == CT_SpinBox || type == CT_LineEdit) {
        // 沈んだ枠2px + 文字16px + 余白。Windows 95 の入力欄は21px前後。
        size.setHeight(std::max(size.height(), 21));
    }
    return size;
}

QPixmap Win95Style::standardPixmap(
    StandardPixmap standardPixmap, const QStyleOption* option, const QWidget* widget) const
{
    const QPixmap classic = ClassicStandardPixmap(standardPixmap);
    return classic.isNull()
        ? QProxyStyle::standardPixmap(standardPixmap, option, widget)
        : classic;
}

QIcon Win95Style::standardIcon(
    StandardPixmap standardIcon, const QStyleOption* option, const QWidget* widget) const
{
    const QPixmap classic = ClassicStandardPixmap(standardIcon);
    return classic.isNull()
        ? QProxyStyle::standardIcon(standardIcon, option, widget)
        : QIcon(classic);
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
        // IE4以降のフラットツールバーではなく、Windows 95 標準の常時隆起ボタン。
        DrawWin95Edge(painter, option->rect,
            pressed ? EdgeStyle::ButtonSunken : EdgeStyle::ButtonRaised);
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
        DrawWin95Edge(painter, option->rect, EdgeStyle::FieldSunken);
        return;
    }
    case PE_FrameGroupBox:
        DrawWin95Edge(painter, option->rect, EdgeStyle::Grouping);
        return;
    case PE_FrameTabWidget:
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect, EdgeStyle::WindowRaised);
        return;
    case PE_FrameStatusBarItem:
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect, EdgeStyle::StatusSunken);
        return;
    case PE_IndicatorCheckBox: {
        QRect box = option->rect;
        box.setSize(QSize(13, 13));
        box.moveCenter(option->rect.center());
        painter->fillRect(box, enabled ? kWindow : kFace);
        DrawWin95Edge(painter, box, EdgeStyle::FieldSunken);
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
        painter->fillRect(option->rect, kFace);
        return;
    case PE_PanelToolBar:
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect, EdgeStyle::WindowRaised);
        return;
    case PE_PanelStatusBar:
        painter->fillRect(option->rect, kFace);
        painter->setPen(kHighlight3d);
        painter->drawLine(option->rect.topLeft(), option->rect.topRight());
        painter->setPen(kShadow);
        painter->drawLine(option->rect.left(), option->rect.top() + 1,
            option->rect.right(), option->rect.top() + 1);
        return;
    case PE_FrameMenu:
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect, EdgeStyle::WindowRaised);
        return;
    case PE_IndicatorBranch: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(kShadow, 1, Qt::DotLine));
        const int middleX = option->rect.center().x();
        const int middleY = option->rect.center().y();
        if (option->state.testFlag(State_Sibling)) {
            painter->drawLine(middleX, option->rect.top(), middleX, option->rect.bottom());
        } else if (option->state.testFlag(State_Item)) {
            painter->drawLine(middleX, option->rect.top(), middleX, middleY);
        }
        if (option->state.testFlag(State_Item)) {
            painter->drawLine(middleX, middleY, option->rect.right(), middleY);
        }
        if (option->state.testFlag(State_Children)) {
            const QRect box(middleX - 4, middleY - 4, 9, 9);
            painter->fillRect(box, kWindow);
            painter->setPen(kDarkShadow);
            painter->drawRect(box.adjusted(0, 0, -1, -1));
            painter->drawLine(box.left() + 2, middleY, box.right() - 2, middleY);
            if (!option->state.testFlag(State_Open)) {
                painter->drawLine(middleX, box.top() + 2, middleX, box.bottom() - 2);
            }
        }
        painter->restore();
        return;
    }
    case PE_IndicatorMenuCheckMark:
        DrawCheckMark(painter, option->rect, enabled ? kText : kDisabledText);
        return;
    case PE_IndicatorHeaderArrow:
        DrawWin95Arrow(painter, option->rect,
            option->state.testFlag(State_UpArrow) ? Qt::UpArrow : Qt::DownArrow,
            enabled);
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
        painter->fillRect(option->rect, kFace);
        if (selected) {
            DrawWin95Edge(painter, option->rect, EdgeStyle::ButtonSunken);
        }
        if (const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
            const QRect textRect = selected ? option->rect.translated(1, 1) : option->rect;
            DrawDisabledText(painter, textRect,
                Qt::AlignCenter | Qt::TextShowMnemonic, menuItem->text,
                option->state.testFlag(State_Enabled));
        }
        return;
    }
    case CE_MenuBarEmptyArea:
        painter->fillRect(option->rect, kFace);
        return;
    case CE_MenuItem: {
        const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (menuItem == nullptr) {
            break;
        }
        const bool enabled = option->state.testFlag(State_Enabled);
        const bool selected = option->state.testFlag(State_Selected);
        painter->fillRect(option->rect, selected ? kSelection : kFace);
        if (menuItem->menuItemType == QStyleOptionMenuItem::Separator) {
            const int y = option->rect.center().y();
            painter->setPen(kShadow);
            painter->drawLine(option->rect.left() + 2, y,
                option->rect.right() - 2, y);
            painter->setPen(kHighlight3d);
            painter->drawLine(option->rect.left() + 2, y + 1,
                option->rect.right() - 2, y + 1);
            return;
        }

        const int checkWidth = std::max(20, menuItem->maxIconWidth + 6);
        const QRect checkRect(option->rect.left() + 2, option->rect.top(),
            checkWidth - 2, option->rect.height());
        if (menuItem->checked) {
            DrawCheckMark(painter, checkRect,
                selected ? kSelectionText : (enabled ? kText : kDisabledText));
        } else if (!menuItem->icon.isNull()) {
            const QPixmap icon = menuItem->icon.pixmap(QSize(16, 16),
                enabled ? QIcon::Normal : QIcon::Disabled,
                option->state.testFlag(State_On) ? QIcon::On : QIcon::Off);
            painter->drawPixmap(checkRect.center() - QPoint(icon.width() / 2, icon.height() / 2),
                icon);
        }

        const int arrowWidth = menuItem->menuItemType == QStyleOptionMenuItem::SubMenu ? 16 : 4;
        QRect textRect = option->rect.adjusted(checkWidth + 2, 0, -arrowWidth - 4, 0);
        const int tab = menuItem->text.indexOf('\t');
        const QString label = tab >= 0 ? menuItem->text.left(tab) : menuItem->text;
        const QString shortcut = tab >= 0 ? menuItem->text.mid(tab + 1) : QString();
        const QColor textColor = selected ? kSelectionText : kText;
        DrawDisabledText(painter, textRect,
            Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
            label, enabled, textColor);
        if (!shortcut.isEmpty()) {
            DrawDisabledText(painter, textRect,
                Qt::AlignRight | Qt::AlignVCenter | Qt::TextShowMnemonic,
                shortcut, enabled, textColor);
        }
        if (menuItem->menuItemType == QStyleOptionMenuItem::SubMenu) {
            DrawWin95Arrow(painter,
                QRect(option->rect.right() - 15, option->rect.top(), 13, option->rect.height()),
                Qt::RightArrow, enabled);
        }
        return;
    }
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
    case CE_ToolButtonLabel: {
        const auto* button = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        if (button == nullptr) {
            break;
        }
        const bool pressed = option->state.testFlag(State_Sunken)
            || option->state.testFlag(State_On);
        if (widget != nullptr
            && widget->objectName() == QStringLiteral("collapsibleSectionHeader")) {
            QRect arrowRect(option->rect.left() + 5, option->rect.top(), 13,
                option->rect.height());
            QRect textRect = option->rect.adjusted(21, 0, -4, 0);
            if (pressed) {
                arrowRect.translate(1, 1);
                textRect.translate(1, 1);
            }
            DrawWin95Arrow(painter, arrowRect, button->arrowType,
                option->state.testFlag(State_Enabled));
            QFont titleFont = painter->font();
            titleFont.setBold(true);
            painter->setFont(titleFont);
            DrawDisabledText(painter, textRect,
                Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
                button->text, option->state.testFlag(State_Enabled));
            return;
        }
        QStyleOptionToolButton shifted = *button;
        if (pressed) {
            shifted.rect.translate(1, 1);
        }
        QProxyStyle::drawControl(element, &shifted, painter, widget);
        return;
    }
    case CE_TabBarTabShape: {
        const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
        if (tab == nullptr
            || (tab->shape != QTabBar::RoundedNorth
                && tab->shape != QTabBar::TriangularNorth)) {
            break;
        }
        const bool selected = option->state.testFlag(State_Selected);
        QRect rect = option->rect;
        if (!selected) {
            rect.adjust(0, 2, -1, -1);
        } else {
            rect.adjust(0, 0, 0, 2);
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        const QPolygon fill{{rect.left(), rect.bottom()}, {rect.left(), rect.top() + 2},
            {rect.left() + 2, rect.top()}, {rect.right() - 2, rect.top()},
            {rect.right(), rect.top() + 2}, {rect.right(), rect.bottom()}};
        painter->setPen(Qt::NoPen);
        painter->setBrush(kFace);
        painter->drawPolygon(fill);
        painter->setPen(kHighlight3d);
        painter->drawLine(rect.left(), rect.bottom(), rect.left(), rect.top() + 2);
        painter->drawLine(rect.left(), rect.top() + 2, rect.left() + 2, rect.top());
        painter->drawLine(rect.left() + 2, rect.top(), rect.right() - 2, rect.top());
        painter->setPen(kDarkShadow);
        painter->drawLine(rect.right(), rect.top() + 2, rect.right(), rect.bottom());
        painter->setPen(kShadow);
        painter->drawLine(rect.right() - 1, rect.top() + 2,
            rect.right() - 1, rect.bottom());
        if (!selected) {
            painter->drawLine(rect.left() + 1, rect.bottom(), rect.right(), rect.bottom());
        }
        painter->restore();
        return;
    }
    case CE_DockWidgetTitle: {
        const auto* dock = qstyleoption_cast<const QStyleOptionDockWidget*>(option);
        if (dock == nullptr) {
            break;
        }
        const bool active = option->state.testFlag(State_Active);
        painter->fillRect(option->rect.adjusted(2, 2, -2, -1),
            active ? kActiveCaption : kInactiveCaption);
        QFont titleFont = painter->font();
        titleFont.setBold(true);
        painter->setFont(titleFont);
        const QRect titleRect = subElementRect(SE_DockWidgetTitleBarText, option, widget)
            .adjusted(2, 0, -2, 0);
        const QString title = painter->fontMetrics().elidedText(
            dock->title, Qt::ElideRight, titleRect.width());
        painter->setPen(kWindow);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title);
        return;
    }
    case CE_HeaderSection: {
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect,
            option->state.testFlag(State_Sunken)
                ? EdgeStyle::ButtonSunken : EdgeStyle::ButtonRaised);
        return;
    }
    case CE_ProgressBarGroove:
        painter->fillRect(option->rect, kFace);
        DrawWin95Edge(painter, option->rect, EdgeStyle::FieldSunken);
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
    case CE_SizeGrip: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        for (int offset = 0; offset < 3; ++offset) {
            const int inset = 3 + offset * 4;
            painter->setPen(kHighlight3d);
            painter->drawLine(option->rect.right() - inset, option->rect.bottom() - 1,
                option->rect.right() - 1, option->rect.bottom() - inset);
            painter->setPen(kShadow);
            painter->drawLine(option->rect.right() - inset + 1, option->rect.bottom() - 1,
                option->rect.right() - 1, option->rect.bottom() - inset + 1);
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
            DrawWin95Edge(painter, rect,
                pressed ? EdgeStyle::ButtonSunken : EdgeStyle::ButtonRaised);
            DrawWin95Arrow(painter, pressed ? rect.translated(1, 1) : rect, arrow,
                slider->state.testFlag(State_Enabled));
        };
        drawButton(subLine, horizontal ? Qt::LeftArrow : Qt::UpArrow, SC_ScrollBarSubLine);
        drawButton(addLine, horizontal ? Qt::RightArrow : Qt::DownArrow, SC_ScrollBarAddLine);
        if (handle.isValid() && !handle.isEmpty()) {
            painter->fillRect(handle, kFace);
            DrawWin95Edge(painter, handle, EdgeStyle::ButtonRaised);
        }
        return;
    }
    case CC_Slider: {
        const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (slider == nullptr) {
            break;
        }
        if (slider->subControls.testFlag(SC_SliderTickmarks)) {
            QStyleOptionSlider tickOption = *slider;
            tickOption.subControls = SC_SliderTickmarks;
            QProxyStyle::drawComplexControl(control, &tickOption, painter, widget);
        }
        if (slider->subControls.testFlag(SC_SliderGroove)) {
            QRect groove = subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
            if (slider->orientation == Qt::Horizontal) {
                groove.setTop(groove.center().y() - 2);
                groove.setHeight(4);
            } else {
                groove.setLeft(groove.center().x() - 2);
                groove.setWidth(4);
            }
            painter->fillRect(groove, kWindow);
            DrawWin95Edge(painter, groove, EdgeStyle::StatusSunken);
        }
        if (slider->subControls.testFlag(SC_SliderHandle)) {
            const QRect handle = subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
            painter->fillRect(handle, kFace);
            DrawWin95Edge(painter, handle,
                slider->activeSubControls.testFlag(SC_SliderHandle)
                        && slider->state.testFlag(State_Sunken)
                    ? EdgeStyle::ButtonSunken : EdgeStyle::ButtonRaised);
        }
        return;
    }
    case CC_SpinBox: {
        const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
        if (spin == nullptr) {
            break;
        }
        const QRect frame = subControlRect(CC_SpinBox, spin, SC_SpinBoxFrame, widget);
        painter->fillRect(frame, spin->state.testFlag(State_Enabled) ? kWindow : kFace);
        DrawWin95Edge(painter, frame, EdgeStyle::FieldSunken);
        const QRect up = subControlRect(CC_SpinBox, spin, SC_SpinBoxUp, widget);
        const QRect down = subControlRect(CC_SpinBox, spin, SC_SpinBoxDown, widget);
        const auto drawSpinButton = [&](const QRect& rect, Qt::ArrowType arrow, SubControl which) {
            const bool pressed = spin->activeSubControls.testFlag(which)
                && spin->state.testFlag(State_Sunken);
            painter->fillRect(rect, kFace);
            DrawWin95Edge(painter, rect,
                pressed ? EdgeStyle::ButtonSunken : EdgeStyle::ButtonRaised);
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
        painter->fillRect(frame, combo->state.testFlag(State_Enabled) ? kWindow : kFace);
        DrawWin95Edge(painter, frame, EdgeStyle::FieldSunken);
        const QRect arrowRect = subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
        painter->fillRect(arrowRect, kFace);
        const bool pressed = combo->state.testFlag(State_Sunken);
        DrawWin95Edge(painter, arrowRect,
            pressed ? EdgeStyle::ButtonSunken : EdgeStyle::ButtonRaised);
        DrawWin95Arrow(painter, pressed ? arrowRect.translated(1, 1) : arrowRect,
            Qt::DownArrow, combo->state.testFlag(State_Enabled));
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}
