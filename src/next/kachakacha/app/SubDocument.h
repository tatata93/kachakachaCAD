#pragma once

//! 選んだものだけを別の文書にする(工程4「選んだ箇所だけを別 kcd2 に」)。
//!
//! V1 の BuildOutputProject と同じ考え方である。表に無いものを消していき、
//! 表のものが参照していて消せないもの(面の元ワイヤー、部品の元の面、作業平面)は残す。
//! 残したものは名前で返す。黙って残すと「選んでいないのに入っている」と見える。
//!
//! V2 の文書は「作り方(Feature)の履歴」なので、消すのではなく **要るものを集める**。
//! 選んだものを作った Feature から、その入力を作った Feature へ、上流へたどる。
//! 入力は Feature の inputEntityIds と、作り方の中身が指す id の両方を見る。
//! 片方だけを見ると、開き直したときに「無いものを指している」で作り直せない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Feature.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 作り方の中身が指している Entity。inputEntityIds に無いものも含めて全部。
[[nodiscard]] std::vector<base::EntityId> DefinitionEntityReferences(
    const domain::FeatureDefinition& definition);

struct SubDocument {
    document::DocumentSnapshot snapshot;
    //! 選んでいないが、選んだものが参照していて残したものの名前。
    std::vector<std::string> keptDependencyNames;
    //! 選んだもののうち、文書にあった数。
    int selectedCount = 0;
};

//! 選んだ Entity と、それを作るのに要る上流だけを持つ文書を作る。
//! 選んだものが無ければ EXP-S001、どれも文書に無ければ EXP-S002 で断る。
//! 出来た文書は Document::Validate を通す。通らなければその理由で断る。
[[nodiscard]] base::Result<SubDocument> ExtractSubDocument(
    const document::DocumentSnapshot& source, const std::vector<base::EntityId>& selected);

//! 「残した依存: 作業平面1, 外形 …」の一文。残していなければ空。
[[nodiscard]] std::string KeptDependenciesSummaryJa(const SubDocument& made);

//! 残したものの知らせ(EXP-S003、情報)。残していなければ空。
[[nodiscard]] std::optional<base::Diagnostic> KeptDependenciesNote(const SubDocument& made);

} // namespace kachakacha::v2::app
