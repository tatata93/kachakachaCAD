#pragma once

//! 書き出しの段取り(ui-workflows §11、WP-11)。
//!
//! 「何を、どの形式で、どこへ」を1本に通す層。
//! ここでやるのは4つ。
//!   1. 対象と形式の組合せが成り立つかを見る。
//!   2. 出す前に検査する。通らなければ出さない。
//!   3. ファイル名を決める(勝手に上書きしない)。
//!   4. 中身を渡された関数から受け取り、途中で切れないように書く。
//!
//! 中身そのものは作らない。作るのは exporters と OCCT 側である。
//! ここで作ると、形式が増えるたびにこの層が太る。
//!
//! V1 は書き出しが画面のボタンごとにばらばらで、検査を通る道と通らない道が
//! あった。0バイトのファイルが残ることもあった。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/io/AtomicFile.h"

#include <functional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 何を出すか(§11.1)。
enum class ExportTarget {
    VisibleParts,
    SelectedParts,
    SelectedFabricationPanels,
    CurrentPattern,
    SelectedWires,
    //! 選んだもの(種類を問わない)と、それを作るのに要る上流だけの別文書(kcd2)。
    SelectedEntities,
    Project,
};

[[nodiscard]] std::string_view ExportTargetNameJa(ExportTarget target) noexcept;

//! どの形式で出すか(§11.2)。
enum class ExportFormat {
    Stl,
    Step,
    Svg,
    Dxf,
    Pdf,
    Kcd2,
};

[[nodiscard]] std::string_view ExportFormatNameJa(ExportFormat format) noexcept;
//! 拡張子。先頭に点を付けて返す。
[[nodiscard]] std::string_view ExportFormatExtension(ExportFormat format) noexcept;
//! 中身が文字か、そのままの並びか。SVG/DXF は文字、STL/PDF/kcd2 は並び。
[[nodiscard]] bool ExportFormatIsText(ExportFormat format) noexcept;

//! その対象をその形式で出せるか。出せない組合せは、押す前に分かるようにする。
[[nodiscard]] bool ExportFormatAllowed(ExportTarget target, ExportFormat format) noexcept;

//! その対象で出せる形式を、決まった順で並べたもの。
[[nodiscard]] std::vector<ExportFormat> AllowedFormatsFor(ExportTarget target);

//! 書き出しの注文。
struct ExportRequest {
    ExportTarget target = ExportTarget::SelectedParts;
    ExportFormat format = ExportFormat::Step;
    //! 出す数。0なら実行不可(§11.1)。
    int targetCount = 0;
    //! 出力先。拡張子は付いていなくてよい(こちらで付ける)。
    std::string path;
    //! 既にあるファイルへ上書きしてよいか。既定は駄目。
    bool overwrite = false;
};

struct ExportOutcome {
    std::string path;
    std::size_t byteCount = 0;
    //! 控えを残したか。
    bool keptBackup = false;
};

//! 中身を作る側。呼ばれたときだけ作る(検査に落ちたら作らせない)。
using ExportContentMaker = std::function<base::Result<std::string>()>;

//! 出す。検査に通ったときだけ中身を作らせ、途中で切れないように書く。
[[nodiscard]] base::Result<ExportOutcome> RunExport(const ExportRequest& request,
    const ExportContentMaker& makeContent);

//! 拡張子を整えたファイル名。既に正しい拡張子なら、そのまま返す。
[[nodiscard]] base::Result<std::string> ResolveExportPath(const std::string& path,
    ExportFormat format);

} // namespace kachakacha::v2::app
