#pragma once

//! V2の作図画面(WP-08)。
//!
//! ここは「描いて、押した場所を core へ渡す」だけの層である。
//! 座標計算も交差もスナップも core が持っている(architecture-and-data.md DOC-002)。
//! V1 は Viewport の中に幾何が埋まっていたので、画面を出さないと何も確かめられず、
//! 同じ計算が別の場所にもう一度書かれていた。
//!
//! 曲線は曲線のまま描く。円弧を折れ線にして描くと、拡大したときに角が出る。
//! QPainterPath の arcTo と cubicTo を使い、種類ごとに描き分ける。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない(V1と同じ制約)。

#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/modeling/WorkPlane.h"
#include "kachakacha/view/ViewOrientation.h"
#include "kachakacha/app/CursorInput.h"
#include "kachakacha/app/DisplaySettings.h"
#include "kachakacha/app/EscapeAction.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SemanticState.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/ShapeMesh.h"

#include <QColor>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <vector>

//! 視点。V1の「見る向き」と同じ6面 + 等角。
enum class ViewDirection {
    Top,
    Bottom,
    Front,
    Back,
    Left,
    Right,
    Isometric,
};

[[nodiscard]] const char* ViewDirectionNameJa(ViewDirection direction);

//! 画面の配色。Win95テーマと通常テーマで色を変える。
//!
//! 意味状態(ui-ux-integrated-spec §3)の色はここだけが持つ。
//! **テーマは色を変えるが、状態の区別は変えない。**
//! どのテーマでも Default / Hover / Selected / Snap / Preview は見分けられる色にする。
//! 見分けられるかどうかは SemanticInksAreDistinct が数で確かめる。
struct ViewportPalette {
    QColor background{0x20, 0x24, 0x2C};
    QColor gridMinor{0x2C, 0x32, 0x3C};
    QColor gridMajor{0x3A, 0x44, 0x52};
    QColor axisX{0xC0, 0x50, 0x50};
    QColor axisY{0x50, 0xC0, 0x60};
    QColor axisZ{0x50, 0x80, 0xE0};
    QColor wire{0xE8, 0xE8, 0xE8};
    QColor construction{0x88, 0x88, 0x98};
    QColor selected{0xFF, 0xC0, 0x40};
    //! カーソルの下の候補。**選択色の明るさ違いにしない。**
    //! 明るさだけの違いは、線が重なっているところで読めない。
    QColor hover{0x55, 0xE0, 0x8A};
    QColor preview{0x60, 0xD0, 0xFF};
    QColor point{0xF0, 0xF0, 0xF0};
    //! 吸着の記号。選択色(橙)と近い黄では、線の上で見分けられなかった。
    //! Win95 テーマと同じ赤系にして、テーマをまたいで同じ役割の色にする。
    QColor snap{0xFF, 0x6B, 0x5A};
    QColor workPlane{0x60, 0x90, 0xC0};
    QColor text{0xE0, 0xE0, 0xE0};

    [[nodiscard]] static ViewportPalette Dark();
    [[nodiscard]] static ViewportPalette Win95();
};

//! 2つの意味状態の色が見分けられるか(RGB のユークリッド距離で見る)。
//!
//! 「別の状態を別の色で描く」を目で確かめると、テーマを足したときに崩れる。
//! 数で門を決めておけば、配色を変えた時点で試験が落ちる。
[[nodiscard]] bool SemanticInksAreDistinct(const QColor& first, const QColor& second);
//! 見分けられると認める最小の距離。
[[nodiscard]] double SemanticInkDistanceGate() noexcept;

class V2Viewport final : public QWidget {
public:
    explicit V2Viewport(kachakacha::v2::app::DrawingSession& session,
        QWidget* parent = nullptr);

    void SetPalette(const ViewportPalette& palette);
    [[nodiscard]] const ViewportPalette& Colors() const noexcept { return palette_; }

    //! 意味状態の色(ui-ux-integrated-spec §3)。描画も試験もここだけを見る。
    //! テーマを変えても、状態と色の対応表は入れ替わらない。
    [[nodiscard]] QColor SemanticColor(kachakacha::v2::app::SemanticState state) const;
    //! 意味状態の線幅(logical px)。線の太さは表示設定の太さを土台にする。
    [[nodiscard]] double SemanticWidthPx(kachakacha::v2::app::SemanticState state) const;
    //! 途中経過(Preview)を描くペン。作図の途中経過、掴んで動かす影、折り曲げの帯は
    //! すべてこれで描く。Preview の色・太さ・破線・半透明を1か所で決める(§3 規則3)。
    [[nodiscard]] QPen PreviewPen() const;
    //! その線分がいまどの意味状態か。判定は core(app/SemanticState)にある。
    [[nodiscard]] kachakacha::v2::app::SemanticState CurveStateOf(
        kachakacha::v2::base::EntityId entityId,
        kachakacha::v2::base::SegmentId segmentId) const;
    //! いま出している途中経過(Preview)の本数。
    //! Preview は文書にも場面にも入れないので、拾えないし選べない(§3 規則3)。
    [[nodiscard]] int PreviewSegmentCount() const noexcept
    {
        return static_cast<int>(hover_.preview.size());
    }

