#pragma once

#include "kachakacha/io/OutputMesh.h"

#include <QDialog>
#include <QPoint>
#include <QWidget>

class QLabel;

//! 出力プレビューの3D表示(オーナー指示: 出力したらどうなるかを視点を動かして確認)。
//! 自動でふさいだ場所は色を変えて描く。
class OutputMeshView final : public QWidget {
public:
    explicit OutputMeshView(QWidget* parent = nullptr);

    void SetMesh(kachakacha::io::OutputMesh mesh);
    void FitView();
    //! テスト用: 現在の三角形数。
    [[nodiscard]] std::size_t TriangleCount() const noexcept
    {
        return mesh_.triangles.size();
    }
    //! テスト用: 視点を回した角度(ラジアン)。
    [[nodiscard]] double YawRadians() const noexcept { return yaw_; }
    void OrbitForTest(double yawDelta, double pitchDelta);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    [[nodiscard]] kachakacha::geometry::Vector3 ViewDirection() const;
    [[nodiscard]] QPointF Project(const kachakacha::geometry::Vector3& point) const;

    kachakacha::io::OutputMesh mesh_;
    kachakacha::geometry::Vector3 center_{0.0, 0.0, 0.0};
    double scale_ = 1.0;
    double yaw_ = 0.7;
    double pitch_ = 0.5;
    QPoint lastMousePosition_;
    bool dragging_ = false;
};

//! 出力プレビューのウィンドウ(表の内容が変わるたびに作り直して見せる)。
class OutputPreviewDialog final : public QDialog {
public:
    explicit OutputPreviewDialog(QWidget* parent = nullptr);

    void SetMesh(kachakacha::io::OutputMesh mesh);
    [[nodiscard]] OutputMeshView* View() const noexcept { return view_; }

private:
    OutputMeshView* view_ = nullptr;
    QLabel* summary_ = nullptr;
};
