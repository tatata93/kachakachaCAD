//! 画面を出さずに一通り触って確かめる道(WP-08)。
//!
//! ここを main.cpp から分けたのは、入口の仕事(引数を読む・窓を出す)と
//! 「動くかどうかを確かめる仕事」を混ぜないためである。
//! 混ぜると入口が長くなり、どちらを直しているのか分からなくなる。
//!
//! ケース自体は種類ごとに別のファイルへ置く。
//!   V2SelfTestBasics.cpp   : 道具・画面・視点・書き出し・ファイル
//!   V2SelfTestInput.cpp    : V1同等の操作系(マウス・キー・変換の道具)
//!   V2SelfTestModeling.cpp : 線の編集・作業平面・部品・部材・型紙
//!   V2SelfTestPlanes.cpp   : 原点の3面・3軸と作業平面の棚
//!   V2SelfTestDrawing.cpp  : 作図の棚
//!   V2SelfTestSemantics.cpp: 意味状態と部分要素の見え方
//!   V2SelfTestPointer.cpp  : クリックと引きずりの分け目・カーソルの形
//! こうしておけば、ケースが増えても 1 ファイルが太らない。

#pragma once

#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Entity.h"

#include <Qt>

#include <vector>

class V2MainWindow;

namespace kachakacha::v2::selftest {

//! 名前と中身の組。名前はそのまま画面へ出るので、日本語でよい。
struct SelfTestCase {
    const char* name;
    bool (*body)(V2MainWindow&);
};

//! 落ちたときに、どの見立てが外れたかを言う。
//! 言わないと、PC でしか出ない失敗を推測で直すことになる。
[[nodiscard]] bool Explain(const char* what, bool ok);

//! 落ちていなくても言う。調べた結果そのものが要るときに使う。
//! 雲で確かめられないこと(OCCT の作り方の当たり外れ)を PC から持ち帰る道。
void Note(const char* what);

//! 道具・画面・視点・書き出し・ファイルのケース。
[[nodiscard]] std::vector<SelfTestCase> BasicCases();
[[nodiscard]] std::vector<SelfTestCase> GptSurfaceCases();

//! 上段メニューの文字幅・重なり・マウス操作のケース。
[[nodiscard]] std::vector<SelfTestCase> MenuCases();

//! V1同等の操作系のケース。
[[nodiscard]] std::vector<SelfTestCase> InputCases();

//! 線の編集・作業平面・部品・部材・型紙のケース。
[[nodiscard]] std::vector<SelfTestCase> ModelingCases();

//! 形状ガイドの役割表のケース。
[[nodiscard]] std::vector<SelfTestCase> GuideCases();

//! 近似(製作モデル)と曲げ状態のケース。
[[nodiscard]] std::vector<SelfTestCase> FabricationCases();

//! 原点の3面・3軸と、作業平面の棚のケース。
[[nodiscard]] std::vector<SelfTestCase> PlaneCases();

//! 作図の棚(円弧の作り方・補助線・指定点・数値で線を作る)のケース。
[[nodiscard]] std::vector<SelfTestCase> DrawingCases();

//! 編集の棚(選んだものの数値編集)のケース。
[[nodiscard]] std::vector<SelfTestCase> EditCases();

//! 画面の読みやすさ(作図面が見えるか・選択が両向きに伝わるか・カーソル・ホバー)。
[[nodiscard]] std::vector<SelfTestCase> ScreenCases();

//! 押し出しの約束(下見中は文書不変・1操作1取り消し・選んだ演算の保持)。
[[nodiscard]] std::vector<SelfTestCase> ExtrudePromiseCases();

//! 何も無いところから面を作り、本番の指示だけで近似・曲げ・出力まで通す(Q4)。
[[nodiscard]] std::vector<SelfTestCase> ApproximationFlowCases();

//! 曲がった形状ガイドの面を、本番の指示だけで作る。近似の試験の下ごしらえ。
[[nodiscard]] bool MakeCurvedGuideSurface(V2MainWindow& window);

//! 押し出しの依存関係(面の縁の線・相手の立体を入力として記録しているか)。
[[nodiscard]] std::vector<SelfTestCase> ExtrudeGraphCases();

//! HO 流線形前頭部を使った総合試験(TM / UI-TM)。
[[nodiscard]] std::vector<SelfTestCase> HoModelCases();

//! 整理用のまとまりの実用試験(GR-01〜10)。
[[nodiscard]] std::vector<SelfTestCase> GroupCases();

//! 「選択に正対」の実用試験(VF-01〜08)。
[[nodiscard]] std::vector<SelfTestCase> FacingCases();

//! 意味状態と部分要素の見え方(通常・Hover・選択・Snap・途中経過の描き分け)。
[[nodiscard]] std::vector<SelfTestCase> SemanticStateCases();

//! クリックと引きずりの分け目、カーソルの形、カメラと物の取り合い。
[[nodiscard]] std::vector<SelfTestCase> PointerCases();

//! 文書にある、その種類のものの数(見えているかは問わない)。
[[nodiscard]] int CountOfKind(V2MainWindow& window, kachakacha::v2::domain::EntityKind kind);

//! 人の道の試験(オーナー指示 2026-09-15 §16)。
//!
//! 選ぶのは実際に拾う道(`SelectAt`)だけ。棚が **見えているか** を必ず見る。
//! 不可視の widget を直に叩くだけの試験は、ここには置かない。
[[nodiscard]] std::vector<SelfTestCase> HumanPathCases();

//! 人の道の下ごしらえ(V2SelfTestHumanPath.cpp が持つ)。他の人の道の試験からも使う。
//! 上から見て矩形を1つ引く。道具を持って画面を押す。
[[nodiscard]] bool DrawRectangleByHand(V2MainWindow& window);
//! 画面に見えている線のどれかを、実際に拾う。
[[nodiscard]] bool ClickOnAnyCurve(V2MainWindow& window, Qt::KeyboardModifiers modifiers);
//! 画面に見えている形状ガイドの塗りのどれかを、実際に拾う。
[[nodiscard]] bool ClickOnAnyGuideSurface(V2MainWindow& window);
//! 上から見て、画面の割合で指した場所に矩形を1つ引く。引いた線の番号(失敗なら Nil)。
[[nodiscard]] kachakacha::v2::base::EntityId DrawRectangleAtByHand(V2MainWindow& window,
    double x0, double y0, double x1, double y1);
//! その線の上を実際に押す。
[[nodiscard]] bool ClickOnCurveOf(V2MainWindow& window, const kachakacha::v2::base::EntityId& id);

//! 足す・引くの人の道(HP-BO)。道具 → 土台 → 相手(自動遷移)→ 下見 → Enter。
[[nodiscard]] std::vector<SelfTestCase> HumanPathBooleanCases();

//! C面取り / R丸めの人の道(HP-CN)。道具 → A → B → 下見 → Enter。
[[nodiscard]] std::vector<SelfTestCase> HumanPathCornerCases();

//! 2段の帯(HP-RB)。カテゴリ → 道具、押せない道具は理由つき。
[[nodiscard]] std::vector<SelfTestCase> RibbonCases();

//! 左の一覧(HP-XP)。節の並び、◉ の出し隠し、選択の同期、右クリックの献立。
[[nodiscard]] std::vector<SelfTestCase> ExplorerCases();

//! 状態行・HUD・測定の重ね道具(HP-ST)。
[[nodiscard]] std::vector<SelfTestCase> StatusLineCases();

//! 作図の作り方カード(HP-DM)。
[[nodiscard]] std::vector<SelfTestCase> DrawingMethodCases();

//! 部品の押し出しの欄(HP-PA)。
[[nodiscard]] std::vector<SelfTestCase> PartPanelCases();

//! 配列(直線/円形)の棚(HP-AR、指示書 D-23)。
[[nodiscard]] std::vector<SelfTestCase> ArrayCases();

//! 部品の配置(HP-PL、P-18)。移動・ミラー・パターン。
[[nodiscard]] std::vector<SelfTestCase> PartPlaceCases();

//! 立体を作る(HP-SO、P-08/P-09)。回転体・スイープ・ロフト立体・断ると足す。
[[nodiscard]] std::vector<SelfTestCase> SolidCases();

//! 辺の丸め・面取り(HP-FL、P-12)。
[[nodiscard]] std::vector<SelfTestCase> EdgeFinishCases();

//! シェル・分割(HP-SH、P-13)。
[[nodiscard]] std::vector<SelfTestCase> ShellSplitCases();

//! 線の上に置いて押す編集(HP-TR、トリム。Inventor の手順)。
[[nodiscard]] std::vector<SelfTestCase> HoverEditCases();

//! 上面 XY から決めた距離だけ離した作業平面を作って使う(断面を高さ違いに描くとき)。
[[nodiscard]] bool UseTopPlaneOffsetBy(V2MainWindow& window, double offsetMm);

//! 道具の棚の共通の枠(HP-PF、C-10)。節の並び・キャンセルと確定・作図の共通。
[[nodiscard]] std::vector<SelfTestCase> PanelFrameCases();

//! 生成の「作り方」カード(HP-GN、matrix F-13/F-14)。現在状態 / Flat 0% / Target 100%。
[[nodiscard]] std::vector<SelfTestCase> GenerateCases();

//! 展開の「作り方」カード(HP-UF、matrix F-11/F-12)。自動展開 / 基準辺指定 / 複数部材配置。
[[nodiscard]] std::vector<SelfTestCase> UnfoldCases();

//! 「厚み」の人の道(HP-TH、指示書 matrix P-10)。道具 → 3D で面 → 作り方 → Enter。
[[nodiscard]] std::vector<SelfTestCase> ThickenCases();

//! 「面の編集」の人の道(HP-SE)。道具 → 3D で面・縁 → 下見 → Enter。
[[nodiscard]] std::vector<SelfTestCase> SurfaceEditCases();

//! 「面の解析」の人の道(HP-SA)。棚のボタン → 塗り替え、下見も塗る。
[[nodiscard]] std::vector<SelfTestCase> SurfaceAnalysisCases();

//! 作業平面の棚の下見(HP-WP、D-24)。
[[nodiscard]] std::vector<SelfTestCase> WorkPlanePreviewCases();

//! 指示書 known_regressions(I-01)の退行のうち、他のケースが触れていなかったもの(RG-*)。
[[nodiscard]] std::vector<SelfTestCase> RegressionCases();

//! 近似の人の道(HP-AP)。道具 → 3D で対象 → 候補を比べる → 下見 → Enter。
[[nodiscard]] std::vector<SelfTestCase> HumanPathApproxCases();

//! 部材の編集を 3D のクリックから当てる(HP-PE、matrix F-05/06/07)。
[[nodiscard]] std::vector<SelfTestCase> PartEditCases();

//! 撮影の5場面がすべて作れるか(HP-RS、指示書 I-02)。
[[nodiscard]] std::vector<SelfTestCase> ResponsiveCases();
//! 引いた線が打ったとおり・狙ったとおりか(V2SelfTestDrawingAccuracy.cpp、HP-AC)。
[[nodiscard]] std::vector<SelfTestCase> DrawingAccuracyCases();
//! 境界面が外周と内側の線を分けて扱うか(V2SelfTestBoundaryFill.cpp、HP-SF-10)。
[[nodiscard]] std::vector<SelfTestCase> BoundaryFillCases();

//! 「線から面」の人の道(HP-LF、オーナー要望 2026-09-22)。線を選んで押すだけで輪を面にする。
[[nodiscard]] std::vector<SelfTestCase> LoopFacesCases();

//! 全ケースを動かす。落ちた数が 0 なら 0、そうでなければ 1 を返す。
[[nodiscard]] int RunSelfTest();

} // namespace kachakacha::v2::selftest
