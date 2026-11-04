#pragma once
#include "kachakacha/app/ShapeRebuild.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include <QObject>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
class V2MainWindow;
// Private reader only: inputs are copied on its GUI thread, native work never
// accesses the window/document. Results are adopted in the original DAG order.
class V2ExtrudeBuildJobs final:public QObject {
public:
    using Builder=std::function<kachakacha::v2::modeling::ExtrudeRequest(const kachakacha::v2::domain::ExtrudeDefinition&)>;
    V2ExtrudeBuildJobs(V2MainWindow&,const std::vector<kachakacha::v2::app::ShapeRebuildStep>&,const Builder&);
    ~V2ExtrudeBuildJobs() override;
    static bool HasIndependentWork(V2MainWindow&);
    static std::optional<kachakacha::v2::base::Result<kachakacha::v2::kernel::ExtrudeBuildResult>>
        Take(V2MainWindow&,kachakacha::v2::base::FeatureId);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
