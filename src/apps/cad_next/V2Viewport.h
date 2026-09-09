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

#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/modeling/WorkPlane.h"
#include "kachakacha/view/ViewOrientation.h"
#include "kachakacha/app/CursorInput.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

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

    void SetViewDirection(ViewDirection direction);
    [[nodiscard]] ViewDirection Direction() const noexcept { return direction_; }

    void SetWorkPlane(const kachakacha::v2::modeling::WorkPlaneFrame& plane);
    [[nodiscard]] const kachakacha::v2::modeling::WorkPlaneFrame& WorkPlane() const noexcept
    {
        return workPlane_;
    }

    //! 画面に収める幅(mm)。小さくすると拡大になる。
    void SetVisibleWidthMm(double value);
    [[nodiscard]] double VisibleWidthMm() const noexcept { return visibleWidthMm_; }
    void SetViewCenter(const kachakacha::v2::geometry::Vector3& center);

    //! 文書全体が入るように合わせる。
    void FitToDocument();

    //! グリッドの主間隔(mm)。
    void SetGridSpacingMm(double value);

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

    //! いま出ている案内文。
    [[nodiscard]] const std::string& StatusMessage() const noexcept { return status_; }

    //! 試験から呼ぶ。マウスを使わずに同じ道を通す。
    void HoverAt(const QPointF& position);
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
    //! 画面のその位置が、キューブのどの区画か。外していれば値を持たない。
    [[nodiscard]] std::optional<kachakacha::v2::view::ViewCubeZone> ViewCubeZoneAtScreen(
        const QPointF& position) const;

    //! キューブを押す・動かす・離す。離した瞬間に止まり、90度へ吸着しない。
    bool PressViewCube(const QPointF& position);
    void DragViewCube(const QPointF& position);
    void ReleaseViewCube(const QPointF& position);
    [[nodiscard]] bool ViewCubeDragging() const { return cubeDrag_.active; }

    //! 形状ガイドの役割テーブルを3Dへ出す(AT-UIX-007 の色同期)。
    //! 色は core の式が決めた値をそのまま使う。画面で作り直さない。
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

    //! XYZ回転矢印(V1同等)。並べ方も当たり判定も core が決める。
    [[nodiscard]] std::vector<kachakacha::v2::view::AxisArrowButton> AxisArrowButtons() const;
    //! 画面のその点にある矢印。試験と描画から使う。
    [[nodiscard]] std::optional<std::size_t> AxisArrowAt(const QPointF& position) const;
    //! 矢印を押した。引きずれば連続、離すまで動かなければ15度。
    bool PressAxisArrow(const QPointF& position,
        kachakacha::v2::view::AxisArrowModifier modifier);
    void DragAxisArrow(const QPointF& position);
    void ReleaseAxisArrow(const QPointF& position);
    [[nodiscard]] bool AxisArrowDragging() const { return arrowDrag_.has_value(); }

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
    void resizeEvent(QResizeEvent* event) override;

private:
    void RebuildMapping();
    void DrawGrid(QPainter& painter) const;
    void DrawAxes(QPainter& painter) const;
    void DrawWorkPlane(QPainter& painter) const;
    void DrawDocument(QPainter& painter) const;
    void DrawPreview(QPainter& painter) const;
    void DrawSnap(QPainter& painter) const;
    void DrawScaleBar(QPainter& painter) const;
    void DrawViewCube(QPainter& painter) const;
    //! 回転矢印を描く。使えないもの(相対軸で選択が無い)は薄く出す。消さない。
    void DrawAxisArrows(QPainter& painter) const;
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
    ViewDirection direction_ = ViewDirection::Isometric;
    kachakacha::v2::view::Quaternion orientation_{};
    kachakacha::v2::view::ViewCubeDrag cubeDrag_;
    QPointF cubePressPosition_;
    bool cubeMoved_ = false;
    std::optional<kachakacha::v2::view::ViewCubeZone> cubeHoverZone_;
    std::optional<kachakacha::v2::view::Quaternion> selectionFrame_;
    //! いま押している矢印。押した場所と、そこからの姿勢を覚えておく。
    struct AxisArrowDrag {
        std::size_t index = 0;
        QPointF pressPosition;
        kachakacha::v2::view::Quaternion orientationAtPress{};
        kachakacha::v2::view::AxisArrowModifier modifier =
            kachakacha::v2::view::AxisArrowModifier::None;
        bool moved = false;
    };
    std::optional<AxisArrowDrag> arrowDrag_;
    std::optional<std::size_t> arrowHoverIndex_;
    std::vector<kachakacha::v2::modeling::GuideTableRowView> guideRows_;
    kachakacha::v2::app::CursorInputPanel cursorPanel_;
    QPointF cursorPosition_;
    kachakacha::v2::geometry::Vector3 cursorAnchor_{};
    std::string viewMessage_;
    kachakacha::v2::modeling::WorkPlaneFrame workPlane_;
    kachakacha::v2::geometry::Vector3 center_{};
    double visibleWidthMm_ = 200.0;
    double gridSpacingMm_ = 10.0;
    kachakacha::v2::geometry::ScreenMapping mapping_;
    kachakacha::v2::app::HoverResult hover_;
    std::string status_;
    std::function<void(const std::string&)> statusCallback_;
    std::function<void()> selectionChangedCallback_;
    kachakacha::v2::app::SelectionSet selection_;
    std::function<void()> documentChangedCallback_;
};
