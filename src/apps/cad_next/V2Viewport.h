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
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/ShapeMesh.h"

#include <QColor>
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
    QColor preview{0x60, 0xD0, 0xFF};
    QColor point{0xF0, 0xF0, 0xF0};
    QColor snap{0xFF, 0xE0, 0x60};
    QColor workPlane{0x60, 0x90, 0xC0};
    QColor text{0xE0, 0xE0, 0xE0};

    [[nodiscard]] static ViewportPalette Dark();
    [[nodiscard]] static ViewportPalette Win95();
};

class V2Viewport final : public QWidget {
public:
    explicit V2Viewport(kachakacha::v2::app::DrawingSession& session,
        QWidget* parent = nullptr);

    void SetPalette(const ViewportPalette& palette);
    [[nodiscard]] const ViewportPalette& Colors() const noexcept { return palette_; }

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

    //! 吸着を一時的に止める(V1の Ctrl)。押している間だけ。
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
    void PressRightWithoutMoving();
    //! 選択道具で右クリックしたときに出すもの。窓が用意する。
    void SetContextMenuCallback(std::function<void(const QPoint&)> callback);
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

    //! いま選んでいるもの。判断は core の Selection にある。
    [[nodiscard]] const kachakacha::v2::app::SelectionSet& Selection() const noexcept
    {
        return selection_;
    }
    void SetSelection(kachakacha::v2::app::SelectionSet selection);
    //! 画面のこの位置で選ぶ。修飾キーで足す・外すが変わる。
    void SelectAt(const QPointF& position, Qt::KeyboardModifiers modifiers);
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
    std::function<void(const QPoint&)> contextMenu_;
    //! 中ボタン・右ボタンで画面を動かしている最中か。
    bool panning_ = false;
    bool orbiting_ = false;
    QPointF lastDragPosition_;
    //! 画面を動かしたか。動かさずに右で離したら、道具ごとの意味になる。
    bool viewDragMoved_ = false;
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
