#pragma once

//! 面取りの棚(V1 の「面取り」欄、走査 §1-16)。
//!
//! 加工種類(C面取り / R丸め)、直線 A / B(選んだ順)、A・B の残す側、
//! A の切戻し / B の切戻し(非対称)、半径、ポリラインの角(頂点番号で 1 つだけ)。
//!
//! 量(A の切戻し / 半径)は「数の棚」の「面取り量 / 丸め半径」と同じ値を映す
//! (二重に持たない)。ここで打つと数の棚へ流し、数の棚で打つとここが変わる。
//! B の切戻し・残す側・頂点番号はこの棚だけが持ち、コマンド(wire.chamfer など)の定義へ入る。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include <QDockWidget>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;

//! 棚で決めること。
struct V2CornerChoice {
    bool fillet = false;          //!< false: C面取り、true: R丸め
    double sizeMm = 1.0;          //!< A の切戻し、または半径(数の棚と同じ値)
    double secondSetbackMm = 0.0; //!< B の切戻し。0 なら A と同じ
    int firstKeepSide = 0;        //!< 0 自動 / 1 始点側 / 2 終点側
    int secondKeepSide = 0;
    bool onlyVertex = false;      //!< ポリラインの角: その頂点だけ
    int vertexIndex = 0;
};

class V2CornerDock final : public QDockWidget {
public:
    explicit V2CornerDock(QWidget* parent);

    [[nodiscard]] V2CornerChoice Choice() const;
    void SetChoice(const V2CornerChoice& choice);
    //! 数の棚の値を映す(変わった知らせは出さない)。
    void SetSizeMm(double sizeMm);
    //! 量をここで打ったときに呼ぶもの(数の棚へ流す)。
    void SetSizeHandler(std::function<void(double)> handler);
    //! 「C面取りを作成 / R丸めを作成」と「角を加工」を押したときに呼ぶもの。
    //! command は "wire.chamfer" / "wire.fillet" / "wire.corner_chamfer" / "wire.corner_fillet"。
    void SetRunHandler(std::function<void(const char* command)> handler);
    //! 直線 A / B の欄に、選んだ線の名前を出す。
    void SetPairText(const QString& first, const QString& second);

    // ---- 試験から ----
    void SetFillet(bool fillet);
    void SetSecondSetback(double mm);
    void SetKeepSides(int first, int second);
    void SetOnlyVertex(bool only, int vertexIndex);
    void PressCreate();
    void PressCorner();
    [[nodiscard]] QString FirstText() const;
    [[nodiscard]] QString SecondText() const;
    [[nodiscard]] QString CreateButtonText() const;

private:
    void RefreshKind();
    void EmitSize();

    QComboBox* kind_ = nullptr;
    QLabel* first_ = nullptr;
    QLabel* second_ = nullptr;
    QComboBox* firstKeep_ = nullptr;
    QComboBox* secondKeep_ = nullptr;
    QFormLayout* form_ = nullptr;
    QDoubleSpinBox* size_ = nullptr;
    QDoubleSpinBox* secondSetback_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QPushButton* create_ = nullptr;
    QCheckBox* onlyVertex_ = nullptr;
    QDoubleSpinBox* vertex_ = nullptr;
    QPushButton* corner_ = nullptr;
    std::function<void(double)> sizeHandler_;
    std::function<void(const char*)> runHandler_;
    bool loading_ = false;
};
