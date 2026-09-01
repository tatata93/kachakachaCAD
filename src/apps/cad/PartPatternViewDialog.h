#pragma once

#include "kachakacha/io/PlateFlatPattern.h"

#include <QDialog>
#include <QStringList>

#include <vector>

//! 専用型紙ビュー(docs/surface-unfolding-spec.md)。部材番号と山谷折りを
//! 色分け表示し、SVGとして保存できる。印刷プレビューを兼ねる読み取り専用ビュー。
class PartPatternViewDialog : public QDialog {
public:
    PartPatternViewDialog(
        QString title,
        std::vector<kachakacha::io::PlateFlatPattern> patterns,
        QStringList captions,
        QWidget* parent = nullptr);

private:
    void SaveSvgFiles();

    std::vector<kachakacha::io::PlateFlatPattern> patterns_;
    QStringList captions_;
};
