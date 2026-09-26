#pragma once
#include "kachakacha/app/FabricationEvaluate.h"
#include <QDockWidget>
#include <optional>
class V2MainWindow;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;
class V2GptFabricationTool final {
public:
    explicit V2GptFabricationTool(V2MainWindow& window);
    void Begin();
    void End();
    bool Active() const { return active_; }
    QDockWidget* Dock() const { return dock_; }
    void HandleSelectionChanged();
    bool HandleKey(int key);
    bool Rebuild(const kachakacha::v2::domain::Feature& feature,kachakacha::v2::base::EntityId output);
private:
    void Controls(QVBoxLayout* layout);
    void Actions(QVBoxLayout* layout);
    void Add(const std::vector<kachakacha::v2::base::EntityId>& ids);
    void Invalidate();
    void Preview();
    void ShowPreview();
    void Confirm(bool pattern=false);
    kachakacha::v2::base::Result<kachakacha::v2::app::FabricationEvaluation> Evaluate(
        const kachakacha::v2::domain::CreateFabricationModelDefinition& definition) const;
    V2MainWindow& window_;
    QDockWidget* dock_=nullptr;
    QLabel* sources_=nullptr;
    QLabel* status_=nullptr;
    QTreeWidget* panels_=nullptr;
    QDoubleSpinBox *tolerance_=nullptr,*width_=nullptr,*thickness_=nullptr,*assembly_=nullptr;
    QSpinBox* count_=nullptr;
    QComboBox* direction_=nullptr;
    QPushButton *confirm_=nullptr,*pattern_=nullptr;
    bool active_=false;
    std::uint64_t revision_=0;
    kachakacha::v2::domain::CreateFabricationModelDefinition definition_;
    std::optional<kachakacha::v2::app::FabricationEvaluation> preview_;
};
