//! 右の棚を組み立てるところ(V2MainWindow の一部)。
//!
//! 棚は「いま使っている道具の設定」を出す場所である(app/ShelfLayout)。
//! V2MainWindow.cpp が 1500 行の上限に届いたので、組み立てだけをここへ移した。
//! 動きは変えていない。

#include "V2MainWindow.h"

#include "kachakacha/app/CommandParameters.h"

#include <QDockWidget>
#include <QString>

void V2MainWindow::BuildEditingShelves()
{
    // 編集の棚(V1 の「選択内容の数値編集」)。選んでいるものの数値を欄で直す。
    editDock_ = new V2EditDock(this);
    editDock_->SetApplyHandler([this] { ApplySelectedEdit(); });
    addDockWidget(Qt::RightDockWidgetArea, editDock_);
    editDock_->hide();
    // 面取りの棚(V1 の「面取り」欄)。量は数の棚と同じ値、残す側と B の切戻しはここだけ。
    cornerDock_ = new V2CornerDock(this);
    cornerDock_->SetRunHandler([this](const char* command) { RunCommand(command); });
    addDockWidget(Qt::RightDockWidgetArea, cornerDock_);
    // 製作の棚(V1 の近似モデル画面)。方式・分割・曲げ・固定・型紙を 1 枚に。
    fabricationDock_ = new V2FabricationDock(this);
    fabricationDock_->SetRunHandler([this](const char* command) { RunCommand(command); });
    fabricationDock_->SetChoiceChangedHandler([this] { AdoptFabricationChoice(); });
    fabricationDock_->SetAssemblyHandler([this](double percent, const QString& parts) {
        SetAssemblyPercent(percent, parts);
    });
    fabricationDock_->SetFreezeOutputHandler(
        [this](kachakacha::v2::fabrication::FreezeOutput value) { freezeOutput_ = value; });
    fabricationDock_->SetMaterialHandler(
        [this](const QString& material, int layers) { ApplyMaterialToSelection(material, layers); });
    addDockWidget(Qt::RightDockWidgetArea, fabricationDock_);
}

void V2MainWindow::BuildRightShelves()
{
    // 測る棚。はじめは畳んでおく。使うときに「測る」で出す。
    measureDock_ = new V2MeasureDock(this);
    addDockWidget(Qt::RightDockWidgetArea, measureDock_);
    measureDock_->hide();
    measureDock_->SetModeChangedHandler([this] {
        viewport_->ClearMeasurePicks();
        RefreshMeasurements();
    });
    measureDock_->SetKeepHandler([this] { KeepMeasuredDimension(); });
    measureDock_->SetClearHandler([this] { ClearMeasurement(); });
    viewport_->SetMeasurePicksChangedCallback([this] { RefreshMeasurements(); });
    BuildEditingShelves();

    // 作業平面の棚(V1 の「平面を作る」タブ)。作図は平面を決めてから始まるので、
    // 札の1つとして最初から置く。「作業平面を作る」を押すと前に出る。
    workPlaneDock_ = new V2WorkPlaneDock(this);
    workPlaneDock_->SetCreateHandler([this] { CreateWorkPlaneFromDock(); });
    addDockWidget(Qt::RightDockWidgetArea, workPlaneDock_);

    // 作図の棚(V1 の「作図」タブ)。円弧の作り方・補助線・指定点・数値で線を作る。
    drawingDock_ = new V2DrawingDock(this);
    drawingDock_->SetSettingsHandler(
        [this](const kachakacha::v2::modeling::ToolSettings& settings) {
            ApplyToolSettings(settings);
        });
    drawingDock_->SetCreateWireHandler([this] { CreateWireFromDock(); });
    addDockWidget(Qt::RightDockWidgetArea, drawingDock_);

    // グリッドの棚と表示の棚(V1 のグリッド欄・表示タブ)。見え方だけで、文書は変えない。
    gridDock_ = new V2GridDock(this);
    gridDock_->SetApplyHandler([this](const V2GridChoice& choice) { ApplyGridChoice(choice); });
    gridDock_->SetPickOriginHandler([this] { RunCommand("grid.move_origin"); });
    addDockWidget(Qt::RightDockWidgetArea, gridDock_);
    displayDock_ = new V2DisplayDock(this);
    displayDock_->SetApplyHandler(
        [this](const V2DisplayChoice& choice) { ApplyDisplayChoice(choice); });
    displayDock_->SetStageHandler(
        [this](kachakacha::v2::app::DisplayStage stage) { ApplyDisplayStage(stage); });
    addDockWidget(Qt::RightDockWidgetArea, displayDock_);

    BuildOutputShelves();

    RefreshCornerDock();
    RefreshFabricationDock();

    // 右側の棚を重ねて札にする。縦に並べると、1180x760 では
    // 1枚あたりが潰れて見出しだけが並ぶ。
    tabifyDockWidget(exportDock_, parameterDock_);
    tabifyDockWidget(parameterDock_, measureDock_);
    tabifyDockWidget(measureDock_, editDock_);
    tabifyDockWidget(editDock_, cornerDock_);
    tabifyDockWidget(cornerDock_, fabricationDock_);
    tabifyDockWidget(fabricationDock_, workPlaneDock_);
    tabifyDockWidget(workPlaneDock_, drawingDock_);
    tabifyDockWidget(drawingDock_, gridDock_);
    tabifyDockWidget(gridDock_, displayDock_);
    // 形状ガイドの役割の表も同じ札の束へ入れる。別の段に置くと、
    // 部品モードで右が上下に割れて、どちらも潰れる。
    tabifyDockWidget(displayDock_, guideDock_);
    tabifyDockWidget(guideDock_, patternDock_);
    tabifyDockWidget(patternDock_, partDock_);
    RefreshRightShelves();
}

