#include "kachakacha/kernel/OcctShapeTransfer.h"
#include "kachakacha/io/Zip.h"
#include <cstdint>
#include <sstream>
#include <string_view>
#include <exception>
#include <utility>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BinTools.hxx>
#include <BRepTools.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <Standard_Failure.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Shape.hxx>
#endif

namespace kachakacha::v2::kernel {
namespace {
constexpr std::string_view magic="kachakacha-shape-transfer-v1\n";
constexpr std::size_t headerSize=magic.size()+12;
template<class T> base::Result<T> Refused(std::string reason={}) {
    return base::Result<T>::Failure(base::MakeError("KER-X001","計算済み形状の転送内容を確認できませんでした。",
        "元の文書は変更していません。"+(reason.empty()
            ?std::string("文書を開き直して形状を再計算してください。"):std::move(reason))));
}
void AppendInteger(std::string& bytes,std::uint64_t value,int size) {
    for(int i=0;i<size;++i)bytes.push_back(static_cast<char>((value>>(i*8))&255));
}
std::uint64_t Integer(std::string_view bytes,std::size_t at,int size) {
    std::uint64_t value=0;
    for(int i=0;i<size;++i)value|=std::uint64_t(static_cast<unsigned char>(bytes[at+i]))<<(i*8);
    return value;
}
#ifdef KACHACAD_V2_WITH_OCCT
bool NeedsText(const TopoDS_Shape& shape) {
    // BinTools reconstructs offset bases with the default continuity check;
    // BRep text deliberately accepts legacy C0 offset bases. Keep the established
    // lossless text path for all offset representations, including their pcurves.
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
        const auto face=TopoDS::Face(f.Current());
        if(BRepAdaptor_Surface(face).GetType()>GeomAbs_BSplineSurface)return true;
        for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next())
            if(BRepAdaptor_Curve2d(TopoDS::Edge(e.Current()),face).GetType()>GeomAbs_BSplineCurve)return true;
    }
    for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());
        if(BRep_Tool::Degenerated(edge))continue;
        double first,last;
        if(BRep_Tool::Curve(edge,first,last).IsNull()||BRepAdaptor_Curve(edge).GetType()>GeomAbs_BSplineCurve)return true;
    }
    return false;
}
#endif
}
base::Result<std::string> CaptureTransferShape(modeling::KernelShapeHandle handle) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape shape;
        if(!LookupShape(handle,shape)||shape.IsNull())return Refused<std::string>("転送する形状が見つかりません。");
        if(!BRepCheck_Analyzer(shape).IsValid())return Refused<std::string>("形状の境界・接続の検査に失敗しました。");
        std::ostringstream output(std::ios::out|std::ios::binary);
        const bool text=NeedsText(shape);
        output.put(text?'T':'B');
        if(text)BRepTools::Write(shape,output,false,false,TopTools_FormatVersion_CURRENT);
        else BinTools::Write(shape,output,false,false,BinTools_FormatVersion_CURRENT);
        if(!output)return Refused<std::string>("形状の転送データを書き込めませんでした。");
        const auto payload=output.str();std::string bytes(magic);bytes.reserve(headerSize+payload.size());
        AppendInteger(bytes,payload.size(),8);AppendInteger(bytes,io::Crc32(payload),4);bytes+=payload;
        return base::Result<std::string>::Success(std::move(bytes));
    }catch(const Standard_Failure& failure){
        return Refused<std::string>("形状の転送データを作成できませんでした: "+std::string(failure.what()));
    }catch(const std::exception& failure){
        return Refused<std::string>("形状の転送データを作成できませんでした: "+std::string(failure.what()));
    }catch(...){return Refused<std::string>("形状の転送データを作成中に問題が発生しました。");}
#else
    (void)handle;return Refused<std::string>();
#endif
}
base::Result<modeling::KernelShapeHandle> RestoreTransferShape(const std::string& bytes) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        const std::string_view view(bytes);
        if(view.size()<=headerSize||!view.starts_with(magic)
            ||Integer(view,magic.size(),8)!=view.size()-headerSize)return Refused<modeling::KernelShapeHandle>();
        const auto payload=view.substr(headerSize);
        if(Integer(view,magic.size()+8,4)!=io::Crc32(payload))return Refused<modeling::KernelShapeHandle>();
        if(payload.size()<2||(payload[0]!='B'&&payload[0]!='T'))return Refused<modeling::KernelShapeHandle>();
        std::istringstream input(std::string(payload.substr(1)),std::ios::in|std::ios::binary);TopoDS_Shape shape;
        if(payload[0]=='B')BinTools::Read(shape,input);
        else {BRep_Builder builder;BRepTools::Read(shape,input,builder);input>>std::ws;}
        if(!input||input.peek()!=std::char_traits<char>::eof()||shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())
            return Refused<modeling::KernelShapeHandle>();
        return base::Result<modeling::KernelShapeHandle>::Success(StoreShape(shape));
    }catch(...){return Refused<modeling::KernelShapeHandle>();}
#else
    (void)bytes;return Refused<modeling::KernelShapeHandle>();
#endif
}
}
