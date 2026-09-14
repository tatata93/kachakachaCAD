#pragma once

//! HO(日本型 1/80・16.5mm)の流線形前頭部。総合試験の元になる文書。
//!
//! オーナー指示 2026-09-14 §14〜22。ここで作るのは **作り方** である。
//! 出来上がった形を BRep で書き込むことは **しない**(§44 の禁止事項)。
//! 利用者が実際にやる順 ── 作業平面 → 断面ワイヤー → 案内線 → 面 ──
//! をそのまま文書に入れ、開いたときに核が作り直す。
//!
//! 寸法は **模型実寸 mm** で作る。実車寸法で作って最後に縮めない(§15)。
//! 幅 35.0mm、高さ 45.0mm、前頭部の奥行き 18.0mm を目安にする。
//! これは 1/80 でおよそ幅 2800mm、高さ 3600mm、奥行き 1440mm に当たる。
//!
//! 形は箱でも半円柱でも球の切片でもない(§17)。
//!   - 左右対称
//!   - 車体側面と屋根へ滑らかにつながる
//!   - 前面中央が少し前へ膨らむ
//!   - 肩で曲率が変わる
//!   - 裾を絞る
//!   - 前端から車体側へ断面の形が変わる
//! したがって二重曲率を含み、1枚では展開できない。板材近似を複数部材で試す意味がある。
//!
//! 既にある 1/87 の見本(`RailwayNoseSample`)は壊さない。別の生成器として足す。

#include "kachakacha/io/DocumentFile.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 日本型 HO。1/80、16.5mm ゲージ。
inline constexpr double kHoScaleDenominator = 80.0;
inline constexpr double kHoGaugeMm = 16.5;

//! 模型実寸(mm)。形を作るときの目安であって、固定値として式へ埋め込まない。
inline constexpr double kHoNoseWidthMm = 35.0;
inline constexpr double kHoNoseHeightMm = 45.0;
inline constexpr double kHoNoseDepthMm = 18.0;

//! 断面を置く前後位置(mm)。0 が前端、kHoNoseDepthMm が車体側。
[[nodiscard]] const std::vector<double>& HoNoseSectionStations();

//! 断面の名前(`NoseSection_X000` など)。位置から決まる。
[[nodiscard]] std::string HoNoseSectionName(double stationMm);
//! その断面の作業平面の名前(`WP_X000` など)。
[[nodiscard]] std::string HoNoseWorkPlaneName(double stationMm);

//! 前頭部の面の上の点。u=左右(-1..1)、station=前後(mm)。
//!
//! 幅・高さ・屋根の丸み・肩の張り・裾の絞り・中央の膨らみが、
//! すべて station で変わる。同じ断面の縮小コピーにはしない(§19)。
[[nodiscard]] geometry::Vector3 HoNosePoint(double u, double stationMm);

//! その位置の断面ワイヤー(左右対称、屋根から裾まで)。
[[nodiscard]] std::vector<geometry::CurveSegment> HoNoseSection(double stationMm);

//! 案内線。面を作るときに実際に使う(飾りにしない、§20)。
//! 裾のいちばん外(u = ±1)を前後に走る線。面の左右の縁そのもの。
//!
//! 案内付きロフトは「外形の線2本と断面」で面を作る。この2本がその外形である。
//! 断面だけで作ると、断面と断面の間で縁が痩せる。
//!
//! 屋根の中央や肩の線は置かない。**置いても面を作るのに使われないからである。**
//! 使われない線を「案内線」として置くのは飾りで、オーナー指示 §44 が禁じている。
[[nodiscard]] std::vector<geometry::CurveSegment> HoNoseSkirtGuide(bool left);

//! HO 総合試験の見本。作業平面・断面・案内線・面・押し出し試験・近似を含む。
[[nodiscard]] io::DocumentFile BuildRailwayNoseHoSampleDocument();
[[nodiscard]] base::Result<std::string> BuildRailwayNoseHoSampleArchive();

[[nodiscard]] std::string_view RailwayNoseHoSampleVersion() noexcept;

} // namespace kachakacha::v2::app
