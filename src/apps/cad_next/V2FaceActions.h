#pragma once
#include <QWidget>
#include <string_view>
#include <string>
#include <functional>
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/app/WorkPlaneOptions.h"
class V2MainWindow;
class QLabel;
class QEvent;
class QCloseEvent;
class QObject;
class V2FaceActions final : public QWidget {
public:
    static bool Run(V2MainWindow& window, std::string_view command);
    using PlaneHandler = std::function<void(std::optional<kachakacha::v2::app::WorkPlaneChoice>, bool planar)>;
    static void PickWorkPlane(V2MainWindow& window, PlaneHandler handler);
private:
    V2FaceActions(V2MainWindow& window, std::string command);
    bool eventFilter(QObject* object, QEvent* event) override;
    void Pick();
    void closeEvent(QCloseEvent* event) override;
    void Apply(const std::vector<kachakacha::v2::app::SelectionRef>& faces);
    static std::optional<kachakacha::v2::modeling::KernelShapeHandle> DrawingShape(
        const V2MainWindow& window, const kachakacha::v2::base::EntityId& id);
    static std::vector<kachakacha::v2::app::SelectionRef> SelectedDrawingFaces(
        const V2MainWindow& window, const kachakacha::v2::app::SelectionSet& selection);
    static bool DrawingCandidate(const V2MainWindow&, const kachakacha::v2::app::PickCandidate&);
    static std::vector<kachakacha::v2::app::SelectionRef> DrawingRefs(
        const V2MainWindow&, const kachakacha::v2::app::PickCandidate&);
    static void ResolvePlane(V2MainWindow&, const kachakacha::v2::app::SelectionRef&, const PlaneHandler&);
    bool ActivateDrawingPlane(const kachakacha::v2::app::WorkPlaneChoice& choice);
    static void FrameDrawingFace(V2MainWindow&, const kachakacha::v2::app::SelectionRef&, const kachakacha::v2::app::WorkPlaneChoice&);
    void DrawOnFace(const kachakacha::v2::app::SelectionRef& face);
    bool Extract(const std::vector<kachakacha::v2::app::SelectionRef>& faces);
    kachakacha::v2::app::SelectionSet sources_;
    bool picking_ = false;
    V2MainWindow& window_;
    std::string command_;
    QLabel* status_ = nullptr;
};
