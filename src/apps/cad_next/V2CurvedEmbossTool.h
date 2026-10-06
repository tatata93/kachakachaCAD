#pragma once
#include <QWidget>
#include "kachakacha/app/Selection.h"
#include "kachakacha/kernel/OcctCurvedEmboss.h"
#include <optional>
class V2MainWindow;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class V2CurvedEmbossTool final : public QWidget {
public:
    static V2CurvedEmbossTool* Open(V2MainWindow& window);
    explicit V2CurvedEmbossTool(V2MainWindow& window);
    ~V2CurvedEmbossTool() override;
    bool SetSupport(const kachakacha::v2::app::SelectionRef& ref);
    bool AddWire(kachakacha::v2::base::EntityId id);
    bool Preview();
    bool Commit();
    void CancelInput();
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    void BuildUi();
    void BuildSettings(QVBoxLayout* layout);
    void TakeSelection();
    void Invalidate();
    bool CollectBoundary(std::vector<kachakacha::v2::geometry::CurveSegment>& boundary);
    V2MainWindow& window_;
    kachakacha::v2::base::DocumentId documentId_;
    kachakacha::v2::base::EntityId support_;
    kachakacha::v2::modeling::KernelShapeHandle supportShape_;
    std::vector<kachakacha::v2::base::EntityId> wires_;
    std::optional<kachakacha::v2::kernel::CurvedEmbossResult> built_;
    std::uint64_t supportRevision_ = 0;
    int role_ = 0;
    QDoubleSpinBox *height_, *draft_, *tilt_, *heading_, *tolerance_;
    QPushButton *draftEnabled_, *tiltEnabled_, *confirm_;
    QComboBox *output_, *operation_;
    QLabel *sourceLabel_, *wireLabel_, *status_;
};
