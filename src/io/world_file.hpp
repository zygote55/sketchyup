#pragma once
// R107.b2 (ADR 0181, R107.b2 design "Inputs"): a six-line world file beside a PNG or JPEG. Qt-free: the
// text is read by the caller, and the names of the files that may hold it are listed here.

#include <string>
#include <string_view>
#include <vector>

namespace sketchy {

// The six numbers of a world file, in the standard order A, D, B, E, C, F (A and E are the pixel
// sizes east and north, D and B the rotation terms, and C and F the centre of the upper-left pixel).
struct WorldFile {
    double a{}, d{}, b{}, e{}, c{}, f{};
};

// The top-left corner of a north-up image in grid metres, and its pixel sizes (positive, metres).
struct NorthUpPlacement {
    double easting{}, northing{};
    double pixelWidth{}, pixelHeight{};
};

// Reads a world file's text: at most 1 KiB, exactly six non-empty lines, each a finite decimal number.
// Throws ImageRefusal: world-file for the shape and the numbers, world-rotated when B or D is not zero,
// and world-flipped when A is not positive or E is not negative.
WorldFile readWorldFile(std::string_view text);

// The corner and pixel sizes of a checked world file. The corner is half a pixel up and left of the
// centre of the upper-left pixel: (C - A/2, F - E/2).
NorthUpPlacement northUpPlacement(const WorldFile &file);

// The suffixes of the world files that may sit beside an image, in the order they are tried (R107.b2
// design): "pngw", "pgw", "wld" for a PNG, and "jpgw", "jgw", "wld" for a JPEG. The image name's
// extension decides the list; any other extension has none.
std::vector<std::string> worldFileSuffixes(std::string_view imageFileName);

} // namespace sketchy
