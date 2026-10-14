#pragma once
#include "kachakacha/app/WorkPlaneOptions.h"
#include <functional>
class V2MainWindow;
class V2WorkPlaneDock;
void InstallWorkPlaneRoleInputs(V2MainWindow&, V2WorkPlaneDock&, std::function<void()> changed);
void ApplyWorkPlaneRoleInputs(const V2WorkPlaneDock*, const kachakacha::v2::app::WorkPlaneChoice&,
    kachakacha::v2::app::WorkPlaneMaterials&);
void SyncWorkPlaneRoleInputs(V2WorkPlaneDock&);
