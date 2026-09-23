#pragma once

//! V2の本体窓(WP-08)。画面は薄く保つ(道具を選ぶ・案内文・一覧と診断・見た目の切替)。
//! 幾何の判断はすべて core にある(architecture-and-data.md DOC-002)。
//! AUTOMOC を使っていないので Q_OBJECT は付けない。信号の受け口はラムダで繋ぐ。

#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/app/CommandAvailability.h"
#include "kachakacha/app/DisplaySettings.h"
#include "kachakacha/app/ShelfLayout.h"
#include "V2DisplayDock.h"
#include "V2DrawingDock.h"
#include "V2CornerDock.h"
#include "V2EditDock.h"
#include "V2FabricationDock.h"
#include "V2GridDock.h"
#include "V2ExportDock.h"
#include "V2MeasureDock.h"
#include "V2ParameterDock.h"
#include "V2ExtrudeDock.h"
#include "V2SurfaceDock.h"
#include "V2BooleanDock.h"
#include "V2ThickenDock.h"
#include "V2Ribbon.h"
#include "V2PartDock.h"
#include "V2PatternDock.h"
#include "V2Viewport.h"
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/SurfaceJig.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/base/Ids.h"
#include "V2ArrayChoice.h"
#include "V2ArrayDock.h"
#include "kachakacha/app/ApproxInput.h"
#include "kachakacha/app/RevolveSurface.h"
#include "kachakacha/app/BooleanInputState.h"
#include "kachakacha/app/ShapeRebuild.h"
#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/app/SurfaceRoleAssist.h"
#include "kachakacha/app/ToolRoleLabels.h"

#include "V2ExtrudeDialog.h"
#include "V2MainWindowTypes.h"
#include "V2WorkPlaneDock.h"
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/fabrication/BandPartition.h"
#include "kachakacha/fabrication/BendRadius.h"
#include "kachakacha/fabrication/FreezeState.h"
#include "kachakacha/app/DiagnosticReport.h"
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/app/ProcessSteps.h"
#include "kachakacha/app/StatusLine.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/exporters/PatternExport.h"
#include "kachakacha/fabrication/PatternLayout.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <array>
#include <cstdint>
#include <map>

#include <QColor>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class QAction;
class QMenu;
class QLabel;
class QLineEdit;
class QListWidget;
class QToolBar;
class QComboBox;
class QDockWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class V2OperationPanelHost;
class V2SurfaceEditTool;
class V2SurfaceAnalysisTool;
class V2SolidTool;
class V2EdgeFinishTool;
class V2ShellSplitTool;
class V2HoverEditTool;
class V2LoopFacesTool;

//! 見た目。
enum class UiTheme {
    Normal,
    Windows95,
};

class V2MainWindow final : public QMainWindow {
    friend class V2SurfaceEditTool;       // 面の編集の道具(状態と手順は向こうが持つ)
    friend class V2SurfaceAnalysisTool;   // 面の解析の道具(同じ)
    friend class V2SolidTool;             // 立体を作る(回転体・ロフト立体・スイープ。同じ)
    friend class V2EdgeFinishTool;        // 辺の丸め・面取り(同じ)
    friend class V2ShellSplitTool;        // シェル・分割(同じ)
    friend class V2HoverEditTool;         // トリム・延長・分割(線の上に置いて押す)
    friend class V2LoopFacesTool;         // 線から面(輪を探して面にする)
public:
    V2MainWindow();
    ~V2MainWindow() override;

    //! 窓のところで Enter / Esc を受ける。焦点がどこにあっても同じように効く(3D を一度クリックして焦点を戻す必要を無くすため。オーナー指示 §14)。
    bool eventFilter(QObject* target, QEvent* event) override;
    //! いま Enter / Esc を引き受ける道具が動いているか。試験からも見る。
    [[nodiscard]] bool ToolWantsConfirmKeys() const;
    //! 合図を道具へ渡す。受け止めたら true。試験は窓を出さずにここを叩く。
    bool HandleToolKey(int key, QObject* target);
    //! 押し出しの Enter / Esc。
    bool HandleExtrudeToolKey(int key, QObject* target);
    //! 「面を作る」の Enter / Esc。
    bool HandleSurfaceToolKey(int key);
    //! 「厚み」の Enter / Esc。
    bool HandleThickenToolKey(int key, QObject* target);

    [[nodiscard]] V2Viewport& Viewport() { return *viewport_; }
    [[nodiscard]] kachakacha::v2::app::DrawingSession& Session() { return *session_; }

    void ApplyTheme(UiTheme theme);
    [[nodiscard]] UiTheme Theme() const noexcept { return theme_; }

    //! 道具を選ぶ。案内文が出る。
    void SelectTool(kachakacha::v2::modeling::DrawingTool tool);

    //! 出す先・開く先を尋ねる手立て(既定は Qt のダイアログ。自己試験は差し替えて止まらないようにする)。
    //! 空を返したら「やめた」とみなす。
    void SetPathChooser(std::function<QString(bool forSave)> chooser);

    //! ファイルを開く。開けなければ理由を知らせに出して false を返す。
    bool OpenDocumentFile(const QString& path);
    //! V1 の .kcd を読んで V2 の文書にする。読めないものは名前を挙げて知らせる。
    bool ImportKcdFile(const QString& path);
    //! いま開いているファイル。まだ保存していなければ空。
    [[nodiscard]] QString DocumentPath() const { return documentPath_; }

    //! いまの画面の状態を集めた診断(DIAGNOSTICS_FEATURE_SPEC.md)。
    //! 貼り板を使わずに中身を見られるので、自己試験からも読める。
    [[nodiscard]] kachakacha::v2::app::DiagnosticSnapshot DiagnosticSnapshotNow() const;
    [[nodiscard]] QString DiagnosticText() const;

    //! 選んだものから、押し出しが何を意味するかを読み取る(オーナー指示 2026-09-14)。
    //! 立体と輪郭の順番は問わない。型で役割が決まる。
    [[nodiscard]] kachakacha::v2::app::ExtrudePlan PlanExtrudeFromSelection() const;
    //! その読み取りを日本語にしたもの。画面と試験が同じ文を見る。
    [[nodiscard]] QString ExtrudePlanTextJa() const;
    //! 押し出しの下見を始める。矢印ハンドルと破線が出る。
    void BeginExtrudePreview();
    //! 距離が変わったときに、破線と右の欄を合わせる。
    void UpdateExtrudePreview(double distanceMm);
    //! 拾う候補の並べ替えを、いま足りないスロットに合わせる(§6)。
    void RefreshExtrudePickSlot();
    //! 一番下の一行を書き直す。道具が動いていなければ空にする。
    void ShowToolFooter(const QString& line);
    //! 一番下の一行に、いま出ている言葉。試験から読む。
    [[nodiscard]] QString ToolFooterTextJa() const;
    //! 下見をやめる。確定・取消・道具替えのとき。
    void EndExtrudePreview();
    //! 出ている下見のとおりに作る。Enter から呼ぶ。
    void ConfirmExtrude();
    //! 輪郭が違う平面にあれば、平面ごとに別の押し出しにする(1 回の元に戻すで全部消える)。1 平面なら偽。
    [[nodiscard]] bool ConfirmExtrudeByPlanes(const kachakacha::v2::app::ExtrudeChoice& choice,
        const kachakacha::v2::app::ExtrudePlan& plan);
    //! 棚の欄が変わったので、下見を作り直す。
    void RefreshExtrudeFromDock();
    //! 「詳細...」。窓で決めて、棚と矢印と下見へ映して戻る。**作らない。**
    void EditExtrudeWithDialog();
    //! 決めたひと組を棚と矢印と下見へ映す。試験からも呼ぶ。
    void ApplyExtrudeChoice(const kachakacha::v2::app::ExtrudeChoice& choice);
    //! 「状態」欄を書き直す。通った道も、足りないものも、ここに出す。
    void RefreshExtrudeStatus(const kachakacha::v2::app::ExtrudePlan& plan);
    //! 下見の最中に選択が変わった。写しを作り直して、下見も出し直す。
    void RefreshExtrudeForSelectionChange();

