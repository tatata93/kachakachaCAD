#include "PartPatternViewDialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

using kachakacha::geometry::Vector2;
using kachakacha::io::PlateFlatPattern;
using kachakacha::io::PlateFlatPatternPath;

struct PatternBounds {
    double minimumX = std::numeric_limits<double>::max();
    double minimumY = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = std::numeric_limits<double>::lowest();

    void Include(const Vector2& point)
    {
        minimumX = std::min(minimumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumX = std::max(maximumX, point.x);
        maximumY = std::max(maximumY, point.y);
    }

    [[nodiscard]] bool IsValid() const
    {
        return maximumX >= minimumX && maximumY >= minimumY;
    }
};

[[nodiscard]] PatternBounds BoundsOf(const PlateFlatPattern& pattern)
{
    PatternBounds bounds;
    const auto includePath = [&bounds](const PlateFlatPatternPath& path) {
        for (const Vector2& point : path.points) {
            bounds.Include(point);
        }
    };
    includePath(pattern.outerBoundary);
    for (const auto& piece : pattern.pieces) {
        includePath(piece.outerBoundary);
        for (const auto& opening : piece.openings) includePath(opening);
        for (const auto& fold : piece.foldLines) includePath(fold);
        for (const auto& cut : piece.reliefCuts) includePath(cut);
    }
    for (const auto& opening : pattern.openings) includePath(opening);
    for (const auto& fold : pattern.foldLines) includePath(fold);
    for (const auto& cut : pattern.reliefCuts) includePath(cut);
    return bounds;
}

//! 型紙を1枚ずつ横へ並べて描く簡易キャンバス。
class PatternCanvas : public QWidget {
public:
    PatternCanvas(
        const std::vector<PlateFlatPattern>* patterns,
        const QStringList* captions,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , patterns_(patterns)
        , captions_(captions)
    {
        setMinimumSize(640, 420);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#fdfdf8"));
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (patterns_ == nullptr || patterns_->empty()) {
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("型紙がありません"));
            return;
        }

        // 全型紙の合計サイズから縮尺を決める。
        constexpr double gapMillimeters = 12.0;
        double totalWidth = 0.0;
        double maximumHeight = 0.0;
        std::vector<PatternBounds> bounds;
        for (const auto& pattern : *patterns_) {
            PatternBounds patternBounds = BoundsOf(pattern);
            if (!patternBounds.IsValid()) {
                patternBounds = PatternBounds{0.0, 0.0, 1.0, 1.0};
            }
            totalWidth += patternBounds.maximumX - patternBounds.minimumX + gapMillimeters;
            maximumHeight = std::max(
                maximumHeight, patternBounds.maximumY - patternBounds.minimumY);
            bounds.push_back(patternBounds);
        }
        const double margin = 28.0;
        const double captionSpace = 34.0;
        const double scale = std::min(
            (width() - margin * 2.0) / std::max(totalWidth, 1.0),
            (height() - margin * 2.0 - captionSpace) / std::max(maximumHeight, 1.0));

        double cursorX = margin;
        for (std::size_t index = 0; index < patterns_->size(); ++index) {
            const auto& pattern = (*patterns_)[index];
            const PatternBounds& patternBounds = bounds[index];
            const double offsetX = cursorX - patternBounds.minimumX * scale;
            const double offsetY = margin
                + (maximumHeight - (patternBounds.maximumY - patternBounds.minimumY)) * scale * 0.5
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
            const auto foldPen = [](int direction) {
                if (direction > 0) {
                    return QPen(QColor("#c2402a"), 1.2, Qt::DashLine); // 山折り
                }
                if (direction < 0) {
                    return QPen(QColor("#2a5fc2"), 1.2, Qt::DashDotLine); // 谷折り
                }
                return QPen(QColor("#8a8a8a"), 1.0, Qt::DashLine);
            };

            const QPen outerPen(QColor("#26323a"), 1.8);
            const QPen openingPen(QColor("#26323a"), 1.2);
            const QPen cutPen(QColor("#7a2ac2"), 1.2, Qt::SolidLine);
            if (!pattern.pieces.empty()) {
                for (const auto& piece : pattern.pieces) {
                    drawPath(piece.outerBoundary, outerPen, true);
                    for (const auto& opening : piece.openings) drawPath(opening, openingPen, true);
                    for (const auto& fold : piece.foldLines) drawPath(fold, foldPen(fold.foldDirection), false);
                    for (const auto& cut : piece.reliefCuts) drawPath(cut, cutPen, false);
                }
            } else {
                drawPath(pattern.outerBoundary, outerPen, true);
                for (const auto& opening : pattern.openings) drawPath(opening, openingPen, true);
                for (const auto& fold : pattern.foldLines) drawPath(fold, foldPen(fold.foldDirection), false);
                for (const auto& cut : pattern.reliefCuts) drawPath(cut, cutPen, false);
            }

            // 部材番号と説明。
            const double centerX = offsetX
                + (patternBounds.minimumX + patternBounds.maximumX) * 0.5 * scale;
            painter.setPen(QColor("#26323a"));
            QFont numberFont = painter.font();
            numberFont.setPointSizeF(14.0);
            numberFont.setBold(true);
            painter.setFont(numberFont);
            painter.drawText(
                QRectF(centerX - 90.0,
                    offsetY + patternBounds.minimumY * scale - 26.0, 180.0, 22.0),
                Qt::AlignCenter,
                QString::number(index + 1));
            if (captions_ != nullptr && static_cast<int>(index) < captions_->size()) {
                QFont captionFont = painter.font();
                captionFont.setPointSizeF(8.5);
                captionFont.setBold(false);
                painter.setFont(captionFont);
                painter.drawText(
                    QRectF(centerX - 150.0,
                        offsetY + patternBounds.maximumY * scale + 6.0, 300.0, 30.0),
                    Qt::AlignHCenter | Qt::AlignTop,
                    captions_->at(static_cast<int>(index)));
            }
            cursorX += (patternBounds.maximumX - patternBounds.minimumX + gapMillimeters) * scale;
        }

        // 凡例。
        QFont legendFont = painter.font();
        legendFont.setPointSizeF(9.0);
        legendFont.setBold(false);
        painter.setFont(legendFont);
        const double legendY = height() - 16.0;
        painter.setPen(QPen(QColor("#c2402a"), 1.2, Qt::DashLine));
        painter.drawLine(QPointF(16.0, legendY), QPointF(46.0, legendY));
        painter.setPen(QColor("#26323a"));
        painter.drawText(QPointF(52.0, legendY + 4.0), QStringLiteral("山折り"));
        painter.setPen(QPen(QColor("#2a5fc2"), 1.2, Qt::DashDotLine));
        painter.drawLine(QPointF(112.0, legendY), QPointF(142.0, legendY));
        painter.setPen(QColor("#26323a"));
        painter.drawText(QPointF(148.0, legendY + 4.0), QStringLiteral("谷折り"));
    }

private:
    const std::vector<PlateFlatPattern>* patterns_;
    const QStringList* captions_;
};

} // namespace

PartPatternViewDialog::PartPatternViewDialog(
    QString title,
    std::vector<kachakacha::io::PlateFlatPattern> patterns,
    QStringList captions,
    QWidget* parent)
    : QDialog(parent)
    , patterns_(std::move(patterns))
    , captions_(std::move(captions))
{
    setWindowTitle(title);
    resize(920, 560);
    auto* layout = new QVBoxLayout(this);
    auto* canvas = new PatternCanvas(&patterns_, &captions_, this);
    layout->addWidget(canvas, 1);

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
        for (std::size_t index = 0; index < patterns_.size(); ++index) {
            const QString fileName = QStringLiteral("%1/%2.svg")
                .arg(directory, QString::fromStdString(patterns_[index].plateName));
            std::ofstream output(
                std::filesystem::path(fileName.toStdWString()), std::ios::binary);
            if (!output) {
                throw std::runtime_error("出力ファイルを開けませんでした。");
            }
            kachakacha::io::WritePlateFlatPatternSvg(output, patterns_[index]);
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