    //! 次の1回のクリックを、道具ではなくこちらへ渡す(1点だけ拾う)。
    //!
    //! トリムや延長やグリッド原点の移動は「どこを押したか」で意味が決まる。
    //! 選択だけでは決まらないので、押す場所を1回だけ聞く。
    //! 聞いている間は帯にそう出す。黙って待つと、何も起きないように見える。
    struct PickedPoint {
        //! 作業平面の上の点。
        kachakacha::v2::geometry::Vector3 point{};
        //! いちばん近い線。無ければ値を持たない。
        std::optional<kachakacha::v2::app::PickCandidate> curve;
    };
    void BeginPointPick(std::function<void(const PickedPoint&)> handler,
        const std::string& promptJa);
    [[nodiscard]] bool PickPending() const { return static_cast<bool>(pickHandler_); }
    //! 拾うのをやめる。Esc で呼ぶ。
    void CancelPointPick();

    //! 見え方の設定(AT-UIX-010)。形は変えない。
    void SetDisplaySettings(const kachakacha::v2::app::DisplaySettings& settings);
    //! 作図モード以外でグリッドを出さない(表示設定 gridInAllModes が偽のとき)。
    void SetGridSuppressedByMode(bool suppressed)
    {
        gridSuppressedByMode_ = suppressed;
        update();
    }
    [[nodiscard]] bool GridSuppressedByMode() const noexcept { return gridSuppressedByMode_; }
    [[nodiscard]] const kachakacha::v2::app::DisplaySettings& DisplaySettingsNow() const
    {
        return display_;
    }

    void SetViewDirection(ViewDirection direction);
    [[nodiscard]] ViewDirection Direction() const noexcept { return direction_; }

    void SetWorkPlane(const kachakacha::v2::modeling::WorkPlaneFrame& plane);
    [[nodiscard]] const kachakacha::v2::modeling::WorkPlaneFrame& WorkPlane() const noexcept
    {
        return workPlane_;
    }

    //! 画面に出す作業平面の1枚。文書の中の作図面を、見えるようにするためだけのもの。
    struct WorkPlaneView {
        kachakacha::v2::base::EntityId entityId;
        kachakacha::v2::modeling::WorkPlaneFrame frame;
        //! 画面に出す名前(「上面 XY」など)。空なら名前を出さない。
        QString label;
        //! いま作業中の面か。作業中だけ色を変える(V1 と同じ)。
        bool active = false;
    };
    //! 出す作業平面を入れ替える。文書が変わるたびに窓が呼ぶ。
    void SetWorkPlaneViews(std::vector<WorkPlaneView> planes);

    //! 画面に出す形(立体・面)の1つ。核が三角形にしたものを受け取る。
    struct ShapeView {
        kachakacha::v2::base::EntityId entityId;
        kachakacha::v2::modeling::ShapeMesh mesh;
        //! 面(形状ガイド)なら true。立体より薄く塗り、裏も描く。
        bool surface = false;
    };
    //! 出す形を入れ替える。形が変わるたびに窓が呼ぶ。
    void SetShapeViews(std::vector<ShapeView> shapes);
    [[nodiscard]] int ShapeViewCount() const noexcept
    {
        return static_cast<int>(shapeViews_.size());
    }
    //! 出ている形の三角形の合計。試験で「本当に描く物があるか」を見る。
    [[nodiscard]] int ShapeTriangleCount() const noexcept;
    //! いま作図用の十字カーソルを出しているか。試験から見る。
    //! カーソルの形そのものは Qt が持っていて読み出せないので、選んだ結果を覚える。
    [[nodiscard]] bool DrawingCursorShown() const noexcept { return drawingCursor_; }
    //! カーソルの下にある線。無ければ Nil。試験から見る。
    [[nodiscard]] const kachakacha::v2::base::EntityId& HoveredEntityId() const noexcept
    {
        return hoveredEntityId_;
    }
    [[nodiscard]] int WorkPlaneViewCount() const noexcept
    {
        return static_cast<int>(workPlaneViews_.size());
    }

    //! 画面に収める幅(mm)。小さくすると拡大になる。
    void SetVisibleWidthMm(double value);
    [[nodiscard]] double VisibleWidthMm() const noexcept { return visibleWidthMm_; }

    //! 画面を平行移動する(V1の中ボタン・右ボタンのドラッグ)。
    void PanByPixels(double dxPx, double dyPx);
    //! 画面基準で視点を回す(V1の Shift+中ボタン)。
    void OrbitByPixels(double dxPx, double dyPx);
    [[nodiscard]] kachakacha::v2::geometry::Vector3 ViewCenter() const { return center_; }

