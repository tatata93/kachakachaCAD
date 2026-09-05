#include "PartPatternViewDialog.h"

#include "kachakacha/model/PartModel.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;
using kachakacha::io::PartPatternResult;
using kachakacha::io::PlateFlatPattern;
using kachakacha::io::PlateFlatPatternPath;

struct Bounds2 {
    double minimumX = std::numeric_limits<double>::max();
    double minimumY = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = std::numeric_limits<double>::lowest();

    void Include(double x, double y)
    {
        minimumX = std::min(minimumX, x);
        minimumY = std::min(minimumY, y);
        maximumX = std::max(maximumX, x);
        maximumY = std::max(maximumY, y);
    }

    [[nodiscard]] bool IsValid() const
    {
        return maximumX >= minimumX && maximumY >= minimumY;
    }

    [[nodiscard]] double Width() const { return maximumX - minimumX; }
    [[nodiscard]] double Height() const { return maximumY - minimumY; }
};

//! 折り畳み中間形状の表示用投影(簡易アクソノメトリック)。
[[nodiscard]] QPointF ProjectIso(const Vector3& point)
{
    const double x = (point.x - point.y) * 0.8660;
    const double y = (point.x + point.y) * 0.45 - point.z * 0.9;
    return {x, y};
}

[[nodiscard]] QPen FoldPen(int direction)
{
    if (direction > 0) {
        return QPen(QColor("#c2402a"), 1.4, Qt::DashLine); // 山折り
    }
    if (direction < 0) {
        return QPen(QColor("#2a5fc2"), 1.4, Qt::DashDotLine); // 谷折り
    }
    return QPen(QColor("#8a8a8a"), 1.0, Qt::DashLine);
}

