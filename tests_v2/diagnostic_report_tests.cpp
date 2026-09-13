// 「診断情報をコピー」の中身(DIAGNOSTICS_FEATURE_SPEC.md)。
#include "kachakacha/app/DiagnosticReport.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::DiagnosticMismatches;
using kachakacha::v2::app::DiagnosticSnapshot;
using kachakacha::v2::app::FileNameOnly;
using kachakacha::v2::app::FormatDiagnosticReport;
using kachakacha::v2::app::kDiagnosticSelectionLimit;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] DiagnosticSnapshot Sample()
{
    DiagnosticSnapshot snapshot;
    snapshot.timestamp = "2026-09-14T00:00:00+09:00";
    snapshot.version = "v2";
    snapshot.commit = "abc1234";
    snapshot.branch = "codex/v2-wp01-build-scaffold";
    snapshot.dirty = false;
    snapshot.dirtyKnown = true;
    snapshot.documentName = "nose_test.kcd2";
    snapshot.activePart = "FrontNose";
    snapshot.activeWorkPlane = "Front";
    snapshot.activeTool = "ベジェ曲線";
    snapshot.rightPanelMode = "道具";
    snapshot.rightPanelTool = "ベジェ曲線";
    snapshot.cursorMode = "十字";
    snapshot.cursorOwner = "ベジェ曲線";
    snapshot.previewOwner = "ベジェ曲線";
    snapshot.snapOwner = "ベジェ曲線";
    snapshot.snapType = "端点";
    snapshot.snapTarget = "線2 の始点";
    snapshot.selectionCount = 1;
    snapshot.selection = {"円弧2"};
    return snapshot;
}

} // namespace

KACHA_V2_TEST(diagnostic, 必須の欄がすべて出る)
{
    const std::string text = FormatDiagnosticReport(Sample());
    for (const char* key : {"timestamp", "version", "commit", "branch", "dirty",
             "document", "activePart", "activeWorkPlane", "activeTool",
             "rightPanelMode", "rightPanelTool", "cursorMode", "cursorOwner",
             "previewOwner",
             "snapOwner", "snapType", "snapTarget", "selectionCount"}) {
        Require(text.find(std::string(key) + ":") != std::string::npos,
            std::string(key) + " が出る");
    }
    Require(text.find("kachakachaCAD Diagnostic") == 0, "見出しから始まる");
}

KACHA_V2_TEST(diagnostic, 道の並びは出さない)
{
    // 貼り付ける先が他人の目に触れることを前提にする。
    // フォルダの並びには、たいてい OS の利用者名が入っている。
    DiagnosticSnapshot snapshot = Sample();
    snapshot.documentName = "C:\\Users\\someone\\Documents\\model\\nose_test.kcd2";
    const std::string text = FormatDiagnosticReport(snapshot);
    Require(text.find("nose_test.kcd2") != std::string::npos, "名前は出る");
    Require(text.find("Users") == std::string::npos, "利用者名は出ない");
    Require(text.find("C:") == std::string::npos, "ドライブは出ない");
    Require(text.find("Documents") == std::string::npos, "フォルダは出ない");
}

KACHA_V2_TEST(diagnostic, ファイル名だけを取り出せる)
{
    Require(FileNameOnly("C:\\a\\b\\c.kcd2") == std::string("c.kcd2"), "円記号の区切り");
    Require(FileNameOnly("/home/a/b/c.kcd2") == std::string("c.kcd2"), "斜線の区切り");
    Require(FileNameOnly("c.kcd2") == std::string("c.kcd2"), "区切りが無くてもよい");
    Require(FileNameOnly("").empty(), "空は空");
    Require(FileNameOnly("/a/b/").empty(), "末尾が区切りなら空");
}

KACHA_V2_TEST(diagnostic, 未保存でも作れる)
{
    // D-005。未保存の文書でも落ちない。
    DiagnosticSnapshot snapshot;
    snapshot.activeTool = "選択";
    const std::string text = FormatDiagnosticReport(snapshot);
    Require(text.find("(未保存)") != std::string::npos, "未保存と言う");
    Require(text.find("selectionCount: 0") != std::string::npos, "0件と言う");
}

KACHA_V2_TEST(diagnostic, そろっていればずれは出ない)
{
    Require(DiagnosticMismatches(Sample()).empty(), "ずれ無し");
    Require(FormatDiagnosticReport(Sample()).find("mismatch") == std::string::npos,
        "見出しも出ない");
}

