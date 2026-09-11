#include "V2PatternDock.h"

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/view/PatternView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <utility>

namespace {

using kachakacha::v2::exporters::PatternLine;
using kachakacha::v2::exporters::PatternPage;
using kachakacha::v2::geometry::Point2;
using kachakacha::v2::view::FitPatternPage;
using kachakacha::v2::view::PatternFit;

//! 線の種類ごとの色と様式。V1 の型紙の出し方に合わせる。
//! 切り取り線は黒の実線、折り線は青の破線、切れ目は赤、開口は緑。
struct LineStyle {
    QColor color;
    Qt::PenStyle style = Qt::SolidLine;
    double widthPx = 1.4;
};

[[nodiscard]] LineStyle StyleFor(PatternLine layer)
{
    switch (layer) {
    case PatternLine::Outline:    return {QColor(0x1a, 0x1a, 0x1a), Qt::SolidLine, 1.6};
    case PatternLine::Fold:       return {QColor(0x1f, 0x6f, 0xb2), Qt::DashLine, 1.2};
    case PatternLine::Cut:        return {QColor(0xc0, 0x39, 0x2b), Qt::SolidLine, 1.4};
    case PatternLine::Opening:    return {QColor(0x2e, 0x8b, 0x57), Qt::SolidLine, 1.2};
    case PatternLine::Annotation: return {QColor(0x80, 0x80, 0x80), Qt::DotLine, 1.0};
    }
    return {QColor(0x1a, 0x1a, 0x1a), Qt::SolidLine, 1.4};
}

//! 型紙の曲線を折れ線にする。直線は両端、曲線は 48 分割。
//! 型紙は XY 平面に置いてあるので、z は見ない。
[[nodiscard]] QPolygonF PolylineOf(const kachakacha::v2::geometry::CurveSegment& segment,
    const PatternFit& fit)
{
    const int steps =
        segment.Kind() == kachakacha::v2::geometry::CurveKind::Line ? 1 : 48;
    QPolygonF polygon;
    for (int index = 0; index <= steps; ++index) {
        const auto point = segment.Evaluate(static_cast<double>(index)
            / static_cast<double>(steps));
        const Point2 screen = fit.ToScreen(Point2{point.x, point.y});
        polygon << QPointF(screen.u, screen.v);
    }
    return polygon;
}

} // namespace

V2PatternView::V2PatternView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
}

void V2PatternView::SetPage(const PatternPage& page)
{
    page_ = page;
    hasPage_ = true;
    update();
}

void V2PatternView::Clear()
{
    page_ = PatternPage{};
    hasPage_ = false;
    update();
}

void V2PatternView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(0x63, 0x6b, 0x70));
    if (!hasPage_) {
        painter.setPen(QPen(QColor(0xe8, 0xe8, 0xe8), 1.0));
        painter.drawText(rect(), Qt::AlignCenter,
            QStringLiteral("型紙はまだありません。\n「型紙を作る」を押してください。"));
        return;
    }
    const PatternFit fit = FitPatternPage(page_.widthMm, page_.heightMm,
        static_cast<double>(width()), static_cast<double>(height()), 14.0);
    // 紙そのものを白で出す。紙の縁が見えないと、収まっているのか分からない。
    const QRectF paper(fit.originPx.u, fit.originPx.v, fit.widthPx, fit.heightPx);
    painter.fillRect(paper, QColor(0xff, 0xff, 0xff));
    painter.setPen(QPen(QColor(0x2a, 0x2f, 0x33), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(paper);
    for (const auto& curve : page_.curves) {
        const LineStyle style = StyleFor(curve.layer);
        painter.setPen(QPen(style.color, style.widthPx, style.style, Qt::RoundCap,
            Qt::RoundJoin));
        const QPolygonF polygon = PolylineOf(curve.segment, fit);
        if (polygon.size() >= 2) {
            painter.drawPolyline(polygon);
        }
    }
}

V2PatternDock::V2PatternDock(QWidget* parent)
    : QDockWidget(QStringLiteral("型紙の下見"), parent)
{
    setObjectName(QStringLiteral("patternDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    view_ = new V2PatternView(body);
    layout->addWidget(view_, 1);

    auto* row = new QWidget(body);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    previous_ = new QPushButton(QStringLiteral("前の紙"), row);
    next_ = new QPushButton(QStringLiteral("次の紙"), row);
    summary_ = new QLabel(row);
    QObject::connect(previous_, &QPushButton::clicked, this,
        [this] { ShowPage(current_ - 1); });
    QObject::connect(next_, &QPushButton::clicked, this, [this] { ShowPage(current_ + 1); });
    rowLayout->addWidget(previous_);
    rowLayout->addWidget(next_);
    rowLayout->addWidget(summary_, 1);
    layout->addWidget(row);

    setWidget(body);
    Refresh();
}

void V2PatternDock::SetPages(std::vector<PatternPage> pages)
{
    pages_ = std::move(pages);
    current_ = 0;
    Refresh();
}

void V2PatternDock::ShowPage(int index)
{
    if (pages_.empty() || index < 0 || index >= static_cast<int>(pages_.size())) {
        return;   // 端で止まる。輪にすると、何枚目にいるのか分からなくなる。
    }
    current_ = index;
    Refresh();
}

QString V2PatternDock::SummaryText() const
{
    return summary_ == nullptr ? QString() : summary_->text();
}

void V2PatternDock::Refresh()
{
    if (pages_.empty()) {
        view_->Clear();
        summary_->setText(QStringLiteral("型紙はまだありません。"));
        previous_->setEnabled(false);
        next_->setEnabled(false);
        return;
    }
    current_ = std::clamp(current_, 0, static_cast<int>(pages_.size()) - 1);
    const PatternPage& page = pages_[static_cast<std::size_t>(current_)];
    view_->SetPage(page);
    previous_->setEnabled(current_ > 0);
    next_->setEnabled(current_ + 1 < static_cast<int>(pages_.size()));
    // 何枚目か・紙の大きさ・線の数。数を出すのは「空の紙が出ていないか」を見るため。
    summary_->setText(QStringLiteral("%1 / %2 枚   %3 × %4 mm   線 %5 本")
            .arg(current_ + 1)
            .arg(static_cast<int>(pages_.size()))
            .arg(page.widthMm, 0, 'f', 0)
            .arg(page.heightMm, 0, 'f', 0)
            .arg(static_cast<int>(page.curves.size())));
}
