#pragma once
//! Qt の当て木、部品の側。宣言だけ。
#include "QtStubGui.h"

#include <initializer_list>

//! QList は QtStubCore.h にある。ここで作り直さない。

class QObject {
public:
    QObject() = default;
    explicit QObject(QObject*) {}
    virtual ~QObject() = default;
    void setObjectName(const QString&);
    [[nodiscard]] QString objectName() const;
    [[nodiscard]] QObject* parent() const;
    void setParent(QObject*);
    [[nodiscard]] QVariant property(const char*) const;
    bool blockSignals(bool);
    void setBackgroundRole(QPalette::ColorRole);
    void setAutoFillBackground(bool);
    void setFocus();
    void setFocus(int);
    void setCursor(Qt::CursorShape);
    void setCursor(const QCursor&);
    void unsetCursor();
    void raise();
    void lower();
    void installEventFilter(QObject*);
    void removeEventFilter(QObject*);
    virtual bool eventFilter(QObject*, QEvent*);
    virtual bool event(QEvent*);
    template<class T> [[nodiscard]] QList<T> findChildren() const { return QList<T>{}; }
    bool setProperty(const char*, const QVariant&);
    template<class Sender, class Signal, class Slot>
    static void connect(Sender, Signal, Slot) {}
    template<class Sender, class Signal, class Context, class Slot>
    static void connect(Sender, Signal, Context, Slot) {}
    template<class Sender, class Signal, class Context, class Slot>
    static void connect(Sender, Signal, Context, Slot, Qt::ConnectionType) {}
};

class QStyle;
class QLayout;
class QMenu;

class QSizePolicy {
public:
    enum Policy { Fixed, Minimum, Maximum, Preferred, Expanding, MinimumExpanding, Ignored };
};

class QSignalBlocker {
public:
    explicit QSignalBlocker(QObject*) {}
    ~QSignalBlocker() = default;
};

class QWidget : public QObject, public QPaintDevice {
public:
    QWidget() = default;
    explicit QWidget(QWidget*) {}
    void setSizePolicy(QSizePolicy::Policy, QSizePolicy::Policy);
    [[nodiscard]] QPoint mapTo(const QWidget*, const QPoint&) const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] QRect rect() const;
    using QPaintDevice::width;
    [[nodiscard]] QSize size() const;
    [[nodiscard]] QPalette palette() const;
    [[nodiscard]] QFont font() const;
    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] bool isHidden() const;
    [[nodiscard]] QWidget* window();
    [[nodiscard]] QPixmap grab();
    [[nodiscard]] double devicePixelRatioF() const;
    void setPalette(const QPalette&);
    void setFont(const QFont&);
    void setStyleSheet(const QString&);
    [[nodiscard]] QString styleSheet() const;
    void setStyle(QStyle*);
    void setMouseTracking(bool);
    void setFocusPolicy(Qt::FocusPolicy);
    void setMinimumSize(int, int);
    void setMinimumWidth(int);
    void setMinimumHeight(int);
    void setMaximumHeight(int);
    void setFixedSize(int, int);
    void setFixedWidth(int);
    void setWindowTitle(const QString&);
    [[nodiscard]] QString windowTitle() const;
    void setLayout(QLayout*);
    void setEnabled(bool);
    [[nodiscard]] bool isEnabled() const;
    void setVisible(bool);
    [[nodiscard]] QWidget* parentWidget() const;
    void setToolTip(const QString&);
    [[nodiscard]] QString toolTip() const;
    [[nodiscard]] QRect geometry() const;
    void resize(int, int);
    void resize(const QSize&);
    void show();
    void hide();
    bool close();
    void update();
    void repaint();
    void render(QPaintDevice*);
    void setAttribute(int, bool = true);
    virtual void paintEvent(QPaintEvent*);
    virtual void mouseMoveEvent(QMouseEvent*);
    virtual void mousePressEvent(QMouseEvent*);
    virtual void mouseReleaseEvent(QMouseEvent*);
    virtual void wheelEvent(QWheelEvent*);
    virtual void keyPressEvent(QKeyEvent*);
    virtual void keyReleaseEvent(QKeyEvent*);
    virtual void resizeEvent(QResizeEvent*);
    void setContextMenuPolicy(Qt::ContextMenuPolicy);
    [[nodiscard]] QPoint mapToGlobal(const QPoint&) const;
    virtual bool focusNextPrevChild(bool);
    void customContextMenuRequested(const QPoint&);
    virtual void focusOutEvent(QFocusEvent*);
};