    // ---- 「面を作る」(オーナー指示 2026-09-15 §10〜§13)
    //! 人が使う入口。1度目で棚を出し、2度目で作る。
    void RunSurfaceCreate();
    //! 作り方を変える。**入れたものは捨てない。**
    void ChooseSurfaceMethod(kachakacha::v2::modeling::GuideSurfaceMethod method);
    //! 断面順の決め方を変える。
    void ChooseSurfaceOrdering(kachakacha::v2::app::SurfaceOrdering ordering);
    //! 断面を1つ動かす。手動固定にする。
    void MoveSurfaceSection(int from, int to);
    //! 選んでいるものを、その役割へ入れる(道具を始めたときの取り込み)。
    void AddSelectionToSurfaceSlot(kachakacha::v2::modeling::ChainRole role);
    // ---- 入力欄と 3D の行き来(引継ぎ 2026-09-17 の 1)。V2SurfaceSlots.cpp が持つ。
    //! 3D の選択が変わった。差分を「いまの欄」へ入れる/外す。
    void RefreshSurfaceForSelectionChange();
    //! 3D の選択の印を、欄に入っているものの合計に合わせる。欄が正本。
    void MirrorSurfaceEntriesToSelection();
    //! 「ここへ選ぶ」。以後の 3D クリックはその欄へ入る。
    void ActivateSurfaceSlot(kachakacha::v2::modeling::ChainRole slot);
    //! 「解除」。その欄を空にする。
    void ClearSurfaceSlot(kachakacha::v2::modeling::ChainRole slot);
    //! 入力を空にする。作り方は残す。
    void ResetSurfaceInput();
    // ---- おまかせ(初心者の入口: 役割と作り方を線のつながりから決める)。V2SurfaceRoles.cpp が持つ。
    void ReclassifySurfaceRoles();
    void SetSurfaceWireRole(const kachakacha::v2::base::EntityId& id, kachakacha::v2::app::WireRoleChoice role);
    void UseSurfaceCandidate(int index);
    void ResumeSurfaceAutoRoles();
    //! 作る。
    void ConfirmSurface();
    //! なぜ作れないかを言う。断り方を1か所にまとめる。
    void ReportSurfaceNotReady();
    //! 棚を片付ける。
    void EndSurfacePreview();
    //! 棚へいまの入力を映す。
    void RefreshSurfaceDock();
    //! いまの入力で出来上がる面を、**文書へ書かずに**線で出す(§12)。
    //! 作れないときは下見を消す。見えない形は確定させない。
    void RefreshSurfacePreview();
    //! 3D の中の役割の札(§7)。V2ToolRoleLabels.cpp が持つ。
    void ShowRoleLabels(const std::vector<kachakacha::v2::app::ToolRoleLabel>& labels);
    void RefreshSurfaceRoleLabels();
    void RefreshExtrudeRoleLabels(const kachakacha::v2::app::ExtrudeInputState& state);
    //! その番号のものを画面のどこで指すか。取れなければ空。
    [[nodiscard]] std::optional<kachakacha::v2::geometry::Vector3> PointForRoleLabel(
        const kachakacha::v2::base::EntityId& id) const;
    //! 正本と見比べる場面を作る(§20、V2UiShotStates.cpp)。名前は `ui-extrude-*` 等。
    bool ApplyUiShotState(const QString& name);
    bool ApplyExtrudeShotState(const QString& name);
    bool ApplySurfaceShotState(const QString& name);
    bool DrawRectangleForShot();
    bool PickAnyCurveForShot();
    //! 引継ぎ 2026-09-17 の 7(V2UiShotStatesMore.cpp)。
    bool PickShapeCenterForShot(const kachakacha::v2::base::EntityId& id);
    bool ApplyGuidedLoftShotState();
    bool ApplyApproxShotState();
    bool ApplyBooleanShotState();
    bool ApplyResponsiveShotState(const QString& name);   // 指示書 I-02(V2UiShotStatesResponsive.cpp)
    bool ApplyToolShotState(const QString& name);   // 回転体・丸め・シェル・分割・面積(V2UiShotStatesTools.cpp)
    //! いま下見が出ているか。試験から見る。
    [[nodiscard]] bool SurfacePreviewShown() const { return surfaceSnapshot_.has_value(); }
    //! いま近似の道具が動いているか。試験から見る。
    [[nodiscard]] bool ApproxShelfShown() const noexcept { return approxShelfShown_; }
    [[nodiscard]] const kachakacha::v2::app::ApproxInputState& ApproxInput() const
    {
        return approxInput_;
    }
    [[nodiscard]] const std::vector<kachakacha::v2::app::ApproxCandidateOutcome>&
    ApproxOutcomes() const
    {
        return approxOutcomes_;
    }
    //! 選んだものから分かる事実。作り方を薦めるのに使う。
    [[nodiscard]] kachakacha::v2::app::SurfaceSelectionFacts SurfaceFactsNow() const;
    //! 入力から表を組み立てる。**作る直前の1回だけ。**
    [[nodiscard]] kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>
    SurfaceTableFromInput() const;
    [[nodiscard]] kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>
    SurfaceTableFromInput(const kachakacha::v2::app::SurfaceInputState& input) const;
    //! 「面を作る」の棚。試験から見る。
    [[nodiscard]] V2SurfaceDock& SurfaceDock() { return *surfaceDock_; }
    [[nodiscard]] V2BooleanDock& BooleanDock() { return *booleanDock_; }
    //! いま「足す・引く」の道具が動いているか。試験から見る。
    [[nodiscard]] bool BooleanShelfShown() const noexcept { return booleanShelfShown_; }
    [[nodiscard]] const kachakacha::v2::app::BooleanInputState& BooleanInput() const
    {
        return booleanInput_;
    }
    [[nodiscard]] V2ThickenDock& ThickenDock() { return *thickenDock_; }
    [[nodiscard]] V2SurfaceEditTool& SurfaceEdit() { return *surfaceEdit_; }
    [[nodiscard]] V2SurfaceAnalysisTool& SurfaceAnalysis() { return *surfaceAnalysis_; }
    [[nodiscard]] V2SolidTool& SolidTool() { return *solidTool_; }
    [[nodiscard]] V2EdgeFinishTool& EdgeFinishTool() { return *edgeFinishTool_; }
    [[nodiscard]] V2ShellSplitTool& ShellSplitTool() { return *shellSplitTool_; }
    [[nodiscard]] V2HoverEditTool& HoverEditTool() { return *hoverEdit_; }
    [[nodiscard]] V2LoopFacesTool& LoopFacesTool() { return *loopFaces_; }
    //! いま「厚み」の道具が動いているか。試験から見る。
    [[nodiscard]] bool ThickenShelfShown() const noexcept { return thickenShelfShown_; }
    [[nodiscard]] const kachakacha::v2::app::ThickenInputState& ThickenInput() const
    {
        return thickenInput_;
    }
    //! いまの入力と、おまかせの分類。試験から見る。
    [[nodiscard]] const kachakacha::v2::app::SurfaceInputState& SurfaceInput() const
    {
        return surfaceInput_;
    }
    [[nodiscard]] const kachakacha::v2::app::SurfaceRoleAnalysis& SurfaceRoles() const { return surfaceRoles_; }
    //! 押し出しの棚を出して、読み取りを映す。
    void ShowExtrudeShelf(const kachakacha::v2::app::ExtrudePlan& plan);
    //! 立体の面を1枚ずつ、近似の元にする。「立体を面ごとに分ける」を選んだとき。
    void AppendSolidFaceSources(const kachakacha::v2::base::EntityId& partId,
        const std::string& partName,
        std::vector<kachakacha::v2::app::FabricationSource>& sources) const;
    //! 断ったときに「何枚に分ければ作れるか」を言う一文(製作近似 §5)。分けはしない。
    [[nodiscard]] QString PanelAdviceTextJa(
        const std::vector<kachakacha::v2::base::EntityId>& partIds) const;
    //! 押す面の縁をその場限りの輪郭として取り出す。文書は変えない。
    bool PickFaceProfile();
    //! 抱えていた縁を文書へ入れる。確定のときだけ、compound の中で呼ぶ。
    bool CommitFaceProfileWires(std::vector<kachakacha::v2::base::EntityId>& made);
    //! 抱えていた縁を捨てる。取消・道具替え・確定のあと。
    void ForgetFaceProfile();
    //! 出来た形を文書へ入れる。1回の操作は1回の取り消しで戻る。
    //! 文書を変えるところだけ。残ったら真。呼ぶ側が必ず後始末をする。
    [[nodiscard]] bool CommitExtrudeAtomically(
        const kachakacha::v2::app::ExtrudeChoice& choice,
        const kachakacha::v2::app::ExtrudePlan& plan,
        const kachakacha::v2::modeling::ExtrudeAnalysis& analysis,
        const kachakacha::v2::kernel::ExtrudeBuildResult& built);
    void CommitExtrude(const kachakacha::v2::app::ExtrudeChoice& choice,
        const kachakacha::v2::app::ExtrudePlan& plan,
        const kachakacha::v2::modeling::ExtrudeAnalysis& analysis,
        const kachakacha::v2::kernel::ExtrudeBuildResult& built);
    //! 抱えている面の縁を、押し出しの輪郭にする。文書へは入れない。
    [[nodiscard]] std::vector<kachakacha::v2::modeling::ExtrudeProfile>
    FaceProfilesNow() const;
    //! 面の押し引きを、押し出しの指定(正の距離・向き・足す/引く)へ言い換える。
    //! 0mm など作れない量なら理由を出して偽を返す。
    bool ApplyFacePushPull(kachakacha::v2::app::ExtrudeChoice& choice);
    using PreparedExtrudeChoice = V2PreparedExtrudeChoice;   // 形と説明は V2MainWindowTypes.h
    //! 読み取った入力の片方を外して選び直す(EX-07)。
    //! target が真なら加工する立体、偽なら輪郭・面を外す。もう片方は残す。
    void ReselectExtrudeInput(bool target);
    //! 押し出しの棚。試験から見る。
    [[nodiscard]] V2ExtrudeDock& ExtrudeDock() { return *extrudeDock_; }
    //! 足す・引くの相手の形。NewPart なら空の番号。見つからなければ値を返さない。
    [[nodiscard]] std::optional<kachakacha::v2::modeling::KernelShapeHandle>
    BooleanTargetShapeFor(const kachakacha::v2::app::ExtrudeChoice& choice,
        const kachakacha::v2::app::ExtrudePlan& plan);
    //! 決めごと(距離・向き・演算)を整える。やめたら値を返さない。
    [[nodiscard]] std::optional<PreparedExtrudeChoice> PrepareExtrudeChoice(
        const kachakacha::v2::app::ExtrudePlan& plan,
        const std::vector<kachakacha::v2::modeling::ExtrudeProfile>& profiles);
    //! いまの距離で出来上がる形の輪郭。試験から見る。
    [[nodiscard]] std::vector<kachakacha::v2::geometry::Vector3> ExtrudeOutline() const
    {
        return extrudeOutline_;
    }
    //! 試験から呼ぶ。指定した状態を作ってから画面を描く。
    //! 状態の名前は --manual-state で渡すものと同じ。
    [[nodiscard]] bool ApplyManualState(const QString& name);
    //! いまの文書を一時の場所へ保存して、開き直す。試験から使う。
    //! 保存と読み込みの道は本物と同じものを通す。別の道を作らない。
    [[nodiscard]] bool SaveAndReopen(const QString& fileName);
    //! 選んだ部品を、いま本当に STEP で出せるか。
    //! 名前が並んでいるだけで形が無い、を見分けるために試験から呼ぶ。
    [[nodiscard]] bool CanExportSelectedParts();
    //! いま形を覚えている数(立体+面)。試験から、開き直しで戻ったかを見る。
    [[nodiscard]] int KernelShapeCount() const { return static_cast<int>(partShapes_.size() + guideShapes_.size()); }

    //! いま出ている案内文。
    [[nodiscard]] QString StatusText() const;

    //! 一覧に出ている件数。試験で見る。
    [[nodiscard]] int EntityRowCount() const;
    //! 試験から呼ぶ。一覧のその行を選ぶ(人が左メニューを押したのと同じ道を通る)。
    //! 行が無ければ false。
    bool SelectTreeRowForEntity(const kachakacha::v2::base::EntityId& entityId);
    //! 一覧でいま光っている行の数。3D 画面から写ったかを試験で見る。
    [[nodiscard]] int TreeSelectedRowCount() const;
    //! その棚がいま右に出ているか。試験で見る。
    [[nodiscard]] bool ShelfShown(kachakacha::v2::app::Shelf shelf) const;
    [[nodiscard]] int DiagnosticRowCount() const;

    //! コマンドを1つ実行する。メニューも道具箱もショートカットも、
    //! すべてここを通る。入口を分けない(command-catalog.md §1)。
    void RunCommand(std::string_view id);

    //! そのコマンドがいま使えるか。使えないときの理由も返す。
    [[nodiscard]] bool CommandEnabled(std::string_view id, QString* reasonOut) const;

    //! 台帳の1件に対応する QAction。試験で押せるようにする。
    [[nodiscard]] QAction* ActionFor(std::string_view id) const;

    //! 上位モードを切り替える。選択は消さない。Feature も触らない(UIX-001/003)。
    void SetMode(kachakacha::v2::app::UiMode mode);
    [[nodiscard]] kachakacha::v2::app::UiMode Mode() const noexcept { return mode_; }

    //! いま道具箱に出ているコマンドの数。モードごとに変わる。
    [[nodiscard]] int VisibleCommandCount() const;
    //! いま道具箱に出ている道具の数。モードごとに変わる。
    [[nodiscard]] int VisibleToolCount() const;
    //! 2段目に、その台帳コマンドが道具として出ているか。
    [[nodiscard]] bool ModeToolVisible(std::string_view id) const;
    //! 2段の帯。試験と場面づくりから押す。
    [[nodiscard]] V2Ribbon& Ribbon() { return *ribbon_; }

    //! モードごとの手順(ui-workflows §9 / §10 / §11)。1本の並びとして右に出す。
    [[nodiscard]] int ProcessStepCount() const;
    [[nodiscard]] QString ProcessStepText(int row) const;
    //! その段の「進めない理由」。無ければ空。
    [[nodiscard]] QString ProcessStepReason(int row) const;
    //! いま入れる段の番号。全部済んでいれば0。
    [[nodiscard]] int CurrentProcessStep() const;
    //! 手順の元になる状況。試験から動かして、手順が変わることを見る。
    void SetProcessContext(const kachakacha::v2::app::ProcessContext& context);
    [[nodiscard]] const kachakacha::v2::app::ProcessContext& ProcessContextOf() const { return processContext_; }

    //! 作業中グループ(AT-UIX-006)。上の帯と一覧の両方に出る。
    bool SetActiveGroup(const std::optional<kachakacha::v2::base::GroupId>& groupId);
    [[nodiscard]] QString ActiveGroupText() const;
    //! 一覧に出ているグループ行の名前。試験で見る。
    [[nodiscard]] QString GroupRowText(int row) const;
    [[nodiscard]] std::vector<QTreeWidgetItem*> ExplorerRows() const;
    [[nodiscard]] int GroupRowCount() const;
    //! 上の帯の「まとまり」コンボ。名前ではなく GroupId で切り替える。
    [[nodiscard]] int GroupComboCount() const;
    [[nodiscard]] QString GroupComboText(int index) const;
    [[nodiscard]] int GroupComboCurrent() const;
    void SelectGroupCombo(int index);
    //! 一覧の「原点」ノードの子(3面と3軸)。試験で見る。
    [[nodiscard]] int OriginChildCount() const;
    [[nodiscard]] QString OriginChildText(int row) const;
    //! 軸の行のチェックを切り替える(一覧を押したのと同じ道)。
    void SetAxisShown(int axis, bool shown);
    //! 上の帯の「作図面」コンボ。試験で見る・選ぶ。
    [[nodiscard]] int PlaneComboCount() const;
    [[nodiscard]] QString PlaneComboText(int index) const;
    [[nodiscard]] int PlaneComboCurrent() const;
    void SelectPlaneCombo(int index);
    //! いま作業中の作業平面の id。無ければ Nil。
    [[nodiscard]] const kachakacha::v2::base::EntityId& ActiveWorkPlaneId() const
    { return activeWorkPlaneId_; }