    //! Esc。V1と同じで、やりかけを1つ取り消してから選択道具へ戻る。
    //! 何をしたかを返す。戻り値が空なら、することが無かった。
    std::vector<kachakacha::v2::app::EscapeStep> PressEscape();
    //! Esc で「選択道具へ戻す」を頼む先。窓が道具を持っているので外から渡す。
    void SetBackToSelectCallback(std::function<void()> callback);

    //! 吸着を一時的に止める(S)。押している間だけ。
    void SetSnapSuppressedByKey(bool suppressed);
    //! 道具として吸着を切る(コマンドの入切)。
    void SetSnapSuppressed(bool suppressed);
    //! カーソルの形をいまの状態に合わせる。掴めるかどうかを手元で分かるようにする。
    void RefreshCursorShape();
    //! いま拾う相手を絞る印(作図中は作業平面の上だけ)。判断は core にある。
    [[nodiscard]] kachakacha::v2::app::PickFocus PickFocusNow() const;
    //! 塗った形を画面の点で拾う。線が拾えなかったときだけ使う。
    [[nodiscard]] std::optional<kachakacha::v2::app::PickCandidate> PickShapeAt(
        const QPointF& position) const;
    //! 作図中の十字カーソル(V1 の白フチ付き十字)。既定の十字は細くて読めない。
    [[nodiscard]] static QCursor DrawingCrossCursor();
    //! 右クリック(動かさずに離した)。道具ごとに意味が違う(V1同等)。
    //! 選択道具では、押したその場所で候補を集め直してから献立を出す。
    void PressRightWithoutMoving(const QPointF& position);
    //! 場所を渡さない版。最後にカーソルがあった場所で同じことをする。
    void PressRightWithoutMoving();
    //! 選択道具で右クリックしたときに出すもの。窓が用意する。
    //!
    //! 責務の境目はここだけである。**画面は候補の並びと確定を持ち、窓は出すだけ。**
    //! 渡すのは「出す場所」と「候補の見出し(並びは CandidateLabels と同じ)」で、
    //! 返ってくるのは選ばれた候補の番号だけ。窓が候補を集め直したり、
    //! 選択集合へ直に書いたりはしない。二重に持つと、画面に出ている候補と
    //! 実際に選ばれるものが食い違う。
    void SetContextMenuCallback(
        std::function<std::optional<int>(const QPoint&, const std::vector<QString>&)>
            callback);
    //! いま持っている候補の見出し。物体の表示名と部分要素の種別を並べる。
    //! 同じ見出しになるものには通し番号を足して、選び分けられるようにする。
    [[nodiscard]] std::vector<QString> CandidateLabels() const;
    //! 候補を1つ、通常の選択として確定する(献立で選んだとき)。
    //! クリックと同じ道を通す。Replace、Hover、案内文、選択変更の知らせまで同じ。
    bool SelectCandidate(std::size_t index);
    //! 作図の拘束(V1の Shift)。押している間だけ水平・垂直・正方形へ寄せる。
    void SetAxisConstraintByKey(bool constrained);
    void SetViewCenter(const kachakacha::v2::geometry::Vector3& center);

    //! 文書全体が入るように合わせる。
    void FitToDocument();

    //! グリッドの主間隔(mm)。

    //! 状態が変わったときに呼ばれる。案内文と診断を画面へ出すのに使う。
    void SetStatusCallback(std::function<void(const std::string&)> callback);
    //! 選択が変わったときに呼ぶ。数を数え直すのは本体窓の仕事。
    void SetSelectionChangedCallback(std::function<void()> callback);
    //! 構えている命令の「これで(Enter)」と「やめる(Esc)」。
    //! 命令は窓が持っているので、画面は伝えるだけにする。
    void SetPendingCommandCallbacks(std::function<void()> confirm, std::function<void()> cancel);

    //! いま選んでいるもの。判断は core の Selection にある。
    [[nodiscard]] const kachakacha::v2::app::SelectionSet& Selection() const noexcept
    {
        return selection_;
    }
    void SetSelection(kachakacha::v2::app::SelectionSet selection);
    //! 画面のこの位置で選ぶ。修飾キーで足す・外すが変わる。
    //! Alt はいま出している候補の **次(奥)** を選ぶ(ui-ux-integrated-spec §4.2)。
    void SelectAt(const QPointF& position, Qt::KeyboardModifiers modifiers);