class QLayout : public QObject {
public:
    void addWidget(QWidget*);
    void addWidget(QWidget*, int);
    void removeWidget(QWidget*);
    void setContentsMargins(int, int, int, int);
    void setSpacing(int);
};
class QBoxLayout : public QLayout {
public:
    void addStretch(int = 0);
    void addLayout(QLayout*, int = 0);
    void insertWidget(int, QWidget*, int = 0);
};
class QGridLayout : public QLayout {
public:
    QGridLayout() = default;
    explicit QGridLayout(QWidget*) {}
    void addWidget(QWidget*, int, int);
    void setHorizontalSpacing(int);
    void setVerticalSpacing(int);
};
class QVBoxLayout : public QBoxLayout {
public:
    QVBoxLayout() = default;
    explicit QVBoxLayout(QWidget*) {}
};
class QHBoxLayout : public QBoxLayout {
public:
    QHBoxLayout() = default;
    explicit QHBoxLayout(QWidget*) {}
};

class QAction : public QObject {
public:
    [[nodiscard]] QMenu* menu() const;
    QAction() = default;
    explicit QAction(QObject*) {}
    QAction(const QString&, QObject*) {}
    void setText(const QString&);
    [[nodiscard]] QString text() const;
    void setShortcut(const QKeySequence&);
    [[nodiscard]] QKeySequence shortcut() const;
    void setCheckable(bool);
    void setChecked(bool);
    [[nodiscard]] bool isChecked() const;
    void setEnabled(bool);
    [[nodiscard]] bool isEnabled() const;
    void setVisible(bool);
    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] bool isSeparator() const;
    [[nodiscard]] bool isCheckable() const;
    void setToolTip(const QString&);
    [[nodiscard]] QString toolTip() const;
    void setStatusTip(const QString&);
    void setIcon(const QIcon&);
    void setData(const QVariant&);
    [[nodiscard]] QVariant data() const;
    void trigger();
    void (*triggered)(bool);
};

class QMenu : public QWidget {
public:
    QMenu() = default;
    explicit QMenu(QWidget*) {}
    QMenu(const QString&, QWidget*) {}
    QAction* addAction(const QString&);
    void addAction(QAction*);
    template<class Receiver, class Slot>
    QAction* addAction(const QString&, Receiver, Slot) { return nullptr; }
    QAction* addSeparator();
    QMenu* addMenu(const QString&);
    [[nodiscard]] bool isEmpty() const;
    QAction* exec(const QPoint&);
    [[nodiscard]] QList<QAction*> actions() const;
    [[nodiscard]] QString title() const;
    QAction* addSection(const QString&);
};

class QMenuBar : public QWidget {
public:
    [[nodiscard]] QRect actionGeometry(QAction*) const;
    [[nodiscard]] QAction* activeAction() const;
    [[nodiscard]] QAction* actionAt(const QPoint&) const;
    [[nodiscard]] QFontMetrics fontMetrics() const;
    QMenu* addMenu(const QString&);
    void addAction(QAction*);
    [[nodiscard]] QList<QAction*> actions() const;
};

class QToolBar : public QWidget {
public:
    QToolBar() = default;
    explicit QToolBar(QWidget*) {}
    QToolBar(const QString&, QWidget*) {}
    void addAction(QAction*);
    QAction* addAction(const QString&);
    QAction* addSeparator();
    QAction* addWidget(QWidget*);
    void setToolButtonStyle(Qt::ToolButtonStyle);
    void setMovable(bool);
    void setOrientation(Qt::Orientation);
    [[nodiscard]] QList<QAction*> actions() const;
};