    //! 書き出しの棚(AT-EXP-001)。数は手順の状況から作る。
    [[nodiscard]] V2ExportDock& ExportDock() { return *exportDock_; }
    //! 手順の状況と文書から数を作り直して棚へ渡す。
    void RefreshExportCounts();
    void RefreshProcessContextFromSelection();   //!< 手順の「選んでいる数」を選択から数え直す

    //! 数の棚。板厚や面取り量を式で打てる。
    [[nodiscard]] V2ParameterDock& ParameterDock() { return *parameterDock_; }
    //! 型紙の下見の棚。出来た型紙を紙の形で見る。
    [[nodiscard]] V2PatternDock& PatternDock() { return *patternDock_; }
    //! 部品の棚(V1 の部品タブ)。押し出しの距離・板厚・厚みの付け方・治具・回転体。
    [[nodiscard]] V2PartDock& PartDock() { return *partDock_; }
    //! 部品の棚を、いまの数と選択に合わせて書き直す。
    void RefreshPartDock();

    //! 並べ方を聞く手立て。既定は窓を出す。試験では差し替える。
    //! 2つめの引数が真なら円、偽なら直線。空を返したら「やめた」。
    void SetArrayChooser(
        std::function<std::optional<V2ArrayChoice>(const V2ArrayChoice&, bool)> chooser);
    //! 配列の棚(D-23)。arrayChooser_ が無いときはここで個数・間隔・中心・角度を聞く。
    [[nodiscard]] V2ArrayDock& ArrayDock() { return *arrayDock_; }
    //! 作図の棚(円弧の作り方・補助線・指定点を残す・数値で線を作る)。
    [[nodiscard]] V2DrawingDock& DrawingDock() { return *drawingDock_; }
    //! グリッドの棚と表示の棚(V1 のグリッド欄・表示タブ)。
    [[nodiscard]] V2GridDock& GridDock() { return *gridDock_; }
    [[nodiscard]] V2DisplayDock& DisplayDock() { return *displayDock_; }
    //! 棚の値を場面と画面へ当てる。文書は変えない。
    void ApplyGridChoice(const V2GridChoice& choice);
    void ApplyDisplayChoice(const V2DisplayChoice& choice);
    //! 見え方の設定を画面と棚へ当てる。
    void ApplyDisplaySettings(const kachakacha::v2::app::DisplaySettings& settings);
    //! 棚を前に出す。いまの場面・色を棚へ写してから出す。
    void ShowGridDock();
    void ShowDisplayDock();
    void ShowNumberDock();   //!< 数の棚を前に出す(view.number_settings)
    //! 場面と色から棚の値を作る。
    [[nodiscard]] V2GridChoice CurrentGridChoice() const;
    [[nodiscard]] V2DisplayChoice CurrentDisplayChoice() const;
    //! 道具の設定を場面へ当てる(棚から呼ぶ)。
    void ApplyToolSettings(const kachakacha::v2::modeling::ToolSettings& settings);
    //! 棚の「数値で線を作る」。作れなければ理由を棚と帯に出す。
    void CreateWireFromDock();

