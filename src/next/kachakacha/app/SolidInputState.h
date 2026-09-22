#pragma once

//! 「立体を作る」(回転体・ロフト立体・スイープ、matrix P-08/P-09)の入力の状態。
//!
//! 道具から始める(何も選んでいなくても棚が出る)。3D で押したものは、種類で欄に入る:
//!   閉じた線 → 輪郭(ロフト立体では断面。押した順が通す順)
//!   直線     → 回転軸(回転体)   開いた線 → 経路(スイープ。何本でも、1 本につながる並び)
//!   部品     → 相手(足す・引くのとき)
//! 入っているものを押し直すと外れる。「ここへ選ぶ」で次のクリックの行き先を選べる。
//! 何がどの欄に入るかは、ここが決める。画面は映して押すだけ。

#include "kachakacha/base/Ids.h"
#include "kachakacha/modeling/SolidInput.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class SolidSlot {
    Profiles,   //!< 輪郭 / 断面
    Axis,       //!< 回転軸(回転体)
    Path,       //!< 経路(スイープ)
    Target,     //!< 足す・引くの相手
};

//! 回転体の回し方(正本の作り方: 全回転 / 角度指定 / 対称回転)。
enum class RevolveMode {
    Full,
    Angle,
    Symmetric,
};

//! 3D で押したものの種類(画面が文書から読んで渡す)。
enum class SolidPickKind {
    ClosedWire,
    LineWire,     //!< 直線 1 本だけのワイヤー
    OpenWire,     //!< ほかの開いた線
    Part,
    Other,
};

struct SolidInputState {
    modeling::SolidMethod method = modeling::SolidMethod::Revolve;
    std::vector<base::EntityId> profiles;
    base::EntityId axis;
    std::vector<base::EntityId> path;
    base::EntityId target;
    std::optional<SolidSlot> activeSlot;
    RevolveMode revolveMode = RevolveMode::Full;
    //! 角度指定・対称回転の角度(度)。
    double angleDeg = 180.0;
    //! 0 = 新しい部品、1 = 足す、2 = 引く。
    int booleanMode = 0;
};

[[nodiscard]] std::string_view SolidSlotKey(SolidSlot slot) noexcept;   //!< PROFILE / AXIS / PATH / TARGET
[[nodiscard]] std::string_view SolidSlotNameJa(SolidSlot slot,
    modeling::SolidMethod method) noexcept;   //!< 輪郭 / 断面 / 回転軸 / 経路 / 相手
[[nodiscard]] std::string_view RevolveModeNameJa(RevolveMode mode) noexcept;
[[nodiscard]] std::string_view SolidBooleanNameJa(int booleanMode) noexcept;   //!< 新しい部品 / 足す / 引く

//! 棚の「作り方」のカード(正本の 3 枚)。まだ作れないものは available = false で、
//! 理由(tipJa)を添えて押せない形で出す。押せるのに何も起きないボタンを作らない。
struct SolidMethodCard {
    std::string_view labelJa;
    std::string_view tipJa;
    bool available = true;
};

[[nodiscard]] const std::array<SolidMethodCard, 3>& SolidMethodCards(modeling::SolidMethod method);

//! いま押された形で出すカードの番号(回転体は回し方、ロフト立体・スイープは 0)。
[[nodiscard]] int SolidMethodCardIndex(const SolidInputState& state) noexcept;

//! 回転体のカード番号から回し方(0 全回転 / 1 角度指定 / 2 対称回転)。
[[nodiscard]] RevolveMode RevolveModeOfCard(int index) noexcept;

//! 命令の名前(part.revolve / part.loft_solid / part.sweep)から作り方。無ければ偽。
[[nodiscard]] bool SolidMethodForCommand(std::string_view commandId,
    modeling::SolidMethod& method) noexcept;

//! 作り方を替える。輪郭と相手はそのまま、使わなくなる欄(軸・経路)は空にする。
[[nodiscard]] SolidInputState WithSolidMethod(const SolidInputState& state,
    modeling::SolidMethod method);

//! 操作を替える(0 新しい部品 / 1 足す / 2 引く)。新しい部品に戻したら相手は空にする。
[[nodiscard]] SolidInputState WithSolidBoolean(const SolidInputState& state, int booleanMode);

//! その作り方で使う欄(並び順)。相手は足す・引くのときだけ。
[[nodiscard]] std::vector<SolidSlot> SolidSlotsFor(const SolidInputState& state);

//! 次の 3D クリックが入る欄。明示した欄があればそれ、無ければ空いている欄を並び順に。
//! 全部入っていれば輪郭(輪郭は何個でも足せる)。
[[nodiscard]] SolidSlot NextSolidSlot(const SolidInputState& state);

//! 3D で押した。入っていれば外れる。種類で欄が決まる(明示した欄が種類に合えばそこへ)。
//! 入らないもの(種類が合わない)なら state をそのまま返し、whyJa に理由を入れる。
[[nodiscard]] SolidInputState WithSolidPick(const SolidInputState& state, const base::EntityId& id,
    SolidPickKind kind, std::string* whyJa = nullptr);

//! 3D の選択から外れた。入っている欄から外す。
[[nodiscard]] SolidInputState WithoutSolidEntries(const SolidInputState& state,
    const std::vector<base::EntityId>& ids);

//! 「解除」と「ここへ選ぶ」。
[[nodiscard]] SolidInputState WithSolidSlotCleared(const SolidInputState& state, SolidSlot slot);
[[nodiscard]] SolidInputState WithActiveSolidSlot(const SolidInputState& state, SolidSlot slot);

//! 入っているものの並び(輪郭・軸・経路・相手の順)。3D の選択の印と同じにする。
[[nodiscard]] std::vector<base::EntityId> SolidEntries(const SolidInputState& state);

//! 作るのに要る欄が全部入っているか(輪郭の数: ロフト立体は 2 以上、ほかは 1 以上)。
[[nodiscard]] bool SolidReady(const SolidInputState& state) noexcept;

//! 回転体の角度(ラジアン)と対称か。全回転なら 2π。
[[nodiscard]] double SolidAngleRad(const SolidInputState& state) noexcept;

//! 「次のクリック → 回転軸」のような、いま何を待っているかの一行。
[[nodiscard]] std::string SolidHintJa(const SolidInputState& state);

//! 実際に作った結果の要約。下見と確定は同じものを使う。
struct SolidPreviewOutcome {
    bool evaluated = false;
    bool available = false;
    double volumeMm3 = 0.0;
    std::string refusalJa;
};

//! 棚の「状態」に出す行。
[[nodiscard]] std::vector<std::string> SolidStatusLinesJa(const SolidInputState& state,
    const SolidPreviewOutcome& outcome, bool previewShown);

//! 一番下の一行。「回転体: PROFILE=矩形 / AXIS=線 / 360° / NEW / Preview only」
[[nodiscard]] std::string SolidFooterLine(const SolidInputState& state,
    const std::string& profileNames, const std::string& axisOrPathNames,
    const SolidPreviewOutcome& outcome, bool previewShown);

} // namespace kachakacha::v2::app