class QStatusBar : public QWidget {
public:
    void addWidget(QWidget*, int = 0);
    void addPermanentWidget(QWidget*, int = 0);
    void showMessage(const QString&, int = 0);
};

class QLabel : public QWidget {
public:
    void clear();
    QLabel() = default;
    explicit QLabel(QWidget*) {}
    QLabel(const QString&, QWidget* = nullptr) {}
    void setText(const QString&);
    [[nodiscard]] QString text() const;
    void setAlignment(Qt::Alignment);
    void setWordWrap(bool);
};

class QListWidgetItem {
public:
    QListWidgetItem() = default;
    explicit QListWidgetItem(const QString&) {}
    [[nodiscard]] QString text() const;
    void setText(const QString&);
    void setForeground(const QColor&);
};

class QFrame : public QWidget {
public:
    enum Shape { NoFrame, Box, Panel, WinPanel, HLine, VLine, StyledPanel };
    enum Shadow { Plain, Raised, Sunken };
    void setFrameShape(Shape);
    void setFrameShadow(Shadow);
    void setLineWidth(int);
};

class QAbstractScrollArea : public QFrame {
public:
    void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy);
    void setVerticalScrollBarPolicy(Qt::ScrollBarPolicy);
    [[nodiscard]] QWidget* viewport() const;
};

class QScrollArea : public QAbstractScrollArea {
public:
    QScrollArea() = default;
    explicit QScrollArea(QWidget*) {}
    void setWidget(QWidget*);
    void setWidgetResizable(bool);
};

class QAbstractItemView : public QAbstractScrollArea {
public:
    enum SelectionMode { NoSelection, SingleSelection, MultiSelection, ExtendedSelection };
    enum DragDropMode { NoDragDrop, DragOnly, DropOnly, DragDrop, InternalMove };
    void setSelectionMode(SelectionMode);
    void setAlternatingRowColors(bool);
};

class QListWidget : public QAbstractItemView {
public:
    QListWidget() = default;
    explicit QListWidget(QWidget*) {}
    void addItem(const QString&);
    void addItem(QListWidgetItem*);
    void clear();
    [[nodiscard]] int count() const;
    [[nodiscard]] QListWidgetItem* item(int) const;
    QListWidgetItem* takeItem(int);
    [[nodiscard]] QListWidgetItem* currentItem() const;
};

class QTreeWidgetItem {
public:
    void setIcon(int, const QIcon&);
    [[nodiscard]] QIcon icon(int) const;
    void setFont(int, const QFont&);
    [[nodiscard]] QFont font(int) const;
    QTreeWidgetItem() = default;
    explicit QTreeWidgetItem(class QTreeWidget*) {}
    explicit QTreeWidgetItem(QTreeWidgetItem*) {}
    void setText(int, const QString&);
    [[nodiscard]] QString text(int) const;
    void setForeground(int, const QColor&);
    void setData(int, int, const QVariant&);
    [[nodiscard]] QVariant data(int, int) const;
    void addChild(QTreeWidgetItem*);
    [[nodiscard]] int childCount() const;
    [[nodiscard]] QTreeWidgetItem* child(int) const;
    void setExpanded(bool);
    void setFlags(Qt::ItemFlags);
    [[nodiscard]] Qt::ItemFlags flags() const;
    void setCheckState(int, Qt::CheckState);
    [[nodiscard]] Qt::CheckState checkState(int) const;
    [[nodiscard]] QTreeWidgetItem* parent() const;
    void setToolTip(int, const QString&);
    void setSelected(bool);
    [[nodiscard]] bool isSelected() const;
    [[nodiscard]] bool isHidden() const;
    void setHidden(bool);
    void (*customContextMenuRequested)(const QPoint&);
};

class QHeaderView : public QWidget {
public:
    enum ResizeMode { Interactive, Stretch, Fixed, ResizeToContents };
    void setSectionResizeMode(int, ResizeMode);
    void setSectionResizeMode(ResizeMode);
    void setStretchLastSection(bool);
};

