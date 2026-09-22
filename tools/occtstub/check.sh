#!/bin/sh
# 使い方: sh tools/occtstub/check.sh src/next_occt/kachakacha/kernel/OcctShellSplit.cpp ...
# OCCT の入っていない雲で、KACHACAD_V2_WITH_OCCT の枝を構文だけ確かめる(身代わりの .hxx を一時に作る)。
# 通っても PC のビルドが通るとは限らない(OCCT の API は確かめない)。落ちたら自分の誤りか、身代わりに
# 足りない宣言。身代わりへ足すときは本物の API(OCCT の .hxx)と同じ形にする。
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
STUB=$(mktemp -d)
trap 'rm -rf "$STUB"' EXIT
for h in Standard_Handle Standard_Failure gp_Pnt gp_Vec gp_Dir gp_Pnt2d gp_Pln TopAbs_ShapeEnum \
    TopAbs_State TopoDS TopoDS_Shape TopoDS_Face TopoDS_Edge TopoDS_Vertex TopoDS_Wire TopoDS_Solid \
    TopTools_IndexedMapOfShape TopTools_ListOfShape TopExp TopExp_Explorer Geom_Surface Geom_Curve \
    BRep_Tool BRepAdaptor_Curve BRepBuilderAPI_MakeVertex BRepBuilderAPI_MakeFace \
    BRepExtrema_DistShapeShape BRepClass_FaceClassifier BRepTools GeomAPI_ProjectPointOnSurf \
    GProp_GProps BRepGProp BRepCheck_Analyzer BRepOffsetAPI_MakeThickSolid BRepPrimAPI_MakeHalfSpace \
    BRepAlgoAPI_Common Bnd_Box BRepBndLib BRepFilletAPI_MakeFillet BRepFilletAPI_MakeChamfer; do
    printf '#pragma once\n#include "%s/tools/occtstub/occt_all.h"\n' "$ROOT" > "$STUB/$h.hxx"
done
status=0
for f in "$@"; do
    if g++ -std=c++20 -fsyntax-only -Wall -Wextra -DKACHACAD_V2_WITH_OCCT -I"$STUB" \
        -I"$ROOT/src/next" -I"$ROOT/src/next_occt" "$f"; then
        echo "occt stub OK: $f"
    else
        echo "occt stub FAILED: $f"
        status=1
    fi
done
exit $status
