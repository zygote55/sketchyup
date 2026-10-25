// Generator for tests/fixtures/step/mirror-box.step (R098.d). It is not built by CMake: build it in
// sketchyup-ci-pinned:r097a-ifc, where the OpenCASCADE 7.9.3 headers and libraries are installed:
//
//   g++ -std=c++17 -I/usr/include/opencascade make_mirror_box.cpp -o make_mirror_box \
//     -lTKBRep -lTKPrim -lTKDESTEP -lTKXSBase -lTKG3d -lTKGeomBase -lTKG2d -lTKMath -lTKernel \
//     -lTKTopAlgo -lTKGeomAlgo -lTKBO -lTKDE
//   ./make_mirror_box mirror-box.step
//
// The corpus box (10 x 20 x 30 mm at the origin) is mirrored in the YZ plane (x -> -x). A mirror is a
// det -1 transform, which a STEP placement cannot express, so BRepBuilderAPI_Transform bakes it into the
// coordinates and reverses the faces; the basic STEP writer then writes the result. The file is written
// once and committed; the FILE_NAME time is the only part a second run changes.
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: make_mirror_box OUTPUT.step\n";
        return 2;
    }
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
    // SetMirror(gp_Ax2) mirrors in the plane through the origin spanned by the X and Y directions of
    // the system: with X along Y and the main direction along X, that plane is YZ.
    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0)));
    const TopoDS_Shape mirrored = BRepBuilderAPI_Transform(box, mirror, true).Shape();
    STEPControl_Writer writer;
    if (writer.Transfer(mirrored, STEPControl_AsIs) != IFSelect_RetDone) {
        std::cerr << "transfer failed\n";
        return 1;
    }
    if (writer.Write(argv[1]) != IFSelect_RetDone) {
        std::cerr << "write failed\n";
        return 1;
    }
    std::cout << "wrote " << argv[1] << '\n';
    return 0;
}
