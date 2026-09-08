#pragma once

//! 作図の1本の流れ(WP-08)。
//!
//! スナップ → ツール → 文書コマンド を1つに繋ぐ層。
//! Qt はこの上に薄く乗るだけにする。
//! こうしておくと、画面が無くても作図の筋道を試験できる。
//! V1はこの流れが Viewport の中に埋まっていたので、
//! 画面を出さないと何も確かめられなかった。

#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/modeling/SnapEngine.h"
#include "kachakacha/modeling/ToolController.h"

#include <memory>
#include <optional>
#include <string>

namespace kachakacha::v2::app {

using document::Document;
using geometry::ScreenMapping;
using geometry::ScreenPoint;
using geometry::Vector3;
using modeling::DrawingTool;
using modeling::SnapCandidate;
using modeling::SnapScene;
using modeling::SnapSettings;
using modeling::ToolSettings;

//! ポインタを動かしたときに画面へ返すもの。
struct HoverResult {
    std::optional<SnapCandidate> snap;
    //! 吸着した位置。吸着していなければ作業平面上の点。
    std::optional<Vector3> position;
    //! 途中経過の形。
    std::vector<geometry::CurveSegment> preview;
    //! 画面に出す案内。
    std::string messageJa;
};

//! クリックしたときの結果。
struct ClickResult {
    bool placedPoint = false;
    bool committed = false;        //!< 文書が変わったか
    std::string commandLabel;
    std::vector<base::Diagnostic> diagnostics;
    std::vector<base::EntityId> createdEntityIds;
};

class DrawingSession {
public:
    DrawingSession(base::DocumentId documentId, base::IdGenerator& ids);

    [[nodiscard]] Document& GetDocument() noexcept { return document_; }
    [[nodiscard]] const Document& GetDocument() const noexcept { return document_; }

    //! いま使うツールを切り替える。途中の点は捨てる(V1と同じ)。
    void SelectTool(DrawingTool tool);
    void SetToolSettings(ToolSettings settings);
    [[nodiscard]] DrawingTool CurrentTool() const noexcept { return tool_; }

    void SetScene(SnapScene scene) { scene_ = std::move(scene); }
    [[nodiscard]] const SnapScene& Scene() const noexcept { return scene_; }
    void SetMapping(ScreenMapping mapping) { mapping_ = mapping; }
    void SetSnapSettings(SnapSettings settings) { snapSettings_ = std::move(settings); }

    //! ポインタを動かした。
    [[nodiscard]] HoverResult Hover(const ScreenPoint& pointer);

    //! クリックした。吸着した位置をツールへ渡す。
    [[nodiscard]] ClickResult Click(const ScreenPoint& pointer);

    //! 右クリックなどで確定する(ポリラインなど)。
    [[nodiscard]] ClickResult FinishTool();

    //! Esc。途中の点を捨てる。
    void CancelTool();

    //! 1点戻す。
    bool UndoLastPoint();

    [[nodiscard]] bool Undo() { return document_.Undo(); }
    [[nodiscard]] bool Redo() { return document_.Redo(); }

private:
    [[nodiscard]] ClickResult Commit(const modeling::ToolOutput& output);
    //! 作った形を、次のスナップの相手にも加える。
    void AddToScene(const modeling::ToolOutput& output, base::EntityId entityId);

    Document document_;
    base::IdGenerator* ids_ = nullptr;
    DrawingTool tool_ = DrawingTool::Line;
    ToolSettings toolSettings_;
    std::unique_ptr<modeling::ToolSession> session_;
    SnapScene scene_;
    ScreenMapping mapping_;
    SnapSettings snapSettings_;
};

} // namespace kachakacha::v2::app