    //! 矩形選択(ui-ux-integrated-spec §4.2)。
    //!
    //! **左から右は完全に含まれたものだけ、右から左は触れたものも。**
    //! 向きで意味を変えるのは、同じ手つきで「囲って選ぶ」と「触って選ぶ」を
    //! 使い分けるためである。取り方と当たり判定は core(app/Selection)が決める。
    //!
    //! 押した場所で構え、引きずって広げ、離して決める。
    //! 押した時点の1件選択(SelectAt)は残したまま構えるので、
    //! 引きずらずに離せば、ただのクリックのままになる。
    void BeginBoxSelect(const QPointF& position, Qt::KeyboardModifiers modifiers);
    void DragBoxSelect(const QPointF& position);
    //! 離す。矩形として扱える大きさなら選択を決め直して true。
    //! 5 logical px 未満しか動いていなければ何もしない(判断は core)。
    bool ReleaseBoxSelect(const QPointF& position);
    //! 引くのをやめる。選択は押した直後のままにする。
    void CancelBoxSelect();
    [[nodiscard]] bool BoxSelecting() const noexcept { return boxSelect_.active; }
    //! いま引いている矩形。引いていなければ空。描画と試験から見る。
    [[nodiscard]] QRectF BoxSelectRect() const;
    //! いま引いている矩形の取り方。引いていなければ値を持たない。
    [[nodiscard]] std::optional<kachakacha::v2::app::BoxSelectionKind> BoxSelectKind() const;

    //! 重なった候補を1つ送る(Tab / Shift+Tab)。送れたら true。
    //!
    //! **通常選択は変えない。** 動くのは「いま出している候補」= Hover だけである。
    //! 選択まで変えると、送っている途中の候補が次の操作の相手になってしまう。
    bool CycleCandidate(bool backward);
    //! カーソルの下に重なっている候補の数。試験から見る。
    [[nodiscard]] int CandidateCount() const noexcept
    {
        return static_cast<int>(cycle_.candidates.size());
    }
    //! いま出している候補の番号(0起点)。候補が無ければ 0。
    [[nodiscard]] int CandidateIndex() const noexcept
    {
        return cycle_.candidates.empty() ? 0 : static_cast<int>(cycle_.index);
    }
    //! いま出している候補。無ければ値を持たない。
    [[nodiscard]] std::optional<kachakacha::v2::app::PickCandidate> CurrentCandidate() const;
    //! 文書から消えたものを選択から外す。文書が変わったら呼ぶ。
    void PruneSelection();
    void SetDocumentChangedCallback(std::function<void()> callback);
    //! 移動・複製・鏡映・回転の点がそろったときに呼ぶ。
    //! 文書を変えるのは主窓の役目なので、画面はここで手放す。
    void SetTransformCallback(
        std::function<void(const kachakacha::v2::modeling::TransformPlan&)> callback);

    //! いま出ている案内文。
    [[nodiscard]] const std::string& StatusMessage() const noexcept { return status_; }

    //! 選んだ物を掴んで動かす(V1同等)。試験からも同じ道を通す。
    //! 掴めなければ false。掴めたら、引きずるあいだ仮の位置を出す。
    bool BeginBodyDrag(const QPointF& position);
    void DragBody(const QPointF& position);
    //! 離す。動かした量が小さければ何もしない(押しただけ、とみなす)。
    //! 文書を変えたら true。
    bool ReleaseBodyDrag(const QPointF& position);
    [[nodiscard]] bool BodyDragging() const noexcept { return bodyDrag_.active; }

    //! ワイヤーの制御点を掴んで動かす(V1同等)。
    //! 制御点は選んでいるワイヤーにだけ出す。全部に出すと画面が埋まる。
    bool BeginControlPointDrag(const QPointF& position);
    void DragControlPoint(const QPointF& position);
    //! 離す。動かしていなければ false。文書を変えたら true。
    bool ReleaseControlPointDrag(const QPointF& position);
    [[nodiscard]] bool ControlPointDragging() const noexcept
    {
        return controlDrag_.active;
    }
    //! 制御点を1つ動かした結果を文書へ入れる。窓が引き受ける。
    void SetControlPointCallback(std::function<void(kachakacha::v2::base::EntityId,
            kachakacha::v2::base::SegmentId,
            const kachakacha::v2::geometry::CurveSegment&)> callback);

    //! 試験から呼ぶ。マウスを使わずに同じ道を通す。
    void HoverAt(const QPointF& position);
    //! 直前の当たり判定が出した点。吸着と拘束を通した後の値。
    [[nodiscard]] std::optional<kachakacha::v2::geometry::Vector3> HoverPosition() const
    {
        return hover_.position;
    }
    void ClickAt(const QPointF& position);
    void FinishTool();
    void CancelTool();

    //! いまの写し方。スナップ半径がpxで効くので、core と同じものを使う。
    [[nodiscard]] kachakacha::v2::geometry::ScreenMapping Mapping() const;

    //! いまの姿勢(AT-UIX-008)。6面+等角も、この四元数1個で表す。
    [[nodiscard]] kachakacha::v2::view::Quaternion Orientation() const { return orientation_; }
    void SetOrientation(const kachakacha::v2::view::Quaternion& orientation);

    //! ビューキューブ。画面の右上に置く。
    [[nodiscard]] QRectF ViewCubeRect() const;

