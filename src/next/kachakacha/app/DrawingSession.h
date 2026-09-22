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
#include "kachakacha/modeling/TransformInput.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    //! 移動・複製・鏡映・回転が決まったときの中身。画面側はこれを選択へ当てる。
    //! 文書を変えるのは画面側なので、ここでは「何をするか」だけを渡す。
    std::optional<modeling::TransformPlan> transform;
};

class DrawingSession {
public:
    DrawingSession(base::DocumentId documentId, base::IdGenerator& ids);

    [[nodiscard]] Document& GetDocument() noexcept { return document_; }
    [[nodiscard]] const Document& GetDocument() const noexcept { return document_; }

    //! いま使うツールを切り替える。途中の点は捨てる(V1と同じ)。
    void SelectTool(DrawingTool tool);
    void SetToolSettings(ToolSettings settings);
    //! いまの道具の設定(作り方など)。画面がカーソル横の欄を出すかを決めるのに読む。
    [[nodiscard]] const ToolSettings& CurrentToolSettings() const noexcept { return toolSettings_; }
    [[nodiscard]] DrawingTool CurrentTool() const noexcept { return tool_; }

    //! 吸着の相手を差し替える。持ち越しも捨てる。
    //!
    //! 文書を開く・Undo/Redo・作業平面やグリッドの変更は、どれもここを通る。
    //! 場面が入れ替わると、持ち越していた吸着先はもう同じものを指していない。
    //! 捨てずに残すと、消えた相手の位置へ吸い付いたままになる。
    void SetScene(SnapScene scene);
    //! 場面が入れ替わったときに呼ばれる。
    //!
    //! 画面は、持ち越しだけでなく **出している一時表示** も捨てなければならない。
    //! ここを1本にしておくのは、`SetScene` の呼び口が10か所以上あるためである。
    //! 呼び口ごとに後始末を書くと、必ずどれかが抜ける。実際に抜けた。
    //!
    //! **返ってきた綱を持っている間だけ呼ばれる。** 綱を手放せば呼ばれなくなる。
    //! 素の `std::function` を持たせると、聞き手(画面)が先に消えたときに
    //! 消えた `this` を呼んでしまう。窓を閉じる・画面を差し替える・
    //! 将来ビューを増やす、のどれでも起きる。
    //! 綱を会員が持つ形にすれば、どちらが先に消えても落ちない。
    class SceneChangedConnection {
    public:
        SceneChangedConnection() = default;
        SceneChangedConnection(SceneChangedConnection&&) noexcept = default;
        SceneChangedConnection& operator=(SceneChangedConnection&& other) noexcept
        {
            if (this != &other) {
                Disconnect();
                alive_ = std::move(other.alive_);
            }
            return *this;
        }
        // 写せないようにする。写すと、手放したつもりの綱が残って呼ばれ続ける。
        SceneChangedConnection(const SceneChangedConnection&) = delete;
        SceneChangedConnection& operator=(const SceneChangedConnection&) = delete;
        ~SceneChangedConnection() { Disconnect(); }

        //! 綱を切る。以後この聞き手は呼ばれない。二度切っても構わない。
        void Disconnect() noexcept
        {
            if (alive_) {
                *alive_ = false;
                alive_.reset();
            }
        }
        [[nodiscard]] bool Connected() const noexcept { return alive_ && *alive_; }

    private:
        friend class DrawingSession;
        explicit SceneChangedConnection(std::shared_ptr<bool> alive)
            : alive_(std::move(alive))
        {
        }
        std::shared_ptr<bool> alive_;
    };

    [[nodiscard]] SceneChangedConnection OnSceneChanged(std::function<void()> callback);
    [[nodiscard]] const SnapScene& Scene() const noexcept { return scene_; }
    void SetMapping(ScreenMapping mapping) { mapping_ = mapping; }
    //! 抑止(S・磁石)を含む吸着の設定。抑止が始まったらその場で持ち越しを捨てる。
    //! 次の Hover まで待つと、Hover が無いまま離したときに古い吸着先が残る。
    void SetSnapSettings(SnapSettings settings);

    //! 吸着したあと、点をもう一度寄せる手立て(V1の Shift の拘束と、直角スナップ)。
    //!
    //! 吸着より **後** に当てる。先に当てると、寄せた先の点へまた吸着してしまい、
    //! 水平にしたはずの線が斜めへ戻る。
    //! 第2引数は「点の吸着先が見つかったか」。端点やグリッドに吸い付いているときは
    //! そちらが正なので、寄せる側はそれを見て手を出さない。空にすれば当てない。
    void SetPointAdjuster(
        std::function<geometry::Vector3(const geometry::Vector3&, bool snapped)> adjust)
    {
        adjustPoint_ = std::move(adjust);
    }