class QTreeWidget : public QAbstractItemView {
public:
    [[nodiscard]] QHeaderView* header() const;
    QTreeWidget() = default;
    explicit QTreeWidget(QWidget*) {}
    void setColumnCount(int);
    [[nodiscard]] int columnCount() const;
    void setHeaderLabels(const QStringList&);
    void setRootIsDecorated(bool);
    void setHeaderHidden(bool);
    void setIndentation(int);
    void setColumnHidden(int, bool);
    void addTopLevelItem(QTreeWidgetItem*);
    void clear();
    [[nodiscard]] int topLevelItemCount() const;
    [[nodiscard]] QTreeWidgetItem* topLevelItem(int) const;
    [[nodiscard]] QTreeWidgetItem* currentItem() const;
    void setCurrentItem(QTreeWidgetItem*);
    [[nodiscard]] int indexOfTopLevelItem(QTreeWidgetItem*) const;
    void editItem(QTreeWidgetItem*, int = 0);
    void expandAll();
    void resizeColumnToContents(int);
    [[nodiscard]] QList<QTreeWidgetItem*> selectedItems() const;
    void clearSelection();
    void scrollToItem(QTreeWidgetItem*);
    [[nodiscard]] QTreeWidgetItem* itemAt(const QPoint&) const;
    void setDragEnabled(bool);
    void setAcceptDrops(bool);
    void setDropIndicatorShown(bool);
    void setDragDropMode(DragDropMode);
    void (*itemClicked)(QTreeWidgetItem*, int);
    void (*itemChanged)(QTreeWidgetItem*, int);
    void (*itemSelectionChanged)();
    void (*customContextMenuRequested)(const QPoint&);

protected:
    //! 本物では仮想。当て木でも仮想にしておかないと、
    //! 引きずって落とす受け口の付け替えが型検査を通らない。
    virtual void dropEvent(class QDropEvent*);
};

class QDockWidget : public QWidget {
public:
    enum DockWidgetFeature { NoDockWidgetFeatures = 0, DockWidgetClosable = 1,
        DockWidgetMovable = 2, DockWidgetFloatable = 4 };
    QDockWidget() = default;
    QDockWidget(const QString&, QWidget* = nullptr) {}
    void setWidget(QWidget*);
    void setMinimumHeight(int);
    void setMinimumWidth(int);
    [[nodiscard]] QWidget* widget() const;
    void setFeatures(int);
    void setAllowedAreas(Qt::DockWidgetAreas);
    //! 表示メニューの「手順の一覧」に使う(出し隠しの QAction)。
    [[nodiscard]] QAction* toggleViewAction() const;
};

namespace Qt {
enum ToolBarArea { LeftToolBarArea = 1, RightToolBarArea = 2, TopToolBarArea = 4,
    BottomToolBarArea = 8 };
}

class QMainWindow : public QWidget {
public:
    QMainWindow() = default;
    explicit QMainWindow(QWidget*) {}
    QMenuBar* menuBar();
    QStatusBar* statusBar();
    void setCentralWidget(QWidget*);
    [[nodiscard]] QWidget* centralWidget() const;
    void addToolBar(QToolBar*);
    QToolBar* addToolBar(const QString&);
    void addToolBarBreak();
    void addToolBar(Qt::ToolBarArea, QToolBar*);
    void addDockWidget(Qt::DockWidgetArea, QDockWidget*);
    void tabifyDockWidget(QDockWidget*, QDockWidget*);
    void splitDockWidget(QDockWidget*, QDockWidget*, Qt::Orientation);
    void resizeDocks(const QList<QDockWidget*>&, const QList<int>&, Qt::Orientation);
    void addAction(QAction*);
};

class QColorDialog : public QWidget {
public:
    [[nodiscard]] static QColor getColor(const QColor& initial = QColor(),
        QWidget* parent = nullptr, const QString& title = QString());
};

class QFileDialog : public QWidget {
public:
    [[nodiscard]] static QString getSaveFileName(QWidget* parent = nullptr,
        const QString& caption = QString(), const QString& directory = QString(),
        const QString& filter = QString());
    [[nodiscard]] static QString getOpenFileName(QWidget* parent = nullptr,
        const QString& caption = QString(), const QString& directory = QString(),
        const QString& filter = QString());
};

