#pragma once
#include "kachakacha/app/ShapeRebuild.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include <QObject>
#include <memory>
#include <optional>
#include <vector>
class V2MainWindow;
struct V2PreparedGuideBuild {
    kachakacha::v2::modeling::GuideTable table;
    kachakacha::v2::modeling::GuideSurfaceResult built;
};
// Scoped to one private reader rebuild. GUI state is never accessed by jobs.
class V2GuideBuildJobs final:public QObject {
public:
    V2GuideBuildJobs(V2MainWindow&,const std::vector<kachakacha::v2::app::ShapeRebuildStep>&);
    ~V2GuideBuildJobs() override;
    static std::optional<V2PreparedGuideBuild> Take(V2MainWindow&,kachakacha::v2::base::EntityId);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
