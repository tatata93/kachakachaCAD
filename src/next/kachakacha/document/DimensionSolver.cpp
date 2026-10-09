#include "kachakacha/document/Dimension.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
namespace kachakacha::v2::document {
using namespace geometry;
namespace {
struct Item {
    std::size_t feature, segment;
    base::EntityId entity;
    base::SegmentId id;
    CurveSegment original;
    bool active=false;
    int offset=-1;
};
struct Join { std::size_t a,b; int endA,endB; };
struct Problem {
    DocumentSnapshot source;
    std::vector<Item> items;
    std::vector<Join> joins;
    std::vector<double> initial;
    ReferenceDimension changed;
};
std::vector<Item> Items(const DocumentSnapshot& snapshot)
{
    std::vector<Item> items;
    for(std::size_t fi=0;fi<snapshot.features.size();++fi) {
        const auto& feature=snapshot.features[fi];
        const auto* wire=std::get_if<domain::CreateWireDefinition>(&feature.definition);
        if(!wire || !feature.enabled)continue;
        const auto entity=std::find_if(snapshot.entities.begin(),snapshot.entities.end(),
            [&](const auto& e){return e.createdBy==feature.id && e.kind==domain::EntityKind::Wire;});
        if(entity==snapshot.entities.end())continue;
        for(std::size_t si=0;si<wire->segments.size() && si<wire->segmentIds.size();++si)
            items.push_back({fi,si,entity->id,wire->segmentIds[si],wire->segments[si]});
    }
    return items;
}
void Append(std::vector<double>& values,Vector3 p) { values.insert(values.end(),{p.x,p.y,p.z}); }
Vector3 Point(const std::vector<double>& values,int at) {return {values[at],values[at+1],values[at+2]};}
bool Contains(const ReferenceDimension& dim,const Item& item)
{
    return std::any_of(dim.segments.begin(),dim.segments.end(),[&](const auto& ref){return ref.entityId==item.entity && ref.segmentId==item.id;});
}
Problem MakeProblem(const DocumentSnapshot& snapshot,const ReferenceDimension& changed)
{
    Problem p; p.source=snapshot;p.changed=changed;p.items=Items(snapshot);
    const double tolerance=std::min(0.001,snapshot.settings.tolerance.interactiveJoinMm);
    for(std::size_t a=0;a<p.items.size();++a) {
        p.items[a].active=Contains(changed,p.items[a]);
        if(p.items[a].original.Kind()==CurveKind::Circle)continue;
        for(std::size_t b=a+1;b<p.items.size();++b) {
            if(p.items[b].original.Kind()==CurveKind::Circle)continue;
            for(int ea=0;ea<2;++ea)for(int eb=0;eb<2;++eb)
                if(Distance(p.items[a].original.Evaluate(ea),p.items[b].original.Evaluate(eb))<=tolerance)
                    p.joins.push_back({a,b,ea,eb});
        }
    }
    bool more=true;
    while(more) {
        more=false;
        for(const auto& join:p.joins) if(p.items[join.a].active!=p.items[join.b].active) {
            p.items[join.a].active=p.items[join.b].active=true;more=true;
        }
        for(const auto& dim:snapshot.referenceDimensions)if(dim.driving) {
            const bool any=std::any_of(p.items.begin(),p.items.end(),[&](const auto& item){return item.active && Contains(dim,item);});
            if(any)for(auto& item:p.items)if(!item.active && Contains(dim,item)){item.active=true;more=true;}
        }
    }
    for(auto& item:p.items) if(item.active) {
        const auto& c=item.original;
        if(c.Kind()!=CurveKind::Line && c.Kind()!=CurveKind::Circle && c.Kind()!=CurveKind::CircularArc)continue;
        item.offset=static_cast<int>(p.initial.size());
        if(c.Kind()==CurveKind::Line) {Append(p.initial,c.StartPoint());Append(p.initial,c.EndPoint());}
        else {Append(p.initial,c.Center());p.initial.push_back(c.Radius());}
    }
    return p;
}
std::optional<DocumentSnapshot> ApplyValues(const Problem& p,const std::vector<double>& values)
{
    auto snapshot=p.source;
    for(const auto& item:p.items) if(item.offset>=0) {
        const auto& c=item.original;const int at=item.offset;
        auto result=c.Kind()==CurveKind::Line ? CurveSegment::MakeLine(Point(values,at),Point(values,at+3))
            : c.Kind()==CurveKind::Circle ? CurveSegment::MakeCircle(Point(values,at),c.Normal(),c.ReferenceDirection(),values[at+3])
            : CurveSegment::MakeCircularArc(Point(values,at),c.Normal(),c.ReferenceDirection(),values[at+3],c.StartAngleRad(),c.SweepAngleRad());
        if(!result.HasValue())return {};
        std::get<domain::CreateWireDefinition>(snapshot.features[item.feature].definition).segments[item.segment]=result.Value();
    }
    return snapshot;
}
const CurveSegment& CurveAt(const DocumentSnapshot& snapshot,const Item& item)
{ return std::get<domain::CreateWireDefinition>(snapshot.features[item.feature].definition).segments[item.segment]; }
std::vector<double> Residuals(const Problem& p,const DocumentSnapshot& snapshot)
{
    std::vector<double> r;
    for(const auto& dim:snapshot.referenceDimensions)if(dim.driving) {
        const auto value=EvaluateDimension(snapshot,dim);
        r.push_back(value.HasValue() ? (value.Value().value-dim.recordedValue)*(dim.unit=="rad" ? 10 : 1) : 1e8);
    }
    for(const auto& join:p.joins)if(p.items[join.a].active) {
        const auto a=CurveAt(snapshot,p.items[join.a]).Evaluate(join.endA);
        const auto b=CurveAt(snapshot,p.items[join.b]).Evaluate(join.endB);
        Append(r,a-b);
    }
    // 既に水平・垂直の直線はその向きを維持。角度寸法の対象は角度拘束を優先する。
    for(const auto& item:p.items)if(item.active && item.original.Kind()==CurveKind::Line) {
        bool angular=false;
        for(const auto& dim:snapshot.referenceDimensions)
            if(dim.driving && dim.kind=="dim_angle" && Contains(dim,item))angular=true;
        if(angular)continue;
        const auto old=Normalized(item.original.EndPoint()-item.original.StartPoint());
        const auto& c=CurveAt(snapshot,item);
        if(std::abs(Dot(old,p.changed.dimensionU))>1-1e-9 || std::abs(Dot(old,p.changed.dimensionV))>1-1e-9)
            Append(r,Cross(c.EndPoint()-c.StartPoint(),old));
    }
    // 最初の対象の始点(円は中心)を基準点とする。角度では1本目を固定する。
    for(const auto& item:p.items)if(!p.changed.segments.empty() && item.entity==p.changed.segments.front().entityId
        && item.id==p.changed.segments.front().segmentId) {
        const auto& now=CurveAt(snapshot,item);
        if(p.changed.segments.size()==2 && p.changed.kind!="dim_angle") {
            const double t=p.changed.segments.front().startParameter;
            Append(r,now.Evaluate(t)-item.original.Evaluate(t));
        } else if(item.original.Kind()==CurveKind::Line) {
            Append(r,now.StartPoint()-item.original.StartPoint());
            if(p.changed.kind=="dim_angle")Append(r,now.EndPoint()-item.original.EndPoint());
        }else Append(r,now.Center()-item.original.Center());
        break;
    }
    return r;
}
double Error(const std::vector<double>& r)
{double error=0;for(double value:r)error+=value*value;return error;}
bool Linear(std::vector<std::vector<double>>& a,std::vector<double>& b)
{
    const auto n=b.size();
    for(std::size_t col=0;col<n;++col) {
        std::size_t pivot=col;
        for(std::size_t row=col+1;row<n;++row)if(std::abs(a[row][col])>std::abs(a[pivot][col]))pivot=row;
        if(std::abs(a[pivot][col])<1e-18)return false;
        std::swap(a[pivot],a[col]);std::swap(b[pivot],b[col]);
        for(std::size_t row=col+1;row<n;++row) {
            const double f=a[row][col]/a[col][col];
            for(std::size_t k=col;k<n;++k)a[row][k]-=f*a[col][k];
            b[row]-=f*b[col];
        }
    }
    for(std::size_t row=n;row-->0;) {
        for(std::size_t k=row+1;k<n;++k)b[row]-=a[row][k]*b[k];
        b[row]/=a[row][row];
    }
    return true;
}
std::vector<double> Step(const Problem& p,const std::vector<double>& x,const std::vector<double>& r,double damping)
{
    const auto n=x.size();
    std::vector<std::vector<double>> j(n,std::vector<double>(r.size()));
    for(std::size_t col=0;col<n;++col) {
        auto trial=x;const double h=1e-6*std::max(1.0,std::abs(x[col]));trial[col]+=h;
        const auto snapshot=ApplyValues(p,trial);if(!snapshot)return {};
        const auto next=Residuals(p,*snapshot);
        for(std::size_t row=0;row<r.size();++row)j[col][row]=(next[row]-r[row])/h;
    }
    std::vector<std::vector<double>> matrix(n,std::vector<double>(n));std::vector<double> b(n);
    for(std::size_t a=0;a<n;++a) {
        for(std::size_t row=0;row<r.size();++row)b[a]-=j[a][row]*r[row];
        for(std::size_t c=0;c<n;++c)for(std::size_t row=0;row<r.size();++row)matrix[a][c]+=j[a][row]*j[c][row];
        matrix[a][a]+=damping;
    }
    return Linear(matrix,b) ? b : std::vector<double>{};
}
}
base::Result<bool> SolveDimensions(DocumentSnapshot& snapshot,const ReferenceDimension& changed)
{
    using Out=base::Result<bool>;
    const auto evaluated=EvaluateDimension(snapshot,changed);
    if(!evaluated.HasValue())return Out::Failure(evaluated.Diagnostics());
    const auto p=MakeProblem(snapshot,changed);
    if(p.initial.empty() || p.initial.size()>240)
        return Out::Failure(base::MakeError("DIM-009","この接続範囲は寸法拘束で編集できません。","対応する作図線は直線・円・円弧、同時に解く変数は240までです。参照寸法は使用できます。"));
    auto x=p.initial;double damping=1e-5;
    for(int iteration=0;iteration<80;++iteration) {
        const auto current=ApplyValues(p,x);if(!current)break;
        const auto r=Residuals(p,*current);const double error=Error(r);
        if(error<1e-12) {
            snapshot=*current;
            std::set<std::size_t> changedFeatures;
            for(const auto& item:p.items)if(item.offset>=0)changedFeatures.insert(item.feature);
            for(const auto index:changedFeatures) {
                ++snapshot.features[index].revision;
                for(auto& entity:snapshot.entities)if(entity.createdBy==snapshot.features[index].id)++entity.revision;
            }
            return Out::Success(true);
        }
        const auto delta=Step(p,x,r,damping);if(delta.empty())break;
        auto next=x;for(std::size_t i=0;i<x.size();++i)next[i]+=delta[i];
        const auto candidate=ApplyValues(p,next);
        if(candidate && Error(Residuals(p,*candidate))<error){x=std::move(next);damping=std::max(1e-10,damping*0.3);}
        else damping*=10;
        if(damping>1e10)break;
    }
    return Out::Failure(base::MakeError("DIM-010","寸法と接続を同時に満たせません。変更は取り消しました。",
        "既存寸法との矛盾、つぶれる線、固定された曲線端を確認してください。"));
}
} // namespace kachakacha::v2::document
