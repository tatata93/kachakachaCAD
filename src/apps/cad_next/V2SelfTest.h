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
//! こうしておけば、ケースが増えても 1 ファイルが太らない。

#pragma once

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

//! 全ケースを動かす。落ちた数が 0 なら 0、そうでなければ 1 を返す。
[[nodiscard]] int RunSelfTest();

} // namespace kachakacha::v2::selftest
