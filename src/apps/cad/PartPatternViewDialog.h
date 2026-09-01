#pragma once

#include "kachakacha/io/PartPatterns.h"

#include <QDialog>
#include <QStringList>

#include <vector>

//! 専用型紙ビュー(docs/surface-unfolding-spec.md)。部材番号と山谷折りを
//! 色分け表示し、SVGとして保存できる。スライダーで「平面(型紙)」から
//! 「折り曲げた近似形状」までの中間状態を確認できる。
class PartPatternViewDialog : public QDialog {
public:
    PartPatternViewDialog(
        QString title,
        std::vector<kachakacha::io::PartPatternResult> results,
        QStringList captions,
        QWidget* parent = nullptr);

private:
    void SaveSvgFiles();

    std::vector<kachakacha::io::PartPatternResult> results_;
    QStringList captions_;
};
