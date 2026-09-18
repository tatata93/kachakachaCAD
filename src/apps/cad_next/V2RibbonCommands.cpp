//! 2段の帯を窓へ繋ぐ(正本 3 HTML 2026-09-18、指示書 common_ui_contract)。
//!
//! 帯は core(app/Ribbon)が決めた並びを出すだけ。押した道具は台帳の命令へ、
//! 作り方つきの道具(面作成の方式 / 測定の測り方)はここで作り方を決めてから同じ命令へ。
//! 押せない道具は理由を状態行へ出す(「押せるが何も起きない」を作らない)。

#include "V2MainWindow.h"

#include "V2MeasureDock.h"
#include "V2Ribbon.h"

#include "kachakacha/app/MeasurePanel.h"
#include "kachakacha/app/Ribbon.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/ToolController.h"

#include <QAction>
#include <QString>
#include <QToolBar>

#include <map>
#include <string>

using kachakacha::v2::app::RibbonTool;
using kachakacha::v2::modeling::DrawingTool;

void V2MainWindow::BuildRibbon(const std::map<std::string, QAction*>& byCommand)
{
    ribbon_ = new V2Ribbon(toolPalette_);
    // 命令 id → QAction。作図の道具は tool ごとの QAction(押された形 = 持っている道具)。
    ribbon_->SetActionLookup([this, byCommand](std::string_view id) -> QAction* {
        const auto found = byCommand.find(std::string(id));
        if (found != byCommand.end()) {
            return found->second;
        }
        return ActionFor(id);
    });
    ribbon_->SetVariantHandler([this](const RibbonTool& tool) { RunRibbonVariant(tool); });
    ribbon_->SetBlockedHandler([this](const RibbonTool& tool) {
        SetStatus(QString::fromUtf8(std::string(tool.labelJa).c_str()) + QStringLiteral(": ")
            + QString::fromUtf8(std::string(tool.blockedReasonJa).c_str()));
    });
    toolPalette_->addWidget(ribbon_);
    ribbon_->ShowMode(mode_);
}

void V2MainWindow::RunRibbonVariant(const RibbonTool& tool)
{
    if (tool.surfaceMethod.has_value()) {
        // 面作成: 作り方を先に決めてから、同じ「面を作る」の道具へ。
        const auto method = static_cast<kachakacha::v2::modeling::GuideSurfaceMethod>(
            *tool.surfaceMethod);
        if (surfaceShelfShown_) {
            ChooseSurfaceMethod(method);
        } else {
            surfaceInput_.method = method;
            surfaceInput_.methodChosenByUser = true;
            RunCommand("surface.create");
        }
        RefreshRibbonState();
        return;
    }
    if (tool.measureMode.has_value()) {
        if (measureDock_ != nullptr) {
            measureDock_->SetMode(
                static_cast<kachakacha::v2::app::MeasureMode>(*tool.measureMode));
        }
        RunCommand("measure.open");
        RefreshRibbonState();
        return;
    }
    if (!tool.commandId.empty()) {
        RunCommand(tool.commandId);
    }
}

void V2MainWindow::RefreshRibbonState()
{
    if (ribbon_ == nullptr) {
        return;
    }
    ribbon_->SetCurrentSurfaceMethod(static_cast<int>(surfaceInput_.method),
        surfaceShelfShown_);
    const bool measuring = session_->CurrentTool() == DrawingTool::Measure;
    ribbon_->SetCurrentMeasureMode(
        measureDock_ == nullptr ? -1 : static_cast<int>(measureDock_->Mode()), measuring);
}

bool V2MainWindow::ModeToolVisible(std::string_view id) const
{
    return ribbon_ != nullptr && ribbon_->CommandAvailable(id);
}
