#pragma once

//! 「面の解析」の道具(プロンプト surface_analysis)。
//!
//! 見るもの(ゼブラ・曲率・U/V 線・曲率コーム・境目・ずれ)を選ぶと、選んだ面(無ければ
//! 全部の面)と、面を作る・面の編集の下見の面を塗り替える。棚を閉じても表示は残る
//! (面を作る下見の間も効く)。面が作り直されれば標本も取り直す(核の番号で覚える)。
//! 窓の中身を使うので窓の友達にしてある(V2SurfaceEditTool と同じ)。

#include "V2SurfaceAnalysisDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/SurfaceAnalysis.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SurfaceAnalysisData.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class V2MainWindow;

class V2SurfaceAnalysisTool final {
public:
    explicit V2SurfaceAnalysisTool(V2MainWindow& window);

    [[nodiscard]] V2SurfaceAnalysisDock* Dock() const noexcept { return dock_; }
    [[nodiscard]] bool Shown() const noexcept { return shown_; }
    [[nodiscard]] kachakacha::v2::app::SurfaceAnalysisMode Mode() const noexcept { return mode_; }
    //! この道具の命令(view.surface_analysis・view.analysis_zebra)か。
    [[nodiscard]] bool Handles(std::string_view commandId) const;
    void Run(std::string_view commandId);
    void SetMode(kachakacha::v2::app::SurfaceAnalysisMode mode);
    //! 棚を閉じる。塗った表示は残す(見るもの「なし」で消える)。
    void Close();
    //! 形・選択・下見が変わった。解析を出していれば取り直して描き直す。
    void Refresh();
    //! いまの対象の製作性の目安(いちばん平らに広げにくい面)。対象が無ければ空。
    [[nodiscard]] const std::string& DevelopabilityLabelJa() const noexcept { return developability_; }
    //! いま塗っている面の数(下見を含む)。試験から見る。
    [[nodiscard]] std::size_t TargetCount() const noexcept { return targetCount_; }
    //! 面を作るの下見の製作性の目安(解析を出していなくても測る)。下見が無ければ空。
    [[nodiscard]] std::string PreviewDevelopabilityJa();

private:
    struct Target {
        kachakacha::v2::base::EntityId entityId;   // Nil は下見
        kachakacha::v2::modeling::KernelShapeHandle handle;
        std::string nameJa;
        //! 面を作った線(入力線からのずれに使う)。線から作っていない面は空。
        std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> chains;
        //! 線を「通している」とみなす離れと、この作り方が許す離れ(mm)。
        double exactMm = 0.0;
        double limitMm = 0.0;
    };
    struct Lines {
        std::vector<V2Viewport::AnalysisLine> lines;
        std::string noteJa;
    };
    //! 核の番号ごとの標本。画面と分け合う(塗り直すたびに写さない)。
    struct Sampled {
        std::shared_ptr<const kachakacha::v2::modeling::SurfaceAnalysisData> data;
        std::shared_ptr<const std::vector<kachakacha::v2::modeling::MeshTriangle>> triangles;
    };
    [[nodiscard]] std::vector<Target> Targets() const;
    void AddChains(Target& target, const kachakacha::v2::modeling::GuideTable& table) const;
    [[nodiscard]] const Sampled* DataFor(const kachakacha::v2::modeling::KernelShapeHandle& handle,
        bool report = true);
    [[nodiscard]] std::vector<kachakacha::v2::modeling::KernelShapeHandle> PreviewHandles() const;
    [[nodiscard]] const Lines& LinesFor(const Target& target,
        const kachakacha::v2::modeling::SurfaceAnalysisData& data,
        const std::vector<kachakacha::v2::modeling::KernelShapeHandle>& neighbors);
    [[nodiscard]] Lines ContinuityLines(const Target& target,
        const std::vector<kachakacha::v2::modeling::KernelShapeHandle>& neighbors) const;
    [[nodiscard]] Lines DeviationLines(const Target& target) const;
    void RefreshDock(const std::vector<Target>& targets, const std::vector<std::string>& notes,
        const std::vector<const kachakacha::v2::modeling::SurfaceAnalysisData*>& data);

    V2MainWindow& window_;
    V2SurfaceAnalysisDock* dock_ = nullptr;
    bool shown_ = false;
    kachakacha::v2::app::SurfaceAnalysisMode mode_ = kachakacha::v2::app::SurfaceAnalysisMode::None;
    //! 核の番号ごとの標本と、(見るもの・番号・隣)ごとの重ねる線。使わなくなったら捨てる。
    std::map<std::uint64_t, Sampled> cache_;
    std::map<std::pair<std::uint64_t, std::string>, Lines> lineCache_;
    std::string developability_;
    std::size_t targetCount_ = 0;
};
