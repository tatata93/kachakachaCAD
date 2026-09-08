// 配る見本の文書(WP-12)。
#include "kachakacha/app/SampleDocument.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/AtomicFile.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using kachakacha::v2::app::BuildSampleArchive;
using kachakacha::v2::app::BuildSampleDocument;
using kachakacha::v2::app::SampleDocumentVersion;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::io::LoadDocument;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

//! リポジトリの根。組み立てのときに渡してある。
[[nodiscard]] std::filesystem::path RepoRoot()
{
    return std::filesystem::path(KACHACAD_V2_REPO_ROOT);
}

[[nodiscard]] std::string ReadBinary(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] int CountOfKind(const kachakacha::v2::document::DocumentSnapshot& snapshot,
    EntityKind kind)
{
    int count = 0;
    for (const auto& entity : snapshot.entities) {
        if (entity.kind == kind) {
            ++count;
        }
    }
    return count;
}

} // namespace

KACHA_V2_TEST(sample_document, 見本にひととおり入っている)
{
    const auto file = BuildSampleDocument();
    // 外形1・腰の高さ1・窓6・前照灯1 で 9本。
    RequireEqual(std::to_string(CountOfKind(file.snapshot, EntityKind::Wire)),
        std::string("9"), "ワイヤーの数");
    RequireEqual(std::to_string(file.snapshot.groups.size()), std::string("2"),
        "まとまりの数");
    Require(!file.metadata.title.empty(), "題がある");
    Require(!file.metadata.description.empty(), "説明がある");
}

KACHA_V2_TEST(sample_document, 補助線は腰の高さだけ)
{
    // 補助線は切る線ではない。型紙へ出してはいけないので、数を決めておく。
    const auto file = BuildSampleDocument();
    int construction = 0;
    for (const auto& entity : file.snapshot.entities) {
        if (entity.construction) {
            ++construction;
        }
    }
    RequireEqual(std::to_string(construction), std::string("1"), "補助線の数");
}

KACHA_V2_TEST(sample_document, 1_87の寸法になっている)
{
    // 実物 20000mm の側面は 1/87 で 229.885...mm になる。
    const auto file = BuildSampleDocument();
    bool found = false;
    for (const auto& feature : file.snapshot.features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::CreateWireDefinition>(&feature.definition);
        if (definition == nullptr || feature.displayName != "側面の外形") {
            continue;
        }
        found = true;
        RequireNear(definition->segments.front().Evaluate(1.0).x, 20000.0 / 87.0, 1.0e-9,
            "長さ");
    }
    Require(found, "外形がある");
}

KACHA_V2_TEST(sample_document, 円は円のまま入っている)
{
    const auto file = BuildSampleDocument();
    bool found = false;
    for (const auto& feature : file.snapshot.features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::CreateWireDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        for (const auto& segment : definition->segments) {
            if (segment.Kind() == CurveKind::Circle) {
                found = true;
            }
        }
    }
    Require(found, "前照灯が円のまま");
}

KACHA_V2_TEST(sample_document, 同じ版からは同じバイト列が出る)
{
    // 出ないと、見本が変わったのか作り直しただけなのかが版管理で分からない。
    const auto first = BuildSampleArchive();
    const auto second = BuildSampleArchive();
    Require(first.HasValue() && second.HasValue(), "作れる");
    Require(first.Value() == second.Value(), "同じ");
}

KACHA_V2_TEST(sample_document, 保存したものが読み直せる)
{
    const auto archive = BuildSampleArchive();
    Require(archive.HasValue(), "作れる");
    const auto loaded = LoadDocument(archive.Value());
    Require(loaded.HasValue(), "読める");
    RequireEqual(std::to_string(CountOfKind(loaded.Value().snapshot, EntityKind::Wire)),
        std::string("9"), "ワイヤーの数が同じ");
    RequireEqual(loaded.Value().metadata.title, BuildSampleDocument().metadata.title,
        "題が同じ");
}

KACHA_V2_TEST(sample_document, 配ってある見本が今の版と一致する)
{
    // 見本を直して置き直し忘れると、マニュアルの図と中身がずれる。ここで落とす。
    const std::filesystem::path path = RepoRoot() / "samples" / "v2-sample.kcd2";
    std::error_code code;
    Require(std::filesystem::exists(path, code),
        "samples/v2-sample.kcd2 がある(無ければ kachakacha_v2_write_sample で作る)");
    const auto archive = BuildSampleArchive();
    Require(archive.HasValue(), "作れる");
    Require(ReadBinary(path) == archive.Value(),
        "配ってある見本が今の版と同じ(違えば kachakacha_v2_write_sample で作り直す)");
    Require(!SampleDocumentVersion().empty(), "版がある");
}

KACHA_V2_TEST_MAIN("sample_document_tests")
