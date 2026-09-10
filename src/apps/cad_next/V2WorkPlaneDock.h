#pragma once

//! 作業平面を作る右パネル(V1 の「平面を作る」タブと同じ欄)。
//!
//! 作り方を選ぶと、その作り方に要る欄だけが出る。数で指定できるものは数で、
//! 選んで指定するものは「いま何を選んでいるか」を出す。押してから断らず、
//! 作れないときはその場で理由を出す。
//!
//! 何を選べばよいか、どの欄が要るかは core(app/WorkPlaneOptions.h)が決める。
//! この棚は欄を並べて、決めたこと(WorkPlaneChoice)を返すだけにする。

#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/base/Ids.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

//! 試験と本体が同じ名前で呼べるように、core の型を短い名前で見せる。
using WorkPlaneChoice = kachakacha::v2::app::WorkPlaneChoice;

class V2WorkPlaneDock final : public QDockWidget {
public:
    explicit V2WorkPlaneDock(QWidget* parent);

    //! いま欄に入っていること。
    [[nodiscard]] WorkPlaneChoice Choice() const;
    //! 欄へ入れる(試験と、開き直しの復元に使う)。
    void SetChoice(const WorkPlaneChoice& choice);
    //! 基準平面のコンボに並べる平面。名前と id。
    void SetPlanes(const std::vector<std::pair<kachakacha::v2::base::EntityId, QString>>& planes);
    //! いま選んでいるものを教える。足りるかどうかの一文を出し直す。
    void Refresh(const kachakacha::v2::app::WorkPlaneFacts& facts);
    //! 作ったあとに作業中にするか。
    [[nodiscard]] bool ActivateAfterCreate() const;
    //! 「平面を作る」を押したときに呼ぶもの。
    void SetCreateHandler(std::function<void()> handler);
    //! 試験から呼ぶ。作り方の位置。
    void SetMethodIndex(int index);
    //! 試験から見る。いま出ている「足りるか」の一文。
    [[nodiscard]] QString NeedsText() const;
    //! 「平面を作る」を押したのと同じ(試験用)。
    void PressCreate();
    //! いま「平面を作る」が押せるか。
    [[nodiscard]] bool CanCreate() const;

private:
    //! 3つの数の欄を横に並べた1行を作り、表へ足す。
    [[nodiscard]] std::array<QDoubleSpinBox*, 3> AddVectorRow(const QString& label,
        const kachakacha::v2::geometry::Vector3& initial, double step);
    [[nodiscard]] QDoubleSpinBox* AddNumberRow(const QString& label, double minimum,
        double maximum, double initial, const QString& suffix);
    [[nodiscard]] QComboBox* AddPlaneRow(const QString& label);
    //! 作り方に応じて、要る欄だけを出す。
    void ApplyVisibility();
    void RefreshNeeds();
    [[nodiscard]] static kachakacha::v2::geometry::Vector3 VectorOf(
        const std::array<QDoubleSpinBox*, 3>& fields);
    static void SetVector(const std::array<QDoubleSpinBox*, 3>& fields,
        const kachakacha::v2::geometry::Vector3& value);
    [[nodiscard]] std::optional<kachakacha::v2::base::EntityId> PlaneOf(
        const QComboBox* combo) const;
    void SelectPlane(QComboBox* combo, const std::optional<kachakacha::v2::base::EntityId>& id);

    QWidget* body_ = nullptr;
    QFormLayout* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* method_ = nullptr;
    QComboBox* standard_ = nullptr;
    std::array<QDoubleSpinBox*, 3> origin_{};
    std::array<QDoubleSpinBox*, 3> normal_{};
    std::array<QDoubleSpinBox*, 3> uAxis_{};
    std::array<std::array<QDoubleSpinBox*, 3>, 3> threePoints_{};
    QComboBox* referencePlane_ = nullptr;
    QComboBox* secondPlane_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    std::array<QDoubleSpinBox*, 3> axisPoint_{};
    std::array<QDoubleSpinBox*, 3> axisDirection_{};
    QDoubleSpinBox* angle_ = nullptr;
    QDoubleSpinBox* curveParameter_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    QLabel* needs_ = nullptr;
    QCheckBox* activate_ = nullptr;
    QPushButton* create_ = nullptr;
    std::vector<kachakacha::v2::base::EntityId> planeIds_;
    std::function<void()> createHandler_;
    kachakacha::v2::app::WorkPlaneFacts facts_;
    bool canCreate_ = false;
};