//! 部品の棚と型紙の下見。BuildRightShelves の続き。
void V2MainWindow::BuildOutputShelves()
{
    // 部品の棚(V1 の部品タブ)。部品モードだけ道具の設定が右に無かった。
    partDock_ = new V2PartDock(this);
    partDock_->SetRunHandler([this](const char* command) { RunCommand(command); });
    partDock_->SetParameterHandler(
        [this](kachakacha::v2::app::ParameterId id, double value) {
            (void)parameterDock_->Apply(id, QString::number(value, 'f', 3));
        });
    partDock_->SetPlacementHandler(
        [this](kachakacha::v2::fabrication::ThicknessPlacement value) {
            thicknessPlacement_ = value;
            SetStatus(QStringLiteral("厚みの付け方: %1")
                    .arg(QString::fromUtf8(
                        kachakacha::v2::fabrication::ThicknessPlacementNameJa(value))));
        });
    addDockWidget(Qt::RightDockWidgetArea, partDock_);

    // 型紙の下見。出す前に紙の形で見る。見ないまま出すと、
    // 紙に収まっていないことに、印刷してから気づく。
    patternDock_ = new V2PatternDock(this);
    addDockWidget(Qt::RightDockWidgetArea, patternDock_);

    // 数の棚。板厚などは、変えられないと使えない。はじめから出しておく。
    parameterDock_ = new V2ParameterDock(this);
    addDockWidget(Qt::RightDockWidgetArea, parameterDock_);
    parameterDock_->SetDiagnosticSink([this](const QString& text) {
        AddDiagnostic(text);
        SetStatus(text);
    });
    parameterDock_->SetChangedHandler([this] {
        RefreshCornerDock();
        RefreshFabricationDock();
        RefreshPartDock();
    });
    fabricationDock_->SetParameterHandler(
        [this](kachakacha::v2::app::ParameterId id, double value) {
            (void)parameterDock_->Apply(id, QString::number(value, 'f', 3));
        });
    cornerDock_->SetSizeHandler([this](double value) {
        (void)parameterDock_->Apply(kachakacha::v2::app::ParameterId::CornerSize,
            QString::number(value, 'f', 3));
    });
}

