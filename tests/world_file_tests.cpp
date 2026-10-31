// R107.b2.1 (ADR 0181, R107.b2 design "Inputs"): the world file reader. Six lines in the order A, D, B, E,
// C, F; exactly six non-empty lines of at most 1 KiB; B and D zero (world-rotated); A positive and E
// negative (world-flipped); the corner is half a pixel up and left of the centre of pixel (0, 0); and the
// file names tried beside an image, case-insensitively.
#include "io/image_refusal.hpp"
#include "io/world_file.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

const std::string fixtures = std::string(SOURCE_DIR) + "/tests/fixtures/reference/";

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const std::string &message) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::cerr << message << ": " << actual << " != " << expected << '\n';
        throw std::runtime_error(message);
    }
}

// The refusal code of a world file, or "accepted".
std::string outcome(const std::string &text) {
    try {
        readWorldFile(text);
        return "accepted";
    } catch (const ImageRefusal &refusal) {
        return refusal.code();
    }
}

void testSixLinesPlaceTheCorner() {
    // C and F are the centre of the upper-left pixel: the corner is (C - A/2, F - E/2).
    const auto placement = northUpPlacement(readWorldFile("2.0\n0.0\n0.0\n-2.0\n499801.0\n5709999.0\n"));
    near(placement.easting, 499800.0, 0, "easting is half a pixel west of C");
    near(placement.northing, 5710000.0, 0, "northing is half a pixel north of F");
    near(placement.pixelWidth, 2.0, 0, "pixel width is A");
    near(placement.pixelHeight, 2.0, 0, "pixel height is -E");
}

void testFractionalPixelsAreExact() {
    // A 0.3 m pixel: the corner is C - 0.15 m. The arithmetic is in double precision.
    const auto placement = northUpPlacement(readWorldFile("0.3\n0\n0\n-0.3\n100.15\n200.15\n"));
    near(placement.easting, 100.0, 1e-12, "corner from a fractional pixel");
    near(placement.northing, 200.3, 1e-12, "north corner from a fractional pixel");
}

void testLineEndingsAndSpacing() {
    check(outcome("2.0\r\n0\r\n0\r\n-2.0\r\n1\r\n1\r\n") == "accepted", "CRLF line endings are read");
    check(outcome("2.0\n0\n0\n-2.0\n1\n1") == "accepted", "a last line with no newline is read");
    check(outcome(" 2.0 \n\t0\n0\n-2.0\n1\n1\n") == "accepted", "spaces around a number are read");
    check(outcome("+2.0\n0\n0\n-2.0\n1\n1\n") == "accepted", "a plus sign is a decimal sign");
    check(outcome("2e0\n0\n0\n-2e0\n1\n1\n") == "accepted", "an exponent is a decimal number");
}

void testShapeRefusals() {
    check(outcome("") == "world-file", "an empty file holds no numbers");
    check(outcome("2\n0\n0\n-2\n1\n") == "world-file", "five lines are refused");
    check(outcome("2\n0\n0\n-2\n1\n1\n1\n") == "world-file", "seven lines are refused");
    check(outcome("2\n0\n\n-2\n1\n1\n") == "world-file", "a blank line is refused");
    check(outcome("2,0\n0\n0\n-2\n1\n1\n") == "world-file", "a comma is not a decimal point");
    check(outcome("nan\n0\n0\n-2\n1\n1\n") == "world-file", "a NaN is refused");
    check(outcome("inf\n0\n0\n-2\n1\n1\n") == "world-file", "an infinity is refused");
    check(outcome("2.0 x\n0\n0\n-2\n1\n1\n") == "world-file", "trailing text is refused");
    check(outcome("2\n0\n0\n-2\n1\n1 1\n") == "world-file", "two numbers on one line are refused");
    check(outcome("2\n0\n0\n-2\n1\n1\n\n") == "world-file", "a blank line after the six is refused");
    const std::string long_(1025, '1');
    check(outcome(long_) == "world-file", "a file over 1 KiB is refused");
}

void testRotationAndFlipRefusals() {
    check(outcome("2\n0\n0.5\n-2\n1\n1\n") == "world-rotated", "a non-zero B is a rotation");
    check(outcome("2\n0.5\n0\n-2\n1\n1\n") == "world-rotated", "a non-zero D is a shear");
    check(outcome("-2\n0\n0\n-2\n1\n1\n") == "world-flipped", "a negative A flips the image");
    check(outcome("0\n0\n0\n-2\n1\n1\n") == "world-flipped", "a zero A is refused");
    check(outcome("2\n0\n0\n2\n1\n1\n") == "world-flipped", "a positive E flips the image");
    check(outcome("2\n0\n0\n0\n1\n1\n") == "world-flipped", "a zero E is refused");
    check(outcome("2\n0.5\n0.5\n2\n1\n1\n") == "world-rotated", "a rotation is named before a flip");
}

void testSuffixesFollowTheImage() {
    const auto png = worldFileSuffixes("photo.png");
    check(png == std::vector<std::string>{"pngw", "pgw", "wld"}, "a PNG's world files are pngw, pgw, wld");
    const auto upper = worldFileSuffixes("PHOTO.PNG");
    check(upper == png, "the extension is matched without regard to case");
    const auto jpeg = worldFileSuffixes("photo.jpeg");
    check(jpeg == std::vector<std::string>{"jpgw", "jgw", "wld"}, "a JPEG's world files are jpgw, jgw, wld");
    const auto jpg = worldFileSuffixes("photo.jpg");
    check(jpg == jpeg, "jpg and jpeg take the same world files");
    check(worldFileSuffixes("photo.tif").empty(), "a GeoTIFF has no world file");
    check(worldFileSuffixes("photo").empty(), "a name with no extension has no world file");
}

std::string readText(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "the fixture is committed: " + path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void testCommittedFixtures() {
    const auto north = northUpPlacement(readWorldFile(readText(fixtures + "north-up.pgw")));
    near(north.easting, 499800.0, 0, "the committed north-up world file places its corner");
    near(north.northing, 5710000.0, 0, "the committed north-up world file's northing");
    near(north.pixelWidth, 2.0, 0, "the committed north-up world file's pixel width");
    check(outcome(readText(fixtures + "refuse-rotated.pngw")) == "world-rotated",
          "the committed rotated world file is refused by name");
}

} // namespace

int main() {
    struct Case {
        const char *name;
        void (*run)();
    };
    const Case cases[] = {
        {"six lines place the corner (R107.b2)", testSixLinesPlaceTheCorner},
        {"fractional pixels are exact", testFractionalPixelsAreExact},
        {"line endings and spacing", testLineEndingsAndSpacing},
        {"shape refusals (world-file)", testShapeRefusals},
        {"rotation and flip refusals", testRotationAndFlipRefusals},
        {"world files follow the image's name", testSuffixesFollowTheImage},
        {"committed north-up world file", testCommittedFixtures},
    };
    int failed = 0;
    for (const auto &item : cases) {
        try {
            item.run();
            std::cout << "ok: " << item.name << '\n';
        } catch (const std::exception &error) {
            ++failed;
            std::cout << "FAILED: " << item.name << ": " << error.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}
