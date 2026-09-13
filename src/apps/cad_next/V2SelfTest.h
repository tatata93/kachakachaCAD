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
//! こうしておけば、ケースが増えても 1 ファイルが太らない。

#pragma once

#include "kachakacha/domain/Entity.h"

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

//! 道具・画面・視点・書き出し・ファイルのケース。
[[nodiscard]] std::vector<SelfTestCase> BasicCases();

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

//! 意味状態と部分要素の見え方(通常・Hover・選択・Snap・途中経過の描き分け)。
[[nodiscard]] std::vector<SelfTestCase> SemanticStateCases();

//! 文書にある、その種類のものの数(見えているかは問わない)。
[[nodiscard]] int CountOfKind(V2MainWindow& window, kachakacha::v2::domain::EntityKind kind);

//! 全ケースを動かす。落ちた数が 0 なら 0、そうでなければ 1 を返す。
[[nodiscard]] int RunSelfTest();

} // namespace kachakacha::v2::selftest