    //! 操作板を押した結果、何を掴んだか。
    enum class ViewPress {
        None,    //!< 操作板ではない。図面の操作へ回す
        Button,  //!< 家・ロール・上下左右・正対
        Cube,    //!< ビューキューブ
        Ring,    //!< 回転リング
    };
    //! 操作板を押す。**ボタン → キューブ → 輪** の順で見る。
    //!
    //! 輪を先に見ると、真横を向いた輪がキューブの中を通るので、
    //! キューブが押せなくなる。mousePressEvent もこれを呼ぶ。
    //! 順を2か所に書くと、必ず食い違う。
    ViewPress PressViewNavigator(const QPointF& position,
        kachakacha::v2::view::AxisArrowModifier modifier);
    //! 画面のその位置が、キューブのどの区画か。外していれば値を持たない。
    [[nodiscard]] std::optional<kachakacha::v2::view::ViewCubeZone> ViewCubeZoneAtScreen(
        const QPointF& position) const;

    //! キューブを押す・動かす・離す。離した瞬間に止まり、90度へ吸着しない。
    bool PressViewCube(const QPointF& position);
    void DragViewCube(const QPointF& position);
    void ReleaseViewCube(const QPointF& position);
    [[nodiscard]] bool ViewCubeDragging() const { return cubeDrag_.active; }

    //! 近似モデルの曲げ状態の姿勢(帯ごとの下レール・上レール)。
    //! 画面のプレビューと固定・出力を同じ点列にする。別の作り方にすると食い違う。
    void SetFoldPreview(std::vector<std::vector<kachakacha::v2::geometry::Vector3>> rails);
    [[nodiscard]] int FoldPreviewRailCount() const
    {
        return static_cast<int>(foldPreview_.size());
    }
    //! 形状ガイドの役割テーブルを3Dへ出す(AT-UIX-007 の色同期)。
    //! 色は core の式が決めた値をそのまま使う。画面で作り直さない。
    //! 原点の軸(0=X 1=Y 2=Z)を出すかどうか。
    void SetAxisVisible(int axis, bool visible);
    [[nodiscard]] bool AxisVisible(int axis) const noexcept
    {
        return axis >= 0 && axis < 3 && axisVisible_[axis];
    }
    void SetGuideTableRows(
        const std::vector<kachakacha::v2::modeling::GuideTableRowView>& rows);
    [[nodiscard]] int GuideRowsShown() const
    {
        return static_cast<int>(guideRows_.size());
    }

    //! カーソル連動の数値入力(AT-UIX-003)。欄立ても解き方も core が持つ。
    [[nodiscard]] const kachakacha::v2::app::CursorInputPanel& CursorPanel() const
    {
        return cursorPanel_;
    }
    //! 最初の点を置いた直後に出す。出せない道具なら false。
    bool OpenCursorInput();
    //! 点を置いたあとに呼ぶ。数値入力を使う道具なら入力列を開き、確定したら閉じる。
    void SyncCursorInputWithTool(bool placedPoint, bool committed);
    //! 入力列の値で次の点を置く(Enter)。置けたら true。
    bool PlacePointFromCursorInput();
    //! 道具が変わった。入力列を閉じる。
    void OnToolChanged();

    //! 測定で押した点(吸着済み)。V1 の 2点間 / 3点角度 / 要素 で使う。
    struct MeasurePick {
        kachakacha::v2::geometry::Vector3 point;
        kachakacha::v2::base::EntityId entityId;   //!< 吸着した線。無ければ Nil
    };
    [[nodiscard]] const std::vector<MeasurePick>& MeasurePicks() const noexcept
    {
        return measurePicks_;
    }
    void ClearMeasurePicks();
    //! 測定の点が増えた・消えたときに呼ぶもの。
    void SetMeasurePicksChangedCallback(std::function<void()> callback);
    //! いまの欄へ文字を入れる。
    bool TypeIntoCursorField(const QString& text);
    //! Tab / Shift+Tab。
    bool FocusNextCursorField(bool backward);
    //! Enter。確定できたら true。
    bool CommitCursorField();
    //! Esc。入力列を閉じる。
    void CloseCursorInput();
    //! 入力列を出す場所。画面端では左または上へ寄る。
    [[nodiscard]] QRectF CursorPanelRect() const;

