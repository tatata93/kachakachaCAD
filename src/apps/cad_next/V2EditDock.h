#pragma once

//! 編集の棚(V1 の「編集」タブの「選択内容の数値編集」)。
//!
//! 選んでいる作業平面なら 原点/法線/平面内X、線なら 点の表(直線・折れ線・ベジエ・
//! スプライン)か 中心/円のX軸/円のY軸/半径/開始角/中心角(円・円弧)を欄に出し、
//! 「変更を適用」で置き換える。直線には「長さ」「平面内角度」の置き直しがある。
//!
//! 欄と定義の往復は core の app/EntityEdit が持つ。ここは欄を並べて値を運ぶだけ。
//! 何が直せて何が直せないかをここで決めない(決めると画面を出さずに確かめられない)。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/EntityEdit.h"
#include "kachakacha/base/Ids.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

class V2EditDock final : public QDockWidget {
public:
    explicit V2EditDock(QWidget* parent);

    //! 何も直せないとき。理由を出す。
    void ShowNothing(const QString& reason);
    //! 作業平面の欄を出す。
    void ShowPlane(const QString& name, const kachakacha::v2::app::PlaneEditFields& fields);
    //! 線の欄を出す。measure は直線の長さと角度(欄の初期値)。frameName は角度の基準の名前。
    void ShowWire(const QString& name, const kachakacha::v2::app::WireEditFields& fields,
        const kachakacha::v2::app::LineMeasure& measure, const QString& frameName);
    //! 「作成元平面」の選択肢(なし + 平面)。
    void SetPlanes(
        const std::vector<std::pair<kachakacha::v2::base::EntityId, QString>>& planes);

    //! いまの欄。
    [[nodiscard]] bool IsShowingPlane() const;
    [[nodiscard]] bool IsShowingWire() const;
    [[nodiscard]] kachakacha::v2::app::PlaneEditFields PlaneFields() const;
    //! 長さ・角度は「置き直す」に印があるときだけ入る。
    [[nodiscard]] kachakacha::v2::app::WireEditFields WireFields() const;

    void SetApplyHandler(std::function<void()> handler);
    void PressApply();
    void SetMessage(const QString& text);
    [[nodiscard]] QString MessageText() const;
    [[nodiscard]] QString SelectionText() const;

    // ---- 試験から欄を触る ----
    [[nodiscard]] int PointRowCount() const;
    void SetPoint(int row, const kachakacha::v2::geometry::Vector3& point);
    void SetPlaneOrigin(const kachakacha::v2::geometry::Vector3& origin);
    void SetPlaneNormal(const kachakacha::v2::geometry::Vector3& normal);
    void SetRadius(double radiusMm);
    void SetSweepAngle(double degrees);
    //! 「長さ」を置き直す。印も付ける。
    void SetLength(double lengthMm);
    void SetAngle(double degrees);
    void SetConstruction(bool construction);
    [[nodiscard]] double LengthValue() const;
    [[nodiscard]] double AngleValue() const;

private:
    struct PointRow {
        QWidget* row = nullptr;
        QLabel* label = nullptr;
        std::array<QDoubleSpinBox*, 3> fields{};
    };
    using Vector3Fields = std::array<QDoubleSpinBox*, 3>;

    QWidget* BuildPlanePage();
    QWidget* BuildWirePage();
    QWidget* BuildLinePanel();
    QWidget* BuildArcPage();
    void EnsurePointRows(std::size_t count);
    static QWidget* MakeVector3Row(QWidget* parent, Vector3Fields& fields, double step);
    static void SetVector3(const Vector3Fields& fields,
        const kachakacha::v2::geometry::Vector3& value);
    [[nodiscard]] static kachakacha::v2::geometry::Vector3 ReadVector3(
        const Vector3Fields& fields);

    QLabel* selection_ = nullptr;
    QStackedWidget* pages_ = nullptr;   //!< 0: なし 1: 平面 2: 線
    QLabel* nothing_ = nullptr;
    Vector3Fields planeOrigin_{};
    Vector3Fields planeNormal_{};
    Vector3Fields planeU_{};
    QComboBox* sourcePlane_ = nullptr;
    QCheckBox* construction_ = nullptr;
    QWidget* linePanel_ = nullptr;
    QCheckBox* relocateLength_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QCheckBox* relocateAngle_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QLabel* angleFrame_ = nullptr;
    QStackedWidget* geometry_ = nullptr;   //!< 0: 点の表 1: 円・円弧
    QVBoxLayout* pointsLayout_ = nullptr;
    QWidget* pointsPage_ = nullptr;
    std::vector<PointRow> pointRows_;
    std::size_t visiblePointRows_ = 0;
    Vector3Fields arcCenter_{};
    Vector3Fields arcU_{};
    Vector3Fields arcV_{};
    QDoubleSpinBox* radius_ = nullptr;
    QDoubleSpinBox* startAngle_ = nullptr;
    QDoubleSpinBox* sweepAngle_ = nullptr;
    QFormLayout* arcForm_ = nullptr;
    QPushButton* apply_ = nullptr;
    QLabel* message_ = nullptr;
    std::function<void()> applyHandler_;
    kachakacha::v2::app::WireEditShape shape_ = kachakacha::v2::app::WireEditShape::Line;
    std::vector<kachakacha::v2::base::EntityId> planeIds_;
};