    //! 測る棚(PRD-070)。選んだものから測れることを全部出す。
    [[nodiscard]] V2MeasureDock& MeasureDock() { return *measureDock_; }
    //! 編集の棚(V1 の「選択内容の数値編集」)。
    [[nodiscard]] V2EditDock& EditDock() { return *editDock_; }
    //! 右の「現在の操作」の入れ物。試験が見出しの案内を読む。
    [[nodiscard]] V2OperationPanelHost& OperationHost() { return *operationHost_; }
    //! 面取りの棚(V1 の「面取り」欄)。
    [[nodiscard]] V2CornerDock& CornerDock() { return *cornerDock_; }
    //! 面取り/丸めの下見が出ているか。試験から見る。
    [[nodiscard]] bool CornerPreviewShown() const noexcept { return cornerPreviewShown_; }
    //! 製作の棚(V1 の近似モデル画面を 1 枚に)。
    [[nodiscard]] V2FabricationDock& FabricationDock() { return *fabricationDock_; }
    //! 製作の棚を、選んでいる近似モデル・数の棚・方式・固定の種類に合わせる。
    void RefreshFabricationDock();
    //! 製作の棚の欄を持ち直す。範囲の外なら理由を棚に出し、前の値のまま。
    void AdoptFabricationChoice();
    //! 材料と積層を、選んでいる部品・形状ガイド・近似モデルに付ける(SetManufacturingCommand)。
    void ApplyMaterialToSelection(const QString& material, int layers);
    //! 次に作る近似モデルの欄(方式・分割軸・境界・上限・最小幅・再現度)。
    [[nodiscard]] const kachakacha::v2::app::FabricationChoice& FabricationChoice() const
    {
        return fabricationChoice_;
    }
    //! 面取りの棚を選択と数の棚に合わせる(直線 A/B の名前、量)。
    void RefreshCornerDock();
    //! 面取り/丸めの下見(V2CornerPreview.cpp、引継ぎ 2026-09-17 の 6)。
    [[nodiscard]] kachakacha::v2::domain::TransformWireDefinition CornerDefinitionFromDock() const;
    [[nodiscard]] bool CornerPairSelected(std::vector<kachakacha::v2::base::EntityId>* wires) const;
    void RefreshCornerPreview();
    [[nodiscard]] bool HandleCornerToolKey(int key);
    //! 選んでいる線を測り直して棚へ渡す。選択が変わるたびに呼ぶ。
    void RefreshMeasurements();
    //! 作図の道具へ入る。入って終わりなら true。続きがあるなら false。
    [[nodiscard]] bool EnterToolFor(
        const kachakacha::v2::app::CommandDescriptor& command);
    //! 選んだものを隠す / 全部出す / 消す。
    void HideSelected();
    void ShowAllEntities();
    void DeleteSelected();
    //! 状態行と HUD(V2StatusLine.cpp)。文言は core(app/StatusLine)。
    [[nodiscard]] kachakacha::v2::app::StatusLineParts BuildStatusLineParts() const;
    void OnViewportHoverChanged();
    //! 測定の重ね道具(C-16)。持ち替える前に戻り先を覚え、Esc で戻す。
    void RememberToolForMeasure(kachakacha::v2::modeling::DrawingTool next);
    void BackToSelectOrResume();
public:
    //! 状態行と HUD をいまの状態から書き直す。試験はカーソルを動かしたあとに呼ぶ。
    void RefreshStatusLine();
    [[nodiscard]] std::string RunningOperationNameJa() const;   //!< 棚で進める操作の名前(HUD用)
    //! 状態行の左(モード ｜ 道具)と右(座標 ｜ Grid ｜ Snap ｜ キー)。試験から読む。
    [[nodiscard]] QString StatusLeftText() const;
    [[nodiscard]] QString StatusRightText() const;
    //! 測定を重ねているときの戻り先。試験から読む。
    [[nodiscard]] std::optional<kachakacha::v2::modeling::DrawingTool> ToolBeforeMeasure() const noexcept { return toolBeforeMeasure_; }
    //! 選択道具で右クリックしたときのメニュー。V1と同じで、ここだけ出す。
    void ShowSelectMenu(const QPoint& at);
    //! 左の一覧の右クリック(V2ExplorerMenu.cpp)。at は一覧の座標。
    void ShowExplorerMenu(const QPoint& at);
    //! 一覧の献立を組むだけ。試験は exec を通さずに並びを確かめる。
    void BuildExplorerMenu(QMenu& menu);
    //! 「グループへ移動」の小献立。落としたのと同じ道を通す。
    void BuildMoveToGroupMenu(QMenu& menu);
    //! プロパティ: 右の「直す」欄を前に出す。
    void ShowProperties();
    //! 一覧の献立の並び(区切りを除く)。試験から読む。
    [[nodiscard]] std::vector<QString> ExplorerMenuLabels();
    //! 候補つきの同じメニュー。選ばれた候補の番号を返す(台帳のコマンド・閉じたときは値なし)。
    [[nodiscard]] std::optional<int> ShowSelectMenuWithCandidates(const QPoint& at,
        const std::vector<QString>& candidateLabels);
    //! 献立を組むだけ。返すのは候補の区画へ並べた QAction(並びは見出しと同じ)。
    //! 出すのは呼び出し側なので、試験は exec を通さずに並び順を確かめられる。
    std::vector<QAction*> BuildSelectMenu(QMenu& menu,
        const std::vector<QString>& candidateLabels);
    //! 押せるかどうかの材料を作る。数え方は core が決める。
    [[nodiscard]] kachakacha::v2::app::SelectionFacts BuildFactsForCommands() const;
    //! 見え方のコマンドか。V2ViewCommands.cpp が持つ。
    [[nodiscard]] static bool IsViewCommand(std::string_view id);
    void RunViewCommand(std::string_view id);
    void ToggleSnap();
    void SendKeyToViewport(int key);   //!< 棚のキャンセル・確定 = 3D の Esc・Enter
    [[nodiscard]] bool SnapEnabled() const noexcept { return snapEnabled_; }
    //! 見え方の段を当てる。段の中身は core が決める。
    void ApplyDisplayStage(kachakacha::v2::app::DisplayStage stage);
    //! 一覧で名前を書き換え始める(F2)。
    void BeginRenameSelected();
    //! 書き換えた名前を文書へ入れる。空や同じ名前は入れない。
    void RenameEntityFromItem(QTreeWidgetItem* item);
    //! 一覧の行が、どのものを指しているか。指していなければ nullptr。
    [[nodiscard]] const kachakacha::v2::base::EntityId* EntityForItem(
        const QTreeWidgetItem* item) const;
    //! 線の編集を1つ実行して Feature を足す。判断は core にある。
    void RunWireTransform(
        const kachakacha::v2::domain::TransformWireDefinition& definition,
        const QString& labelJa, bool consumesFirstOnly, bool consumesInputs = true, bool perWire = false);
    void RunWireTransformEach(const kachakacha::v2::domain::TransformWireDefinition& definition,
        const QString& labelJa, bool consumesInputs);
    //! 変換を線1本へ当てて、新しいワイヤーを1本作る。作れたら true。
    [[nodiscard]] bool TransformOneWire(
        const kachakacha::v2::domain::TransformWireDefinition& definition,
        kachakacha::v2::base::EntityId entityId, const QString& labelJa);
    //! 部品 1 つに同じ変換を掛けた新しい部品を作る(P-18、V2PartPlaceCommands.cpp)。
    [[nodiscard]] bool TransformOnePart(
        const kachakacha::v2::domain::TransformWireDefinition& definition,
        const kachakacha::v2::base::EntityId& partId, const QString& labelJa, bool replacesSource);
    bool RebuildTransformPartShape(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);
    bool HideConsumedParts(const std::vector<kachakacha::v2::base::EntityId>& parts);
    [[nodiscard]] bool TransformOneEntity(
        const kachakacha::v2::domain::TransformWireDefinition& definition,
        kachakacha::v2::base::EntityId entityId, const QString& labelJa);
    //! 制御点を1つ動かした結果を文書へ入れる。元のワイヤーは置き換える。
    void ReplaceWireSegment(kachakacha::v2::base::EntityId entityId,
        kachakacha::v2::base::SegmentId segmentId,
        const kachakacha::v2::geometry::CurveSegment& replacement);
    //! 移動・複製・鏡映・回転を、いま選んでいる線へ当てる。
    //! 何をするかは core の PlanTransform が決めたものをそのまま使う。
    void ApplyTransformPlan(const kachakacha::v2::modeling::TransformPlan& plan);
    //! トリム・延長は押した場所で意味が決まる。1回だけ押す場所を聞く。
    void BeginTrimOrExtend(bool trim);
    //! 選んだ線を作業平面へ落とす。元の線は残す。
    void ProjectSelectedWires();
    //! 線を、作業平面の向きに沿って形状ガイドの曲面へ落とす(折れ線になる)。
    void ProjectSelectedWiresOntoSurface();
    //! 角をまたぐ窓を、面ごとの区間に分けて落とす(V1 の「複数の面へ回り込み投影」)。
    void WrapProjectSelectedWires();
    //! 固定のコマンドか。V2FreezeCommands.cpp が持つ。
    [[nodiscard]] static bool IsFreezeCommand(std::string_view id);
    void RunFreezeCommand(std::string_view id);
    //! 作り方に付いていかない、ただの線を1つ足す。足せたら EntityId を返す。
    kachakacha::v2::base::EntityId AddPlainWire(
        std::vector<kachakacha::v2::geometry::CurveSegment> segments,
        const char* labelJa);
    //! 選んだものを、作り方に付いていかない形にする。元は隠す。
    void FreezeSelectedDerived();
    //! いまの部材を、型紙と同じ形の線にする。
    void FreezeFabricationState();
    //! 「Target 100%」。いまの曲げ具合を変えずに、100%(目標の形)の状態を固定する。
    void FreezeTargetShape();
    //! 「輪郭を線にする」(F-14)。固定で作るものの設定に関わらず線のみ。
    void FreezeContourWires();
    //! freeze系が共有する道。レールから線・面・部品を作る先を1本化し、
    //! 1つの取り消しで戻せるようひとまとまりにする。戻り値は最後まで進んだか。
    [[nodiscard]] bool FreezeWithDefinition(
        const kachakacha::v2::domain::CreateFabricationModelDefinition& definition,
        const std::string& modelName,
        const kachakacha::v2::app::FabricationEvaluation& evaluated,
        const std::string& stateName, int& wires, int& surfaces, int& parts);
    //! 押し出しの結果を文書へ足す。1つでも入らなければ偽(呼ぶ側はまとめごと無かったことにする)。
    [[nodiscard]] bool AdoptExtrudeResult(const kachakacha::v2::app::ExtrudeChoice& choice,
        const kachakacha::v2::domain::ExtrudeDefinition& definition,
        const kachakacha::v2::kernel::ExtrudeBuildResult& built,
        const std::vector<kachakacha::v2::geometry::CurveSegment>& edges,
        const std::vector<kachakacha::v2::base::EntityId>& inputs);
    //! 選んだ面に厚みを付けて立体にする。工程2の「面をソリッド化する」。
    void RunThickenSurface();
    //! 面と作業平面の間を埋めて立体にする(任意の面まで)。
    void RunThickenSurfaceToPlane();
    //! 治具を作る(V1 の body_surface_jig)。離した面 + 厚みの2手をひとまとまりで。
    void RunSurfaceJig();
    //! 治具の当たり面。すき間 0 なら元の面をそのまま返す。作れなければ空の id。
    [[nodiscard]] kachakacha::v2::base::EntityId JigContactSurface(
        kachakacha::v2::base::EntityId sourceId,
        const kachakacha::v2::app::SurfaceJigPlan& plan);
    //! 当たり面へ厚みを付けて当て板にする。
    [[nodiscard]] bool AddJigSolid(kachakacha::v2::base::EntityId contactId,
        const kachakacha::v2::app::SurfaceJigPlan& plan);
    //! 厚みをどちらへ付けるか。外側・中央・内側。
    kachakacha::v2::fabrication::ThicknessPlacement thicknessPlacement_ =
        kachakacha::v2::fabrication::ThicknessPlacement::Centered;
    //! 作業平面の作り方を選ばせる。差し替えると、棚を開く代わりに答えを聞く(試験用)。
    void SetWorkPlaneChooser(
        std::function<std::optional<WorkPlaneChoice>(const WorkPlaneChoice&,
            const kachakacha::v2::app::WorkPlaneFacts&)>
            chooser);
    //! いまの測定(選んだ線・押した点・測り方)。
    [[nodiscard]] kachakacha::v2::app::MeasureRequest CurrentMeasureRequest() const;
    //! 「寸法を残す」「測定を消去」(V1 と同じ)。
    void KeepMeasuredDimension();
    void ClearMeasurement();
    //! 選んでいるものから、作業平面の可否に要る事実を作る。
    [[nodiscard]] kachakacha::v2::app::WorkPlaneFacts BuildWorkPlaneFacts() const;
    //! 選んでいるものとコンボの平面から、core へ渡す材料を集める。選んだ順を保つ。
    [[nodiscard]] kachakacha::v2::app::WorkPlaneMaterials CollectWorkPlaneMaterials(
        const WorkPlaneChoice& choice) const;
    //! 決めた作り方で作業平面を作り、文書へ入れる。棚の「平面を作る」と試験が通る道。
    void CreateWorkPlaneFromChoice(const WorkPlaneChoice& choice, bool activate);
    //! 作業平面の棚(試験から欄を触る)。
    [[nodiscard]] V2WorkPlaneDock* WorkPlaneDock() const
    {
        return workPlaneDock_;
    }
    //! 作業平面の下見(V2WorkPlanePreview.cpp、D-24)が出ているか。試験から見る。
    [[nodiscard]] bool WorkPlanePreviewShown() const noexcept { return workPlanePreviewShown_; }
    //! 固定で作るものを順に切り替える(ワイヤーのみ → 部品のみ → 両方)。
    void CycleFreezeOutput();
    [[nodiscard]] kachakacha::v2::fabrication::FreezeOutput FreezeOutputInUse() const
    {
        return freezeOutput_;
    }
    //! 組立率を聞く。窓を出さない試験では差し替える。値を返さなければ「やめた」。
    void SetAssemblyChooser(std::function<std::optional<double>(double current)> chooser);
    //! 組立率を変える。文書の作り方を書き換えるので、元に戻せる。
    //! 組立率を当てる。parts が空でなければ、その部材番号だけを曲げる(V1 と同じ)。
    void SetAssemblyPercent(double percent, const QString& parts = QString());
    //! 次に作る近似モデルの方式。試験と帯から読む。
    [[nodiscard]] kachakacha::v2::app::FabricationMethod FabricationMethodInUse() const
    {
        return fabricationMethod_;
    }
    //! 部材に開いた開口の取り分の数(試験用)。窓が帯の型紙へ届いたかを見る。
    [[nodiscard]] int FabricationOpeningCount() const
    {
        int count = 0;
        for (const auto& panel : fabricationPanels_) {
            count += static_cast<int>(panel.openings.size());
        }
        return count;
    }
    //! 直前の作り直しで作れなかったものの名前。作れていれば空。
    //! 帯はすぐ書き換わるので、試験と診断の一覧がここを読む。
    [[nodiscard]] const QString& RebuildProblems() const { return rebuildProblems_; }
    [[nodiscard]] int FabricationModelCount() const { return static_cast<int>(fabricationModels_.size()); }
    //! 選んでいる(または最後に作った)近似モデル。試験から曲げ状態を見るのに使う。
    [[nodiscard]] kachakacha::v2::base::EntityId CurrentFabricationModel() const
    { return CurrentFabricationModelId(); }
    //! 押し出しで選ばせるものを出す。窓を出さない試験では差し替える。
    //! 値を返さなければ「やめた」。
    void SetExtrudeChooser(
        std::function<std::optional<kachakacha::v2::app::ExtrudeChoice>(
            const kachakacha::v2::app::ExtrudeChoice&,
            const kachakacha::v2::app::ExtrudeFacts&)>
            chooser);
    //! いま覚えている押し出しの選択。次に押したときの初期値になる。
    [[nodiscard]] const kachakacha::v2::app::ExtrudeChoice& ExtrudeChoice() const
    {
        return extrudeChoice_;
    }
    //! 選んでいるものから、押し出しの可否に要る事実を作る。
    [[nodiscard]] kachakacha::v2::app::ExtrudeFacts BuildExtrudeFacts(
        const std::vector<kachakacha::v2::modeling::ExtrudeProfile>& profiles) const;
    //! 「ある面まで」の相手に選べるもの。
    [[nodiscard]] std::vector<ExtrudeTargetChoice> ExtrudeTargets() const;
    //! その作業平面の枠。相手として押し出しへ渡す。
    [[nodiscard]] std::optional<kachakacha::v2::modeling::WorkPlaneFrame>
    WorkPlaneFrameOf(const kachakacha::v2::base::EntityId& entityId) const;
    //! 押し出しの輪郭にまとめる。押し出しと作り直しで同じ道を通す。
    [[nodiscard]] std::vector<kachakacha::v2::modeling::ExtrudeProfile>
    ExtrudeProfilesFor(
        const std::vector<kachakacha::v2::base::EntityId>& entityIds) const;
    //! 開き直したときに、立体と面を作り方から作り直す。V2RebuildCommands.cpp が持つ。
    void RebuildKernelShapes();
    [[nodiscard]] bool RebuildOneShape(const kachakacha::v2::app::ShapeRebuildStep& step,
        const kachakacha::v2::domain::Feature& featureRef);
    bool RebuildExtrudeShape(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output, std::size_t ordinal);
    bool RebuildWireCageShape(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);
    bool RebuildBooleanShape(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);
    bool RebuildGuideSurfaceShape(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);
    bool RebuildThickenShape(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);
    //! 形状ガイドのコマンドか。V2GuideCommands.cpp が持つ。
    [[nodiscard]] static bool IsGuideCommand(std::string_view id);
    void RunGuideCommand(std::string_view id);
    //! 回転体(V1 の回転面)。1本目の線を 2本目の直線を軸に回した断面を並べ、ロフトする。
    void CreateRevolvedSurface();
    //! 回転体を「面を作る」の道具で始める(引継ぎ 2026-09-17 の 6)。断面 → 軸 → 下見 → Enter。
    void RunRevolveTool();
    //! 断面(断面の欄)と軸(ガイドの欄 = 軸)から、回転体の要求を組む。下見も確定もこれ1つ。
    [[nodiscard]] kachakacha::v2::base::Result<kachakacha::v2::app::RevolveRequest>
    RevolveAxisFromInput() const;
    [[nodiscard]] kachakacha::v2::modeling::GuideTable WithRowsAdded(
        kachakacha::v2::modeling::GuideTable table, kachakacha::v2::modeling::ChainRole role,
        const std::vector<kachakacha::v2::base::EntityId>& ids) const;
    [[nodiscard]] static kachakacha::v2::modeling::GuideTable WithRevolveAxis(
        kachakacha::v2::modeling::GuideTable table,
        const kachakacha::v2::app::RevolveRequest& axis);
    //! 表 → 要求 → 検査 → kernel。作るときも開き直すときも同じ道を通す。
    //! 離した面のときは、表が指す元の面の handle を渡す。
    [[nodiscard]] std::optional<kachakacha::v2::modeling::GuideSurfaceResult>
    BuildSurfaceFromTable(const kachakacha::v2::modeling::GuideTable& table, bool report);
    //! 作った面を文書へ足し、形を覚える。足せたら面の EntityId、だめなら空。
    kachakacha::v2::base::EntityId AdoptGuideSurface(
        const kachakacha::v2::modeling::GuideTable& table,
        const kachakacha::v2::modeling::GuideSurfaceResult& built,
        const std::vector<kachakacha::v2::base::EntityId>& inputs, const std::string& label);