    //! 視点の操作板(V1同等)。輪・上下左右・画面のまま回す・家・選択に正対。
    //! 並べ方も当たり判定も core が決める。
    [[nodiscard]] kachakacha::v2::view::ViewGadgetLayout ViewGadgets() const;
    //! 画面のその点にある部品。試験と描画から使う。
    [[nodiscard]] std::optional<std::size_t> ViewGadgetAt(const QPointF& position) const;
    //! 輪ではない部品だけ。キューブより先に見る。
    [[nodiscard]] std::optional<std::size_t> ViewButtonAt(const QPointF& position) const;
    //! 輪だけ。線から5px以内でも当たる。キューブより後に見る。
    [[nodiscard]] std::optional<std::size_t> ViewRingAt(const QPointF& position) const;
    bool PressViewButton(const QPointF& position,
        kachakacha::v2::view::AxisArrowModifier modifier);
    bool PressViewRing(const QPointF& position,
        kachakacha::v2::view::AxisArrowModifier modifier);
    //! 部品を押した。引きずれば連続、離すまで動かなければ15度。
    bool PressViewGadget(const QPointF& position,
        kachakacha::v2::view::AxisArrowModifier modifier);
    void DragViewGadget(const QPointF& position);
    void ReleaseViewGadget(const QPointF& position);
    [[nodiscard]] bool ViewGadgetDragging() const { return gadgetDrag_.has_value(); }
    //! 「選択に正対」を押したときに呼ぶ。本体窓が繋ぐ。
    void SetAlignSelectionCallback(std::function<void()> callback);

