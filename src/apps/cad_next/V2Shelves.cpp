//! 右の「現在の操作」パネルを組み立てるところ(V2MainWindow の一部)。
//!
//! app/ShelfLayout が選んだ設定ページを、同時に一つだけ見せる。
//! V2MainWindow.cpp が 1500 行の上限に届いたので、組み立てだけをここへ移した。

#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"

#include "kachakacha/app/CommandParameters.h"

#include <QDockWidget>
#include <QString>

void V2MainWindow::BuildEditingShelves()
{
    // 編集の棚(V1 の「選択内容の数値編集」)。選んでいるものの数値を欄で直す。
    editDock_ = new V2EditDock(this);
    editDock_->SetApplyHandler([this] { ApplySelectedEdit(); });
    editDock_->hide();
    // 面取りの棚(V1 の「面取り」欄)。量は数の棚と同じ値、残す側と B の切戻しはここだけ。
    cornerDock_ = new V2CornerDock(this);
    cornerDock_->SetRunHandler([this](const char* command) { RunCommand(command); });
    cornerDock_->SetChoiceChangedHandler([this] { RefreshCornerPreview(); });
    // 製作の棚(V1 の近似モデル画面)。方式・分割・曲げ・固定・型紙を 1 枚に。
    fabricationDock_ = new V2FabricationDock(this);
    fabricationDock_->SetRunHandler([this](const char* command) { RunCommand(command); });
    fabricationDock_->SetChoiceChangedHandler([this] {
        AdoptFabricationChoice();
        // 欄が変われば、道具の最中なら候補を作り直す。欄と下見がずれたままにしない。
        if (approxShelfShown_) {
            RefreshApproxAll();
        }
    });
    fabricationDock_->SetCandidateHandler([this](int candidate) {
        ChooseApproxCandidate(candidate);
    });
    fabricationDock_->SetClearSourcesHandler([this] { ClearApproxSources(); });
    fabricationDock_->SetPolicyHandler([this](int policy) { ChooseApproxPolicy(policy); });
    fabricationDock_->SetAssemblyHandler([this](double percent, const QString& parts) {
        SetAssemblyPercent(percent, parts);
        // 曲げ具合と半径は同じことの言い換えである。片方を動かしたら両方を映す。
        RefreshBendRadius();
    });
    fabricationDock_->SetRadiusHandler([this](double radiusMm, bool locked) {
        ApplyBendRadius(radiusMm, locked);
    });
    fabricationDock_->SetFreezeOutputHandler(
        [this](kachakacha::v2::fabrication::FreezeOutput value) { freezeOutput_ = value; });
    fabricationDock_->SetMaterialHandler(
        [this](const QString& material, int layers) { ApplyMaterialToSelection(material, layers); });

    // 配列の棚(指示書 D-23)。wire.array_linear/circular が構えている間だけ出す。
    arrayDock_ = new V2ArrayDock(this);
    arrayDock_->SetActionHandlers([this] { ConfirmArray(); },
        [this] {
            EndArray();
            SetStatus(QStringLiteral("配列: やめました。"));
        });
}