KACHA_V2_TEST(diagnostic, 右の棚が前の道具のままならずれとして出る)
{
    // D-004。USER-UI-001 がまさにこれである。
    DiagnosticSnapshot snapshot = Sample();
    snapshot.rightPanelTool = "円弧";
    const auto found = DiagnosticMismatches(snapshot);
    Require(found.size() == 1, "1件");
    Require(found.front().find("rightPanelTool") != std::string::npos, "どの欄かを言う");
    Require(found.front().find("円弧") != std::string::npos, "何になっているかを言う");
    Require(found.front().find("ベジェ曲線") != std::string::npos, "何であるべきかも言う");
    const std::string text = FormatDiagnosticReport(snapshot);
    Require(text.find("mismatch:") != std::string::npos, "本文にも出る");
}

KACHA_V2_TEST(diagnostic, カーソルの形は道具の名前と照合しない)
{
    // cursorMode は「十字」「矢印」であって、道具の名前ではない。
    // 照合すると、正しく動いていても毎回ずれになる。
    DiagnosticSnapshot snapshot = Sample();
    snapshot.cursorMode = "矢印";
    Require(DiagnosticMismatches(snapshot).empty(), "形の違いはずれではない");
    // 作り直していないカーソルは、cursorOwner で分かる。
    snapshot.cursorOwner = "円弧";
    const auto found = DiagnosticMismatches(snapshot);
    Require(found.size() == 1, "1件");
    Require(found.front().find("cursorOwner") != std::string::npos, "欄の名前を言う");
}

KACHA_V2_TEST(diagnostic, 見ていない欄はずれと数えない)
{
    // 空の欄は「まだ繋いでいない」ということ。ずれと数えると毎回出る。
    DiagnosticSnapshot snapshot = Sample();
    snapshot.previewOwner.clear();
    snapshot.snapOwner.clear();
    snapshot.cursorOwner.clear();
    Require(DiagnosticMismatches(snapshot).empty(), "空はずれではない");
}

KACHA_V2_TEST(diagnostic, 選択が多すぎるときは切り詰める)
{
    // 全部並べると、貼り付けた先で本文が埋まる。
    DiagnosticSnapshot snapshot = Sample();
    snapshot.selection.clear();
    for (std::size_t index = 0; index < kDiagnosticSelectionLimit + 7; ++index) {
        snapshot.selection.push_back("線" + std::to_string(index));
    }
    snapshot.selectionCount = snapshot.selection.size();
    const std::string text = FormatDiagnosticReport(snapshot);
    Require(text.find("- 線0\n") != std::string::npos, "先頭は出る");
    Require(text.find("- 線" + std::to_string(kDiagnosticSelectionLimit) + "\n")
            == std::string::npos,
        "上限を超えた分は並べない");
    Require(text.find("(ほか 7 件)") != std::string::npos, "残りの数は言う");
    Require(text.find("selectionCount: " + std::to_string(snapshot.selectionCount))
            != std::string::npos,
        "全体の数は正しい");
}

KACHA_V2_TEST(diagnostic, 分からない欄は分からないと書く)
{
    DiagnosticSnapshot snapshot = Sample();
    snapshot.commit.clear();
    snapshot.dirtyKnown = false;
    const std::string text = FormatDiagnosticReport(snapshot);
    Require(text.find("commit: (なし)") != std::string::npos, "空欄にしない");
    Require(text.find("dirty: (不明)") != std::string::npos, "分からないと書く");
}

KACHA_V2_TEST(diagnostic, 吸着の種類すべてに名前がある)
{
    using kachakacha::v2::app::SnapKindNameJa;
    using kachakacha::v2::modeling::SnapKind;
    constexpr SnapKind kAll[] = {SnapKind::Intersection, SnapKind::Endpoint,
        SnapKind::Center, SnapKind::DrawingPoint, SnapKind::Tangent,
        SnapKind::Perpendicular, SnapKind::Midpoint, SnapKind::Quadrant,
        SnapKind::Extension, SnapKind::ProjectedOnPlane, SnapKind::ClosestOnCurve,
        SnapKind::GridMajor, SnapKind::GridMinor, SnapKind::FreeOnPlane,
        SnapKind::ScreenIntersection};
    for (const SnapKind kind : kAll) {
        const std::string name(SnapKindNameJa(kind));
        Require(!name.empty(), "名前がある");
        Require(name != std::string("不明"), "不明ではない");
    }
}

KACHA_V2_TEST_MAIN("diagnostic_report_tests")
