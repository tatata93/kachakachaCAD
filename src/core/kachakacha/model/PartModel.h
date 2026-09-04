#pragma once

#include "kachakacha/geometry/Vector2.h"
#include "kachakacha/model/Plate.h"
#include "kachakacha/model/Wire.h"

#include <string>
#include <vector>

namespace kachakacha::model {

//! 部材近似モデル(docs/surface-unfolding-spec.md, ADR 0019)。
//! 完成品(の板材)を「1部材=基本1軸曲げ」の帯へ近似し直すためのレシピと計算結果。

enum class PartSplitAxis {
    U, //!< uパラメータ方向に境界を置く(帯はvに沿って並ぶ)
    V, //!< vパラメータ方向に境界を置く
};

struct PartApproximationOptions {
    PartSplitAxis splitAxis = PartSplitAxis::V;
    //! true なら公差から自動分割。false なら manualBoundaryParameters を境界に使う。
    bool automaticBoundaries = true;
    //! 1軸曲げ近似からの許容偏差(mm)。自動分割の基準。
    double maximumDeviationMillimeters = 0.25;
    //! 自動分割の上限部材数。超える場合は公差より部材数を優先する。
    int maximumPartCount = 12;
    //! 部材の最小実幅(mm)。これ未満の細い帯は作らない。
    double minimumPartWidthMillimeters = 4.0;
    //! 手動境界(分割軸パラメータ、0..1、開区間)。automaticBoundaries=false のとき使用。
    std::vector<double> manualBoundaryParameters;
};

//! 近似の入力(面、または板材の厚み中央面)をUVサンプリングで共通に扱う軽量ビュー。
//! 指した元オブジェクトより長生きさせないこと(参照のみ保持する)。
class PartSource {
public:
    PartSource(const Surface& surface) : surface_(&surface) {}
    PartSource(const Plate& plate) : plate_(&plate) {}

    [[nodiscard]] geometry::Vector3 Evaluate(double u, double v) const
    {
        return plate_ != nullptr ? plate_->Evaluate(u, v, 0.5) : surface_->Evaluate(u, v);
    }

private:
    const Surface* surface_ = nullptr;
    const Plate* plate_ = nullptr;
};

struct ApproximatedPart {
    int number = 1;                    //!< 型紙・一覧に出す部材番号(1始まり)
    double minimumParameter = 0.0;     //!< 分割軸方向の範囲(板材ローカル 0..1)
    double maximumParameter = 1.0;
    double widthMillimeters = 0.0;     //!< 分割軸方向の実幅(平均)
    double estimatedDeviationMillimeters = 0.0; //!< 1軸曲げ近似からの推定偏差(最大)
    PlateDevelopability classification = PlateDevelopability::Developable;
};

struct PartApproximationResult {
    std::vector<ApproximatedPart> parts;
    double maximumDeviationMillimeters = 0.0; //!< 全部材の推定偏差の最大
    bool reachedRequestedTolerance = true;    //!< 全部材が許容偏差以下か
};

//! 入力(面、または板材の厚み中央面)を部材へ近似分割する。
[[nodiscard]] PartApproximationResult ApproximatePlateParts(
    const PartSource& source,
    const PartApproximationOptions& options);

//! 部材境界(分割軸パラメータ=一定の線)を入力面上のポリラインとして作る。
[[nodiscard]] Wire BuildPartBoundaryWire(
    const PartSource& source,
    PartSplitAxis splitAxis,
    double parameter,
    int samples = 64);

//! 部材近似モデルの帯メッシュ(近似の実形状)とその厳密展開。
//! rows = レール本数(部材数+1)。各レールは columns 個の点列。
//! world は角ばった近似形状(レール間は直線=ルールド)、developed はその等長展開。
//! 展開は三角形単位で辺長を厳密に保存する(標準的なペーパークラフト展開)。
struct PartMeshDevelopment {
    int rows = 0;
    int columns = 0;
    //! [row][column] の順。row0 が範囲の始まり側。
    std::vector<std::vector<geometry::Vector3>> world;
    std::vector<std::vector<geometry::Vector2>> developed;
    //! 内部レール(折り線)の山谷。サイズ rows-2。+1=山, -1=谷, 0=ほぼ平ら。
    std::vector<int> creaseDirections;
};

//! 帯範囲 [t0,t1] を boundaries(内部境界のパラメータ列)で区切ってメッシュ展開する。
[[nodiscard]] PartMeshDevelopment DevelopPartMesh(
    const PartSource& source,
    PartSplitAxis splitAxis,
    const std::vector<double>& railParameters,
    int columns = 96);

//! 展開状態(progress=0)から折り曲げた近似形状(progress=1)までの中間形状を返す。
//! スライダー確認用。返り値は world と同じ [row][column]。
[[nodiscard]] std::vector<std::vector<geometry::Vector3>> BuildFoldPreview(
    const PartMeshDevelopment& mesh,
    double progress);

//! 展開平面配置(progress=0)から任意の目標状態 target(progress=1)への補間。
//! BuildFoldPreview は target=mesh.world の特殊形。
[[nodiscard]] std::vector<std::vector<geometry::Vector3>> BuildFoldPreviewToState(
    const PartMeshDevelopment& mesh,
    const std::vector<std::vector<geometry::Vector3>>& target,
    double progress);

//! 完成形(world)での各内部レールの平均折り角(符号付きラジアン、0=平ら)。
//! サイズは rows-2。可動折り線のUI表示(度)と進行度⇄角度の換算に使う。
[[nodiscard]] std::vector<double> MeasureCreaseAngles(const PartMeshDevelopment& mesh);

//! 帯(部材)1つぶんの剛体変換。world の点を折り状態の位置へ写す。
//! 回転は行ベクトル3本で持つ(適用は各行との内積+平行移動)。
struct PartBandTransform {
    geometry::Vector3 rotationRowX{1.0, 0.0, 0.0};
    geometry::Vector3 rotationRowY{0.0, 1.0, 0.0};
    geometry::Vector3 rotationRowZ{0.0, 0.0, 1.0};
    geometry::Vector3 translation{0.0, 0.0, 0.0};