    //! XYZ回転矢印。感度は core が決める。
    bool RotateByArrow(kachakacha::v2::view::RotationAxis axis,
        kachakacha::v2::view::RotationAxisMode mode,
        kachakacha::v2::view::AxisArrowModifier modifier, double dragPx);
    //! 相対軸で使う、選んだ部品の姿勢。無ければ相対軸は断る。
    void SetSelectionFrame(const std::optional<kachakacha::v2::view::Quaternion>& frame);
    [[nodiscard]] std::string LastViewMessage() const { return viewMessage_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    //! Tab をこの画面で使う。既定のままだと QWidget::event が Tab を先に取って
    //! 次の部品へ焦点を移すので、keyPressEvent まで届かない。
    //! 図面の上での Tab は候補送りと数値入力欄の移動に使う。
    bool focusNextPrevChild(bool next) override;

private:
    void RebuildMapping();
    void DrawGrid(QPainter& painter) const;
    void DrawAxes(QPainter& painter) const;
    //! 原点の軸 X/Y/Z を出すか。一覧の「原点」ノードのチェックで変える。
    bool axisVisible_[3] = {true, true, true};
    void DrawWorkPlane(QPainter& painter) const;
    //! 作業平面を1枚描く。塗り・枠・u/v の目印・名前。
    void DrawOneWorkPlane(QPainter& painter, const WorkPlaneView& plane) const;
    //! 立体と面を描く。奥から手前へ塗り、稜線を上から重ねる。
    void DrawShapes(QPainter& painter) const;
    //! 形1つ分。塗りと稜線。
    void DrawOneShape(QPainter& painter, const ShapeView& shape) const;
    void DrawDocument(QPainter& painter) const;
    void DrawPreview(QPainter& painter) const;
    //! 選んだワイヤーの制御点。掴める場所を見せる。
    void DrawControlPoints(QPainter& painter) const;
    //! 近似モデルの曲げ状態。帯のレールを折れ線で出す。
    void DrawFoldPreview(QPainter& painter) const;
    void DrawSnap(QPainter& painter) const;
    void DrawScaleBar(QPainter& painter) const;
    void DrawViewCube(QPainter& painter) const;
    //! 索引を渡して押す。ボタンと輪で拾い方が違うので、押す側は共通にする。
    bool PressViewGadgetIndex(const QPointF& position,
        const std::optional<std::size_t>& index,
        kachakacha::v2::view::AxisArrowModifier modifier);
    //! 部品の種類に応じて回す。輪は世界か部品の軸、それ以外は画面の軸。
    void ApplyGadgetRotation(const kachakacha::v2::view::ViewGadget& gadget,
        double degrees, const kachakacha::v2::view::Quaternion& from);
    //! 視点の操作板を描く。使えないもの(選択が要るもの)は薄く出す。消さない。
    void DrawViewGadgets(QPainter& painter) const;
    void DrawViewButtonsOnTop(QPainter& painter) const;
    //! 操作板の中心。ここだけが場所を決める。
    [[nodiscard]] QPointF NavigatorCenter() const;
    //! その矢じりが、いま指されているか押されているか。
    [[nodiscard]] bool IsRingHeadHot(kachakacha::v2::view::RotationAxis axis,
        bool positive) const;
    //! 軸ごとの輪と、その両端の矢じり。
    void DrawViewRings(QPainter& painter,
        const kachakacha::v2::view::ViewGadgetLayout& layout) const;
    //! 輪ではない部品(家・上下左右・画面のまま回す・切替・正対)。
    void DrawViewButtons(QPainter& painter,
        const kachakacha::v2::view::ViewGadgetLayout& layout) const;
    //! 矢じり1つ。置く点と進む向きから三角を作る。
    static void DrawArrowHead(QPainter& painter, const QPointF& head,
        const QPointF& tangent, double sizePx, const QColor& ink);
    void DrawCursorInput(QPainter& painter) const;
    void DrawGuideRows(QPainter& painter) const;
    void DrawViewCubeFace(QPainter& painter, int faceAxis, int faceSign,
        const QPointF& center, double scale) const;

    //! 曲線1本を、種類を保ったまま QPainterPath へ足す。
    //! 種類ごとに分ける。1つの関数へ詰めると読めなくなる。
    void AppendCurve(class QPainterPath& path,
        const kachakacha::v2::geometry::CurveSegment& segment, bool& started) const;
    void AppendLine(class QPainterPath& path,
        const kachakacha::v2::geometry::CurveSegment& segment, bool& started) const;
    void AppendArc(class QPainterPath& path,
        const kachakacha::v2::geometry::CurveSegment& segment, bool& started) const;
    void AppendBezier(class QPainterPath& path,
        const kachakacha::v2::geometry::CurveSegment& segment, bool& started) const;
    void AppendSpline(class QPainterPath& path,
        const kachakacha::v2::geometry::CurveSegment& segment, bool& started) const;

    [[nodiscard]] std::optional<QPointF> ToScreen(
        const kachakacha::v2::geometry::Vector3& world) const;

    //! 画面の1点で拾えるものを、優先順位の順に全部集める。
    //! 点と線は core(app/CollectPickCandidates)、塗った形は core(modeling/CollectMeshHits)。
    //! ここでは順番に混ぜるだけで、拾い方そのものは書かない。
    [[nodiscard]] std::vector<kachakacha::v2::app::PickCandidate> CollectCandidatesAt(
        const QPointF& position) const;
    //! 塗った形の候補を手前から順に。奥の形を手前より先に選ばない。
    [[nodiscard]] std::vector<kachakacha::v2::app::PickCandidate> CollectShapeCandidatesAt(
        const QPointF& position) const;
    //! 矩形に入る塗った形。稜線を core の規則(app/AccumulateBoxReach)で数える。
    //!
    //! 線と点は core が場面から集める。形は画面が持っている網なので、ここで見る。
    //! 数え方は core と同じ1か所に寄せる。別に書くと、線と形で
    //! 「完全に入った」の意味が食い違う。
    [[nodiscard]] std::vector<kachakacha::v2::app::PickCandidate> CollectBoxShapeCandidates(
        const kachakacha::v2::app::BoxSelection& request) const;
    //! 修飾キーから選択の更新方法を決める。クリックと矩形で同じ規則にする。
    [[nodiscard]] static kachakacha::v2::app::SelectionMode SelectionModeFor(
        Qt::KeyboardModifiers modifiers);
    //! 引いている矩形を出す。取り方が読めるように、包含と交差で線を変える。
    void DrawBoxSelect(QPainter& painter) const;
    //! 候補一覧を集め直す。別の場所へ移ったか中身が変わったら番号を先頭へ戻す。
    void RefreshPickCycle(const QPointF& position);
    //! 候補一覧を捨てる。文書が変わったら呼ぶ。無いものを送り続けないため。
    void ForgetPickCycle();
    //! 候補の番号を1つ進める(または戻す)。候補が無ければ何もしない。
    void AdvanceCandidate(bool backward);
    //! いまの候補に合わせて Hover を書き直す。選択には触らない。
    void SyncHoverWithCandidate();
    //! 「選んでいるもの: n 件」を出す。クリックと献立で同じ文言にする。
    void ReportSelectionCount();

    //! 同じ場所で重なっている候補。Tab も Alt+クリックもここだけを見る。
    //! Hover / Selection / Preview とは別の状態である。混ぜない。
    struct PickCycle {
        //! 一度でも集めたか。集めた場所が anchorPx。
        bool valid = false;
        //! 集めたときの画面位置。ここから離れたら番号を捨てる。
        QPointF anchorPx;
        std::vector<kachakacha::v2::app::PickCandidate> candidates;
        //! いま出している候補。candidates が空なら意味を持たない。
        std::size_t index = 0;
    };
    PickCycle cycle_;

    //! 矩形選択の途中。Hover / Selection / 候補一覧とは別の状態である。混ぜない。
    struct BoxSelect {
        bool active = false;
        QPointF startPx;
        QPointF currentPx;
        //! 押した時点の選択。矩形はここから当て直す。押した拍子の1件選択を
        //! 足し込むと、囲んでいないものが混ざる。
        kachakacha::v2::app::SelectionSet selectionAtPress;
        Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    };
    BoxSelect boxSelect_;

    kachakacha::v2::app::DrawingSession* session_ = nullptr;
    ViewportPalette palette_ = ViewportPalette::Dark();
    kachakacha::v2::app::DisplaySettings display_;
    bool gridSuppressedByMode_ = false;
    //! Ctrl で吸着を止めているか。押している間だけ真。
    bool snapSuppressedByKey_ = false;
    //! Shift で拘束しているか。押している間だけ真。
    bool axisConstrainedByKey_ = false;
    //! コマンドとして吸着を切っているか。
    bool snapSuppressedBySetting_ = false;
    //! Shift の拘束を当てた点を返す。当てないときはそのまま返す。
    [[nodiscard]] kachakacha::v2::geometry::Vector3 ConstrainedPoint(
        const kachakacha::v2::geometry::Vector3& point) const;
    void ApplySnapSettings();
    std::function<void()> backToSelect_;
    std::function<std::optional<int>(const QPoint&, const std::vector<QString>&)>
        contextMenu_;
    //! 中ボタンで画面を動かしている最中か。
    //! 右ボタンはここに入らない。右はカメラを動かさない(ui-ux-integrated-spec §5.2)。
    bool panning_ = false;
    bool orbiting_ = false;
    QPointF lastDragPosition_;
    //! 右ボタンを押している最中か。押した場所と、そこから動いたかを覚える。
    //! 動かさずに離したときだけ、道具ごとの意味(選択道具なら献立)になる。
    bool rightPressed_ = false;
    QPointF rightPressPosition_;
    bool rightDragMoved_ = false;
    //! 次の1回のクリックを受け取る先。拾い終えたら空へ戻す。
    std::function<void(const PickedPoint&)> pickHandler_;
    ViewDirection direction_ = ViewDirection::Isometric;
    kachakacha::v2::view::Quaternion orientation_{};
    kachakacha::v2::view::ViewCubeDrag cubeDrag_;
    QPointF cubePressPosition_;
    bool cubeMoved_ = false;
    std::optional<kachakacha::v2::view::ViewCubeZone> cubeHoverZone_;
    std::optional<kachakacha::v2::view::Quaternion> selectionFrame_;
    //! いま押している部品。押した場所と、そこからの姿勢を覚えておく。
    struct ViewGadgetDrag {
        std::size_t index = 0;
        QPointF pressPosition;
        kachakacha::v2::view::Quaternion orientationAtPress{};
        kachakacha::v2::view::AxisArrowModifier modifier =
            kachakacha::v2::view::AxisArrowModifier::None;
        bool moved = false;
    };
    std::optional<ViewGadgetDrag> gadgetDrag_;
    std::optional<std::size_t> gadgetHoverIndex_;
    std::function<void()> alignSelectionCallback_;
    std::vector<kachakacha::v2::modeling::GuideTableRowView> guideRows_;
    kachakacha::v2::app::CursorInputPanel cursorPanel_;
    QPointF cursorPosition_;
    //! 入力列の基準(直前に置いた点)から見たポインタの位置。作業平面の u, v(mm)。
    kachakacha::v2::geometry::Vector3 cursorDelta_{};
    std::vector<MeasurePick> measurePicks_;
    std::function<void()> measurePicksChanged_;
    std::string viewMessage_;
    kachakacha::v2::modeling::WorkPlaneFrame workPlane_;
    std::vector<WorkPlaneView> workPlaneViews_;
    std::vector<ShapeView> shapeViews_;
    //! 当たり判定へ渡す網だけを並べたもの。SetShapeViews で作り直す。
    //! カーソルが動くたびに shapeViews_ から作り直すと、三角形を丸ごと写すことになる。
    std::vector<kachakacha::v2::modeling::ShapeMesh> pickMeshes_;
    //! カーソルの下の線。押さなくても「どれに当たるか」が見えるようにする。
    kachakacha::v2::base::EntityId hoveredEntityId_;
    kachakacha::v2::base::SegmentId hoveredSegmentId_;
    //! 直前に選んだカーソルが作図用の十字だったか。
    bool drawingCursor_ = false;
    kachakacha::v2::geometry::Vector3 center_{};
    double visibleWidthMm_ = 200.0;
    kachakacha::v2::geometry::ScreenMapping mapping_;
    kachakacha::v2::app::HoverResult hover_;
    std::string status_;
    std::function<void(const std::string&)> statusCallback_;
    std::function<void()> selectionChangedCallback_;
    std::function<void()> confirmPending_;
    std::function<void()> cancelPending_;
    kachakacha::v2::app::SelectionSet selection_;
    std::function<void()> documentChangedCallback_;
    std::function<void(const kachakacha::v2::modeling::TransformPlan&)> transform_;
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> foldPreview_;
    //! 選んだ物を掴んでいる間の状態。掴んだ場所と、いまの場所を持つ。
    struct BodyDrag {
        bool active = false;
        bool moved = false;
        QPointF startPx;
        kachakacha::v2::geometry::Vector3 startPoint{};
        kachakacha::v2::geometry::Vector3 delta{};
    };
    BodyDrag bodyDrag_;

    //! 制御点を掴んでいる間の状態。動かした後の形をここに持って、破線で出す。
    struct ControlDrag {
        bool active = false;
        bool moved = false;
        QPointF startPx;
        kachakacha::v2::app::ShownControlPoint handle;
        std::optional<kachakacha::v2::geometry::CurveSegment> preview;
    };
    ControlDrag controlDrag_;
    std::function<void(kachakacha::v2::base::EntityId, kachakacha::v2::base::SegmentId,
        const kachakacha::v2::geometry::CurveSegment&)> controlPointChanged_;
};