    //! 役割表の操作(guide.set_method / add_row / append_row / row_* / build / clear)。
    //! V2GuideTableCommands.cpp が持つ。
    void RunGuideTableCommand(std::string_view id);
    void SetGuideMethod();
    void AddSelectionToGuideTable();
    void AppendSelectionToGuideRow();
    void MoveGuideRow(int delta);
    void RemoveGuideRow();
    void ReverseGuideRow();
    void BuildGuideSurfaceFromTable();
    void ClearGuideTable();
    //! 表で選んでいる行。選んでいなければ空。
    [[nodiscard]] std::optional<std::size_t> CurrentGuideRow() const;
    //! その作り方で使う役割を「外形U・断面」のように並べる。
    [[nodiscard]] static QString RolesLabelJa(kachakacha::v2::modeling::GuideSurfaceMethod method);

    //! 試験から呼ぶ。表の行を選ぶ。
    void SelectGuideRow(int row);
    //! 作り方を聞く窓の代わり。試験では窓を出さずに答えを返す。
    //! 候補の並びを受け取り、選んだ位置を返す。空は「やめた」。
    void SetGuideChoiceChooser(
        std::function<std::optional<int>(const QString& title, const QStringList& items,
            int initial)>
            chooser);
    std::function<std::optional<int>(const QString& title, const QStringList& items,
        int initial)>
        guideChoiceChooser_;
    //! id で指した線から面を作る(固定などが呼ぶ)。作り方は「おまかせ」(CreateGuideSurfaceFromSelection)と同じ。
    kachakacha::v2::base::EntityId CreateGuideSurfaceFromWires(
        const std::vector<kachakacha::v2::base::EntityId>& wireIds, const std::string& label);
    //! 出来た面の handle と境界。文書ではなく画面側が覚える。
    std::map<std::string, kachakacha::v2::modeling::KernelShapeHandle> guideShapes_;
    std::map<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>> guideEdges_;
    //! 面の標本。曲がった面を展開するときに要る。
    std::map<std::string, kachakacha::v2::fabrication::SurfacePatchSamples> guideSamples_;

    //! 選んだ作業平面へ正対する。形は変わらない。
    void AlignViewToSelection();
    //! 選んだものが入っているまとまりを作業中にする。選んでいなければ外す。
    void ActivateSelectedGroup();
    //! いまの見え方の段。文書には入らない。
    kachakacha::v2::app::DisplayStage displayStage_ =
        kachakacha::v2::app::DisplayStage::All;

    //! 形状ガイドの役割テーブル(AT-UIX-007)。表は core が持つ。
    [[nodiscard]] const kachakacha::v2::modeling::GuideTable& GuideRoleTable() const
    {
        return guideTable_;
    }
    //! 表を差し替えて画面を作り直す。断られたら知らせに出して、表は変えない。
    bool SetGuideTable(const kachakacha::v2::base::Result<
        kachakacha::v2::modeling::GuideTable>& result);
    //! 表に出ている行数と、行の色(3D色同期の確認に使う)。
    [[nodiscard]] int GuideRowCount() const;
    [[nodiscard]] QColor GuideRowColor(int row) const;
    [[nodiscard]] QString GuideRowText(int row, int column) const;

private:
    void BuildMenus();   // V2Menus.cpp
    void AddMenuCommands(QMenu* menu, std::initializer_list<std::string_view> ids);
    void BuildModeBar();
    void BuildToolPalette();
    //! 2段の帯(V2RibbonCommands.cpp)。道具の QAction は tool ごとに渡す。
    void BuildRibbon(const std::map<std::string, QAction*>& toolActionsByCommand);
    //! 作り方つきの道具(面作成の方式 / 測定の測り方)を押した。
    void RunRibbonVariant(const kachakacha::v2::app::RibbonTool& tool);
    //! いまの道具・作り方を帯の印へ映す。
    void RefreshRibbonState();
    //! 台帳 QAction を2段目のモード別道具として再利用する。
    //! その命令は作図の道具(kToolBindings)として既に並んでいるか。
    [[nodiscard]] static bool IsToolBoundCommand(std::string_view id);
    void RefreshCommandVisibility();
    //! 文書のまとまりと上の帯を同期する。
    void RefreshActiveGroupCombo();
    void ActivateGroupByComboIndex(int index);
    void BuildPanels();
    //! 右の「現在の操作」パネルへ、各操作の設定ページを登録する。
    void BuildRightShelves();
    //! 編集の棚・面取りの棚・製作の棚。BuildRightShelves から呼ぶ。
    void BuildEditingShelves();
    //! 下の帯(道具・作業中グループ・案内文)。
    void BuildStatusBar();
    //! 動かさずに作れる状態(絵だけの状態)。ApplyManualState から呼ぶ。
    [[nodiscard]] bool ApplyStaticState(const QString& name);
    //! 形状ガイドの役割テーブルの見本。ApplyManualState から呼ぶ。
    [[nodiscard]] bool ApplyGuideTableState();
    //! 作図の見本。ApplyManualState から呼ぶ。
    [[nodiscard]] bool ApplyDrawingState(const QString& name);
    //! 選択と書き出しの見本。ApplyManualState から呼ぶ。
    [[nodiscard]] bool ApplySelectionState(const QString& name);
    //! 作業中グループの見本。ApplyManualState から呼ぶ。
    [[nodiscard]] bool ApplyActiveGroupState();
    //! 手順の並びの見本。ApplyManualState から呼ぶ。
    [[nodiscard]] bool ApplyStepsState(const QString& name);
    void RefreshEntityList();
    //! 役割テーブルを画面へ出し直す。色は core の式から取る(画面で作らない)。
    void RefreshGuideTable();
    //! 手順の並びを作り直す。モードを変えたときと、状況が変わったときに呼ぶ。
    void RefreshProcessSteps();
    //! 書き出しの棚を作って、中身を作る手立てを繋ぐ。BuildPanels から呼ぶ。
    void BuildExportDock();
    //! 書き出しの台帳コマンド。棚を出して、形式を選ぶ。
    void RunExportCommand(std::string_view id);
    //! 出す先・開く先を尋ねる。差し替えが無ければ Qt のダイアログを出す。
    [[nodiscard]] QString AskForPath(bool forSave);
    //! 製作のコマンドか。V2FabricationCommands.cpp が持つ。
    [[nodiscard]] static bool IsFabricationCommand(std::string_view id);
    void RunFabricationCommand(std::string_view id);
    void RunSetAssembly();
    void RunFabricationCreate();
    //! 道具に結びついた命令のうち、棚を構えてから相手を選ぶもの(V2BooleanCommands.cpp)。
    [[nodiscard]] bool BeginToolFirstCommand(std::string_view id);
    //! 自分の棚を持つ道具(立体・辺の丸め面取り・シェル分割・面にする)を、keep 以外やめる。
    void EndOwnedToolsBut(const void* keep);
    //! 「足す・引く」の道具(引継ぎ 2026-09-17 の 4、V2BooleanCommands.cpp)。
    void RunBooleanTool(kachakacha::v2::app::BooleanKind kind);
    [[nodiscard]] static kachakacha::v2::app::BooleanKind BooleanKindForCommand(std::string_view id);
    [[nodiscard]] static kachakacha::v2::kernel::BooleanOperation KernelBooleanOperation(
        kachakacha::v2::app::BooleanKind kind);
    void MirrorBooleanToSelection();
    void RefreshBooleanForSelectionChange();
    void RefreshBooleanPreview();
    void RefreshBooleanDock();
    void RefreshBooleanAll();
    void ActivateBooleanSlot(kachakacha::v2::app::BooleanSlot slot);
    void ClearBooleanSlot(kachakacha::v2::app::BooleanSlot slot);
    void ChooseBooleanOperation(kachakacha::v2::app::BooleanKind kind);
    void EndBoolean();
    void ConfirmBoolean();
    //! 「厚み」の道具(指示書 matrix P-10、V2ThickenCommands.cpp)。
    void RunThickenTool();
    void MirrorThickenToSelection();
    void RefreshThickenForSelectionChange();
    void RefreshThickenPreview();
    void RefreshThickenDock();
    void RefreshThickenAll();
    void ReselectThicken();
    void ChooseThickenPlacement(kachakacha::v2::fabrication::ThicknessPlacement value);
    void ChooseThickenToPlane();
    void ChooseThickenTarget(const kachakacha::v2::base::EntityId& planeId);
    void ApplyThickenThicknessMm(double value);
    void EndThicken();
    void ConfirmThicken();
    // ---- 「近似」を道具から始める(引継ぎ 2026-09-17 の 3)。V2ApproxCommands.cpp が持つ。
    [[nodiscard]] kachakacha::v2::domain::CreateFabricationModelDefinition
    ApproxBaseDefinition() const;
    void EvaluateApproxCandidates();
    void ShowApproxPreview();
    void RefreshApproxDock();
    void RefreshApproxAll();
    void MirrorApproxSourcesToSelection();
    void RefreshApproxForSelectionChange();
    void ChooseApproxCandidate(int candidate);
    //! 作り方のカード(標準/少部品優先/精度優先/手動条件)。既定の候補を替える。
    void ChooseApproxPolicy(int policy);
    void ClearApproxSources();
    void EndApprox();
    void ConfirmApprox();
    void RunCreatePattern();
    //! 選んだ線を開口/折り線に(reliefCut=false)、または切れ目に(true)する。
    void AssignOpeningRole(bool reliefCut);
    //! 選んだ線を接続スコープにし、近似の形へ寄せた「_接続」の線を作る。
    void SetConnectionScope();
    //! 定義の接続スコープの線を、名前と線の組で集める。
    [[nodiscard]] std::vector<
        std::pair<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>>>
    ConnectionScopeCurves(
        const kachakacha::v2::domain::CreateFabricationModelDefinition& definition) const;
    //! 定義に書いてある開口・折り線の id から、いまの線を集める。
    [[nodiscard]] kachakacha::v2::app::FabricationMarkings FabricationMarkingsFor(
        const kachakacha::v2::domain::CreateFabricationModelDefinition& definition) const;
    //! 出来た部材(全近似モデルの部材を並べたもの)。型紙はこれを使う。
    std::vector<kachakacha::v2::fabrication::PatternPanel> fabricationPanels_;
    //! 並べた型紙。書き出しはこれを使う。
    std::vector<kachakacha::v2::exporters::PatternPage> patternPages_;
    //! 近似モデルごとの結果。鍵は FabricationModel の EntityId。
    //! 文書には作り方だけが入り、開いたら作り直す(立体と同じ考え)。
    std::map<std::string, kachakacha::v2::app::FabricationEvaluation> fabricationModels_;
    std::function<std::optional<double>(double current)> assemblyChooser_;
    //! 固定で作るもの。V1 と同じく「ワイヤーのみ / 部品のみ / 両方」。
    kachakacha::v2::fabrication::FreezeOutput freezeOutput_ =
        kachakacha::v2::fabrication::FreezeOutput::WiresOnly;
    //! V2 方式(曲げ状態の形が無い)の固定。型紙の線をそのまま置く。
    [[nodiscard]] bool FreezeFlatPanels(
        const std::vector<kachakacha::v2::fabrication::PatternPanel>& panels, int& wires);
    void FreezeFlatOutline();
    void ShowPartEditShelf();
    void FocusFabricationStageFor(std::string_view id);
    //! 点列を直線でつないだ線にする。
    [[nodiscard]] static std::vector<kachakacha::v2::geometry::CurveSegment> PolylineOf(
        const std::vector<kachakacha::v2::geometry::Vector3>& points);
    //! 次に作る近似モデルの方式。V1 方式(帯)と V2 方式(面の分類)を切り替える。
    kachakacha::v2::app::FabricationMethod fabricationMethod_ =
        kachakacha::v2::app::FabricationMethod::BandApproximation;
    //! 選んでいる(または最後に作った)近似モデル。無ければ空の id。
    [[nodiscard]] kachakacha::v2::base::EntityId CurrentFabricationModelId() const;
    //! 元になるものを、いま画面が持っている材料から集める。作るときも作り直すときも同じ。
    [[nodiscard]] std::vector<kachakacha::v2::app::FabricationSource>
    FabricationSourcesFor(const std::vector<kachakacha::v2::base::EntityId>& ids,
        bool splitSolidFaces) const;
    //! 作り方から近似モデルを作り直し、覚える。開き直しから呼ぶ。
    bool RebuildFabricationModel(const kachakacha::v2::domain::Feature& feature,
        const kachakacha::v2::base::EntityId& output);
    //! 全近似モデルの部材を fabricationPanels_ へ並べ直し、曲げ状態の姿勢を画面へ出す。
    void RefreshFabricationView();
    //! 製作モードで押した部材を「対象部材」欄へ(F-05/06/07)。方式/最大誤差も映す。
    void RefreshFabricationPartPickForSelectionChange();
    void RefreshFabricationPartInfo(const kachakacha::v2::base::EntityId& modelId);

