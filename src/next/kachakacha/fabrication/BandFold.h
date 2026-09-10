#pragma once

//! 帯近似モデルの「曲げ具合」── V1 の可動折り線(合意10)をそのまま移す。
//!
//! オーナー想定の (e)(f): 近似面は曲げ状態を **可変** できる形式で持ち、
//! 任意の曲げ具合でワイヤ・面・展開図を出せること。
//!
//! progress = 0 は平面に置いた展開状態(型紙そのもの)、1 は近似完成形。
//! 中間は「三角形は剛体のまま、折り目の二面角だけを progress 倍」する等長の曲げ。
//! **辺長・帯幅・素線長はどの瞬間も保存される。** 形と寸法が正しいことを、
//! 部材どうしを繋ぐことより優先する(曲がった折り線では隣の帯との間に隙間ができる)。
//!
//! 折り線ごと(creaseProgress)にも、帯ごと(bandProgress)にも独立に動かせる。
//! 「選んだ部材だけが曲がる」は V1 でのオーナー指示である。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/BandApproximation.h"

#include <vector>

namespace kachakacha::v2::fabrication {

//! 展開状態(progress=0)から折り曲げた近似形状(progress=1)までの中間形状。
//! 0 は厳密な展開平面配置、1 は world と厳密一致。
[[nodiscard]] std::vector<std::vector<Vector3>> FoldBandMesh(const BandMesh& mesh,
    double progress);

//! 完成形(world)での各内部レールの平均折り角(符号付きラジアン、0 = 平ら)。
//! サイズは CreaseCount()。可動折り線の表示(度)と進行度⇄角度の換算に使う。
[[nodiscard]] std::vector<double> MeasureCreaseAngles(const BandMesh& mesh);

//! 帯1つぶんの剛体変換。world の点を折り状態の位置へ写す。
struct BandTransform {
    Vector3 rotationRowX{1.0, 0.0, 0.0};
    Vector3 rotationRowY{0.0, 1.0, 0.0};
    Vector3 rotationRowZ{0.0, 0.0, 1.0};
    Vector3 translation{};

    [[nodiscard]] Vector3 RotateVector(const Vector3& value) const
    {
        return {Dot(rotationRowX, value), Dot(rotationRowY, value), Dot(rotationRowZ, value)};
    }
    [[nodiscard]] Vector3 Apply(const Vector3& point) const
    {
        return RotateVector(point) + translation;
    }
};

//! 折り線ごとの進行度(1 = 完成形の折り角、0 = 平ら)で、各帯へ掛かる剛体変換。
//! サイズ = 帯数。帯そのものは一切変形しない。
//! creaseProgress.size() == CreaseCount() でなければ断る。
[[nodiscard]] base::Result<std::vector<BandTransform>> BuildRigidBandTransforms(
    const BandMesh& mesh, const std::vector<double>& creaseProgress);

//! 曲げ確認用の帯ごとの姿勢(V1 のオーナー指示の見え方)。
//!
//! bandProgress = 0 で各帯の展開形を帯の外向き法線方向へ liftMm だけ離した位置に置き、
//! 1 で「折り線ごとの進行度 creaseProgress どおりの剛体折り状態」へ一致する。
//! 返り値は帯ごとの(下レール, 上レール)= 2×帯数 本の点列。
//! bandProgress のサイズが帯数に満たない分は最後の値を使う。空なら全帯 1。
[[nodiscard]] base::Result<std::vector<std::vector<Vector3>>> BuildBandFoldRails(
    const BandMesh& mesh, const std::vector<double>& creaseProgress,
    const std::vector<double>& bandProgress, double liftMm);

//! 3D 点を近似メッシュ(world)の最近三角形へ対応付け、同じ位相の状態
//! state(FoldBandMesh の戻り値など)上の対応点を返す。接続部分の変形に使う。
struct BandMappedPoint {
    int band = 0;
    Vector3 point{};
    double distanceMm = 0.0; //!< メッシュからの距離(メッシュ上の点なら ~0)
};

[[nodiscard]] base::Result<BandMappedPoint> MapPointToBandState(const BandMesh& mesh,
    const std::vector<std::vector<Vector3>>& state, const Vector3& point);

} // namespace kachakacha::v2::fabrication