void V2MainWindow::BuildRightShelves()
{
    // 測る棚。はじめは畳んでおく。使うときに「測る」で出す。
    measureDock_ = new V2MeasureDock(this);
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
    // 欄が変わるたびに、作る前の平面を3Dへ下見として出す(D-24)。
    workPlaneDock_->SetChangedHandler([this] { RefreshWorkPlanePreview(); });

    // 作図の棚(V1 の「作図」タブ)。円弧の作り方・補助線・指定点・数値で線を作る。
    drawingDock_ = new V2DrawingDock(this);
    drawingDock_->SetSettingsHandler(
        [this](const kachakacha::v2::modeling::ToolSettings& settings) {
            ApplyToolSettings(settings);
        });
    drawingDock_->SetCreateWireHandler([this] { CreateWireFromDock(); });
    // 核に無い作り方のカードを押したら、理由を状態行へ(「押せるが何も起きない」を作らない)。
    drawingDock_->SetBlockedMethodHandler([this](const QString& reasonJa) { SetStatus(reasonJa); });
    // 作り方カードが名指しした入力欄を、3D の入力列で先に選んでおく(D-02 の「直径指定」)。
    drawingDock_->SetCursorFieldHandler([this](const QString& fieldId) {
        if (viewport_ != nullptr) {
            viewport_->SetPreferredCursorField(fieldId);
        }
    });

    // グリッドの棚と表示の棚(V1 のグリッド欄・表示タブ)。見え方だけで、文書は変えない。
    gridDock_ = new V2GridDock(this);
    gridDock_->SetApplyHandler([this](const V2GridChoice& choice) { ApplyGridChoice(choice); });
    gridDock_->SetPickOriginHandler([this] { RunCommand("grid.move_origin"); });
    displayDock_ = new V2DisplayDock(this);
    displayDock_->SetApplyHandler(
        [this](const V2DisplayChoice& choice) { ApplyDisplayChoice(choice); });
    displayDock_->SetStageHandler(
        [this](kachakacha::v2::app::DisplayStage stage) { ApplyDisplayStage(stage); });

    BuildOutputShelves();

    RefreshCornerDock();
    RefreshFabricationDock();

    operationDock_ = new QDockWidget(QStringLiteral("現在の操作"), this);
    operationDock_->setObjectName(QStringLiteral("currentOperationDock"));
    operationDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    operationDock_->setMinimumWidth(280);
    operationHost_ = new V2OperationPanelHost(operationDock_);
    for (const auto shelf : kachakacha::v2::app::AllShelves()) {
        if (QDockWidget* source = DockForShelf(shelf); source != nullptr
            && source->widget() != nullptr) {
            // 中身は統合パネルへ移す。空になった元Dockを表示したままにすると、
            // Qtの初期位置(左上 0,0 / 100x30)へ全Dockが重なって文字が潰れる。
            source->hide();
            operationHost_->AddPage(shelf, source->widget());
        }
    }
    operationDock_->setWidget(operationHost_);
    addDockWidget(Qt::RightDockWidgetArea, operationDock_);
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

    // 押し出しの棚。押し出しの最中だけ出す(オーナー指示 2026-09-14 §7)。
    // 窓で全部決めてから作る道をやめ、右で見ながら決められるようにする。
    // 「面を作る」の棚。作っている最中だけ出す(UI の正本)。
    surfaceDock_ = new V2SurfaceDock(this);
    surfaceDock_->SetMethodHandler(
        [this](kachakacha::v2::modeling::GuideSurfaceMethod method) {
            ChooseSurfaceMethod(method);
        });
    surfaceDock_->SetActivateHandler([this](kachakacha::v2::modeling::ChainRole slot) {
        ActivateSurfaceSlot(slot);
    });
    surfaceDock_->SetClearHandler([this](kachakacha::v2::modeling::ChainRole slot) {
        ClearSurfaceSlot(slot);
    });
    surfaceDock_->SetOrderingHandler(
        [this](kachakacha::v2::app::SurfaceOrdering ordering) {
            ChooseSurfaceOrdering(ordering);
        });
    surfaceDock_->SetMoveSectionHandler([this](int from, int to) {
        MoveSurfaceSection(from, to);
    });
    surfaceDock_->SetActionHandlers([this] { ConfirmSurface(); },
        [this] {
            EndSurfacePreview();
            SetStatus(QStringLiteral("面を作る: やめました。"));
        },
        [this] { ResetSurfaceInput(); });

    booleanDock_ = new V2BooleanDock(this);
    booleanDock_->SetOperationHandler([this](bool cut) { ChooseBooleanOperation(cut); });
    booleanDock_->SetActivateHandler(
        [this](kachakacha::v2::app::BooleanSlot slot) { ActivateBooleanSlot(slot); });
    booleanDock_->SetClearHandler(
        [this](kachakacha::v2::app::BooleanSlot slot) { ClearBooleanSlot(slot); });
    booleanDock_->SetActionHandlers([this] { ConfirmBoolean(); },
        [this] {
            EndBoolean();
            SetStatus(QStringLiteral("足す・引く: やめました。"));
        });

    BuildThickenDock();

    extrudeDock_ = new V2ExtrudeDock(this);
    extrudeDock_->SetDistanceHandler([this](double value) { UpdateExtrudePreview(value); });
    extrudeDock_->SetOptionHandler([this] { RefreshExtrudeFromDock(); });
    extrudeDock_->SetActionHandlers([this] { ConfirmExtrude(); },
        [this] {
            EndExtrudePreview();
            SetStatus(QStringLiteral("押し出し: やめました。"));
        },
        [this] { EditExtrudeWithDialog(); });
    extrudeDock_->SetReselectHandlers([this] { ReselectExtrudeInput(true); },
        [this] { ReselectExtrudeInput(false); });

    // 型紙の下見。出す前に紙の形で見る。見ないまま出すと、
    // 紙に収まっていないことに、印刷してから気づく。
    patternDock_ = new V2PatternDock(this);

    // 数の棚。板厚などは、変えられないと使えない。はじめから出しておく。
    parameterDock_ = new V2ParameterDock(this);
    parameterDock_->SetDiagnosticSink([this](const QString& text) {
        AddDiagnostic(text);
        SetStatus(text);
    });
    parameterDock_->SetChangedHandler([this] {
        RefreshCornerDock();
        RefreshFabricationDock();
        RefreshPartDock();
        // 板厚を数の棚から直したときも、厚みの道具の欄と下見をそろえる。
        if (thickenShelfShown_) {
            thickenInput_.thicknessMm = kachakacha::v2::app::ParameterValueOf(
                parameterDock_->Values(), kachakacha::v2::app::ParameterId::ExtrudeDistance);
            RefreshThickenAll();
        }
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

//! 「厚み」の棚(指示書 matrix P-10)。BuildOutputShelves から切り出した
//! (1関数100行の門)。
void V2MainWindow::BuildThickenDock()
{
    thickenDock_ = new V2ThickenDock(this);
    thickenDock_->SetReselectHandler([this] { ReselectThicken(); });
    thickenDock_->SetPlacementHandler(
        [this](kachakacha::v2::fabrication::ThicknessPlacement value) {
            ChooseThickenPlacement(value);
        });
    thickenDock_->SetToPlaneHandler([this] { ChooseThickenToPlane(); });
    thickenDock_->SetTargetHandler(
        [this](const kachakacha::v2::base::EntityId& id) { ChooseThickenTarget(id); });
    thickenDock_->SetThicknessHandler([this](double value) { ApplyThickenThicknessMm(value); });
    thickenDock_->SetActionHandlers([this] { ConfirmThicken(); },
        [this] {
            EndThicken();
            SetStatus(QStringLiteral("厚み: やめました。"));
        });
}
