#include "kachakacha/view/SurfaceRaster.h"
#include <algorithm>
#include <cmath>
namespace kachakacha::v2::view {
namespace {
std::uint32_t Blend(std::uint32_t src,std::uint32_t dst,double opacity) {
    const double a=((src>>24)&255)/255.0*opacity,b=((dst>>24)&255)/255.0*(1-a),out=a+b;
    if(out<=0)return 0;
    const auto channel=[&](int shift){return std::uint32_t(std::clamp((((src>>shift)&255)*a+((dst>>shift)&255)*b)/out,0.0,255.0))<<shift;};
    return (std::uint32_t(out*255+.5)<<24)|channel(16)|channel(8)|channel(0);
}
std::uint32_t Sample(const RasterImage& image,double x,double y) {
    x=std::clamp(x-.5,0.0,double(image.width-1));y=std::clamp(y-.5,0.0,double(image.height-1));
    const int ix=int(x),iy=int(y),jx=std::min(ix+1,image.width-1),jy=std::min(iy+1,image.height-1);
    const double fx=x-ix,fy=y-iy;
    const auto at=[&](int a,int b){return image.pixels[std::size_t(b)*image.width+a];};
    const auto channel=[&](int shift){return std::uint32_t(((at(ix,iy)>>shift)&255)*(1-fx)*(1-fy)
        +((at(jx,iy)>>shift)&255)*fx*(1-fy)+((at(ix,jy)>>shift)&255)*(1-fx)*fy+((at(jx,jy)>>shift)&255)*fx*fy+.5)<<shift;};
    return channel(24)|channel(16)|channel(8)|channel(0);
}
}
void SurfaceRaster::DrawImage(const modeling::ImageTriangle& triangle,const geometry::ScreenMapping& mapping,const RasterImage& image) {
    if(image.width<=0||image.height<=0||image.pixels.size()!=std::size_t(image.width)*image.height)return;
    std::array<Vertex,3> vertices{};const auto& m=mapping.matrix;
    for(std::size_t i=0;i<3;++i){const auto& p=triangle.mesh.points[i];const auto screen=mapping.Project(p);if(!screen)return;
        const double w=m[12]*p.x+m[13]*p.y+m[14]*p.z+m[15];if(std::abs(w)<1e-12)return;
        vertices[i]={screen->x,screen->y,(m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11])/w,1/w,{},triangle.pixels[i]};
        if(!std::isfinite(vertices[i].x+vertices[i].y+vertices[i].z)||!triangle.pixels[i].IsFinite())return;}
    PaintImage(vertices,image);
}
void SurfaceRaster::PaintImage(const std::array<Vertex,3>& v,const RasterImage& image) {
    if(width_==0||height_==0)return;
    const double determinant=(v[1].y-v[2].y)*(v[0].x-v[2].x)+(v[2].x-v[1].x)*(v[0].y-v[2].y);
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-10)return;
    const int x0=int(std::clamp(std::floor(std::min({v[0].x,v[1].x,v[2].x})),0.0,double(width_-1)));
    const int x1=int(std::clamp(std::ceil(std::max({v[0].x,v[1].x,v[2].x})),0.0,double(width_-1)));
    const int y0=int(std::clamp(std::floor(std::min({v[0].y,v[1].y,v[2].y})),0.0,double(height_-1)));
    const int y1=int(std::clamp(std::ceil(std::max({v[0].y,v[1].y,v[2].y})),0.0,double(height_-1)));
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        const double a=((v[1].y-v[2].y)*(x+.5-v[2].x)+(v[2].x-v[1].x)*(y+.5-v[2].y))/determinant;
        const double b=((v[2].y-v[0].y)*(x+.5-v[2].x)+(v[0].x-v[2].x)*(y+.5-v[2].y))/determinant,c=1-a-b;
        const auto inside=[&](double weight,const Vertex& from,const Vertex& to){
            if(weight>1e-10)return true;if(weight < -1e-10)return false;
            const double dx=(to.x-from.x)*(determinant>0?1:-1),dy=(to.y-from.y)*(determinant>0?1:-1);
            return dy<0||(dy==0&&dx>0);
        };
        if(!inside(a,v[1],v[2])||!inside(b,v[2],v[0])||!inside(c,v[0],v[1]))continue;
        const auto index=std::size_t(y)*width_+x;const double depth=a*v[0].z+b*v[1].z+c*v[2].z;
        if(depth>depths_[index]+1e-7)continue;
        const double w=a*v[0].inverseW+b*v[1].inverseW+c*v[2].inverseW;if(std::abs(w)<1e-12)continue;
        const auto uv=(v[0].pixel*(a*v[0].inverseW)+v[1].pixel*(b*v[1].inverseW)+v[2].pixel*(c*v[2].inverseW))*(1/w);
        if(uv.x<0||uv.y<0||uv.x>=image.width||uv.y>=image.height)continue;
        const auto color=Sample(image,uv.x,uv.y);if((color>>24)==0||image.opacity<=0)continue;
        pixels_[index]=Blend(color,pixels_[index],image.opacity);depths_[index]=depth;
    }
}
}
