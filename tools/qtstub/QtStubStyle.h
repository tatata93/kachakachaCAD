#pragma once
//! Qt の当て木、見た目の側(QStyle 一式)。宣言だけ。
#include "QtStubWidgets.h"

class QStyleOption {
public:
    enum StyleOptionType { Type = 0 };
    enum StateFlag { State_None = 0, State_Enabled = 1, State_Raised = 2, State_Sunken = 4,
        State_On = 8, State_Off = 16, State_NoChange = 32, State_MouseOver = 64,
        State_HasFocus = 128, State_Selected = 256, State_Active = 512, State_Open = 1024,
        State_Children = 2048, State_Item = 4096, State_Sibling = 8192,
        State_Horizontal = 16384, State_Editing = 32768,
        State_UpArrow = 65536, State_DownArrow = 131072,
        State_KeyboardFocusChange = 262144, State_ReadOnly = 524288,
        State_Window = 1048576, State_Small = 2097152, State_Mini = 4194304 };
    class StateFlags {
    public:
        StateFlags() = default;
        StateFlags(int) {}
        [[nodiscard]] bool testFlag(StateFlag) const;
        StateFlags operator|(StateFlag) const;
        StateFlags operator&(StateFlag) const;
        explicit operator bool() const;
    };
    StateFlags state;
    QRect rect;
    QPalette palette;
    QFont fontMetricsFont;
    Qt::Orientation orientation = Qt::Horizontal;
    Qt::Alignment alignment = 0;
    int version = 1;
    int type = 0;
    [[nodiscard]] QFontMetrics fontMetrics() const;
    template<class T> [[nodiscard]] const T* qStyleOptionCast() const { return nullptr; }
};

class QStyleOptionButton : public QStyleOption {
public:
    enum ButtonFeature { None = 0, Flat = 1, HasMenu = 2, DefaultButton = 4,
        AutoDefaultButton = 8, CommandLinkButton = 16 };
    using ButtonFeatures = int;
    ButtonFeatures features = 0;
    QString text;
    QIcon icon;
    QSize iconSize;
};

class QStyleOptionFrame : public QStyleOption {
public:
    int lineWidth = 1;
    int midLineWidth = 0;
};

//! 本物では QStyle::SubControls(旗の束)である。testFlag が要る。
class QStubSubControlFlags {
public:
    QStubSubControlFlags() = default;
    QStubSubControlFlags(int) {}
    [[nodiscard]] bool testFlag(int) const;
    QStubSubControlFlags operator|(int) const;
    QStubSubControlFlags operator&(int) const;
    QStubSubControlFlags& operator&=(int);
    QStubSubControlFlags& operator|=(int);
    explicit operator bool() const;
    operator int() const;
};

class QStyleOptionComplex : public QStyleOption {
public:
    QStubSubControlFlags subControls;
    QStubSubControlFlags activeSubControls;
};

class QStyleOptionComboBox : public QStyleOptionComplex {
public:
    QString currentText;
    QIcon currentIcon;
    QSize iconSize;
    bool editable = false;
    bool frame = true;
    QRect popupRect;
};

class QStyleOptionSlider : public QStyleOptionComplex {
public:
    int minimum = 0;
    int maximum = 100;
    int sliderPosition = 0;
    int sliderValue = 0;
    int pageStep = 1;
    int singleStep = 1;
    bool upsideDown = false;
    Qt::Orientation orientation = Qt::Horizontal;
};

class QStyleOptionSpinBox : public QStyleOptionComplex {
public:
    int stepEnabled = 0;
    bool frame = true;
};

class QStyleOptionToolButton : public QStyleOptionComplex {
public:
    enum ToolButtonFeature { None = 0, Arrow = 1, Menu = 2, MenuButtonPopup = 4,
        PopupDelay = 8, HasMenu = 16 };
    using ToolButtonFeatures = int;
    ToolButtonFeatures features = 0;
    QString text;
    QIcon icon;
    QSize iconSize;
    Qt::ToolButtonStyle toolButtonStyle = Qt::ToolButtonIconOnly;
    Qt::ArrowType arrowType = Qt::NoArrow;
};

class QStyleOptionTab : public QStyleOption {
public:
    enum TabPosition { Beginning, Middle, End, OnlyOneTab };
    enum SelectedPosition { NotAdjacent, NextIsSelected, PreviousIsSelected };
    enum CornerWidget { NoCornerWidgets = 0 };
    TabPosition position = Beginning;
    SelectedPosition selectedPosition = NotAdjacent;
    QString text;
    QIcon icon;
    int shape = 0;
};