//! 型紙(平面)と折り畳み中間形状を1枚ずつ横に並べて描くキャンバス。
class PatternCanvas : public QWidget {
public:
    PatternCanvas(
        const std::vector<PartPatternResult>* results,
        const QStringList* captions,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , results_(results)
        , captions_(captions)
    {
        setMinimumSize(680, 420);
    }

    void SetFoldProgress(double progress)
    {
        foldProgress_ = progress;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#fdfdf8"));
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (results_ == nullptr || results_->empty()) {
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("型紙がありません"));
            return;
        }

        if (foldProgress_ <= 1.0e-6) {
            PaintPatterns(painter);
        } else {
            PaintFoldPreview(painter);
        }
        PaintLegend(painter);
    }

private:
    void PaintPatterns(QPainter& painter)
    {
        constexpr double gapMillimeters = 12.0;
        double totalWidth = 0.0;
        double maximumHeight = 0.0;
        std::vector<Bounds2> bounds;
        for (const auto& result : *results_) {
            Bounds2 patternBounds;
            const auto include = [&patternBounds](const PlateFlatPatternPath& path) {
                for (const Vector2& point : path.points) {
                    patternBounds.Include(point.x, point.y);
                }
            };
            include(result.pattern.outerBoundary);
            for (const auto& opening : result.pattern.openings) include(opening);
            for (const auto& fold : result.pattern.foldLines) include(fold);
            if (!patternBounds.IsValid()) {
                patternBounds = Bounds2{0.0, 0.0, 1.0, 1.0};
            }
            totalWidth += patternBounds.Width() + gapMillimeters;
            maximumHeight = std::max(maximumHeight, patternBounds.Height());
            bounds.push_back(patternBounds);
        }
        const double margin = 30.0;
        const double captionSpace = 40.0;
        const double scale = std::min(
            (width() - margin * 2.0) / std::max(totalWidth, 1.0),
            (height() - margin * 2.0 - captionSpace) / std::max(maximumHeight, 1.0));

        double cursorX = margin;
        for (std::size_t index = 0; index < results_->size(); ++index) {
            const auto& pattern = (*results_)[index].pattern;
            const Bounds2& patternBounds = bounds[index];
            const double offsetX = cursorX - patternBounds.minimumX * scale;
            const double offsetY = margin
                + (maximumHeight - patternBounds.Height()) * scale * 0.5
                - patternBounds.minimumY * scale;
            const auto map = [&](const Vector2& point) {
                return QPointF(offsetX + point.x * scale, offsetY + point.y * scale);
            };
            const auto drawPath = [&](const PlateFlatPatternPath& path, const QPen& pen, bool closed) {
                if (path.points.size() < 2) {
                    return;
                }
                QPainterPath painterPath(map(path.points.front()));
                for (std::size_t pointIndex = 1; pointIndex < path.points.size(); ++pointIndex) {
                    painterPath.lineTo(map(path.points[pointIndex]));
                }
                if (closed) {
                    painterPath.closeSubpath();
                }
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(painterPath);
            };
            drawPath(pattern.outerBoundary, QPen(QColor("#26323a"), 1.8), true);
            for (const auto& opening : pattern.openings) {
                drawPath(opening, QPen(QColor("#26323a"), 1.2), true);
            }
            for (const auto& fold : pattern.foldLines) {
                drawPath(fold, FoldPen(fold.foldDirection), false);
            }
            // 折り角の表示: 各折り線の中点へ完成形の折り角(度)を出す。
            // foldLines[i] は内部レール i+1、角度は MeasureCreaseAngles の i 番。
            const auto creaseAngles =
                kachakacha::model::MeasureCreaseAngles((*results_)[index].mesh);
            QFont angleFont = painter.font();
            angleFont.setPointSizeF(8.5);
            angleFont.setBold(false);
            painter.setFont(angleFont);
            for (std::size_t foldIndex = 0; foldIndex < pattern.foldLines.size();
                 ++foldIndex) {
                const auto& fold = pattern.foldLines[foldIndex];
                if (foldIndex >= creaseAngles.size() || fold.points.size() < 2) {
                    continue;
                }
                const Vector2& middle = fold.points[fold.points.size() / 2];
                const QPointF at = map(middle);
                const double degrees =
                    std::abs(creaseAngles[foldIndex]) * 180.0 / 3.14159265358979323846;
                const QString label = QStringLiteral("%1 %2°")
                    .arg(fold.foldDirection > 0 ? QStringLiteral("山")
                        : fold.foldDirection < 0 ? QStringLiteral("谷")
                                                 : QStringLiteral("折"))
                    .arg(degrees, 0, 'f', 1);
                const QRectF box(at.x() + 5.0, at.y() - 17.0, 62.0, 14.0);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(253, 253, 248, 215));
                painter.drawRect(box);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(fold.foldDirection > 0 ? QColor("#c2402a")
                    : fold.foldDirection < 0 ? QColor("#2a5fc2")
                                             : QColor("#5c6670"));
                painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, label);
            }
            DrawTitle(painter, index,
                offsetX + (patternBounds.minimumX + patternBounds.maximumX) * 0.5 * scale,
                offsetY + patternBounds.minimumY * scale - 26.0,
                offsetY + patternBounds.maximumY * scale + 6.0);
            cursorX += (patternBounds.Width() + gapMillimeters) * scale;
        }
    }

    void PaintFoldPreview(QPainter& painter)
    {
        // 各型紙の折り畳み中間形状をワイヤフレームで並べて描く。
        constexpr double gapMillimeters = 14.0;
        struct Projected {
            std::vector<std::vector<QPointF>> grid;
            Bounds2 bounds;
            std::vector<int> creases;
        };
        std::vector<Projected> projectedResults;
        double totalWidth = 0.0;
        double maximumHeight = 0.0;
        for (const auto& result : *results_) {
            const auto grid = kachakacha::model::BuildFoldPreview(result.mesh, foldProgress_);
            Projected projected;
            projected.creases = result.mesh.creaseDirections;
            projected.grid.resize(grid.size());
            for (std::size_t row = 0; row < grid.size(); ++row) {
                projected.grid[row].reserve(grid[row].size());
                for (const Vector3& point : grid[row]) {
                    const QPointF mapped = ProjectIso(point);
                    projected.grid[row].push_back(mapped);
                    projected.bounds.Include(mapped.x(), mapped.y());
                }
            }
            if (!projected.bounds.IsValid()) {
                projected.bounds = Bounds2{0.0, 0.0, 1.0, 1.0};
            }
            totalWidth += projected.bounds.Width() + gapMillimeters;
            maximumHeight = std::max(maximumHeight, projected.bounds.Height());
            projectedResults.push_back(std::move(projected));
        }
        const double margin = 30.0;
        const double captionSpace = 40.0;
        const double scale = std::min(
            (width() - margin * 2.0) / std::max(totalWidth, 1.0),
            (height() - margin * 2.0 - captionSpace) / std::max(maximumHeight, 1.0));

        double cursorX = margin;
        for (std::size_t index = 0; index < projectedResults.size(); ++index) {
            const Projected& projected = projectedResults[index];
            const double offsetX = cursorX - projected.bounds.minimumX * scale;
            const double offsetY = margin
                + (maximumHeight - projected.bounds.Height()) * scale * 0.5
                - projected.bounds.minimumY * scale;
            const auto map = [&](const QPointF& point) {
                return QPointF(offsetX + point.x() * scale, offsetY + point.y() * scale);
            };
            painter.setBrush(Qt::NoBrush);
            const int rows = static_cast<int>(projected.grid.size());
            const int columns = rows > 0 ? static_cast<int>(projected.grid.front().size()) : 0;
            // レール(横方向)。内部レールは山谷色。
            for (int row = 0; row < rows; ++row) {
                const bool crease = row > 0 && row + 1 < rows;
                painter.setPen(crease
                    ? FoldPen(projected.creases[row - 1])
                    : QPen(QColor("#26323a"), 1.6));
                QPainterPath path(map(projected.grid[row][0]));
                for (int column = 1; column < columns; ++column) {
                    path.lineTo(map(projected.grid[row][column]));
                }
                painter.drawPath(path);
            }
            // 素線(縦方向)は間引いて描く。
            painter.setPen(QPen(QColor("#9aa4ab"), 0.8));
            for (int column = 0; column < columns; column += 8) {
                QPainterPath path(map(projected.grid[0][column]));
                for (int row = 1; row < rows; ++row) {
                    path.lineTo(map(projected.grid[row][column]));
                }
                painter.drawPath(path);
            }
            painter.setPen(QPen(QColor("#9aa4ab"), 0.8));
            {
                QPainterPath path(map(projected.grid[0][columns - 1]));
                for (int row = 1; row < rows; ++row) {
                    path.lineTo(map(projected.grid[row][columns - 1]));
                }
                painter.drawPath(path);
            }
            DrawTitle(painter, index,
                offsetX + (projected.bounds.minimumX + projected.bounds.maximumX) * 0.5 * scale,
                offsetY + projected.bounds.minimumY * scale - 26.0,
                offsetY + projected.bounds.maximumY * scale + 6.0);
            cursorX += (projected.bounds.Width() + gapMillimeters) * scale;
        }
    }

    void DrawTitle(QPainter& painter, std::size_t index, double centerX, double topY, double bottomY)
    {
        painter.setPen(QColor("#26323a"));
        QFont numberFont = painter.font();
        numberFont.setPointSizeF(14.0);
        numberFont.setBold(true);
        painter.setFont(numberFont);
        painter.drawText(
            QRectF(centerX - 90.0, topY, 180.0, 22.0),
            Qt::AlignCenter,
            QString::number(index + 1));
        if (captions_ != nullptr && static_cast<int>(index) < captions_->size()) {
            QFont captionFont = painter.font();
            captionFont.setPointSizeF(8.5);
            captionFont.setBold(false);
            painter.setFont(captionFont);
            painter.drawText(
                QRectF(centerX - 150.0, bottomY, 300.0, 30.0),
                Qt::AlignHCenter | Qt::AlignTop,
                captions_->at(static_cast<int>(index)));
        }
    }

    void PaintLegend(QPainter& painter)
    {
        QFont legendFont = painter.font();
        legendFont.setPointSizeF(9.0);
        legendFont.setBold(false);
        painter.setFont(legendFont);
        const double legendY = height() - 14.0;
        painter.setPen(QPen(QColor("#c2402a"), 1.2, Qt::DashLine));
        painter.drawLine(QPointF(16.0, legendY), QPointF(46.0, legendY));
        painter.setPen(QColor("#26323a"));
        painter.drawText(QPointF(52.0, legendY + 4.0), QStringLiteral("山折り"));
        painter.setPen(QPen(QColor("#2a5fc2"), 1.2, Qt::DashDotLine));
        painter.drawLine(QPointF(112.0, legendY), QPointF(142.0, legendY));
        painter.setPen(QColor("#26323a"));
        painter.drawText(QPointF(148.0, legendY + 4.0), QStringLiteral("谷折り"));
        painter.drawText(QPointF(210.0, legendY + 4.0),
            QStringLiteral("折り線の数字＝完成形の折り角"));
    }

    const std::vector<PartPatternResult>* results_;
    const QStringList* captions_;
    double foldProgress_ = 0.0;
};

} // namespace

