#pragma once

//! 書き出しの中身を作る(ui-workflows §11、WP-11)。
//!
//! ExportPanel は「何を、どの形式で、どこへ」を決める。ここは「中身」を作る。
//! 分けている理由は、検査を通ってから中身を作らせるためである。
//! 検査より先に作ると、断られたときに計算が丸ごと無駄になる。
//!
//! ここで作れるのは、OCCT を要らない中身だけである。
//!   - ワイヤーを平面に置いた型紙(SVG / DXF / 1:1 PDF)
//!   - 文書そのもの(kcd2)
//! 立体(STL / STEP)は面を張らないと出せないので、kernel 側で作る。

#include "kachakacha/app/ExportService.h"
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/exporters/PatternExport.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/io/DocumentFile.h"

#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! ワイヤーを1枚の型紙に置く。
//!
//! 置き方の約束:
//!   - 曲線は曲線のまま渡す。折れ線へ落とさない。
//!   - 紙の左下から余白ぶんだけ空けて置く。実寸のまま。縮めない。
//!   - 作業平面ではなく、XY平面へ落とした形で出す。Z は捨てる。
//!     捨ててよいのは、型紙が平らなものだからである。
//!     平らでないものを型紙にしたいときは、先に展開する(fabrication 側の仕事)。
struct WirePatternRequest {
    std::vector<geometry::CurveSegment> segments;
    double marginMm = 10.0;
    std::string title;
};

//! 置いた結果。紙の大きさは中身から決める。決まった入力からは決まった大きさになる。
[[nodiscard]] base::Result<exporters::PatternPage> BuildWirePatternPage(
    const WirePatternRequest& request, double toleranceMm);

//! ワイヤーを、頼まれた形式の中身にする。
[[nodiscard]] base::Result<std::string> MakeWireContent(const WirePatternRequest& request,
    ExportFormat format, double toleranceMm);

//! 文書そのものを kcd2 の中身にする。
[[nodiscard]] base::Result<std::string> MakeProjectContent(const io::DocumentFile& file);

} // namespace kachakacha::v2::app
