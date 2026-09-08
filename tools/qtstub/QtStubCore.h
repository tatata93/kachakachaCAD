#pragma once
//! Qt の当て木。宣言だけ。-fsyntax-only 専用。実装は無い。
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

// Qt がマクロにしている語。本物と同じように空へ潰す。
// これで「slots という名前の変数」を雲の側でも捕まえられる。
#define signals public
#define slots
#define emit
#define Q_OBJECT
#define Q_UNUSED(x) (void)(x);
#define QStringLiteral(s) QString(s)
#define QT_VERSION_STR "6.9.2"

namespace Qt {
enum GlobalColor { white, black, transparent, gray, red, blue, green, darkGray, lightGray };
enum AlignmentFlagValue { AlignLeft = 1, AlignRight = 2, AlignHCenter = 4, AlignTop = 8,
    AlignBottom = 16, AlignVCenter = 32, AlignCenter = 36, TextSingleLine = 64,
    TextShowMnemonic = 128, TextHideMnemonic = 256, AlignAbsolute = 512 };
enum PenStyle { NoPen, SolidLine, DashLine, DotLine, DashDotLine };
enum BrushStyle { NoBrush, SolidPattern };
enum FocusPolicy { NoFocus, StrongFocus, ClickFocus, TabFocus, WheelFocus };
enum DockWidgetArea { LeftDockWidgetArea = 1, RightDockWidgetArea = 2,
    TopDockWidgetArea = 4, BottomDockWidgetArea = 8 };
enum Orientation { Horizontal = 1, Vertical = 2 };
enum ItemDataRole { DisplayRole = 0, DecorationRole = 1, UserRole = 32 };
enum MouseButton { NoButton = 0, LeftButton = 1, RightButton = 2, MiddleButton = 4 };
enum KeyboardModifier { NoModifier = 0, ShiftModifier = 1, ControlModifier = 2,
    AltModifier = 4 };
enum Key { Key_Escape = 1, Key_Return, Key_Enter, Key_Backspace, Key_Tab, Key_Backtab,
    Key_Space, Key_Delete, Key_Left, Key_Right, Key_Up, Key_Down };
enum ToolButtonStyle { ToolButtonIconOnly, ToolButtonTextOnly, ToolButtonTextBesideIcon,
    ToolButtonTextUnderIcon };
enum WindowType { Widget = 0, Window = 1 };
enum TransformationMode { FastTransformation, SmoothTransformation };
enum AspectRatioMode { IgnoreAspectRatio, KeepAspectRatio };
enum CaseSensitivity { CaseInsensitive, CaseSensitive };
enum ConnectionType { AutoConnection, DirectConnection, QueuedConnection };
enum ScrollBarPolicy { ScrollBarAsNeeded, ScrollBarAlwaysOff, ScrollBarAlwaysOn };
using Alignment = int;
using MouseButtons = int;
using KeyboardModifiers = int;
using WindowFlags = int;
using DockWidgetAreas = int;
} // namespace Qt

class QString;
class QStringList;
class QChar {
public:
    QChar() = default;
    QChar(char) {}
};

class QString {
public:
    QString() = default;
    QString(const char*) {}
    QString(const QChar*) {}
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] int length() const;
    [[nodiscard]] int size() const;
    [[nodiscard]] QString trimmed() const;
    [[nodiscard]] QString toUpper() const;
    [[nodiscard]] QString toLower() const;
    [[nodiscard]] std::string toStdString() const;
    [[nodiscard]] const char* toUtf8() const;
    [[nodiscard]] const char* toLocal8Bit() const;
    [[nodiscard]] bool contains(const QString&) const;
    void clear();
    [[nodiscard]] bool startsWith(const QString&) const;
    [[nodiscard]] bool endsWith(const QString&) const;
    [[nodiscard]] QString mid(int, int = -1) const;
    [[nodiscard]] QString left(int) const;
    [[nodiscard]] int toInt() const;
    [[nodiscard]] QStringList split(const QString&) const;
    [[nodiscard]] QString arg(const QString&) const;
    [[nodiscard]] QString arg(const QString&, const QString&) const;
    [[nodiscard]] QString arg(int) const;
    [[nodiscard]] QString arg(double, int = 0, char = 'g', int = -1) const;
    [[nodiscard]] QString arg(double, int, char, int, QChar) const;
    QString& append(const QString&);
    static QString number(int);
    static QString number(double, char = 'g', int = 6);
    static QString fromStdString(const std::string&);
    static QString fromUtf8(const char*, int = -1);
    static QString fromLocal8Bit(const char*, int = -1);
    static QString fromLatin1(const char*, int = -1);
    QString operator+(const QString&) const;
    QString& operator+=(const QString&);
    bool operator==(const QString&) const;
    bool operator!=(const QString&) const;
    bool operator<(const QString&) const;
};

class QStringList {
public:
    QStringList() = default;
    QStringList(std::initializer_list<QString>) {}
    [[nodiscard]] bool contains(const QString&) const;
    [[nodiscard]] int size() const;
    [[nodiscard]] int indexOf(const QString&) const;
    [[nodiscard]] QString at(int) const;
    [[nodiscard]] QString join(const QString&) const;
    QStringList& operator<<(const QString&);
    [[nodiscard]] const QString* begin() const;
    [[nodiscard]] const QString* end() const;
    QString& operator[](int);
    [[nodiscard]] const QString& operator[](int) const;
};

class QByteArray {
public:
    [[nodiscard]] const char* constData() const;
    [[nodiscard]] const char* data() const;
};

class QVariant {
public:
    QVariant() = default;
    template<class T> QVariant(const T&) {}
    template<class T> [[nodiscard]] T value() const { return T(); }
    [[nodiscard]] int toInt() const;
    [[nodiscard]] QString toString() const;
    [[nodiscard]] bool isValid() const;
};
