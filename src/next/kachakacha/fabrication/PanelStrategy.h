#pragma once

//! 部材の分け方(fabrication-contract.md §5.4、AT-FAB-005)。
//!
//! 同じ形から4通りの分け方を作る。どれを選んでも、
//! 「三角形へ全面分割したもの」を完成として出さない(§5.3)。
//! 出せないなら、なぜ出せないかを言う。
//!
//! ここは OCCT を使わない。入力は「候補となる面の並び」と「隣り合わせ」だけで、
//! 面そのものはカーネルが作ったものを指し示す番号で持つ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/CurvatureAnalysis.h"
#include "kachakacha/fabrication/FabricationSettings.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

//! 分け方を決めるための、面1枚ぶんの情報。
struct PanelCandidate {
    std::string panelId;
    PanelGeometryClass classification = PanelGeometryClass::Planar;
    //! 1枚のまま展開したときの最大偏差(mm)。平面なら0。
    double flattenDeviationMm = 0.0;
    //! 面積。少数分割のときに、大きい面を残すかどうかの判断に使う。
    double areaMm2 = 0.0;
    //! Gauss曲率が許容を超えた標本の割合。強い二重曲率かの判断に使う。
    double doubleCurvedRatio = 0.0;
};

//! 面と面の隣り合わせ。ここを折るか、切るか、離すかを決める。
struct PanelAdjacency {
    std::size_t firstIndex = 0;
    std::size_t secondIndex = 0;
    //! 共有している辺の長さ。長い辺ほど、つないだままにする値打ちがある。
    double sharedEdgeLengthMm = 0.0;
    //! 共有辺での折り角(ラジアン)。0なら平ら。
    double dihedralAngleRad = 0.0;
    //! 利用者が「ここで必ず分ける」と指定したか(手動役割 PanelBoundary)。
    bool forcedBoundary = false;
    //! 利用者が「ここはつないだままにする」と指定したか(手動役割 KeepTogether)。
    bool keepTogether = false;
};

//! 隣り合わせをどう扱うことにしたか。
enum class JointKind {
    Fold,        //!< つないだまま折る
    Relief,      //!< 途中まで切って曲げる
    Separate,    //!< 別部材にして、接着で合わせる
};

[[nodiscard]] std::string_view JointKindNameJa(JointKind value) noexcept;

struct PanelJoint {
    std::size_t firstIndex = 0;
    std::size_t secondIndex = 0;
    JointKind kind = JointKind::Fold;
    //! Separate のとき、貼り合わせる相手を表す番号。両側で同じ値になる。
    std::string matePairId;
};

//! 分けた結果の1つ。
struct PanelPiece {
    std::string pieceId;
    //! この部材に入る面の添字。並びは決定的(添字の昇順)。
    std::vector<std::size_t> panelIndices;
    double areaMm2 = 0.0;
    //! この部材の中で一番大きい展開偏差。
    double maximumDeviationMm = 0.0;
};

struct PanelPartition {
    FabricationStrategy strategy = FabricationStrategy::FewPieces;
    std::vector<PanelPiece> pieces;
    std::vector<PanelJoint> joints;
    //! 部材の数。事前表示と一致すること。
    [[nodiscard]] std::size_t PieceCount() const noexcept { return pieces.size(); }
    std::vector<base::Diagnostic> notes;
};

//! 4通りの分け方のうち1つを作る。
//!
//! 作れない場合は値を返さず、なぜ作れないかを言う。
//! 例: OnePiece を頼まれたが、面が隣り合っていない島に分かれている。
[[nodiscard]] base::Result<PanelPartition> BuildPanelPartition(
    const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, const FabricationSettings& settings,
    double targetMaxDeviationMm);

//! 4通りをまとめて作る。画面で見比べるため。
//! 作れない戦略は、その理由つきで結果に残す。
struct PanelPartitionComparison {
    struct Entry {
        FabricationStrategy strategy = FabricationStrategy::OnePiece;
        bool available = false;
        PanelPartition partition;
        std::vector<base::Diagnostic> refusal;
    };
    std::vector<Entry> entries;
};

[[nodiscard]] PanelPartitionComparison CompareAllStrategies(
    const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, const FabricationSettings& settings,
    double targetMaxDeviationMm);

} // namespace kachakacha::v2::fabrication
