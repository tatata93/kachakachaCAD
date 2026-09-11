// 配布する流線形前頭部が、説明だけでなく V2 の文書として成立することを見る。
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/app/RailwayNoseSample.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/io/DocumentFile.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using kachakacha::v2::app::BuildRailwayNoseFabricationSource;
using kachakacha::v2::app::BuildRailwayNoseOpenings;
using kachakacha::v2::app::BuildRailwayNoseSampleArchive;
using kachakacha::v2::app::BuildRailwayNoseSampleDocument;
using kachakacha::v2::app::EvaluateFabrication;
using kachakacha::v2::app::RailwayNoseSampleVersion;
using kachakacha::v2::app::kRailwayNoseWidthMm;
using kachakacha::v2::domain::CreateFabricationModelDefinition;
using kachakacha::v2::domain::CreateGuideSurfaceDefinition;
using kachakacha::v2::domain::CreateWireDefinition;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::ThickenSurfaceDefinition;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::io::LoadDocument;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

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

[[nodiscard]] int CountKind(EntityKind kind)
{
    int count = 0;
    for (const auto& entity : BuildRailwayNoseSampleDocument().snapshot.entities) {
        count += entity.kind == kind ? 1 : 0;
    }
    return count;
}

[[nodiscard]] const CreateFabricationModelDefinition& FabricationDefinition()
{
    static const auto file = BuildRailwayNoseSampleDocument();
    for (const auto& feature : file.snapshot.features) {
        if (const auto* definition =
                std::get_if<CreateFabricationModelDefinition>(&feature.definition)) {
            return *definition;
        }
    }
    throw std::runtime_error("製作モデルの定義が無い");
}

} // namespace

KACHA_V2_TEST(railway_nose_sample, V2の実用見本に必要な種類がそろう)
{
    RequireEqual(std::to_string(CountKind(EntityKind::Wire)), std::string("14"),
        "水平断面7・窓6・前照灯1");
    RequireEqual(std::to_string(CountKind(EntityKind::GuideSurface)), std::string("1"),
        "ロフト面");
    RequireEqual(std::to_string(CountKind(EntityKind::Part)), std::string("1"),
        "厚み付き外板");
    RequireEqual(std::to_string(CountKind(EntityKind::FabricationModel)), std::string("1"),
        "帯近似");
}

KACHA_V2_TEST(railway_nose_sample, 幅は3520mmの1_87で断面は7本)
{
    RequireNear(kRailwayNoseWidthMm, 40.4598, 1.0e-3, "模型幅");
    const auto file = BuildRailwayNoseSampleDocument();
    int sections = 0;
    for (const auto& feature : file.snapshot.features) {
        const auto* guide = std::get_if<CreateGuideSurfaceDefinition>(&feature.definition);
        if (guide != nullptr) {
            sections = static_cast<int>(guide->chains.size());
        }
    }
    RequireEqual(std::to_string(sections), std::string("7"), "ロフト断面");
}

KACHA_V2_TEST(railway_nose_sample, 窓と前照灯は滑らかな曲線で閉じる)
{
    const auto file = BuildRailwayNoseSampleDocument();
    int openings = 0;
    for (const auto& feature : file.snapshot.features) {
        const auto* wire = std::get_if<CreateWireDefinition>(&feature.definition);
        if (wire == nullptr || (feature.displayName.rfind("前面窓", 0) != 0
                && feature.displayName != "中央上部前照灯")) {
            continue;
        }
        ++openings;
        Require(!wire->segments.empty(), "曲線がある");
        for (const auto& segment : wire->segments) {
            Require(segment.Kind() == CurveKind::CubicBezier, "角を折れ線にしない");
        }
        RequireNear((wire->segments.front().StartPoint()
                        - wire->segments.back().EndPoint()).Length(),
            0.0, 1.0e-9, "閉じている");
    }
    RequireEqual(std::to_string(openings), std::string("7"), "6枚窓と前照灯");
}

KACHA_V2_TEST(railway_nose_sample, 帯近似して窓の取り分を型紙へ残す)
{
    const auto made = EvaluateFabrication(FabricationDefinition(),
        {BuildRailwayNoseFabricationSource()}, BuildRailwayNoseOpenings(), 0.01);
    Require(made.HasValue(), "近似できる");
    Require(made.Value().panels.size() >= 2, "大きな帯に分かれる");
    Require(made.Value().panels.size() <= 24, "部材上限を守る");
    int pieces = 0;
    for (const auto& panel : made.Value().panels) {
        pieces += static_cast<int>(panel.openings.size());
    }
    Require(pieces >= 7, "6枚窓と前照灯が型紙に残る");
}

KACHA_V2_TEST(railway_nose_sample, 保存して読み直しても作り方が残る)
{
    const auto archive = BuildRailwayNoseSampleArchive();
    Require(archive.HasValue(), "保存できる");
    const auto loaded = LoadDocument(archive.Value());
    Require(loaded.HasValue(), "読み直せる");
    Require(loaded.Value().metadata.description.find("実車寸法ではない")
            != std::string::npos,
        "近似値だと明記する");
    RequireEqual(std::to_string(loaded.Value().snapshot.entities.size()),
        std::to_string(BuildRailwayNoseSampleDocument().snapshot.entities.size()),
        "要素数を保つ");
}

KACHA_V2_TEST(railway_nose_sample, 配る見本が生成器と一致する)
{
    const auto archive = BuildRailwayNoseSampleArchive();
    Require(archive.HasValue(), "生成できる");
    const auto path = RepoRoot() / "samples" / "streamlined-railway-nose-1-87.kcd2";
    std::error_code code;
    Require(std::filesystem::exists(path, code), "流線形前頭部の見本がある");
    Require(ReadBinary(path) == archive.Value(), "生成器と同じ内容");
    Require(!RailwayNoseSampleVersion().empty(), "版がある");
}

KACHA_V2_TEST_MAIN("railway_nose_sample_tests")
