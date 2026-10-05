# SketchyUp's offline serial core target; upstream source remains unmodified.
add_library(sketchyup_manifold STATIC
  src/boolean3.cpp
  src/boolean_result.cpp
  src/constructors.cpp
  src/csg_tree.cpp
  src/edge_op.cpp
  src/face_op.cpp
  src/impl.cpp
  src/manifold.cpp
  src/minkowski.cpp
  src/polygon.cpp
  src/properties.cpp
  src/quickhull.cpp
  src/sdf.cpp
  src/smoothing.cpp
  src/sort.cpp
  src/subdivision.cpp
  src/tree2d.cpp
)
target_include_directories(sketchyup_manifold SYSTEM PUBLIC include)
target_compile_definitions(sketchyup_manifold PUBLIC MANIFOLD_PAR=-1)
target_compile_features(sketchyup_manifold PUBLIC cxx_std_17)
# Match upstream floating-point policy; do not enable fast-math.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sketchyup_manifold PRIVATE -ffp-contract=off -fexcess-precision=standard)
endif()