    [[nodiscard]] geometry::Vector3 RotateVector(const geometry::Vector3& value) const
    {
        return {Dot(rotationRowX, value), Dot(rotationRowY, value), Dot(rotationRowZ, value)};
    }
    [[nodiscard]] geometry::Vector3 Apply(const geometry::Vector3& point) const
    {
        return RotateVector(point) + translation;
    }
};

//! 折り線ごとの進行度(1=完成形の折り角、0=平ら、1超・負は外挿)で、
//! 各帯へ掛かる剛体変換を返す(可動折り線、合意10)。サイズ=帯数(rows-1)。
//! 帯そのものは一切変形しない。曲がった折り線では隣の帯との間に隙間ができるが、
//! 形状と寸法は常に正確(部材どうしを繋ぐことより正しさを優先する)。
//! creaseProgress.size() == rows-2。全要素1なら全て恒等変換(=world)。
[[nodiscard]] std::vector<PartBandTransform> BuildRigidBandTransforms(
    const PartMeshDevelopment& mesh,
    const std::vector<double>& creaseProgress);

//! 曲げ確認用の帯ごとアニメーション状態(オーナー指示の見え方)。
//! assemblyProgress=0 で各帯の展開形(型紙と同じ平面形)を帯の外向き法線方向へ
//! liftDistanceMillimeters だけ離した位置に置き、1 で「折り線ごとの進行度
//! creaseProgress どおりの剛体折り状態(近似モデルの位置)」へ一致する。
//! 中間は帯ごとに平面形→折り形を補間する(型紙ビューと同じ見え方。帯は互いに独立)。
//! 返り値は帯ごとの(下レール, 上レール)ペア = 2×帯数 本の点列。
[[nodiscard]] std::vector<std::vector<geometry::Vector3>> BuildBandFoldAnimationRails(
    const PartMeshDevelopment& mesh,
    const std::vector<double>& creaseProgress,
    double assemblyProgress,
    double liftDistanceMillimeters);

//! 3D点を近似メッシュ(world)の最近三角形へ対応付け、同じ位相の状態
//! state(BuildFoldPreview の戻り値など)上の対応点を返す。
//! band は点が属した帯(0始まり = 部材番号-1)、distanceMillimeters は
//! メッシュからの距離(メッシュ上の点なら ~0)。
struct PartMeshMappedPoint {
    int band = 0;
    geometry::Vector3 point;
    double distanceMillimeters = 0.0;
};

[[nodiscard]] PartMeshMappedPoint MapPointToPartMeshState(
    const PartMeshDevelopment& mesh,
    const std::vector<std::vector<geometry::Vector3>>& state,
    const geometry::Vector3& point);

} // namespace kachakacha::model
