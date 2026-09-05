#include "OutputPreviewDialog.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using kachakacha::geometry::Cross;
using kachakacha::geometry::Dot;
using kachakacha::geometry::Vector3;

namespace {

constexpr double kMinimumScale = 0.05;
constexpr double kMaximumScale = 400.0;

} // namespace

OutputMeshView::OutputMeshView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 420);
    setMouseTracking(true);
    setAutoFillBackground(true);
}

void OutputMeshView::SetMesh(kachakacha::io::OutputMesh mesh)
{
    mesh_ = std::move(mesh);
    FitView();
    update();
}

void OutputMeshView::FitView()
{
    if (mesh_.vertices.empty()) {
        center_ = {0.0, 0.0, 0.0};
        scale_ = 1.0;
        return;
    }
    Vector3 low = mesh_.vertices.front();
    Vector3 high = low;
    for (const Vector3& point : mesh_.vertices) {
        low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
        high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
    }
    center_ = (low + high) * 0.5;
    const double size = std::max(1.0, (high - low).Length());
    const double viewSize = std::max(80, std::min(width(), height()));
    scale_ = std::clamp(viewSize * 0.65 / size, kMinimumScale, kMaximumScale);
}

void OutputMeshView::OrbitForTest(double yawDelta, double pitchDelta)
{
    yaw_ += yawDelta;
    pitch_ = std::clamp(pitch_ + pitchDelta, -1.5, 1.5);
    update();
}

Vector3 OutputMeshView::ViewDirection() const
{
    return {std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_),
        std::sin(pitch_)};
}

QPointF OutputMeshView::Project(const Vector3& point) const
{
    const Vector3 forward = ViewDirection();
    Vector3 right = Cross(Vector3{0.0, 0.0, 1.0}, forward);
    if (right.LengthSquared() <= 1.0e-12) {
        right = {1.0, 0.0, 0.0};
    }
    right = right.Normalized();
    const Vector3 up = Cross(forward, right).Normalized();
    const Vector3 relative = point - center_;
    return {width() * 0.5 + Dot(relative, right) * scale_,
        height() * 0.5 - Dot(relative, up) * scale_};
}

void OutputMeshView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor("#f2f5f6"));
    if (mesh_.triangles.empty()) {
        painter.setPen(QColor("#6a7781"));
        painter.drawText(rect(), Qt::AlignCenter,
            QStringLiteral("出力する物を表へ追加すると、ここに完成形が出ます"));
        return;
    }

    // 奥行きバッファで塗る(重なり・穴の前後が正しく見えるようにする。
    // Codexレビュー指摘: 重心の並べ替えだけでは前後が入れ替わる)。
    const int imageWidth = std::max(1, width());
    const int imageHeight = std::max(1, height());
    QImage canvas(imageWidth, imageHeight, QImage::Format_RGB32);
    canvas.fill(QColor("#f2f5f6"));
    std::vector<double> depth(
        static_cast<std::size_t>(imageWidth) * imageHeight,
        std::numeric_limits<double>::lowest());

    const Vector3 forward = ViewDirection();
    for (const auto& triangle : mesh_.triangles) {
        const Vector3& a = mesh_.vertices[static_cast<std::size_t>(triangle.a)];
        const Vector3& b = mesh_.vertices[static_cast<std::size_t>(triangle.b)];
        const Vector3& c = mesh_.vertices[static_cast<std::size_t>(triangle.c)];
        Vector3 normal = Cross(b - a, c - a);
        if (normal.LengthSquared() <= 1.0e-18) {
            continue;
        }
        normal = normal.Normalized();
        const double lighting =
            std::clamp(0.35 + 0.65 * std::abs(Dot(normal, forward)), 0.0, 1.0);
        QColor color = triangle.fill ? QColor("#e08a2e") : QColor("#7fb2c4");
        color = color.lighter(static_cast<int>(70 + lighting * 70));
        const QRgb rgb = color.rgb();

        const QPointF pa = Project(a);
        const QPointF pb = Project(b);
        const QPointF pc = Project(c);
        // 画面上の三角形をスキャンして、視線方向の深さで手前だけ残す。
        const double minX = std::floor(std::min({pa.x(), pb.x(), pc.x()}));
        const double maxX = std::ceil(std::max({pa.x(), pb.x(), pc.x()}));
        const double minY = std::floor(std::min({pa.y(), pb.y(), pc.y()}));
        const double maxY = std::ceil(std::max({pa.y(), pb.y(), pc.y()}));
        const int left = std::max(0, static_cast<int>(minX));
        const int right = std::min(imageWidth - 1, static_cast<int>(maxX));
        const int top = std::max(0, static_cast<int>(minY));
        const int bottom = std::min(imageHeight - 1, static_cast<int>(maxY));
        if (left > right || top > bottom) {
            continue;
        }
        const double area = (pb.x() - pa.x()) * (pc.y() - pa.y())
            - (pc.x() - pa.x()) * (pb.y() - pa.y());
        if (std::abs(area) <= 1.0e-9) {
            continue;
        }
        const double depthA = Dot(a - center_, forward);
        const double depthB = Dot(b - center_, forward);
        const double depthC = Dot(c - center_, forward);
        for (int y = top; y <= bottom; ++y) {
            auto* line = reinterpret_cast<QRgb*>(canvas.scanLine(y));
            for (int x = left; x <= right; ++x) {
                const double px = x + 0.5;
                const double py = y + 0.5;
                const double w0 = ((pb.x() - pa.x()) * (py - pa.y())
                                      - (px - pa.x()) * (pb.y() - pa.y()))
                    / area;
                const double w1 = ((px - pa.x()) * (pc.y() - pa.y())
                                      - (pc.x() - pa.x()) * (py - pa.y()))
                    / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < -1.0e-9 || w1 < -1.0e-9 || w2 < -1.0e-9) {
                    continue;
                }
                // w1=b重み, w0=c重み, w2=a重み
                const double pointDepth = depthA * w2 + depthB * w1 + depthC * w0;
                const std::size_t index =
                    static_cast<std::size_t>(y) * imageWidth + static_cast<std::size_t>(x);
                if (pointDepth <= depth[index]) {
                    continue;
                }
                depth[index] = pointDepth;
                line[x] = rgb;
            }
        }
    }
    painter.drawImage(0, 0, canvas);
}