class QStyleOptionMenuItem : public QStyleOption {
public:
    enum MenuItemType { Normal, DefaultItem, Separator, SubMenu, Scroller, TearOff, Margin,
        EmptyArea };
    enum CheckType { NotCheckable, Exclusive, NonExclusive };
    MenuItemType menuItemType = Normal;
    CheckType checkType = NotCheckable;
    bool checked = false;
    bool menuHasCheckableItems = true;
    QString text;
    QIcon icon;
    QFont font;
    QRect menuRect;
    int maxIconWidth = 0;
};

class QStyleOptionGroupBox : public QStyleOptionComplex {
public:
    QString text;
    Qt::Alignment textAlignment = 0;
    QColor textColor;
    int lineWidth = 1;
    int midLineWidth = 0;
    int features = 0;
};

class QStyleOptionDockWidget : public QStyleOption {
public:
    QString title;
    bool closable = true;
    bool movable = true;
    bool floatable = true;
    bool verticalTitleBar = false;
};

class QStyleOptionHeader : public QStyleOption {
public:
    QString text;
    int section = 0;
};

class QStyleOptionViewItem : public QStyleOption {
public:
    QString text;
};

class QStyleOptionProgressBar : public QStyleOption {
public:
    int minimum = 0;
    int maximum = 100;
    int progress = 0;
    QString text;
};

class QStyleHintReturn;

