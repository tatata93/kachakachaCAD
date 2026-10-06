#include "kachakacha/geometry/PolylineCorners.h"

#include "kachakacha/geometry/WireEdit.h"

#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/CornerCurves.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kNoCorner = "GEO-E021";

[[nodiscard]] bool Touches(const CurveSegment& a, const CurveSegment& b, double toleranceMm)
{
    return (a.EndPoint() - b.StartPoint()).Length() <= toleranceMm;
}

//! 頂点番号 → 角の番号(index 番目の辺と index+1 番目の辺の角)。角でなければ -1。
[[nodiscard]] int CornerOfVertex(int vertexIndex, std::size_t segmentCount, bool closed)
{
    const int count = static_cast<int>(segmentCount);
    if (closed) {
        // 頂点 0 は最後の辺と最初の辺の角 = 角番号 count-1。頂点 k(k>=1)は角番号 k-1。
        if (vertexIndex < 0 || vertexIndex >= count) {
            return -1;
        }
        return vertexIndex == 0 ? count - 1 : vertexIndex - 1;
    }
    // 開いた並び: 頂点 1 〜 count-1 だけが角。両端は角でない。
    if (vertexIndex < 1 || vertexIndex >= count) {
        return -1;
    }
    return vertexIndex - 1;
}

// Compute each corner from the original edges, then combine both end cuts.
// This permits exactly meeting cuts without ever constructing a zero-length curve.
Result<std::vector<CurveSegment>> ProcessOrderedCorners(
    const std::vector<CurveSegment>& work, CornerStyle style, double sizeMm,
    double toleranceMm, bool closed, int only, bool linesOnly)
{
    using Out=Result<std::vector<CurveSegment>>;
    const auto count=work.size();
    std::vector<double> start(count,0),end(count,1);
    std::vector<std::optional<CurveSegment>> bridges(count);
    int processed=0;
    for(std::size_t i=0;i<(closed?count:count-1);++i){
        const auto j=(i+1)%count;const auto& first=work[i];const auto& second=work[j];
        if((only>=0&&static_cast<int>(i)!=only)||!Touches(first,second,toleranceMm))continue;
        if(linesOnly&&(first.Kind()!=CurveKind::Line||second.Kind()!=CurveKind::Line))continue;
        const auto a=first.FirstDerivative(1),b=second.FirstDerivative(0);
        if(a.Length()>0&&b.Length()>0&&Dot(a,b)/(a.Length()*b.Length())>1-1e-8)continue;
        CornerOptions options;options.firstKeepSide=1;options.secondKeepSide=2;
        const auto cut=style==CornerStyle::Fillet
            ? FilletLines(first,second,sizeMm,options,toleranceMm)
            : ChamferLines(first,second,sizeMm,options,toleranceMm);
        if(!cut.HasValue())return Out::Failure(cut.Diagnostics());
        const auto& c=cut.Value();
        end[i]=c.first ? first.ClosestPoint(c.corner.StartPoint()).parameter : 0;
        start[j]=c.second ? second.ClosestPoint(c.corner.EndPoint()).parameter : 1;
        bridges[i]=c.corner;++processed;
    }
    if(!processed)return Out::Failure(MakeError(kNoCorner,"加工する角がありません。","滑らかな接続や指定外の辺はそのまま残します。"));
    std::vector<CurveSegment> result;
    for(std::size_t i=0;i<count;++i){
        const auto& curve=work[i];const double a=start[i],b=end[i];
        const double removedOverlap=curve.ArcLength(std::min(a,b),std::max(a,b),1e-9);
        if(a>b&&removedOverlap>1e-8)return Out::Failure(MakeError("GEO-E004","隣り合う角の加工範囲が重なります。","半径・切戻し量を小さくしてください。"));
        if(b>a&&removedOverlap>1e-8){
            CurveSegment piece=curve;
            if(b<1){
                const auto split=piece.Split(b);if(!split.HasValue())return Out::Failure(split.Diagnostics());
                piece=*split.Value().first;
            }
            if(a>0){
                const auto split=piece.Split(a/b);if(!split.HasValue())return Out::Failure(split.Diagnostics());
                piece=*split.Value().second;
            }
            result.push_back(piece);
        }
        if(bridges[i])result.push_back(*bridges[i]);
    }
    return Out::Success(std::move(result));
}

} // namespace