    //! 形のコマンドか。V2PartCommands.cpp が持つ。
    [[nodiscard]] static bool IsPartCommand(std::string_view id);
    void RunPartCommand(std::string_view id);
    void RunExtrude();
    void RunWireCage();
    //! 出来た部品を文書へ足す。形は持たせず、作り方だけを持たせる。
    //! 足せたら、その部品の EntityId を返す。足せなければ空を返す。
    //! `inputs` は「何に依っているか」。空なら画面の選択を使う(旧来の呼び口)。
    //! 押し出しは必ず明示して渡す。記録と依存の番号を必ず一致させるため。
    kachakacha::v2::base::EntityId AddPartFeature(kachakacha::v2::domain::FeatureType type,
        kachakacha::v2::domain::FeatureDefinition definition,
        kachakacha::v2::modeling::KernelShapeHandle handle,
        const std::vector<kachakacha::v2::geometry::CurveSegment>& edges,
        const char* labelJa,
        const std::vector<kachakacha::v2::base::EntityId>& inputs = {});
    //! 部品の辺を場面へ出し直す。立体そのものはまだ描かない。
    void RefreshPartEdges();
    //! 押し出しの距離(板厚)。数の棚から取る(決め打ちにするとプラ板を使い分けられない)。
    [[nodiscard]] double ExtrudeDistanceMm() const;
    //! 出す対象の立体を集める。selectedOnly が偽なら見えているものを集める。
    [[nodiscard]] std::vector<kachakacha::v2::modeling::KernelShapeHandle> PartShapesFor(
        bool selectedOnly) const;
    //! 部材のもとになった部品の立体を集める。
    [[nodiscard]] std::vector<kachakacha::v2::modeling::KernelShapeHandle>
        PanelSourceShapes() const;
    //! 選んだ部品の立体が出せる形かを調べる。出す前に言う。
    void ValidateSelectedSolid();
    //! 立体を STEP か STL の中身にする。持っていなければ断る。
    [[nodiscard]] kachakacha::v2::base::Result<std::string> MakeSolidContent(
        const std::vector<kachakacha::v2::modeling::KernelShapeHandle>& shapes,
        kachakacha::v2::app::ExportFormat format);
    //! 出来た立体の handle。文書ではなく画面側が覚える。
    std::function<std::optional<WorkPlaneChoice>(const WorkPlaneChoice&,
        const kachakacha::v2::app::WorkPlaneFacts&)>
        workPlaneChooser_;
    //! 押し出しで前に選んだもの。次に押すときの初期値にする。
    kachakacha::v2::app::ExtrudeChoice extrudeChoice_;
    //! 下見に出している輪郭(折れ線)。押し出しを始めたときに作る。
    std::vector<kachakacha::v2::geometry::Vector3> extrudeOutline_;
    //! 下見に出す輪郭の全部(先頭は extrudeOutline_)。**見えていない輪郭で作らない**(§9)。
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> extrudeOutlines_;
    //! 直前の確定で出来た部品。平面ごとの足す・引くで、次の平面の相手にする。
    std::vector<kachakacha::v2::base::EntityId> adoptedExtrudeParts_;
    using ExtrudeSnapshot = V2ExtrudeSnapshot;   // 形と説明は V2MainWindowTypes.h
    std::optional<ExtrudeSnapshot> extrudeSnapshot_;
    //! 「開始側の輪郭ワイヤー」を作るときの、押す前の輪郭。確定の間だけ持つ。
    //! **元の輪郭は触らない。**ここから新しい文書のワイヤーを作る。
    std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> extrudeStartLoops_;
    //! 押し出しの棚を出しているか。出している間だけ右に並ぶ。
    bool extrudeShelfShown_ = false;
    //! 「面を作る」の棚を出しているか。
    bool surfaceShelfShown_ = false;
    //! 「面を作る」の入力。**画面の欄と1対1。**
    kachakacha::v2::app::SurfaceInputState surfaceInput_;
    //! 3D の選択に映した、欄の合計。差分を読むための前回の写し。
    std::vector<kachakacha::v2::base::EntityId> surfaceMirror_;
    //! 直近の検査が採用した断面の並び(元のワイヤーの番号)。BuildSurfaceFromTable が書く。
    std::vector<kachakacha::v2::base::EntityId> surfaceAdoptedSections_;
    std::string surfaceSolverNote_;   // 検査が決めた作り方の内訳(棚に出す)
    //! 「近似」の道具。対象・候補・結果。確定するまで文書へは入らない。
    bool approxShelfShown_ = false;
    kachakacha::v2::app::ApproxInputState approxInput_;
    std::vector<kachakacha::v2::app::ApproxCandidateOutcome> approxOutcomes_;
    std::vector<std::optional<kachakacha::v2::app::FabricationEvaluation>> approxEvaluations_;
    std::vector<kachakacha::v2::domain::CreateFabricationModelDefinition> approxDefinitions_;
    std::vector<kachakacha::v2::base::EntityId> approxMirror_;
    bool approxMirroring_ = false;
    //! 「足す・引く」の道具。土台・相手・下見の形。確定するまで文書へは入らない。
    bool booleanShelfShown_ = false;
    kachakacha::v2::app::BooleanInputState booleanInput_;
    kachakacha::v2::app::BooleanPreviewOutcome booleanOutcome_;
    //! 下見に使った形。**確定はこれをそのまま入れる。**
    std::optional<kachakacha::v2::modeling::KernelShapeHandle> booleanBuilt_;
    std::vector<kachakacha::v2::base::EntityId> booleanMirror_;
    bool booleanMirroring_ = false;
    V2BooleanDock* booleanDock_ = nullptr;
    //! 「厚み」の道具。面・作り方・相手の平面・下見の形。確定するまで文書へは入らない。
    bool thickenShelfShown_ = false;
    kachakacha::v2::app::ThickenInputState thickenInput_;
    kachakacha::v2::app::ThickenPreviewOutcome thickenOutcome_;
    //! 下見に使った形。**確定はこれをそのまま入れる。**
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> thickenBuilt_;   // 面ごと(欄と同じ並び)
    std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> thickenBuiltEdges_;
    std::vector<double> thickenBuiltThickness_;
    //! 3D の選択に映した、面の写し(空なら Nil 相当)。差分を読むための前回の写し。
    std::vector<kachakacha::v2::base::EntityId> thickenMirror_;
    bool thickenMirroring_ = false;
    V2ThickenDock* thickenDock_ = nullptr;
    std::unique_ptr<V2SurfaceEditTool> surfaceEdit_;
    std::unique_ptr<V2SurfaceAnalysisTool> surfaceAnalysis_;
    std::unique_ptr<V2SolidTool> solidTool_;
    std::unique_ptr<V2EdgeFinishTool> edgeFinishTool_;
    std::unique_ptr<V2ShellSplitTool> shellSplitTool_;
    std::unique_ptr<V2HoverEditTool> hoverEdit_;
    std::unique_ptr<V2LoopFacesTool> loopFaces_;
    //! 自分の棚を持つ道具(立体を作る・辺の丸め面取り)が構えていれば、その棚。無ければ None。
    [[nodiscard]] kachakacha::v2::app::Shelf OwnedToolShelf() const;
    //! 自分で選択を入れ替えている最中(その便りは読まない)。
    bool surfaceMirroring_ = false;
    using SurfaceSnapshot = V2SurfaceSnapshot;   // 形と説明は V2MainWindowTypes.h
    std::optional<SurfaceSnapshot> surfaceSnapshot_;
    kachakacha::v2::app::SurfaceRoleAnalysis surfaceRoles_;
    V2SurfaceDock* surfaceDock_ = nullptr;
    [[nodiscard]] std::vector<std::vector<kachakacha::v2::geometry::Vector3>>
    ExtrudePreviewLoops(double distanceMm) const;
    //! 下見に敷くうすい面(§8)。ソリッドを作るときだけ返す。
    [[nodiscard]] std::vector<std::vector<kachakacha::v2::geometry::Vector3>>
    ExtrudePreviewFaces(double distanceMm) const;
    std::function<std::optional<kachakacha::v2::app::ExtrudeChoice>(
        const kachakacha::v2::app::ExtrudeChoice&,
        const kachakacha::v2::app::ExtrudeFacts&)>
        extrudeChooser_;
    std::map<std::string, kachakacha::v2::modeling::KernelShapeHandle> partShapes_;
    //! 押す面の縁。**確定するまで文書へ入れない。** その場限りの値。
    std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> faceProfileLoops_;
    //! その縁を持っている立体。足す・引くの相手になる。
    kachakacha::v2::base::EntityId faceProfileSolid_;
    //! 直前に押した面の外向き法線(EX-02)。矢印と押す向きに使う。その場限りの値。
    kachakacha::v2::geometry::Vector3 faceNormal_{0.0, 0.0, 1.0};
    //! いまの押し出しが「面の押し引き」か。矢印の向きと足す/引くの決め方が変わる。
    bool facePushPull_ = false;
    //! 部品を見せるための辺。
    std::map<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>> partEdges_;
    //! 型紙にするときの「平らな1枚」。立体の辺を全部使うと平らにならない。
    std::map<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>>
        partFlatBoundary_;

    //! 基準のコマンドか。V2PlaneCommands.cpp が持つ。
    [[nodiscard]] static bool IsPlaneCommand(std::string_view id);
    void RunPlaneCommand(std::string_view id);
    //! 「作業平面を作る」。棚を出して作り方を選ばせる(試験では差し替えた答えで作る)。
    void RunWorkPlaneCreate();
    //! 棚の「平面を作る」を押したとき。
    void CreateWorkPlaneFromDock();
    //! 棚とコンボへ、いまの選択と文書の平面一覧を出し直す。
    void RefreshWorkPlaneDock();
    //! 作業平面の下見を出し直す(V2WorkPlanePreview.cpp、D-24)。
    //! 棚が見えていて、いまの欄から平面が組み立てられる間だけ出す。
    void RefreshWorkPlanePreview();
    //! その作業平面を作業中にする(コンボ・一覧・コマンドが同じ道を通る)。
    bool ActivateWorkPlaneById(const kachakacha::v2::base::EntityId& id);
    //! 作業中の作図面に正対する(上の帯の「正対」)。
    void AlignViewToActiveWorkPlane();
    //! 上の帯の「作図面」コンボ。
    QComboBox* planeCombo_ = nullptr;
    std::vector<kachakacha::v2::base::EntityId> planeComboIds_;
    bool refreshingPlaneCombo_ = false;
    //! 押した場所へグリッドの原点を動かす。
    void MoveGridOriginByClick();
    //! 選んでいる作業平面を作業中にする。
    void ActivateSelectedWorkPlane();
    //! 作業平面を画面と場面へ反映する。
    void ApplyWorkPlane(const kachakacha::v2::modeling::WorkPlaneFrame& frame,
        const kachakacha::v2::base::EntityId& entityId);
    //! 次に作る標準面(棚の初期値)。作るたびに XY→YZ→ZX と回す。
    kachakacha::v2::modeling::StandardPlaneKind nextStandardPlane_ =
        kachakacha::v2::modeling::StandardPlaneKind::ZX;
    V2WorkPlaneDock* workPlaneDock_ = nullptr;
    //! 作業平面の下見(40mm四方の四角)を出しているか(D-24)。
    bool workPlanePreviewShown_ = false;
    //! いま作業中の作業平面。無ければ空。
    kachakacha::v2::base::EntityId activeWorkPlaneId_;
    //! 一覧の「原点」ノードの軸の行(X/Y/Z)。チェックで表示を切り替える。
    std::array<QTreeWidgetItem*, 3> axisItems_{};