//! ダイアログ一式。作図の選択肢を並べるのに要る。
//! 本物と同じ入れ子と名前空間にしておくこと。ずれると MSVC でだけ落ちる。
class QDialog : public QWidget {
public:
    QDialog() = default;
    explicit QDialog(QWidget*) {}
    enum DialogCode { Rejected = 0, Accepted = 1 };
    int exec();
    void accept();
    void reject();
    void setWindowTitle(const QString&);
    void setModal(bool);
    [[nodiscard]] int result() const;
};

class QAbstractButton : public QWidget {
public:
    void setText(const QString&);
    [[nodiscard]] QString text() const;
    void setChecked(bool);
    [[nodiscard]] bool isChecked() const;
    void setCheckable(bool);
    void click();
    void (*clicked)(bool);
    void (*toggled)(bool);
};

class QPushButton : public QAbstractButton {
public:
    QPushButton() = default;
    explicit QPushButton(QWidget*) {}
    explicit QPushButton(const QString&, QWidget* = nullptr) {}
    void setDefault(bool);
};

class QCheckBox : public QAbstractButton {
public:
    QCheckBox() = default;
    explicit QCheckBox(QWidget*) {}
    explicit QCheckBox(const QString&, QWidget* = nullptr) {}
    void (*stateChanged)(int);
};

class QComboBox : public QWidget {
public:
    enum SizeAdjustPolicy { AdjustToContents, AdjustToContentsOnFirstShow, AdjustToMinimumContentsLengthWithIcon };
    void setSizeAdjustPolicy(SizeAdjustPolicy);
    void setMinimumContentsLength(int);
    void setMaximumWidth(int);
    QComboBox() = default;
    explicit QComboBox(QWidget*) {}
    void addItem(const QString&);
    void addItem(const QString&, const QVariant&);
    void clear();
    [[nodiscard]] int count() const;
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int);
    [[nodiscard]] QString currentText() const;
    [[nodiscard]] QString itemText(int) const;
    void setItemText(int, const QString&);
    void removeItem(int);
    void (*currentIndexChanged)(int);
};

class QStackedWidget : public QWidget {
public:
    QStackedWidget() = default;
    explicit QStackedWidget(QWidget*) {}
    int addWidget(QWidget*);
    void setCurrentIndex(int);
    void setCurrentWidget(QWidget*);
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] int count() const;
};

class QLineEdit : public QWidget {
public:
    QLineEdit() = default;
    explicit QLineEdit(QWidget*) {}
    explicit QLineEdit(const QString&, QWidget* = nullptr) {}
    void setText(const QString&);
    [[nodiscard]] QString text() const;
    void setPlaceholderText(const QString&);
    void (*textChanged)(const QString&);
    void setClearButtonEnabled(bool);
};

class QAbstractSpinBox : public QWidget {
public:
    // xyz の欄は幅を詰めるため上下ボタンを消す(setButtonSymbols)。実 Qt と同じ enum 名。
    enum ButtonSymbols { UpDownArrows, PlusMinus, NoButtons };
    void setReadOnly(bool);
    void interpretText();
    void setButtonSymbols(ButtonSymbols);
    void selectAll();
};

class QDoubleSpinBox : public QAbstractSpinBox {
public:
    QDoubleSpinBox() = default;
    explicit QDoubleSpinBox(QWidget*) {}
    void setRange(double, double);
    void setDecimals(int);
    void setSingleStep(double);
    void setSuffix(const QString&);
    void setValue(double);
    [[nodiscard]] double value() const;
    void (*valueChanged)(double);
};

class QSpinBox : public QAbstractSpinBox {
public:
    QSpinBox() = default;
    explicit QSpinBox(QWidget*) {}
    void setRange(int, int);
    void setSingleStep(int);
    void setSuffix(const QString&);
    void setValue(int);
    [[nodiscard]] int value() const;
    void (*valueChanged)(int);
};

class QSlider : public QWidget {
public:
    QSlider() = default;
    explicit QSlider(Qt::Orientation, QWidget* = nullptr) {}
    void setRange(int, int);
    void setValue(int);
    [[nodiscard]] int value() const;
    void (*valueChanged)(int);
    void (*sliderReleased)();
};

