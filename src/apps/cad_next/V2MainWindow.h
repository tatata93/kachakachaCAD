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

#include "V2Viewport.h"
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/base/Ids.h"

#include <QMainWindow>
#include <QString>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

class QAction;
class QLabel;
class QListWidget;
class QToolBar;
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

private:
    void BuildMenus();
    void BuildModeBar();
    void BuildToolPalette();
    void RefreshCommandVisibility();
    void BuildPanels();
    //! 動かさずに作れる状態(絵だけの状態)。ApplyManualState から呼ぶ。
    [[nodiscard]] bool ApplyStaticState(const QString& name);
    void RefreshEntityList();
    //! 案内を作り直して画面へ出す。6つがそろった形で出す(AT-UIX-002)。
    void RefreshGuide();
    void SetStatus(const QString& text);
    void AddDiagnostic(const QString& codeAndText);
    void ClearDiagnostics();

    std::unique_ptr<kachakacha::v2::base::IdGenerator> ids_;
    std::unique_ptr<kachakacha::v2::app::DrawingSession> session_;
    V2Viewport* viewport_ = nullptr;
    QToolBar* toolPalette_ = nullptr;
    QTreeWidget* entityTree_ = nullptr;
    QListWidget* diagnosticList_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* toolLabel_ = nullptr;
    UiTheme theme_ = UiTheme::Normal;
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
