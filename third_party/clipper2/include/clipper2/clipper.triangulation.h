/*******************************************************************************
* Author    :  Angus Johnson                                                   *
* Date      :  6 December 2025                                                 *
* Release   :  BETA RELEASE                                                    *
* Website   :  https://www.angusj.com                                          *
* Copyright :  Angus Johnson 2010-2025                                         *
* Purpose   :  Delaunay Triangulation                                          *
* License   :  https://www.boost.org/LICENSE_1_0.txt                           *
*******************************************************************************/

#ifndef CLIPPER_TRIANGULATION_H
#define CLIPPER_TRIANGULATION_H

#include <stack>
#include "clipper2/clipper.core.h"

namespace Clipper2Lib
{

  enum class TriangulateResult { success, fail, no_polygons, paths_intersect };

  // Triangulate - this function will not accept intesecting paths
  TriangulateResult Triangulate(const Paths64& pp, Paths64& solution, bool useDelaunay = true);
  TriangulateResult Triangulate(const PathsD& pp, int decPlaces, PathsD& solution, bool useDelaunay = true);

  // SketchyUp patch: Triangulate() refuses (TriangulateResult::fail) once its work
  // exceeds bounds proportional to the input vertex count. Tests can scale those
  // bounds on the calling thread; 1.0 is the production value. Returns the old scale.
  double SetTriangulateWorkBoundScale(double scale);

} // Clipper2Lib namespace
#endif  // CLIPPER_TRIANGULATION_H
