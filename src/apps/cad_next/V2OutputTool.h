#pragma once
#include <QDialog>
#include <QString>
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/OutputPlacement.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <array>
#include <string>
#include <vector>
class V2MainWindow;
class V2OutputPreview;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QFormLayout;
class QEvent;
struct V2OutputAsset {
    kachakacha::v2::domain::Entity entity;
    kachakacha::v2::modeling::KernelShapeHandle shape;
    std::vector<kachakacha::v2::geometry::CurveSegment> curves;
    kachakacha::v2::geometry::OutputFrame frame;
};
class V2OutputTool final:public QDialog {
public:
    static V2OutputTool* Open(V2MainWindow& window,int format=-1);
    static void Generate(V2MainWindow& window,std::string_view command);
    explicit V2OutputTool(V2MainWindow& window,int format);
    void TakeSelection();
    bool SaveTo(const QString& path);
    void RefreshPreview();
    void ChooseFormat(int format);
    void SetPlacement(const kachakacha::v2::geometry::OutputPlacement& placement);
    int AssetCount() const {return static_cast<int>(assets_.size());}
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    void BuildUi(int format);
    void BuildPlacement(QVBoxLayout* layout);
    void AddVectorRow(QFormLayout* layout,const QString& label,std::array<QDoubleSpinBox*,3>& values,const kachakacha::v2::geometry::Vector3& initial);
    void Pick(int role);
    void SetPickedFrame(int role,const kachakacha::v2::geometry::OutputFrame& frame);
    kachakacha::v2::geometry::OutputPlacement Placement() const;
    void FillTargets();
    bool CaptureModels(const std::vector<kachakacha::v2::base::EntityId>& ids);
    bool Capture(const std::vector<kachakacha::v2::base::EntityId>& ids);
    bool CaptureElements();
    bool Prepare();
    bool CopyIntoDocument();
    void Run();
    void SetProblem(const QString& problem);
    V2MainWindow& window_;
    std::vector<V2OutputAsset> assets_,prepared_;
    std::uint64_t revision_=0;
    kachakacha::v2::base::DocumentId documentId_;
    QComboBox* format_=nullptr;
    QComboBox* placement_=nullptr;
    QListWidget* targets_=nullptr;
    QLabel* status_=nullptr;
    QPushButton* save_=nullptr;
    V2OutputPreview* preview_=nullptr;
    QWidget* placementFields_=nullptr;
    std::array<QDoubleSpinBox*,3> sourcePoint_{},sourceNormal_{},sourceX_{},targetPoint_{},targetNormal_{},targetX_{};
    int pickRole_=0;
    bool loading_=false,generated_=false;
};