class QFormLayout : public QLayout {
public:
    // 380px の棚で欄がはみ出さないよう、長い行は折り返し、伸ばせる欄は伸ばす
    // (setRowWrapPolicy / setFieldGrowthPolicy)。実 Qt と同じ enum 名。
    enum RowWrapPolicy { DontWrapRows, WrapLongRows, WrapAllRows };
    enum FieldGrowthPolicy { FieldsStayAtSizeHint, ExpandingFieldsGrow, AllNonFixedFieldsGrow };
    QFormLayout() = default;
    explicit QFormLayout(QWidget*) {}
    void addRow(const QString&, QWidget*);
    void addRow(QWidget*, QWidget*);
    void addRow(QWidget*);
    void setRowVisible(QWidget*, bool);
    void setRowWrapPolicy(RowWrapPolicy);
    void setFieldGrowthPolicy(FieldGrowthPolicy);
};

class QDialogButtonBox : public QWidget {
public:
    enum StandardButton { Ok = 0x0400, Cancel = 0x0040 };
    QDialogButtonBox() = default;
    explicit QDialogButtonBox(QWidget*) {}
    QDialogButtonBox(int, QWidget* = nullptr) {}
    void (*accepted)();
    void (*rejected)();
};

class QToolButton : public QAbstractButton {
public:
    enum ToolButtonPopupMode { DelayedPopup, MenuButtonPopup, InstantPopup };
    QToolButton() = default;
    explicit QToolButton(QWidget*) {}
    void setPopupMode(ToolButtonPopupMode);
    void setMenu(QMenu*);
    void setToolTip(const QString&);
    void setAutoRaise(bool);
    void setToolButtonStyle(Qt::ToolButtonStyle);
    void setDefaultAction(QAction*);
    [[nodiscard]] QAction* defaultAction() const;
};

class QTabBar : public QWidget {
public:
    enum Shape { RoundedNorth, RoundedSouth, RoundedWest, RoundedEast,
        TriangularNorth, TriangularSouth, TriangularWest, TriangularEast };
};

class QTabWidget : public QWidget {
public:
    QTabWidget() = default;
    explicit QTabWidget(QWidget*) {}
    int addTab(QWidget*, const QString&);
    [[nodiscard]] int count() const;
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int);
    void setDocumentMode(bool);
    [[nodiscard]] QWidget* widget(int) const;
    void setTabText(int, const QString&);
};

class QCoreApplication : public QObject {
public:
    static void setApplicationName(const QString&);
    static QStringList arguments();
    static int exec();
    static void processEvents();
    static bool sendEvent(QObject*, QEvent*);
    static QWidget* focusWidget();
};

//! 本物の Qt の qApp に当たるもの。当て木では唯一の実体を1つ返す。
//! 使うのは QObject の口(installEventFilter)だけなので、基底で足りる。
[[nodiscard]] QCoreApplication* QtStubApplication();

#define qApp (QtStubApplication())

class QGuiApplication : public QCoreApplication {
public:
    static QClipboard* clipboard();
    static void setPalette(const QPalette&);
    static QPalette palette();
    static void setFont(const QFont&);
};

class QStyle;
class QApplication : public QGuiApplication {
public:
    QApplication(int&, char**) {}
    static void setStyle(QStyle*);
    static QStyle* style();
    static void setPalette(const QPalette&);
    static void setFont(const QFont&);
    static QPalette palette();
    static QFont font();
    static void processEvents();
    static int exec();
    static QWidget* activeWindow();
    static std::vector<QWidget*> topLevelWidgets();
    static std::vector<QWidget*> allWidgets();
    static bool sendEvent(QObject*, QEvent*);
    static QWidget* focusWidget();
};

//! 本物の Qt の qApp に当たるもの。当て木では唯一の実体を1つ返す。
//! 使うのは QObject の口(installEventFilter)だけなので、基底で足りる。
[[nodiscard]] QCoreApplication* QtStubApplication();

#define qApp (QtStubApplication())
