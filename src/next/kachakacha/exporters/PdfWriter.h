#pragma once

//! 型紙のPDF(WP-11)。
//!
//! 印刷して切るためのものなので、**原寸で出ることが最優先**。
//! ビューアの「用紙に合わせる」で縮むと寸法が狂うので、
//! 用紙の大きさをページの大きさとして正しく入れ、拡大縮小を一切かけない。
//!
//! 依存を増やさないため手で書く(ADR 0027)。
//! 必要なのはベクター線だけなので、PDFの機能のうち使うのはごく一部でよい。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/exporters/PatternExport.h"

#include <string>
#include <vector>

namespace kachakacha::v2::exporters {

struct PdfMetadata {
    std::string title;
    //! 縮尺の分母。1/87 なら 87。0 なら注記しない。
    double referenceScaleDenominator = 0.0;
};

//! 複数ページの型紙を1つのPDFにする。
[[nodiscard]] base::Result<std::string> WritePatternPdf(const std::vector<PatternPage>& pages,
    const PdfMetadata& metadata);

} // namespace kachakacha::v2::exporters
