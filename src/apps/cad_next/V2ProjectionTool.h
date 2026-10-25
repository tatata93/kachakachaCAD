#pragma once
#include <QWidget>
#include "kachakacha/app/Selection.h"
#include "kachakacha/kernel/OcctWireProjection.h"
#include <optional>
class V2MainWindow;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;
class QVBoxLayout;
class V2ProjectionTool final : public QWidget {
public:
    static V2ProjectionTool* Open(V2MainWindow& window,bool wrap=false);
    explicit V2ProjectionTool(V2MainWindow& window);
    ~V2ProjectionTool() override;
    bool AddWire(const kachakacha::v2::app::SelectionRef& ref,bool toggle=true);
    bool AddTarget(const kachakacha::v2::app::SelectionRef& ref,bool toggle=true);
    bool Preview();
    bool Commit();
    void CancelInput();
    void TakeSelection();
    int WireCount() const {return static_cast<int>(sources_.size());}
    int TargetCount() const {return static_cast<int>(targets_.size());}
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    struct Source {kachakacha::v2::app::SelectionRef ref; kachakacha::v2::kernel::WireProjectionInput input; bool directionExplicit=false;};
    struct Target {kachakacha::v2::app::SelectionRef ref; kachakacha::v2::modeling::KernelShapeHandle shape;};
    void BuildUi();
    void BuildSettings(QVBoxLayout* layout);
    void RefreshModeUi();
    void Invalidate();
    void RefreshRows();
    void SetDirection(kachakacha::v2::geometry::Vector3 direction);
    void PickViewport(const QPointF& position);
    void SetPickRole(int role);
    void RefreshMarks();
    void ClearMarks();
    void TakeActiveSelection();
    bool AddResults();
    void ClearResult();
    bool SyncInputRevision();
    bool PreviewRegions(const std::vector<kachakacha::v2::modeling::KernelShapeHandle>& faces,
        const std::vector<kachakacha::v2::kernel::WireProjectionInput>& inputs,
        const kachakacha::v2::kernel::WireProjectionOptions& options);
    bool AddRegions(const std::vector<kachakacha::v2::base::EntityId>& inputIds,
        std::vector<kachakacha::v2::base::EntityId>& faceIds);
    V2MainWindow& window_;
    kachakacha::v2::base::DocumentId documentId_;
    std::uint64_t revision_=0,previewRevision_=0;
    std::vector<Source> sources_;
    std::vector<Target> targets_;
    std::optional<kachakacha::v2::kernel::WireProjectionResult> result_;
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> regions_;
    kachakacha::v2::geometry::Vector3 sourceOrigin_{},targetPoint_{};
    kachakacha::v2::geometry::Vector3 sourceU_{1,0,0},sourceV_{0,1,0};
    int role_=0;
    QComboBox *mode_,*hits_,*output_,*wireForm_;
    QDoubleSpinBox *tolerance_,*distance_,*rotation_;
    QLabel *status_, *pickPrompt_;
    QListWidget *wireRows_,*targetRows_;
    QPushButton *confirm_, *pickWire_, *pickTarget_;
    QTimer* update_;
};
