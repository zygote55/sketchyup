#pragma once
// Terrain enumerations and declarations for widely included headers (core/body.hpp,
// app/viewport.hpp, app/terrain_tool.hpp), apart from the records and operations that define
// them (core/terrain_records.hpp, core/terrain_sculpt.hpp, core/terrain_stamp.hpp,
// core/terrain_display.hpp, automation/terrain_commands.hpp), so that a change there does not
// rebuild everything that includes those headers (build fan-out, ADR 0170).
#include <memory>
namespace sketchy {
// The role of a terrain source (core/terrain_records.hpp).
enum class TerrainSourceRole { Contour, Breakline, Boundary, Hole, Points };
// Sculpt and smooth falloff (core/terrain_sculpt.hpp).
enum class TerrainFalloff { Smooth, Linear, Constant };
// Building-pad grading (core/terrain_stamp.hpp).
enum class TerrainStampMode { Daylight, Width };
struct TerrainRecord;
struct TerrainEdit;
using TerrainEditPtr = std::shared_ptr<const TerrainEdit>;
class TerrainSourceState;
struct TerrainSculptPreview;
struct TerrainContours;
struct TerrainTileSurface;
struct TerrainTileSurfaces;
class TerrainContourCache;
class TerrainSurfaceCache;
struct TerrainCommandPreview;
} // namespace sketchy
