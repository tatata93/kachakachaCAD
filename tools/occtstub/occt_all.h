#pragma once
// 雲で OCCT の枝を構文だけ確かめるための最小の身代わり(本物の API に合わせて書く)。
// **OCCT の API そのものは確かめない。**確かめるのは自分の C++ の誤り(Result の使い方・型の取り違え・
// include 忘れの一部)だけ。本当の確かめは PC のビルド。使う型・関数が増えたらここへ足す。
#include <exception>
typedef double Standard_Real;
typedef int Standard_Integer;
typedef bool Standard_Boolean;
typedef const char* Standard_CString;
#define Standard_True true
#define Standard_False false
namespace occ { template <class T> class handle { public: T* operator->() const { return p; } T& operator*() const { return *p; } bool IsNull() const { return p == nullptr; } T* p = nullptr; }; }
class Standard_Failure { public: Standard_CString GetMessageString() const { return ""; } };
class gp_Vec { public: gp_Vec() {} gp_Vec(double, double, double) {} };
class gp_Pnt { public: gp_Pnt() {} gp_Pnt(double, double, double) {} double X() const { return 0; } double Y() const { return 0; } double Z() const { return 0; } };
class gp_Dir { public: gp_Dir(double, double, double) {} explicit gp_Dir(const gp_Vec&) {} };
class gp_Pnt2d { public: gp_Pnt2d(double, double) {} double X() const { return 0; } double Y() const { return 0; } };
class gp_Pln { public: gp_Pln(const gp_Pnt&, const gp_Dir&) {} };
enum TopAbs_ShapeEnum { TopAbs_COMPOUND, TopAbs_COMPSOLID, TopAbs_SOLID, TopAbs_SHELL, TopAbs_FACE, TopAbs_WIRE, TopAbs_EDGE, TopAbs_VERTEX, TopAbs_SHAPE };
enum TopAbs_State { TopAbs_IN, TopAbs_OUT, TopAbs_ON, TopAbs_UNKNOWN };
class TopoDS_Shape { public: bool IsNull() const { return true; } };
class TopoDS_Face : public TopoDS_Shape {};
class TopoDS_Edge : public TopoDS_Shape {};
class TopoDS_Vertex : public TopoDS_Shape {};
class TopoDS_Wire : public TopoDS_Shape {};
class TopoDS_Solid : public TopoDS_Shape {};
class TopoDS { public: static const TopoDS_Face& Face(const TopoDS_Shape&); static const TopoDS_Edge& Edge(const TopoDS_Shape&); };
class TopTools_IndexedMapOfShape { public: int Extent() const { return 0; } const TopoDS_Shape& operator()(int) const; };
class TopTools_ListOfShape { public: void Append(const TopoDS_Shape&) {} };
class TopExp { public: static void MapShapes(const TopoDS_Shape&, TopAbs_ShapeEnum, TopTools_IndexedMapOfShape&) {} };
class TopExp_Explorer { public: TopExp_Explorer(const TopoDS_Shape&, TopAbs_ShapeEnum) {} bool More() const { return false; } void Next() {} const TopoDS_Shape& Current() const; };
class Geom_Surface { public: gp_Pnt Value(double, double) const { return {}; } };
class Geom_Curve {};
class BRep_Tool { public: static bool Degenerated(const TopoDS_Edge&) { return false; } static const occ::handle<Geom_Surface>& Surface(const TopoDS_Face&); };
class BRepAdaptor_Curve { public: explicit BRepAdaptor_Curve(const TopoDS_Edge&) {} double FirstParameter() const { return 0; } double LastParameter() const { return 0; } gp_Pnt Value(double) const { return {}; } };
class BRepBuilderAPI_MakeVertex { public: explicit BRepBuilderAPI_MakeVertex(const gp_Pnt&) {} operator TopoDS_Vertex() { return {}; } };
class BRepBuilderAPI_MakeFace { public: explicit BRepBuilderAPI_MakeFace(const gp_Pln&) {} BRepBuilderAPI_MakeFace(const gp_Pln&, double, double, double, double) {} bool IsDone() const { return true; } const TopoDS_Face& Face() const; };
class BRepExtrema_DistShapeShape { public: BRepExtrema_DistShapeShape(const TopoDS_Shape&, const TopoDS_Shape&) {} bool IsDone() const { return true; } int NbSolution() const { return 0; } double Value() const { return 0; } gp_Pnt PointOnShape2(int) const { return {}; } };
class BRepClass_FaceClassifier { public: BRepClass_FaceClassifier(const TopoDS_Face&, const gp_Pnt2d&, double) {} TopAbs_State State() const { return TopAbs_IN; } };
class BRepTools { public: static void UVBounds(const TopoDS_Face&, double&, double&, double&, double&) {} };
class GeomAPI_ProjectPointOnSurf { public: GeomAPI_ProjectPointOnSurf(const gp_Pnt&, const occ::handle<Geom_Surface>&) {} bool IsDone() const { return true; } int NbPoints() const { return 0; } void LowerDistanceParameters(double&, double&) const {} };
class GProp_GProps { public: double Mass() const { return 0; } };
class BRepGProp { public: static void VolumeProperties(const TopoDS_Shape&, GProp_GProps&) {} };
class BRepCheck_Analyzer { public: explicit BRepCheck_Analyzer(const TopoDS_Shape&) {} bool IsValid() const { return true; } };
class BRepOffsetAPI_MakeThickSolid { public: BRepOffsetAPI_MakeThickSolid() {} void MakeThickSolidByJoin(const TopoDS_Shape&, const TopTools_ListOfShape&, double, double) {} void MakeThickSolidBySimple(const TopoDS_Shape&, double) {} bool IsDone() const { return true; } const TopoDS_Shape& Shape(); };
class BRepPrimAPI_MakeHalfSpace { public: BRepPrimAPI_MakeHalfSpace(const TopoDS_Face&, const gp_Pnt&) {} bool IsDone() const { return true; } const TopoDS_Solid& Solid() const; };
class BRepAlgoAPI_Common { public: BRepAlgoAPI_Common(const TopoDS_Shape&, const TopoDS_Shape&) {} void Build() {} bool IsDone() const { return true; } const TopoDS_Shape& Shape(); };
class Bnd_Box { public: bool IsVoid() const { return false; } void Get(double&, double&, double&, double&, double&, double&) const {} };
class BRepBndLib { public: static void Add(const TopoDS_Shape&, Bnd_Box&) {} };
class BRepFilletAPI_MakeFillet { public: explicit BRepFilletAPI_MakeFillet(const TopoDS_Shape&) {} void Add(double, const TopoDS_Edge&) {} void Build() {} bool IsDone() const { return true; } const TopoDS_Shape& Shape(); };
class BRepFilletAPI_MakeChamfer { public: explicit BRepFilletAPI_MakeChamfer(const TopoDS_Shape&) {} void Add(double, const TopoDS_Edge&) {} void Build() {} bool IsDone() const { return true; } const TopoDS_Shape& Shape(); };
