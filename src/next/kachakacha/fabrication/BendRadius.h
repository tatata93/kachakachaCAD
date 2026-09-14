#pragma once

//! 曲げ具合と半径の結びつき、AUTO と LOCK(オーナー指示 2026-09-14 §30・§31)。
//!
//! 円筒として近似した部材は、曲げ具合(0〜100%)と半径 R が **同じことの言い換え**
//! である。どちらから操作してもよい。
//!
//! いちばん大事な決まりは **面内長を変えないこと** である。
//! 板は伸びない。平らなときの長さ L は、どれだけ曲げても変わらない。
//! したがって、曲げ角 θ と半径 R は必ず `L = R * θ` で結ばれる。
//! 3次元の頂点を線形に混ぜるやり方は、この関係を壊すので使わない
//! (`fabrication/Assembly.h` の §10.1 と同じ考え方)。
//!
//! AUTO と LOCK:
//!   - AUTO: 近似が測った半径をそのまま使う。作り直すと更新される。
//!   - LOCK: 人が入れた半径を使う。**作り直しても勝手に戻さない。**
//!     模型工作では、手元にある丸棒や治具の径へ合わせたいことがある。
//!     計算した 21.63mm より、22.00mm のほうが作れる、ということが起きる。

#include "kachakacha/base/Diagnostic.h"

#include <optional>
#include <string>

namespace kachakacha::v2::fabrication {

//! その値をどう決めたか。
enum class ValueLock {
    Auto,     //!< 近似が測った値。作り直すと更新される。
    Locked,   //!< 人が入れた値。作り直しても戻さない。
};

[[nodiscard]] std::string_view ValueLockNameJa(ValueLock value) noexcept;

//! 円筒として曲げる部材1枚ぶんの、曲げと半径。
struct BendRadius {
    //! 平らなときの、曲げる向きの長さ(mm)。**曲げても変わらない。**
    double flatLengthMm = 0.0;
    //! 100% まで曲げたときの半径(mm)。
    double radiusMm = 0.0;
    ValueLock lock = ValueLock::Auto;
};

//! 100% まで曲げたときの曲げ角(ラジアン)。`θ = L / R`。
[[nodiscard]] base::Result<double> FullSweepAngleRad(const BendRadius& bend);

//! その曲げ具合(0〜100%)での曲げ角(ラジアン)。0% は 0。
[[nodiscard]] base::Result<double> SweepAngleRadAt(const BendRadius& bend,
    double percent);

//! その曲げ具合での半径(mm)。0% は平ら(値を返さない)。
//!
//! 面内長が変わらないので `R(p) = L / θ(p) = R100 * 100 / p` になる。
//! 途中の状態でも板の長さは同じである。
[[nodiscard]] std::optional<double> RadiusAtPercent(const BendRadius& bend,
    double percent);

//! 人が半径を入れた。以後 LOCK。
//!
//! `percent` は **そのとき画面に出ていた曲げ具合** である。
//! 70% のときに「R = 22mm」と入れたら、70% で 22mm になるように 100% の半径を決める
//! (§30 の「70% と R=○mm の両方から操作可能」)。
[[nodiscard]] base::Result<BendRadius> LockRadiusAtPercent(BendRadius bend,
    double radiusMm, double percent);

//! LOCK を外して AUTO へ戻す。近似が測った半径を入れ直す。
[[nodiscard]] BendRadius UnlockRadius(BendRadius bend, double measuredRadiusMm);

//! 近似をやり直した。AUTO なら測った値へ更新し、LOCK なら **触らない。**
[[nodiscard]] BendRadius RefitRadius(BendRadius bend, double measuredRadiusMm);

//! 画面に出す一文。「R = 21.63 mm AUTO」「R = 22.00 mm LOCK」。
[[nodiscard]] std::string DescribeBendRadiusJa(const BendRadius& bend, double percent);

} // namespace kachakacha::v2::fabrication