    //! ポインタを動かした。
    //! 直前に選んだ吸着先を持ち越す(modeling::SnapHysteresis)。小さな揺れで入れ替わらない。
    //! 持ち越しは、ツールの切替・設定の変更・取消・S の間に捨てる。
    [[nodiscard]] HoverResult Hover(const ScreenPoint& pointer);

    //! Hover と同じ答えを出すが、持ち越しを変えない。
    //! 案内文のように、ポインタの位置ではない所で様子を見るときに使う。
    //! Hover を使うと、ポインタと無関係な位置の吸着先を持ち越してしまう。
    [[nodiscard]] HoverResult PeekHover(const ScreenPoint& pointer);

    //! クリックした。吸着した位置をツールへ渡す。
    [[nodiscard]] ClickResult Click(const ScreenPoint& pointer);

    //! 数値入力で決めた点を置く。吸着はしない(数で決めた点を寄せてはならない)。
    //! 置いたあとの流れはクリックと同じ。
    [[nodiscard]] ClickResult PlacePoint(const geometry::Vector3& world);

    //! 右クリックなどで確定する(ポリラインなど)。
    [[nodiscard]] ClickResult FinishTool();

    //! 数値で決めた線をそのまま置く(右パネルの「数値で線を作る」)。道具は変えない。
    //! label を付けると、その名前で一覧に出る。
    [[nodiscard]] ClickResult AddWire(std::vector<geometry::CurveSegment> segments,
        bool construction, std::string_view label);

    //! いま置いてある点の数。Esc がどこまで戻ればよいかの判断に使う。
    [[nodiscard]] std::size_t PlacedPointCount() const noexcept;

    //! 拘束の基準になる点。
    //!
    //! ポリラインとスプラインは **直前に置いた点**、それ以外は **1点目**。
    //! V1 と同じ決め方である。折れ線は1本ずつ向きを決めたいが、
    //! 矩形や直線は最初の点から見た向きで決めたいためである。
    //! 点が無ければ原点を返す(呼ぶ側が HasPlacedPoints で先に見ること)。
    [[nodiscard]] geometry::Vector3 ConstraintAnchor() const noexcept;

    //! 途中の点があるか。
    [[nodiscard]] bool HasPlacedPoints() const noexcept
    {
        return PlacedPointCount() > 0;
    }

    //! Esc。途中の点を捨てる。
    void CancelTool();

    //! 1点戻す。
    bool UndoLastPoint();

    [[nodiscard]] bool Undo() { return document_.Undo(); }
    [[nodiscard]] bool Redo() { return document_.Redo(); }

private:
    //! 場面が入れ替わったことを、生きている聞き手だけへ知らせる。
    void NotifySceneChanged();
    //! Hover と PeekHover の中身。keepHold が真なら持ち越しを更新する。
    [[nodiscard]] HoverResult Evaluate(const ScreenPoint& pointer, bool keepHold);
    [[nodiscard]] ClickResult Commit(const modeling::ToolOutput& output,
        std::string_view label = {});
    //! 作った形を、次のスナップの相手にも加える。
    void AddToScene(const modeling::ToolOutput& output, base::EntityId entityId);
    //! 指した点を作図点として文書へ残す(keepPoints)。
    void AddKeptPoints(const modeling::ToolOutput& output, ClickResult& result);
    //! ベジェの制御多角形を補助線として足す(線と同じまとまりの中で呼ぶ)。
    void AddControlPolygon(const modeling::ToolOutput& output, ClickResult& result);

    Document document_;
    base::IdGenerator* ids_ = nullptr;
    DrawingTool tool_ = DrawingTool::Line;
    ToolSettings toolSettings_;
    std::unique_ptr<modeling::ToolSession> session_;
    SnapScene scene_;
    ScreenMapping mapping_;
    SnapSettings snapSettings_;
    modeling::SnapHysteresis snapHysteresis_;
    //! 場面の入れ替わりを聞いている相手。綱が切れたものは呼ばない。
    struct SceneListener {
        std::weak_ptr<bool> alive;
        std::function<void()> callback;
    };

    std::function<geometry::Vector3(const geometry::Vector3&, bool)> adjustPoint_;
    std::vector<SceneListener> sceneListeners_;
};

} // namespace kachakacha::v2::app
