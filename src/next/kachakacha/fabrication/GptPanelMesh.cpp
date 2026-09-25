#include "kachakacha/fabrication/GptApproximation.h"
#include <algorithm>
#include <cmath>
namespace kachakacha::v2::fabrication {
modeling::ShapeMesh GptPanelMesh(const GptApproxPanel& panel,double progress)
{
    modeling::ShapeMesh mesh;
    std::vector<std::vector<geometry::Point2>> loops{panel.pattern.outline};
    loops.insert(loops.end(),panel.pattern.openings.begin(),panel.pattern.openings.end());
    std::vector<double> breaks=panel.lengths;
    for (const auto& loop:loops) {
        mesh.edges.push_back(GptPanelLoop(panel,loop,progress));
        for (const auto& p:loop) { breaks.push_back(p.u); }
    }
    std::sort(breaks.begin(),breaks.end());
    breaks.erase(std::unique(breaks.begin(),breaks.end(),[](double a,double b){return std::abs(a-b)<1e-9;}),breaks.end());
    struct Crossing { double mid,left,right; };
    for (std::size_t i=1;i<breaks.size();++i) {
        const double left=breaks[i-1],right=breaks[i],mid=(left+right)*.5;
        std::vector<Crossing> crossing;
        for (const auto& loop:loops) {
            if (loop.empty()) { continue; }
            auto a=loop.back();
            for (const auto& b:loop) {
                if ((a.u<mid && b.u>mid)||(b.u<mid && a.u>mid)) {
                    const auto y=[&](double x){return a.v+(b.v-a.v)*(x-a.u)/(b.u-a.u);};
                    crossing.push_back({y(mid),y(left),y(right)});
                }
                a=b;
            }
        }
        std::sort(crossing.begin(),crossing.end(),[](auto a,auto b){return a.mid<b.mid;});
        for (std::size_t at=1;at<crossing.size();at+=2) {
            const std::array<geometry::Vector3,4> points{panel.Point({left,crossing[at-1].left},progress),
                panel.Point({right,crossing[at-1].right},progress),panel.Point({right,crossing[at].right},progress),
                panel.Point({left,crossing[at].left},progress)};
            for (int last:{2,3}) {
                modeling::MeshTriangle triangle;
                triangle.points={points[0],points[last-1],points[last]};
                mesh.triangles.push_back(triangle);
            }
        }
    }
    modeling::RefreshNormals(mesh); modeling::RefreshBounds(mesh); mesh.faceCount=1;
    return mesh;
}
}