class QStyle : public QObject {
public:
    // 本物の Qt では QStyleOption::state の型は QStyle::State である。
    // 当て木でも同じ型にしておかないと、片方で作った旗をもう片方へ渡せない。
    using StateFlag = QStyleOption::StateFlag;
    using enum QStyleOption::StateFlag;
    using State = QStyleOption::StateFlags;
    enum PrimitiveElement { PE_Frame, PE_FrameDefaultButton, PE_FrameDockWidget,
        PE_FrameFocusRect, PE_FrameGroupBox, PE_FrameLineEdit, PE_FrameMenu,
        PE_FrameStatusBarItem, PE_FrameTabWidget, PE_FrameWindow, PE_FrameButtonBevel,
        PE_FrameButtonTool, PE_FrameTabBarBase, PE_PanelButtonCommand, PE_PanelButtonBevel,
        PE_PanelButtonTool, PE_PanelMenuBar, PE_PanelToolBar, PE_PanelLineEdit,
        PE_IndicatorArrowDown, PE_IndicatorArrowLeft, PE_IndicatorArrowRight,
        PE_IndicatorArrowUp, PE_IndicatorBranch, PE_IndicatorButtonDropDown,
        PE_IndicatorItemViewItemCheck, PE_IndicatorCheckBox, PE_IndicatorDockWidgetResizeHandle,
        PE_IndicatorHeaderArrow, PE_IndicatorMenuCheckMark, PE_IndicatorProgressChunk,
        PE_IndicatorRadioButton, PE_IndicatorSpinDown, PE_IndicatorSpinMinus,
        PE_IndicatorSpinPlus, PE_IndicatorSpinUp, PE_IndicatorToolBarHandle,
        PE_IndicatorToolBarSeparator, PE_PanelTipLabel, PE_PanelScrollAreaCorner,
        PE_Widget, PE_PanelItemViewItem, PE_PanelItemViewRow, PE_PanelStatusBar,
        PE_CustomBase = 0xf000000 };
    enum ControlElement { CE_PushButton, CE_PushButtonBevel, CE_PushButtonLabel,
        CE_CheckBox, CE_CheckBoxLabel, CE_RadioButton, CE_RadioButtonLabel, CE_TabBarTab,
        CE_TabBarTabShape, CE_TabBarTabLabel, CE_ProgressBar, CE_ProgressBarGroove,
        CE_ProgressBarContents, CE_ProgressBarLabel, CE_MenuItem, CE_MenuScroller,
        CE_MenuVMargin, CE_MenuHMargin, CE_MenuTearoff, CE_MenuEmptyArea, CE_MenuBarItem,
        CE_MenuBarEmptyArea, CE_ToolButtonLabel, CE_Header, CE_HeaderSection, CE_HeaderLabel,
        CE_ToolBoxTab, CE_SizeGrip, CE_Splitter, CE_RubberBand, CE_DockWidgetTitle,
        CE_ScrollBarAddLine, CE_ScrollBarSubLine, CE_ScrollBarAddPage, CE_ScrollBarSubPage,
        CE_ScrollBarSlider, CE_ScrollBarFirst, CE_ScrollBarLast, CE_FocusFrame,
        CE_ComboBoxLabel, CE_ToolBar, CE_ShapedFrame, CE_ItemViewItem,
        CE_CustomBase = 0xf0000000 };
    enum ComplexControl { CC_SpinBox, CC_ComboBox, CC_ScrollBar, CC_Slider, CC_ToolButton,
        CC_TitleBar, CC_GroupBox, CC_Dial, CC_MdiControls, CC_CustomBase = 0xf0000000 };
    enum SubControl { SC_None = 0, SC_ScrollBarAddLine = 1, SC_ScrollBarSubLine = 2,
        SC_ScrollBarAddPage = 4, SC_ScrollBarSubPage = 8, SC_ScrollBarFirst = 16,
        SC_ScrollBarLast = 32, SC_ScrollBarSlider = 64, SC_ScrollBarGroove = 128,
        SC_SpinBoxUp = 256, SC_SpinBoxDown = 512, SC_SpinBoxFrame = 1024,
        SC_SpinBoxEditField = 2048, SC_ComboBoxFrame = 4096, SC_ComboBoxEditField = 8192,
        SC_ComboBoxArrow = 16384, SC_ComboBoxListBoxPopup = 32768, SC_SliderGroove = 65536,
        SC_SliderHandle = 131072, SC_SliderTickmarks = 262144, SC_ToolButton = 524288,
        SC_ToolButtonMenu = 1048576, SC_GroupBoxCheckBox = 2097152,
        SC_GroupBoxLabel = 4194304, SC_GroupBoxContents = 8388608,
        SC_GroupBoxFrame = 16777216, SC_All = 0xffffffff };
    using SubControls = int;
    enum PixelMetric { PM_ButtonMargin, PM_ButtonDefaultIndicator, PM_MenuButtonIndicator,
        PM_ButtonShiftHorizontal, PM_ButtonShiftVertical, PM_DefaultFrameWidth,
        PM_SpinBoxFrameWidth, PM_ComboBoxFrameWidth, PM_MaximumDragDistance,
        PM_ScrollBarExtent, PM_ScrollBarSliderMin, PM_SliderThickness, PM_SliderControlThickness,
        PM_SliderLength, PM_SliderTickmarkOffset, PM_SliderSpaceAvailable, PM_DockWidgetSeparatorExtent,
        PM_DockWidgetHandleExtent, PM_DockWidgetFrameWidth, PM_TabBarTabOverlap,
        PM_TabBarTabHSpace, PM_TabBarTabVSpace, PM_TabBarBaseHeight, PM_TabBarBaseOverlap,
        PM_ProgressBarChunkWidth, PM_SplitterWidth, PM_TitleBarHeight, PM_MenuScrollerHeight,
        PM_MenuHMargin, PM_MenuVMargin, PM_MenuPanelWidth, PM_MenuTearoffHeight,
        PM_MenuDesktopFrameWidth, PM_MenuBarPanelWidth, PM_MenuBarItemSpacing,
        PM_MenuBarVMargin, PM_MenuBarHMargin, PM_IndicatorWidth, PM_IndicatorHeight,
        PM_ExclusiveIndicatorWidth, PM_ExclusiveIndicatorHeight, PM_ToolBarFrameWidth,
        PM_ToolBarHandleExtent, PM_ToolBarItemSpacing, PM_ToolBarItemMargin,
        PM_ToolBarSeparatorExtent, PM_ToolBarExtensionExtent, PM_SmallIconSize,
        PM_LargeIconSize, PM_ToolBarIconSize,
        PM_DockWidgetTitleMargin, PM_DockWidgetTitleBarButtonMargin,
        PM_ToolTipLabelFrameWidth, PM_HeaderMargin, PM_HeaderMarkSize,
        PM_HeaderGripMargin, PM_TabCloseIndicatorWidth, PM_TabCloseIndicatorHeight,
        PM_ScrollView_ScrollBarSpacing, PM_ScrollView_ScrollBarOverlap,
        PM_SubMenuOverlap, PM_TreeViewIndentation, PM_TitleBarButtonSize,
        PM_TitleBarButtonIconSize, PM_LineEditIconSize, PM_LineEditIconMargin, PM_ListViewIconSize, PM_IconViewIconSize,
        PM_TabBarIconSize, PM_ButtonIconSize, PM_MessageBoxIconSize, PM_FocusFrameVMargin, PM_FocusFrameHMargin, PM_LayoutLeftMargin,
        PM_LayoutTopMargin, PM_LayoutRightMargin, PM_LayoutBottomMargin,
        PM_LayoutHorizontalSpacing, PM_LayoutVerticalSpacing, PM_CustomBase = 0xf0000000 };
    enum ContentsType { CT_PushButton, CT_CheckBox, CT_RadioButton, CT_ToolButton,
        CT_ComboBox, CT_Splitter, CT_ProgressBar, CT_MenuItem, CT_MenuBarItem, CT_MenuBar,
        CT_Menu, CT_TabBarTab, CT_Slider, CT_ScrollBar, CT_LineEdit, CT_SpinBox,
        CT_SizeGrip, CT_TabWidget, CT_DialogButtons, CT_HeaderSection, CT_GroupBox,
        CT_MdiControls, CT_ItemViewItem, CT_CustomBase = 0xf0000000 };
    enum StyleHint { SH_EtchDisabledText, SH_DitherDisabledText, SH_ScrollBar_MiddleClickAbsolutePosition,
        SH_ScrollBar_LeftClickAbsolutePosition, SH_MenuBar_AltKeyNavigation,
        SH_Menu_KeyboardSearch, SH_UnderlineShortcut, SH_ToolTipLabel_Opacity,
        SH_ComboBox_Popup, SH_Widget_Animate, SH_Menu_SubMenuPopupDelay,
        SH_TitleBar_NoBorder, SH_FocusFrame_AboveWidget,
        SH_Menu_MouseTracking, SH_MenuBar_MouseTracking, SH_ItemView_ShowDecorationSelected,
        SH_ItemView_ActivateItemOnSingleClick, SH_Table_GridLineColor,
        SH_Menu_Scrollable, SH_Menu_SloppySubMenus, SH_ToolBox_SelectedPageTitleBold,
        SH_Button_FocusPolicy, SH_ComboBox_ListMouseTracking,
        SH_CustomBase = 0xf0000000 };
    enum SubElement { SE_PushButtonContents, SE_PushButtonFocusRect, SE_CheckBoxIndicator,
        SE_CheckBoxContents, SE_CheckBoxFocusRect, SE_RadioButtonIndicator,
        SE_RadioButtonContents, SE_RadioButtonFocusRect, SE_ProgressBarGroove,
        SE_ProgressBarContents, SE_ProgressBarLabel, SE_ItemViewItemCheckIndicator,
        SE_ItemViewItemDecoration, SE_ItemViewItemText, SE_ItemViewItemFocusRect,
        SE_LineEditContents, SE_FrameContents, SE_DockWidgetCloseButton,
        SE_DockWidgetFloatButton, SE_DockWidgetTitleBarText, SE_DockWidgetIcon,
        SE_CustomBase = 0xf0000000 };
    enum StandardPixmap { SP_TitleBarCloseButton, SP_TitleBarNormalButton,
        SP_TitleBarMinButton, SP_TitleBarMaxButton, SP_TitleBarShadeButton,
        SP_TitleBarUnshadeButton, SP_DockWidgetCloseButton,
        SP_FileIcon, SP_DirIcon, SP_DirOpenIcon, SP_ComputerIcon, SP_TrashIcon,
        SP_ArrowBack, SP_ArrowForward,
        SP_DialogOpenButton, SP_DialogSaveButton, SP_DialogCancelButton,
        SP_DialogApplyButton, SP_DialogHelpButton,
        SP_CustomBase = 0xf0000000 };
    virtual QPixmap standardPixmap(StandardPixmap, const QStyleOption* = nullptr,
        const QWidget* = nullptr) const;
    virtual QIcon standardIcon(StandardPixmap, const QStyleOption* = nullptr,
        const QWidget* = nullptr) const;

