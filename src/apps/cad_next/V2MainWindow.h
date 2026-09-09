#pragma once

//! V2の本体窓(WP-08)。
//!
//! 画面は薄く保つ。ここでやるのは
//!   - 道具を選ぶ
//!   - 選んだ道具の案内文を出す
//!   - 出来たものと診断を一覧に出す
//!   - 見た目(Windows 95 / 通常)を切り替える
//! の4つで、幾何の判断はすべて core にある(architecture-and-data.md DOC-002)。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。
//! 信号の受け口はラムダで繋ぐ。

#include "V2ExportDock.h"
#include "V2Viewport.h"
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/app/ProcessSteps.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <map>

#include <QColor>
#include <QMainWindow>
#include <QString>

#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

class QAction;
class QLabel;
class QListWidget;
class QToolBar;
class QDockWidget;
class QTreeWidget;

//! 見た目。
enum class UiTheme {
    Normal,
    Windows95,
};

class V2MainWindow final : public QMainWindow {
public:
    V2MainWindow();
    ~V2MainWindow() override;

    [[nodiscard]] V2Viewport& Viewport() { return *viewport_; }
    [[nodiscard]] kachakacha::v2::app::DrawingSession& Session() { return *session_; }

    void ApplyTheme(UiTheme theme);
    [[nodiscard]] UiTheme Theme() const noexcept { return theme_; }

    //! 道具を選ぶ。案内文が出る。
    void SelectTool(kachakacha::v2::modeling::DrawingTool tool);

    //! 出す先・開く先を尋ねる手立て。既定は Qt のファイルダイアログ。
    //! 画面を出さずに試すときは、ここを差し替える。
    //! 差し替えられないと、自己試験がダイアログの前で止まってしまう。
    //! 空を返したら「やめた」とみなす。
    void SetPathChooser(std::function<QString(bool forSave)> chooser);

    //! ファイルを開く。開けなければ理由を知らせに出して false を返す。
    bool OpenDocumentFile(const QString& path);
    //! いま開いているファイル。まだ保存していなければ空。
    [[nodiscard]] QString DocumentPath() const { return documentPath_; }

    //! 試験から呼ぶ。指定した状態を作ってから画面を描く。
    //! 状態の名前は --manual-state で渡すものと同じ。
    [[nodiscard]] bool ApplyManualState(const QString& name);

    //! いま出ている案内文。
    [[nodiscard]] QString StatusText() const;

    //! 一覧に出ている件数。試験で見る。
    [[nodiscard]] int EntityRowCount() const;
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

    //! モードごとの手順(ui-workflows §9 / §10 / §11)。1本の並びとして右に出す。
    [[nodiscard]] int ProcessStepCount() const;
    [[nodiscard]] QString ProcessStepText(int row) const;
    //! その段の「進めない理由」。無ければ空。
    [[nodiscard]] QString ProcessStepReason(int row) const;
    //! いま入れる段の番号。全部済んでいれば0。
    [[nodiscard]] int CurrentProcessStep() const;
    //! 手順の元になる状況。試験から動かして、手順が変わることを見る。
    void SetProcessContext(const kachakacha::v2::app::ProcessContext& context);
    [[nodiscard]] const kachakacha::v2::app::ProcessContext& ProcessContextOf() const
    {
        return processContext_;
    }

    //! 作業中グループ(AT-UIX-006)。上の帯と一覧の両方に出る。
    bool SetActiveGroup(const std::optional<kachakacha::v2::base::GroupId>& groupId);
    [[nodiscard]] QString ActiveGroupText() const;
    //! 一覧に出ているグループ行の名前。試験で見る。
    [[nodiscard]] QString GroupRowText(int row) const;
    [[nodiscard]] int GroupRowCount() const;

    //! 書き出しの棚(AT-EXP-001)。数は手順の状況から作る。
    [[nodiscard]] V2ExportDock& ExportDock() { return *exportDock_; }
    //! 手順の状況と文書から数を作り直して棚へ渡す。
    void RefreshExportCounts();

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
    void BuildMenus();
    void BuildModeBar();
    void BuildToolPalette();
    void RefreshCommandVisibility();
    void BuildPanels();
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
    //! 形のコマンドか。V2PartCommands.cpp が持つ。
    [[nodiscard]] static bool IsPartCommand(std::string_view id);
    void RunPartCommand(std::string_view id);
    void RunExtrude();
    void RunWireCage();
    void RunBoolean(bool cut);
    //! 出来た部品を文書へ足す。形は持たせず、作り方だけを持たせる。
    void AddPartFeature(kachakacha::v2::domain::FeatureType type,
        kachakacha::v2::domain::FeatureDefinition definition,
        kachakacha::v2::modeling::KernelShapeHandle handle,
        const std::vector<kachakacha::v2::geometry::CurveSegment>& edges,
        const char* labelJa);
    //! 部品の辺を場面へ出し直す。立体そのものはまだ描かない。
    void RefreshPartEdges();
    //! 押し出しの距離。数値入力が付くまでの既定値(プラ板0.5mm)。
    double extrudeDistanceMm_ = 0.5;
    //! 出来た立体の handle。文書ではなく画面側が覚える。
    std::map<std::string, kachakacha::v2::modeling::KernelShapeHandle> partShapes_;
    //! 部品を見せるための辺。
    std::map<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>> partEdges_;

    //! 基準のコマンドか。V2PlaneCommands.cpp が持つ。
    [[nodiscard]] static bool IsPlaneCommand(std::string_view id);
    void RunPlaneCommand(std::string_view id);
    //! 標準面を1つ作って、作業中にする。押すたびに XY→YZ→ZX と回る。
    void CreateStandardWorkPlane();
    //! 選んでいる作業平面を作業中にする。
    void ActivateSelectedWorkPlane();
    //! 作業平面を画面と場面へ反映する。
    void ApplyWorkPlane(const kachakacha::v2::modeling::WorkPlaneFrame& frame,
        const kachakacha::v2::base::EntityId& entityId);
    //! グリッドの間隔を順ぐりに変える。
    void CycleGridSpacing();
    //! 次に作る標準面。
    kachakacha::v2::modeling::StandardPlaneKind nextStandardPlane_ =
        kachakacha::v2::modeling::StandardPlaneKind::ZX;
    //! いま作業中の作業平面。無ければ空。
    kachakacha::v2::base::EntityId activeWorkPlaneId_;

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
    double wireEditSizeMm_ = 2.0;

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
    QTreeWidget* entityTree_ = nullptr;
    QListWidget* diagnosticList_ = nullptr;
    QTreeWidget* guideTableView_ = nullptr;
    QDockWidget* guideDock_ = nullptr;
    QTreeWidget* processView_ = nullptr;
    V2ExportDock* exportDock_ = nullptr;
    QDockWidget* processDock_ = nullptr;
    kachakacha::v2::app::ProcessContext processContext_;
    kachakacha::v2::modeling::GuideTable guideTable_;
    QLabel* statusLabel_ = nullptr;
    QLabel* toolLabel_ = nullptr;
    QLabel* groupLabel_ = nullptr;
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
    //! 台帳のIDから作った QAction。並びは台帳と同じ。
    std::vector<std::pair<std::string_view, QAction*>> commandActions_;
};