Result<std::vector<CurveSegment>> ProcessPolylineCorners(
    const std::vector<CurveSegment>& segments, CornerStyle style, double sizeMm,
    double toleranceMm, int vertexIndex)
{
    using Out = Result<std::vector<CurveSegment>>;
    if (segments.size()<2) return Out::Failure(MakeError(kNoCorner,"落とせる角がありません。",{}));
    const bool closed=segments.size()>=3 && Touches(segments.back(),segments.front(),toleranceMm);
    const int only=vertexIndex>=0 ? CornerOfVertex(vertexIndex,segments.size(),closed) : -2;
    if(only==-1)return Out::Failure(MakeError(kNoCorner,"指定した頂点は角ではありません。",{}));
    return ProcessOrderedCorners(segments,style,sizeMm,toleranceMm,closed,only,true);
}

Result<std::vector<CurveSegment>> ProcessSelectedCorners(
    const std::vector<CurveSegment>& segments, CornerStyle style, double sizeMm, double toleranceMm)
{
    using Out=Result<std::vector<CurveSegment>>;
    if(segments.size()<2)return Out::Failure(MakeError(kNoCorner,"加工する辺が不足しています。","各角の両側の辺を選んでください。"));
    std::vector<std::vector<CurveSegment>> groups;
    std::vector<bool> used(segments.size());
    for(std::size_t seed=0;seed<segments.size();++seed){
        if(used[seed])continue;
        std::vector<std::size_t> members{seed};used[seed]=true;
        for(std::size_t k=0;k<members.size();++k)for(std::size_t j=0;j<segments.size();++j){
            if(used[j])continue;
            const auto& a=segments[members[k]];const auto& b=segments[j];
            if((a.StartPoint()-b.StartPoint()).Length()<=toleranceMm||(a.StartPoint()-b.EndPoint()).Length()<=toleranceMm
                ||(a.EndPoint()-b.StartPoint()).Length()<=toleranceMm||(a.EndPoint()-b.EndPoint()).Length()<=toleranceMm){used[j]=true;members.push_back(j);}
        }
        groups.emplace_back();for(auto i:members)groups.back().push_back(segments[i]);
    }
    if(groups.size()>1){
        std::vector<CurveSegment> result;
        for(const auto& group:groups){
            if(group.size()<2)return Out::Failure(MakeError(kNoCorner,"単独の辺が含まれています。","一括加工する各角の両側の辺を選んでください。"));
            const auto made=ProcessSelectedCorners(group,style,sizeMm,toleranceMm);
            if(!made.HasValue())return Out::Failure(made.Diagnostics());
            result.insert(result.end(),made.Value().begin(),made.Value().end());
        }
        return Out::Success(std::move(result));
    }
    std::vector<ChainInput> inputs;
    for(std::size_t i=0;i<segments.size();++i){
        std::array<std::uint8_t,16> bytes{};
        for(int b=0;b<8;++b)bytes[15-b]=static_cast<std::uint8_t>((i+1)>>(b*8));
        inputs.push_back({{},base::SegmentId(base::Uuid(bytes)),segments[i]});
    }
    GeometryTolerance tolerance;tolerance.interactiveJoinMm=toleranceMm;
    const auto chain=AnalyzeChain(inputs,tolerance);
    if(!chain.HasValue())return Out::Failure(chain.Diagnostics());
    std::vector<CurveSegment> work;
    for(const auto& ref:chain.Value().order.segments){
        const auto found=std::find_if(inputs.begin(),inputs.end(),[&](const auto& v){return v.segmentId==ref.segmentId;});
        if(ref.reversed){const auto reversed=ReverseCurve(found->segment);if(!reversed.HasValue())return Out::Failure(reversed.Diagnostics());work.push_back(reversed.Value());}
        else work.push_back(found->segment);
    }
    return ProcessOrderedCorners(work,style,sizeMm,toleranceMm,chain.Value().order.closed,-2,false);
}

} // namespace kachakacha::v2::geometry
