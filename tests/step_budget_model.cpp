// Generator for the R098.d tessellation and export budget model (ADR 0172 section 6). It is not built by
// CMake: build it in sketchyup-ci-pinned:r097a-ifc, where the OpenCASCADE 7.9.3 headers and libraries are
// installed (see tests/step_budgets.py for the measurements that use it):
//
//   g++ -std=c++17 -I/usr/include/opencascade tests/step_budget_model.cpp -o step_budget_model \
//     -lTKBRep -lTKPrim -lTKDESTEP -lTKXSBase -lTKG3d -lTKGeomBase -lTKG2d -lTKMath -lTKernel \
//     -lTKTopAlgo -lTKGeomAlgo -lTKBO -lTKDE
//   ./step_budget_model OUTPUT.step SPHERES RADIUS_MM
//
// It writes SPHERES solid spheres of RADIUS_MM, each 2.5 m from the last along X, as one STEP compound
// (AP214, millimetres). A sphere is a curved face, so the import tessellates it into many triangles from
// a few entities: that keeps a 1,000,000-triangle model well under the 128 MiB input limit.
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pnt.hxx>

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: step_budget_model OUTPUT.step SPHERES RADIUS_MM\n";
        return 2;
    }
    const int count = std::atoi(argv[2]);
    const double radius = std::atof(argv[3]);
    if (count < 1 || radius <= 0.0) {
        std::cerr << "SPHERES and RADIUS_MM must be positive\n";
        return 2;
    }
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (int i = 0; i < count; ++i) {
        const gp_Pnt centre(2500.0 * i, 0.0, 0.0);
        builder.Add(compound, BRepPrimAPI_MakeSphere(centre, radius).Shape());
    }
    STEPControl_Writer writer;
    if (writer.Transfer(compound, STEPControl_AsIs) != IFSelect_RetDone) {
        std::cerr << "transfer failed\n";
        return 1;
    }
    if (writer.Write(argv[1]) != IFSelect_RetDone) {
        std::cerr << "write failed\n";
        return 1;
    }
    std::cout << "wrote " << argv[1] << " with " << count << " spheres\n";
    return 0;
}
