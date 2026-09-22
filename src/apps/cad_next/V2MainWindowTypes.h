#pragma once

//! 本体窓(V2MainWindow)が道具ごとに持つ写しや案の形。窓の宣言が 1 ファイル 1500 行の門に
//! 迫ったので、形と説明をここへ分けた(窓の中では同じ名前で使える: using で結んである)。

#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/fabrication/BandPartition.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QString>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

//! 決めたひと組。**覚える形** と **カーネルへ渡す形** を分けて持つ。分けないと、
//! 渡すために向きを畳んだ値がそのまま覚えられて次に「数値で決める」に化け、
//! 逆に覚える形だけにすると矢印と作る形が別々に向きを当て直すことになる。
struct V2PreparedExtrudeChoice {
    //! 人が選んだ決め方のまま。次の初期値になる。
    kachakacha::v2::app::ExtrudeChoice remembered;
    //! 向きを解いたもの。作る形はこれで作る。
    kachakacha::v2::app::ExtrudeChoice resolved;
};

//! 下見を出した瞬間の入力の写し(オーナー指示 §9)。**下見と確定は同じ写しから作る。**
//! 確定のときに選択を読み直すと、下見のあとに選択が変わった分だけ別の形が出来る。
//! 選択が変わったら写しを作り直し、下見も出し直す(黙って読み直さない)。
struct V2ExtrudeSnapshot {
    kachakacha::v2::app::ExtrudePlan plan;
    std::vector<kachakacha::v2::modeling::ExtrudeProfile> profiles;
    //! 面を押しているか。面の縁は `faceProfileLoops_` が持つ。
    bool facePushPull = false;
};

//! 下見の写し。**下見も確定も、これ1つから作る**(§9 と同じ決まり)。
//! 確定のときに選び直さない。見たものと違う面が出来るのを防ぐ。
struct V2SurfaceSnapshot {
    kachakacha::v2::modeling::GuideTable table;
    kachakacha::v2::modeling::GuideSurfaceResult built;
    std::vector<std::pair<kachakacha::v2::modeling::GuideTable, kachakacha::v2::modeling::GuideSurfaceResult>> batch;   // 一括の 2 つ目以降
};

//! 「選択に正対」の相手。点と、はっきりしている面の向き。
struct V2FacingTarget {
    std::vector<kachakacha::v2::geometry::Vector3> points;
    //! 作業平面のように向きがはっきりしているときだけ入る。推さない。
    std::optional<kachakacha::v2::geometry::Vector3> normal;
    std::optional<kachakacha::v2::geometry::Vector3> uAxis;
    //! 正対の相手になったものの数。画面の一文に出す。
    int count = 0;
    //! 向きを変えない相手か(立体そのものなど)。中央と大きさだけ合わせる。
    bool keepOrientation = false;
    //! 複数選んだときの決まり(Q1 の契約): **向きは向きを持つ最初の1つ、収まりは全部**が決める。
    //! 向きの違う相手が混じっていたら真になり、帯にそう出る(黙ってどれかの向きにしない)。
    bool mixedDirections = false;
};

//! 分け方を変える前に見せている案。1度目の指示で用意し(**文書は変えない**、Codex Q1-Q5-R3 B1)、
//! 2度目で当てる。やめる・道具を替える・別の指示を出すと消える。
struct V2PendingPartition {
    QString what;
    std::vector<std::size_t> numbers;
    //! どの模型の、どの値に対して見せた案か。変わっていたら当てない。
    std::string signature;
    kachakacha::v2::fabrication::BandPartitionPreview preview;
    kachakacha::v2::fabrication::BandValueRemap carried;
};

//! 棚の「曲げる部材」の欄を1回だけ読んだ結果。
//! **空欄と、書いてあって読めない字を、型で分ける。**
struct V2PartNumberSelection {
    bool blank = true;                  //!< 何も書いていない
    bool unreadable = false;            //!< 書いてあるが読めない
    std::string whyJa;                  //!< 読めない理由
    std::vector<std::size_t> numbers;   //!< 0 起点
};