    //! 選んだ線どうしの交点に作図点を作る(V1 の「交点に点」)。
    void CopyDiagnostics();
    void MakeIntersectionPoints();
    //! 選んだ線の形から作図点を作る。centersOnly なら円・円弧の中心だけ、
    //! そうでなければ始点・終点・中点。線は変えない。
    void MakePointsFromCurves(bool centersOnly);
    //! 選んだ線を基準線にする / やめる。
    void SetSelectedDatum(bool datum);
    //! 棚を出す命令(測定・数値で編集)か。V2EditCommands.cpp が持つ。
    [[nodiscard]] static bool IsShelfCommand(std::string_view id);
    void RunShelfCommand(std::string_view id);
    //! 元に戻す / やり直す。文書を戻したあと、場面(画面の線)と立体も作り直す。
    void RunHistoryCommand(bool undo);
    //! 編集の棚(V1 の「選択内容の数値編集」)。選んでいるものの欄を出し直す。
    void RefreshEditDock();
    //! 事実の行(載る面・長さ・端のつながり)を編集の棚に出す(線を 1 本選んだとき)。
    void ShowWireFacts(const kachakacha::v2::base::EntityId& wireId);
    //! 事実の行の [寄せる]: 選んだ直線の端(atEnd: 終点側)を、近い相手の端へ動かす。
    void CloseSelectedWireEnd(bool atEnd);
    //! 「変更を適用」。欄の値を core で定義にし、文書へ入れる。
    //! wireOverride があれば、棚の欄ではなくその欄で線を直す(事実の行の [寄せる] が正確な点を渡す)。
    void ApplySelectedEdit(const kachakacha::v2::app::WireEditFields* wireOverride = nullptr);
    //! 「平面内角度」の基準(作成元平面か、作業中の平面)。name にその名前を書く。
    [[nodiscard]] kachakacha::v2::modeling::WorkPlaneFrame EditAngleFrame(
        const std::optional<kachakacha::v2::base::EntityId>& sourcePlaneId,
        QString* name) const;
    //! 線の編集コマンドか。V2WireCommands.cpp が持つ。
    [[nodiscard]] static bool IsWireEditCommand(std::string_view id);
    //! 線の編集を通す。判断は core にあり、ここは渡すだけ。
    void RunWireEditCommand(std::string_view id);
    //! 使い切った線を消す。下流がいて消せなければ、表示だけ消す。
    void RemoveConsumedWires(
        const std::vector<kachakacha::v2::base::EntityId>& entityIds);
    //! 文書が変わったあとの後始末。場面・選択・一覧・件数を作り直す。
    void AdoptCurrentDocument();
    //! 面取り量・丸め半径・オフセット距離。数値入力が付くまでの既定値。
    //! 面取り量 / 丸め半径。数の棚から取る。
    [[nodiscard]] double CornerSizeMm() const;

    //! ファイルの台帳コマンド。新規・開く・保存・名前を付けて保存。
    void RunFileCommand(std::string_view id);
    //! 文書を入れ替えて、場面と一覧を作り直す。開いた直後の後始末を1か所にまとめる。
    void AdoptDocument(kachakacha::v2::document::DocumentSnapshot snapshot);
    //! 頼まれた組合せの中身を作る。作れないものは断る。
    [[nodiscard]] kachakacha::v2::base::Result<std::string> MakeExportContent(
        const kachakacha::v2::app::ExportRequest& request);
    //! 足りない役割の案内だけを消す・足す。ほかの知らせは残す。
    void ClearGuideGuidance();
    void AddGuideGuidance(const QString& text);
    //! 案内を作り直して画面へ出す。6つがそろった形で出す(AT-UIX-002)。
    void RefreshGuide();
    void SetStatus(const QString& text);
    void AddDiagnostic(const QString& codeAndText);
    //! 診断を知らせと帯の両方へ出す。
    void ReportDiagnostics(
        const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics);
    void ClearDiagnostics();