    virtual void drawPrimitive(PrimitiveElement, const QStyleOption*, QPainter*,
        const QWidget* = nullptr) const;
    virtual void drawControl(ControlElement, const QStyleOption*, QPainter*,
        const QWidget* = nullptr) const;
    virtual void drawComplexControl(ComplexControl, const QStyleOptionComplex*, QPainter*,
        const QWidget* = nullptr) const;
    virtual int pixelMetric(PixelMetric, const QStyleOption* = nullptr,
        const QWidget* = nullptr) const;
    virtual int styleHint(StyleHint, const QStyleOption* = nullptr,
        const QWidget* = nullptr, QStyleHintReturn* = nullptr) const;
    virtual QSize sizeFromContents(ContentsType, const QStyleOption*, const QSize&,
        const QWidget* = nullptr) const;
    virtual QRect subElementRect(SubElement, const QStyleOption*,
        const QWidget* = nullptr) const;
    virtual QRect subControlRect(ComplexControl, const QStyleOptionComplex*, SubControl,
        const QWidget* = nullptr) const;
    virtual void polish(QWidget*);
    virtual void polish(QPalette&);
    virtual void unpolish(QWidget*);
    [[nodiscard]] QPalette standardPalette() const;
};

class QCommonStyle : public QStyle {};

class QProxyStyle : public QCommonStyle {
public:
    QProxyStyle() = default;
    explicit QProxyStyle(QStyle*) {}
    explicit QProxyStyle(const QString&) {}
    [[nodiscard]] QStyle* baseStyle() const;
    void setBaseStyle(QStyle*);
};

class QStyleFactory {
public:
    static QStyle* create(const QString&);
    static QStringList keys();
};

template<class T> T qstyleoption_cast(const QStyleOption*) { return T(); }
template<class T> T qobject_cast(QObject*) { return T(); }