void OutputMeshView::mousePressEvent(QMouseEvent* event)
{
    lastMousePosition_ = event->position().toPoint();
    dragging_ = true;
}

void OutputMeshView::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        return;
    }
    const QPoint delta = event->position().toPoint() - lastMousePosition_;
    lastMousePosition_ = event->position().toPoint();
    yaw_ -= delta.x() * 0.01;
    pitch_ = std::clamp(pitch_ + delta.y() * 0.01, -1.5, 1.5);
    update();
}

void OutputMeshView::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = false;
}

void OutputMeshView::wheelEvent(QWheelEvent* event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale_ = std::clamp(scale_ * factor, kMinimumScale, kMaximumScale);
    update();
}

OutputPreviewDialog::OutputPreviewDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("3Dモデル出力プレビュー"));
    setModal(false);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral(
        "ドラッグで回す・ホイールで拡大縮小。橙色は自動でふさいだ場所です。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    view_ = new OutputMeshView;
    layout->addWidget(view_, 1);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    resize(720, 620);
}

void OutputPreviewDialog::SetMesh(kachakacha::io::OutputMesh mesh)
{
    QStringList notes;
    for (const std::string& note : mesh.notes) {
        notes << QString::fromStdString(note);
    }
    // 「閉じている」＝「正しい部品」ではないので、状態を分けて伝える
    // (Codexレビュー指摘: 窓の穴を塞いでも "3Dプリント可" と出てしまう)。
    QString state;
    if (!mesh.Closed()) {
        state = QStringLiteral("開いた縁が残っています（このままでは3Dプリントに向きません）");
    } else if (mesh.filledLoopCount > 0) {
        state = QStringLiteral("水密です／ただし%1か所は自動でふさいだ形（橙色）です")
                    .arg(mesh.filledLoopCount);
    } else {
        state = QStringLiteral("水密です（自動でふさいだ場所はありません）");
    }
    if (mesh.unfillableLoopCount > 0) {
        state += QStringLiteral("／ふさげない縁 %1か所").arg(mesh.unfillableLoopCount);
    }
    summary_->setText(QStringLiteral("三角形 %1 / %2\n%3")
                          .arg(mesh.triangles.size())
                          .arg(state, notes.join(QStringLiteral(" / "))));
    view_->SetMesh(std::move(mesh));
}
