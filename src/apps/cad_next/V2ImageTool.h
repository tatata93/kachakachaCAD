#pragma once
#include <QWidget>
#include <QImage>
#include <QString>
#include <QPointF>
#include <optional>
#include "V2ImageViews.h"
#include "kachakacha/domain/ImageDefinition.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
class V2MainWindow;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QEvent;
class QCloseEvent;
class V2ImageTool final:public QWidget {
public:
    static V2ImageTool* Open(V2MainWindow&);
    static void Refresh(V2MainWindow&);
    static bool MakeView(const kachakacha::v2::domain::CreateImageDefinition&,V2ImageView&,QString&);
    bool LoadImage(const QString& path);
    bool SetTarget(const kachakacha::v2::app::SelectionRef&);
    bool SetAnchor(const kachakacha::v2::geometry::Vector3&);
    void SetImagePoint(const kachakacha::v2::geometry::Vector3&);
    bool Commit();
    void Preview();
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    explicit V2ImageTool(V2MainWindow&);
    void BuildUi();
    void CancelInput();
    void UseWorkPlane();
    void Pick(int role);
    void ClickViewport(const QPointF&);
    bool DragViewport(QEvent*);
    void MoveImageDrag(const QPointF&);
    void UpdateFields();
    bool FitPickedPoints();
    bool EditSelectedImage();
    V2MainWindow& window_;
    kachakacha::v2::domain::CreateImageDefinition definition_;
    kachakacha::v2::base::EntityId editing_;
    kachakacha::v2::modeling::KernelShapeHandle face_;
    std::vector<V2ImageView> savedViews_;
    QImage image_;
    QCheckBox* mirror_=nullptr;
    QCheckBox* fitRotate_=nullptr;
    bool PickImagePoint(const QPointF&);
    bool ImagePointAt(const QPointF&,kachakacha::v2::geometry::Vector3&,kachakacha::v2::geometry::Vector3&) const;
    void RefreshFitLine();
    std::optional<kachakacha::v2::geometry::Vector3> imageHover_;
    std::vector<kachakacha::v2::geometry::Vector3> imageMarks_;
    QLabel* status_=nullptr;
    QComboBox* mode_=nullptr;
    QDoubleSpinBox *width_=nullptr,*rotation_=nullptr,*opacity_=nullptr;
    int role_=0;
    bool dragging_=false,dragMoved_=false;
    QPointF dragPress_;
    kachakacha::v2::geometry::Vector3 dragOrigin_,dragNormal_;
    std::vector<kachakacha::v2::geometry::Vector3> pixels_,points_;
    bool updating_=false,previewOk_=false;
};