    std::unique_ptr<kachakacha::v2::base::IdGenerator> ids_;
    std::unique_ptr<kachakacha::v2::app::DrawingSession> session_;
    V2Viewport* viewport_ = nullptr;
    QToolBar* toolPalette_ = nullptr;
    V2Ribbon* ribbon_ = nullptr;
    class V2EntityTree* entityTree_ = nullptr;
    QLineEdit* entityFilter_ = nullptr;
    //! 左の一覧で選んだものを、3D 画面の選択にする(V1 と同じ)。
    //! まとまりの行を選んだら、その下のもの全部へ広げる。
    void AdoptTreeSelection();
    //! 3D 画面の選択を、左の一覧の光り方へ写す。最後のものまで送る。
    void HighlightTreeForSelection();
    //! 画面から窓へ戻ってくる知らせを、まとめて繋ぐ。組み立ての続き。
    void WireViewportCallbacks();
    void HandleSelectionChanged();
    using FacingTarget = V2FacingTarget;   // 形と説明は V2MainWindowTypes.h
    //! 次の「選択に正対」で、わざと裏側から見るか。「反対側から正対」が立てる。
    bool facingFromBehind_ = false;
    //! 直前の作り直しで作れなかったものの名前。作れていれば空。
    QString rebuildProblems_;
    void CollectFacingTarget(FacingTarget& target) const;
    //! 向きを持つ相手が見つかった。最初の1つだけが向きを決める。
    static void NoteFacingDirection(FacingTarget& target,
        const kachakacha::v2::geometry::Vector3& normal,
        const kachakacha::v2::geometry::Vector3& uAxis);
    //! まとまりの行を、入れ子のまま作る。作った行を id 文字列で引けるようにする。
    void BuildGroupItems(std::map<std::string, QTreeWidgetItem*>& byGroupId,
        QTreeWidgetItem* groupsRoot);
    //! Model Explorer の組み立て(V2ExplorerBuild.cpp)。
    void BuildOriginRows(QTreeWidgetItem* originRoot);
    QTreeWidgetItem* AddEntityRow(QTreeWidgetItem* parent,
        const kachakacha::v2::domain::Entity& entity);
    void AddApproximationRows(QTreeWidgetItem* modelItem,
        const kachakacha::v2::domain::Entity& entity);
    void AddGeneratedRows(QTreeWidgetItem* modelItem, const kachakacha::v2::domain::Entity& model);
    [[nodiscard]] bool ToggleEntityVisibilityFromItem(QTreeWidgetItem* item);
    [[nodiscard]] QString DocumentDisplayName() const;
public:
    //! 選んだ線を全部「断面」にして一気に面を作る(以前の `guide.create`)。人の入口は
    //! 「面を作る」1 つに統一したので献立から外した。いまは自己試験だけが呼ぶ。
    void CreateGuideSurfaceFromSelection();
    //! 部材の分割と統合(§32)。判断は core(`fabrication/BandPartition`)がする。
    void MergeFabricationParts();
    void SplitFabricationPart();
    //! いまの帯の境目と、部材ごとの幅。近似がまだなら偽。
    [[nodiscard]] bool CurrentBandPartition(std::vector<double>& railParameters,
        std::vector<double>& widthsMm) const;
    //! 見せた候補をそのまま文書へ書く。見せた形と出来た形を食い違わせない。
    //! 1度目は見せるだけ、2度目で当てる。
    void ProposeOrApplyPartition(const QString& what,
        const std::vector<std::size_t>& numbers,
        const kachakacha::v2::fabrication::BandPartitionPreview& preview,
        const kachakacha::v2::fabrication::BandValueRemap& carried);
    void ApplyBandPartition(
        const kachakacha::v2::fabrication::BandPartitionPreview& preview,
        const QString& what,
        const kachakacha::v2::fabrication::BandValueRemap& carried);
    //! いま部材ごとに持っている値。引き継ぎの元になる。
    [[nodiscard]] kachakacha::v2::fabrication::BandValueRemap BandValuesNow() const;
    using PendingPartition = V2PendingPartition;   // 形と説明は V2MainWindowTypes.h
    std::optional<PendingPartition> pendingPartition_;
public:
    //! 見せている案を捨てる。やめたとき・道具を替えたときに通る。
    void ForgetPendingPartition();
    //! 案を見せた相手と値の指紋。案が古くなっていないかを見る。
    [[nodiscard]] std::string FabricationInputSignature() const;
    //! 「曲げる部材」の欄が、書いてあるのに読めない状態か(空欄とは違う)。
    [[nodiscard]] bool PartNumbersUnreadable() const;
    //! 文書に入った境目が、当てようとした境目と同じか。
    [[nodiscard]] bool BoundariesMatch(const std::vector<double>& inner) const;
    //! 見せた案と、いま出した案が同じものか。
    [[nodiscard]] static bool SamePartitionProposal(
        const kachakacha::v2::fabrication::BandPartitionPreview& shown,
        const kachakacha::v2::fabrication::BandPartitionPreview& now);
    //! 捨てる値の言い方。見せるときと済んだあとで同じ文を使う。
    [[nodiscard]] static QString DroppedValuesTextJa(
        const kachakacha::v2::fabrication::BandValueRemap& carried);
    //! いま案を見せているか。試験から見る。
    [[nodiscard]] bool PendingPartitionShown() const { return pendingPartition_.has_value(); }
    //! 展開の基準にする辺を決める(§33)。棚の「曲げる部材」の番号で選ぶ。
    void SetUnfoldBaseRail();
    //! 帯の境目を文書へ書き、以後は自動で切り直さない(§32)。
    bool ApplyBandBoundaries(const std::vector<double>& inner, const QString& what,
        const std::string& messageJa,
        const kachakacha::v2::fabrication::BandValueRemap& carried);
    //! いま決まっている、展開の基準にする辺。試験から見る。
    [[nodiscard]] int UnfoldBaseRailNow() const;
    //! いまの製作モデルの部材の数。
    [[nodiscard]] std::size_t FabricationPanelCount() const;
    using PartNumberSelection = V2PartNumberSelection;   // 形と説明は V2MainWindowTypes.h
    [[nodiscard]] PartNumberSelection ReadPartNumbers() const;
    //! 棚の「曲げる部材」に書いた番号。0 起点。
    [[nodiscard]] std::vector<std::size_t> SelectedPartNumbers() const;
    //! 押し出す向き。矢印・下見・確定形状はすべてここから取る。
    [[nodiscard]] kachakacha::v2::geometry::Vector3 ExtrudeDirectionNow() const;
    //! 反転を掛ける前の押し出しの向き。反転は棚が持つので二重に掛けない。
    [[nodiscard]] kachakacha::v2::geometry::Vector3 ExtrudeBaseDirectionNow() const;
    //! 決め方ひとつを向きひとつに解く。**7通りすべてここで解く。**
    //! 矢印・下見・確定・保存する作り方が、みなここを通る。
    [[nodiscard]] kachakacha::v2::geometry::Vector3 ExtrudeDirectionForMode(
        kachakacha::v2::modeling::ExtrudeDirectionMode mode,
        const kachakacha::v2::geometry::Vector3& custom,
        const std::vector<kachakacha::v2::geometry::Vector3>* outline = nullptr) const;
    //! 曲げた先の半径を測り直す。固定してあれば触らない(§31)。
    void RefreshBendRadius();
    //! いまの組立率(0〜100)。近似モデルが無ければ 100。
    [[nodiscard]] double AssemblyPercentNow() const;
    //! 「固定」「固定を外す」を押した。試験からも呼ぶ。
    void ApplyBendRadius(double radiusMm, bool locked);
    //! いま棚に出ている部材の曲げと半径。試験から見る。
    //! 値は画面ではなく **文書** が持つ。ここは読み出すだけ。
    [[nodiscard]] kachakacha::v2::fabrication::BendRadius BendRadiusNow() const;
    //! 部材ごとの曲げと半径。測った値に、固定してある分を重ねたもの。
    [[nodiscard]] std::vector<kachakacha::v2::fabrication::BendRadius>
    BendRadiiNow() const;
    //! その作り方で面を作れるか試す。文書は変えない。
    //! 作れたら空、作れなければ断った理由を返す。試験が作り方を選ぶために使う。
    [[nodiscard]] QString TryBuildSurface(
        const kachakacha::v2::domain::CreateGuideSurfaceDefinition& definition);
    //! いまの近似モデルの作り方。無ければ空。
    [[nodiscard]] const kachakacha::v2::domain::CreateFabricationModelDefinition*
    CurrentFabricationDefinition() const;
    //! いまの曲げ状態での形の指紋。試験が「形が本当に変わったか」を見るために読む。
    [[nodiscard]] std::string FabricationShapeSignature() const;
private:
    //! 整理用まとまりの操作(§7〜13)。
    [[nodiscard]] static bool IsGroupCommand(std::string_view id);
    void RunGroupCommand(std::string_view id);
    void CreateGroupFromSelection();
    void DissolveSelectedGroup();
    void RenameSelectedGroup();
    //! いま左の一覧で選んでいるまとまり。
    [[nodiscard]] std::optional<kachakacha::v2::base::GroupId> SelectedGroupId() const;
    //! 次に作るまとまりの名前。同じ名前が並ばないように番号を送る。
    [[nodiscard]] std::string NextGroupName() const;

public:
    //! 木の行が書き換わった。まとまりの行なら名前か出し隠しとして扱い、真を返す。
    bool RenameOrToggleGroupFromItem(QTreeWidgetItem* item);
    //! 引きずって落とした。落ちた先のまとまりへ入れる。試験からも呼ぶ。
    void DropTreeItemsOnto(const std::vector<QTreeWidgetItem*>& moved,
        QTreeWidgetItem* onto);
    //! その物の行。無ければ空。試験が引きずる相手を引くのに使う。
    [[nodiscard]] QTreeWidgetItem* ItemOfEntity(
        const kachakacha::v2::base::EntityId& id) const;
    //! 左の一覧。試験が行を引くために読む。
    [[nodiscard]] class V2EntityTree* EntityTree() const { return entityTree_; }
    //! まとまりの行。試験が引きずる相手を引くために読む。
    [[nodiscard]] const std::vector<std::pair<QTreeWidgetItem*,
        kachakacha::v2::base::GroupId>>&
    GroupItems() const
    {
        return groupItems_;
    }

private:
    //! 立体を正対の相手にする。面を選んでいればその面だけ。集まったら真。
    [[nodiscard]] bool AppendSolidFacing(const kachakacha::v2::base::EntityId& id,
        FacingTarget& target) const;
    //! 形状ガイドの面を正対の相手にする。集まったら真。
    [[nodiscard]] bool AppendSurfaceFacing(const kachakacha::v2::base::EntityId& id,
        FacingTarget& target) const;
    //! 画面に出している網の点を集める。向きは推させる。集まったら真。
    [[nodiscard]] bool AppendMeshPoints(const kachakacha::v2::base::EntityId& id,
        FacingTarget& target) const;
    //! 文書の作図面を、画面に出す形へ写す。一覧を作り直すたびに呼ぶ。
    void RefreshWorkPlaneViews();
    //! 右に出す棚を、いまの道具とモードに合わせる。どれを出すかは core が決める。
    void RefreshRightShelves();
    //! 核の形(立体・面)を三角形にして画面へ渡す。番号が同じなら作り直さない。
    void RefreshShapeViews();

public:
    //! 構えている命令の表示名。構えていなければ空。試験と帯で見る。
    [[nodiscard]] QString PendingCommandLabel() const;
    //! 構えを解く(Esc、モード替え、別の命令)。
    void ClearPendingCommand();
    //! 「これで」と言う(Enter)。そろっていれば走る。
    void ConfirmPendingCommand();

private:
    //! いま使えない命令なら構えて待つ。構えたら true。
    bool ArmCommandIfUnsatisfied(std::string_view id);
    //! 選択が変わったときに呼ぶ。そろっていれば走る(条件によっては Enter を待つ)。
    void RefreshPendingCommand(bool confirmed);
    //! 構えている命令の id。空なら構えていない。
    std::string pendingCommandId_;
    //! 配列(並べて複製する。指示書 D-23)。試験で窓(arrayChooser_)を差し替えていなければ、
    //! 右の棚(Shelf::Array、V2ArrayDock)を出して欄で聞く。
    [[nodiscard]] static bool IsArrayCommand(std::string_view id);
    void RunArrayCommand(std::string_view id);
    void RunLinearArray();
    void RunCircularArray();
    //! 実際に並べる。窓の道でも棚の道でも同じ道を通す。並べられたら真。
    bool CommitLinearArray();
    bool CommitCircularArray();
    void ConfirmArray();
    void EndArray();
    std::function<std::optional<V2ArrayChoice>(const V2ArrayChoice&, bool)> arrayChooser_;
    //! 前に決めた並べ方。次に開いたときの初期値にする。打ち直しを減らす。
    V2ArrayChoice arrayChoice_;
    V2ArrayDock* arrayDock_ = nullptr;
    //! 文字にした id から Entity を探す。核の形の表が文字の鍵を使っているため。
    [[nodiscard]] static const kachakacha::v2::domain::Entity* FindEntityByIdText(
        const kachakacha::v2::document::DocumentSnapshot& snapshot,
        const std::string& idText);
    //! 作った網を覚えておく表。鍵は形の番号(handle)。作り直しは重い。
    std::map<std::uint64_t, kachakacha::v2::modeling::ShapeMesh> shapeMeshes_;
    //! 左の一覧の棚を組み立てる。組み立てたものを返す(並べ方は呼び出し側が決める)。
    QDockWidget* BuildEntityTreeDock();
    //! 形状ガイドの役割の表と、表を動かすボタン(表の隣に置く)。
    QWidget* BuildGuideTableBody(QWidget* parent);
    //! 手順・書き出し・現在の操作パネル・知らせ。BuildPanels の続き。
    void BuildRemainingPanels(QDockWidget* treeDock);
    //! 部品の棚と型紙の下見と数の棚。BuildRightShelves の続き。
    void BuildOutputShelves();
    //! 「厚み」の棚の組み立て。BuildOutputShelves から切り出した(1関数100行の門)。
    void BuildThickenDock();
    void BuildSurfaceDock();
    //! 一覧を絞り込む(V1 の「名前・種類で絞り込み」)。残すかどうかは core が決める。
    void ApplyEntityTreeFilter();
    //! 絞り込みの語を入れる(試験用)。人が打ったのと同じ道を通る。
public:
    void SetEntityFilterText(const QString& text);
    //! いま一覧に見えている行の数(絞り込みで隠れたものを除く)。試験で見る。
    [[nodiscard]] int VisibleEntityRowCount() const;
private:
    //! 棚と QDockWidget の対応。無ければ nullptr。
    [[nodiscard]] QDockWidget* DockForShelf(kachakacha::v2::app::Shelf shelf) const;
    //! 明示的に開く設定ページを、右の「現在の操作」へ出す。
    void ShowShelf(kachakacha::v2::app::Shelf shelf);
    //! 面の上の「横」の見当。上向き(縦)はここから作る。
    [[nodiscard]] static kachakacha::v2::geometry::Vector3 FacingUAxisHint(
        const std::vector<kachakacha::v2::geometry::Vector3>& points,
        const kachakacha::v2::geometry::Vector3& normal);
    //! 一覧と 3D 画面が呼び合って回らないようにする印。
    bool syncingSelection_ = false;
    //! 一覧の行と、それが指すもの。行に id を持たせられないので横に持つ。
    std::vector<std::pair<QTreeWidgetItem*, kachakacha::v2::base::EntityId>> entityItems_;
    //! まとまりの行。引きずり・改名・出し隠しの相手を引くのに要る。
    std::vector<std::pair<QTreeWidgetItem*, kachakacha::v2::base::GroupId>> groupItems_;
    QListWidget* diagnosticList_ = nullptr;
    QTreeWidget* guideTableView_ = nullptr;
    QDockWidget* guideDock_ = nullptr;
    QDockWidget* operationDock_ = nullptr;
    V2OperationPanelHost* operationHost_ = nullptr;
    QTreeWidget* processView_ = nullptr;
    V2ExportDock* exportDock_ = nullptr;
    V2MeasureDock* measureDock_ = nullptr;
    V2EditDock* editDock_ = nullptr;
    V2CornerDock* cornerDock_ = nullptr;
    //! 面取り/丸めの下見を出しているか(自分が出したものだけ片づけるため)。
    bool cornerPreviewShown_ = false;
    V2FabricationDock* fabricationDock_ = nullptr;
    kachakacha::v2::app::FabricationChoice fabricationChoice_;
    V2ParameterDock* parameterDock_ = nullptr;
    V2PatternDock* patternDock_ = nullptr;
    V2PartDock* partDock_ = nullptr;
    V2ExtrudeDock* extrudeDock_ = nullptr;
    V2DrawingDock* drawingDock_ = nullptr;
    V2GridDock* gridDock_ = nullptr;
    V2DisplayDock* displayDock_ = nullptr;
    //! 「作図モード以外でも表示」を外したときにグリッドを消す。
    void RefreshGridSuppression();
    QDockWidget* processDock_ = nullptr;
    QMenu* viewMenu_ = nullptr;   //!< 表示メニュー(組み立て後に「手順」の出し隠しを足す)
    QDockWidget* diagnosticDock_ = nullptr;
    kachakacha::v2::app::ProcessContext processContext_;
    kachakacha::v2::modeling::GuideTable guideTable_;
    QLabel* statusLabel_ = nullptr;
    QLabel* toolLabel_ = nullptr;
    QLabel* groupLabel_ = nullptr;
    //! 状態行の右側(座標 ｜ Grid ｜ Snap ｜ Enter/Esc)。
    QLabel* cursorLabel_ = nullptr;
    //! 測定を重ねる前に持っていた道具(C-16)。無ければ重ねていない。
    std::optional<kachakacha::v2::modeling::DrawingTool> toolBeforeMeasure_;
    //! 一番下の一行(UI の正本の footer)。いまの道具の入力が全部並ぶ。
    QLabel* toolFooterLabel_ = nullptr;
    //! 合図の説明。**いつでも見えている。**Ctrl を知らなくても使えるように。
    QLabel* keyHintLabel_ = nullptr;
    UiTheme theme_ = UiTheme::Normal;
    //! いま開いているファイル。無ければ空(まだ保存していない)。
    QString documentPath_;
    std::function<QString(bool forSave)> pathChooser_;
    bool snapEnabled_ = true;
    int selectionCount_ = 0;
    kachakacha::v2::app::UiMode mode_ =
        kachakacha::v2::app::UiMode::Drawing;
    QToolBar* modeBar_ = nullptr;
    std::vector<std::pair<kachakacha::v2::app::UiMode, QAction*>> modeActions_;
    std::vector<QAction*> toolActions_;
    QComboBox* groupCombo_ = nullptr;
    std::vector<std::optional<kachakacha::v2::base::GroupId>> groupComboIds_;
    bool refreshingGroupCombo_ = false;
    //! 台帳のIDから作った QAction。並びは台帳と同じ。
    std::vector<std::pair<std::string_view, QAction*>> commandActions_;
};
