#include "OutputPreviewDialog.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

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
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#f2f5f6"));
    if (mesh_.triangles.empty()) {
        painter.setPen(QColor("#6a7781"));
        painter.drawText(rect(), Qt::AlignCenter,
            QStringLiteral("出力する物を表へ追加すると、ここに完成形が出ます"));
        return;
    }

    // 奥から手前へ描く(ペインターズアルゴリズム)。
    const Vector3 forward = ViewDirection();
    std::vector<std::pair<double, const kachakacha::io::OutputMesh::Triangle*>> ordered;
    ordered.reserve(mesh_.triangles.size());
    for (const auto& triangle : mesh_.triangles) {
        const Vector3 centroid = (mesh_.vertices[static_cast<std::size_t>(triangle.a)]
                                     + mesh_.vertices[static_cast<std::size_t>(triangle.b)]
                                     + mesh_.vertices[static_cast<std::size_t>(triangle.c)])
            * (1.0 / 3.0);
        ordered.emplace_back(Dot(centroid - center_, forward), &triangle);
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const auto& left, const auto& right) { return left.first > right.first; });

    for (const auto& [depth, triangle] : ordered) {
        static_cast<void>(depth);
        const Vector3& a = mesh_.vertices[static_cast<std::size_t>(triangle->a)];
        const Vector3& b = mesh_.vertices[static_cast<std::size_t>(triangle->b)];
        const Vector3& c = mesh_.vertices[static_cast<std::size_t>(triangle->c)];
        Vector3 normal = Cross(b - a, c - a);
        if (normal.LengthSquared() <= 1.0e-18) {
            continue;
        }
        normal = normal.Normalized();
        const double lighting = std::clamp(0.35 + 0.65 * std::abs(Dot(normal, forward)), 0.0, 1.0);
        QColor color = triangle->fill ? QColor("#e08a2e") : QColor("#7fb2c4");
        color = color.lighter(static_cast<int>(70 + lighting * 70));
        QPolygonF polygon;
        polygon << Project(a) << Project(b) << Project(c);
        painter.setBrush(color);
        painter.setPen(QPen(color.darker(120), 0.3));
        painter.drawPolygon(polygon);
    }
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
    const QString state = mesh.Closed()
        ? QStringLiteral("閉じた形です（3Dプリント可）")
        : QStringLiteral("まだ開いた縁があります");
    summary_->setText(QStringLiteral("三角形 %1 / %2\n%3")
                          .arg(mesh.triangles.size())
                          .arg(state, notes.join(QStringLiteral(" / "))));
    view_->SetMesh(std::move(mesh));
}
