#include "kachakacha/fabrication/BandApproximation.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kachakacha::v2::fabrication {
namespace {
double RailError(const SampledSurface& source,BandSplitAxis axis,
    const std::vector<double>& rails,double from,double to)
{
    double maximum=0;
    for(double rail:rails) {
        const auto a=source.EvaluateSplit(axis,rail,from),b=source.EvaluateSplit(axis,rail,to);
        if(!a.IsFinite() || !b.IsFinite())return std::numeric_limits<double>::infinity();
        const auto chord=b-a;
        for(double fraction:{.25,.5,.75}) {
            const auto point=source.EvaluateSplit(axis,rail,from+(to-from)*fraction);
            if(!point.IsFinite())return std::numeric_limits<double>::infinity();
            // Match the SAME parameter on both rails. Nearest chord distance
            // misses tangential drift and changes the correspondence inside a strip.
            maximum=std::max(maximum,(point-a-chord*fraction).Length());
        }
    }
    return maximum;
}
}
double RailSamplingDeviation(const SampledSurface& source,BandSplitAxis axis,
    const std::vector<double>& rails,const std::vector<double>& parameters)
{
    double maximum=0;
    for(std::size_t i=1;i<parameters.size();++i)
        maximum=std::max(maximum,RailError(source,axis,rails,parameters[i-1],parameters[i]));
    return maximum;
}
base::Result<std::vector<double>> AdaptiveRailParameters(const SampledSurface& source,
    BandSplitAxis axis,const std::vector<double>& railParameters,double toleranceMm)
{
    using Out=base::Result<std::vector<double>>;
    if(!source.Valid() || railParameters.size()<2 || !std::isfinite(toleranceMm) || toleranceMm<=0)
        return Out::Failure(base::MakeError(kBandBadOptions,"曲線標本の指定が正しくありません。",{}));
    std::vector<double> parameters;
    constexpr int baseSteps=96,maximumSteps=2048;
    for(int i=0;i<=baseSteps;++i)parameters.push_back(static_cast<double>(i)/baseSteps);
    std::size_t interval=0;
    while(interval+1<parameters.size()) {
        const double from=parameters[interval],to=parameters[interval+1];
        if(RailError(source,axis,railParameters,from,to)<=toleranceMm) { ++interval;continue; }
        if(parameters.size()>maximumSteps || to-from<1e-12)
            return Out::Failure(base::MakeError("FAB-B007","曲線の標本誤差を指定内にできません。",
                "許容誤差を見直すか、元の面の急な折れや退化を確認してください。"));
        parameters.insert(parameters.begin()+interval+1,(from+to)*.5);
    }
    return Out::Success(std::move(parameters));
}
}