PartPatternViewDialog::PartPatternViewDialog(
    QString title,
    std::vector<kachakacha::io::PartPatternResult> results,
    QStringList captions,
    QWidget* parent)
    : QDialog(parent)
    , results_(std::move(results))
    , captions_(std::move(captions))
{
    setWindowTitle(title);
    resize(960, 600);
    auto* layout = new QVBoxLayout(this);
    auto* canvas = new PatternCanvas(&results_, &captions_, this);
    layout->addWidget(canvas, 1);

    auto* sliderRow = new QHBoxLayout;
    auto* flatLabel = new QLabel(QStringLiteral("平面(型紙)"));
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(0);
    slider->setToolTip(QStringLiteral("平面の型紙と、折り曲げた近似形状の間を確認できます"));
    auto* foldedLabel = new QLabel(QStringLiteral("折り曲げ(近似形状)"));
    connect(slider, &QSlider::valueChanged, canvas, [canvas](int value) {
        canvas->SetFoldProgress(static_cast<double>(value) / 100.0);
    });
    sliderRow->addWidget(flatLabel);
    sliderRow->addWidget(slider, 1);
    sliderRow->addWidget(foldedLabel);
    layout->addLayout(sliderRow);

    auto* buttonRow = new QHBoxLayout;
    auto* saveButton = new QPushButton(QStringLiteral("SVGを保存..."));
    saveButton->setToolTip(QStringLiteral("各型紙を1:1のSVGとして選んだフォルダへ保存します"));
    connect(saveButton, &QPushButton::clicked, this, &PartPatternViewDialog::SaveSvgFiles);
    auto* closeButton = new QPushButton(QStringLiteral("閉じる"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(saveButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);
}

void PartPatternViewDialog::SaveSvgFiles()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("型紙SVGの保存先フォルダ"));
    if (directory.isEmpty()) {
        return;
    }
    try {
        for (std::size_t index = 0; index < results_.size(); ++index) {
            const QString fileName = QStringLiteral("%1/%2.svg")
                .arg(directory, QString::fromStdString(results_[index].pattern.plateName));
            std::ofstream output(
                std::filesystem::path(fileName.toStdWString()), std::ios::binary);
            if (!output) {
                throw std::runtime_error("出力ファイルを開けませんでした。");
            }
            kachakacha::io::WritePlateFlatPatternSvg(output, results_[index].pattern);
            output.close();
            if (!output) {
                throw std::runtime_error("出力ファイルの保存に失敗しました。");
            }
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("保存エラー"), QString::fromUtf8(error.what()));
        return;
    }
    QMessageBox::information(
        this, QStringLiteral("保存"), QStringLiteral("型紙SVGを保存しました。"));
}
