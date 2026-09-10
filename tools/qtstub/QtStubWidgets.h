#pragma once
//! Qt の当て木、部品の側。宣言だけ。
#include "QtStubGui.h"

#include <initializer_list>

//! 本物の QList のごく一部。初期化子リストから作れれば足りる。
template<class T>
class QList {
public:
    QList() = default;
    QList(std::initializer_list<T>) {}
};

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
    bool setProperty(const char*, const QVariant&);
    template<class Sender, class Signal, class Slot>
    static void connect(Sender, Signal, Slot) {}
    template<class Sender, class Signal, class Context, class Slot>
    static void connect(Sender, Signal, Context, Slot) {}
};

class QStyle;
class QLayout;

class QWidget : public QObject, public QPaintDevice {
public:
    QWidget() = default;
    explicit QWidget(QWidget*) {}
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] QRect rect() const;
    using QPaintDevice::width;
    [[nodiscard]] QSize size() const;
    [[nodiscard]] QPalette palette() const;
    [[nodiscard]] QFont font() const;
    [[nodiscard]] bool isVisible() const;
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
    void setFixedSize(int, int);
    void setWindowTitle(const QString&);
    [[nodiscard]] QString windowTitle() const;
    void setLayout(QLayout*);
    void setEnabled(bool);
    void setVisible(bool);
    [[nodiscard]] QWidget* parentWidget() const;
    void setToolTip(const QString&);
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
};

class QLayout : public QObject {
public:
    void addWidget(QWidget*);
    void setContentsMargins(int, int, int, int);
    void setSpacing(int);
};
class QBoxLayout : public QLayout {
public:
    void addStretch(int = 0);
    void addLayout(QLayout*, int = 0);
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
    [[nodiscard]] std::vector<QAction*> actions() const;
    [[nodiscard]] QString title() const;
};

class QMenuBar : public QWidget {
public:
    QMenu* addMenu(const QString&);
    void addAction(QAction*);
    [[nodiscard]] std::vector<QAction*> actions() const;
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
    [[nodiscard]] std::vector<QAction*> actions() const;
};

class QStatusBar : public QWidget {
public:
    void addWidget(QWidget*, int = 0);
    void addPermanentWidget(QWidget*, int = 0);
    void showMessage(const QString&, int = 0);
};

class QLabel : public QWidget {
public:
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

class QAbstractItemView : public QAbstractScrollArea {
public:
    enum SelectionMode { NoSelection, SingleSelection, MultiSelection, ExtendedSelection };
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
};

class QTreeWidget : public QAbstractItemView {
public:
    QTreeWidget() = default;
    explicit QTreeWidget(QWidget*) {}
    void setColumnCount(int);
    [[nodiscard]] int columnCount() const;
    void setHeaderLabels(const QStringList&);
    void setRootIsDecorated(bool);
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
    void (*itemClicked)(QTreeWidgetItem*, int);
    void (*itemChanged)(QTreeWidgetItem*, int);
};

class QDockWidget : public QWidget {
public:
    QDockWidget() = default;
    QDockWidget(const QString&, QWidget* = nullptr) {}
    void setWidget(QWidget*);
    void setMinimumHeight(int);
    void setMinimumWidth(int);
    [[nodiscard]] QWidget* widget() const;
    void setFeatures(int);
    void setAllowedAreas(Qt::DockWidgetAreas);
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
    void resizeDocks(const QList<QDockWidget*>&, const QList<int>&, Qt::Orientation);
    void addAction(QAction*);
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
    void (*currentIndexChanged)(int);
};

class QStackedWidget : public QWidget {
public:
    QStackedWidget() = default;
    explicit QStackedWidget(QWidget*) {}
    int addWidget(QWidget*);
    void setCurrentIndex(int);
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
};

class QAbstractSpinBox : public QWidget {
public:
    void setReadOnly(bool);
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

class QFormLayout : public QLayout {
public:
    QFormLayout() = default;
    explicit QFormLayout(QWidget*) {}
    void addRow(const QString&, QWidget*);
    void addRow(QWidget*);
    void setRowVisible(QWidget*, bool);
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

class QTabBar : public QWidget {
public:
    enum Shape { RoundedNorth, RoundedSouth, RoundedWest, RoundedEast,
        TriangularNorth, TriangularSouth, TriangularWest, TriangularEast };
};

class QCoreApplication : public QObject {
public:
    static void setApplicationName(const QString&);
    static QStringList arguments();
    static int exec();
    static void processEvents();
};

class QGuiApplication : public QCoreApplication {
public:
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
};
