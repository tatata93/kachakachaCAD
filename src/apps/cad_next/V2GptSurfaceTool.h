#pragma once

#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <QDockWidget>
#include <optional>

class V2MainWindow;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;

class V2GptSurfaceTool final {
public:
    explicit V2GptSurfaceTool(V2MainWindow& window);
    ~V2GptSurfaceTool();
    void Begin();
    void End();
    bool Active() const { return active_; }
    QDockWidget* Dock() const { return dock_; }
    void HandleSelectionChanged();
    bool HandleKey(int key);
    bool Rebuild(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);
private:
    void BuildControls(QVBoxLayout* layout);
    void BuildActions(QVBoxLayout* layout);
    void Add(const std::vector<kachakacha::v2::base::EntityId>& ids, bool grouped);
    void ChangeMode(int mode);
    void EditRow(int operation);
    void RefreshList();
    void Invalidate();
    void Preview();
    void Confirm();
    void ShowResult(const kachakacha::v2::modeling::GuideSurfaceResult& result);
    void Store(const kachakacha::v2::base::EntityId& id,
        const kachakacha::v2::modeling::GuideSurfaceResult& result);
    V2MainWindow& window_;
    QDockWidget* dock_ = nullptr;
    QComboBox* method_ = nullptr;
    QComboBox* role_ = nullptr;
    QDoubleSpinBox* tolerance_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool active_ = false;
    kachakacha::v2::domain::CreateGuideSurfaceDefinition definition_;
    std::optional<kachakacha::v2::modeling::GuideSurfaceResult> preview_;
    std::uint64_t previewRevision_ = 0;
};
