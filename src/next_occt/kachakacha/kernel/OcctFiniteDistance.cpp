#include "kachakacha/kernel/OcctFiniteDistance.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctBoundaryDistance.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <TopoDS_Vertex.hxx>
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>

namespace kachakacha::v2::kernel::detail {
namespace {
std::atomic<unsigned> extraWorkers{0};
class WorkerPermits {
public:
    explicit WorkerPermits(std::size_t wanted) {
        const unsigned hardware=std::thread::hardware_concurrency();
        wanted=std::min<std::size_t>(wanted,hardware>1?std::min(2u,hardware-1):0u);
        for(std::size_t i=0;i<wanted;++i) {
            unsigned current=extraWorkers.load();
            while(current<2&&!extraWorkers.compare_exchange_weak(current,current+1)){}
            if(current>=2)break;
            ++count_;
        }
    }
    ~WorkerPermits(){extraWorkers.fetch_sub(count_);}
    unsigned Count()const{return count_;}
private:
    unsigned count_=0;
};
void Measure(const TopoDS_Shape& shape,std::span<const NativeDistanceProbe> points,
    double precision,std::atomic<std::size_t>& next,std::vector<double>& result) {
    BRepExtrema_DistShapeShape measure;measure.SetDeflection(precision);measure.LoadS2(shape);
    const NativeBoundaryDistance boundary(shape);
    for(std::size_t i=next.fetch_add(1);i<points.size();i=next.fetch_add(1)) {
        const auto& probe=points[i];
        if(probe.boundary)if(const auto exact=boundary.Coincident(probe.point,precision)) {
            result[i]=*exact;continue;
        }
        const TopoDS_Vertex vertex=BRepBuilderAPI_MakeVertex(probe.point);
        measure.LoadS1(vertex);measure.Perform();
        if(measure.IsDone()&&measure.NbSolution()>0)result[i]=measure.Value();
    }
}
}
std::vector<double> MeasureFinitePoints(const TopoDS_Shape& shape,
    std::span<const NativeDistanceProbe> points,double precision,std::size_t maxHelpers) {
    std::vector<double> result(points.size(),-1);
    if(points.empty())return result;
    WorkerPermits permits(points.size()>=32?maxHelpers:0);
    std::vector<TopoDS_Shape> copies;
    for(unsigned i=0;i<permits.Count();++i) {
        BRepBuilderAPI_Copy copy(shape,true,false);
        if(!copy.IsDone())break;
        copies.push_back(copy.Shape());
    }
    std::atomic<std::size_t> next{0};std::exception_ptr failure;std::mutex failureMutex;
    const auto run=[&](const TopoDS_Shape& source) {
        try {Measure(source,points,precision,next,result);}
        catch(...) {std::lock_guard lock(failureMutex);if(!failure)failure=std::current_exception();}
    };
    std::vector<std::jthread> workers;workers.reserve(copies.size());
    for(const auto& copy:copies)try {workers.emplace_back(run,copy);}
        catch(const std::system_error&){break;} // Available workers and caller finish every probe.
    run(shape);
    for(auto& worker:workers)worker.join();
    if(failure)std::rethrow_exception(failure);
    return result;
}
}
#endif
