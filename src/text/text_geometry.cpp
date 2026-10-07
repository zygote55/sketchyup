#include "text/text_geometry.hpp"
#include <QCryptographicHash>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QPainterPath>
#include <QRawFont>
#include <QTextLayout>
#include <algorithm>
#include <clipper2/clipper.h>
#include <functional>
#include <map>
#include <tuple>
namespace sketchy {
namespace {
using namespace Clipper2Lib;
constexpr double integerScale = 1e8, fontPixels = 1000;
constexpr size_t glyphLimit = 1024, pointLimit = 32768, regionLimit = 1024;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QString fingerprint(const QRawFont &font, size_t &fontBytes) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const char *tag : {"head", "name", "cmap", "maxp", "hhea", "hmtx", "OS/2", "glyf", "loca",
                            "CFF ", "CFF2", "GSUB", "GPOS", "fvar", "gvar"}) {
        const auto bytes = font.fontTable(tag);
        fontBytes += size_t(bytes.size());
        require(fontBytes <= 128 * 1024 * 1024, "Font fingerprint work exceeds 128 MiB");
        hash.addData(QByteArray(tag, 4));
        hash.addData(QByteArray::number(bytes.size()));
        hash.addData(QByteArray(1, '\0'));
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
struct Flatten {
    double scale, error;
    QPointF position;
    size_t &total;
    Paths64 paths;
    Path64 path;
    QPointF current;
    QPointF world(QPointF p) const {
        return {(p.x() + position.x()) * scale, -(p.y() + position.y()) * scale};
    }
    void append(QPointF p) {
        checkPoint({p.x(), p.y(), 0});
        Point64 next{std::llround(p.x() * integerScale), std::llround(p.y() * integerScale)};
        if (!path.empty() && path.back() == next)
            return;
        require(++total <= pointLimit, "Text outlines exceed 32768 sampled points");
        path.push_back(next);
    }
    void close() {
        if (path.empty())
            return;
        if (path.size() > 1 && path.front() == path.back())
            path.pop_back();
        path = TrimCollinear(path);
        require(path.size() >= 3, "Glyph contour collapses below model precision");
        paths.push_back(std::move(path));
        path.clear();
    }
    static double distance(QPointF p, QPointF a, QPointF b) {
        const auto d = b - a;
        const auto n = d.x() * d.x() + d.y() * d.y();
        if (n == 0)
            return std::hypot(p.x() - a.x(), p.y() - a.y());
        const auto t = std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / n, 0., 1.);
        const auto q = a + d * t;
        return std::hypot(p.x() - q.x(), p.y() - q.y());
    }
    void cubic(QPointF a, QPointF b, QPointF c, QPointF d, unsigned depth = 0) {
        if (std::max(distance(b, a, d), distance(c, a, d)) <= error) {
            append(d);
            return;
        }
        require(depth < 20, "Text curve subdivision exceeds depth bound");
        const auto ab = (a + b) * .5, bc = (b + c) * .5, cd = (c + d) * .5;
        const auto abc = (ab + bc) * .5, bcd = (bc + cd) * .5, mid = (abc + bcd) * .5;
        cubic(a, ab, abc, mid, depth + 1);
        cubic(mid, bcd, cd, d, depth + 1);
    }
    Paths64 run(const QPainterPath &outline) {
        for (int i = 0; i < outline.elementCount(); ++i) {
            const auto e = outline.elementAt(i);
            const auto p = world({e.x, e.y});
            if (e.type == QPainterPath::MoveToElement) {
                close();
                append(p);
                current = p;
            } else if (e.type == QPainterPath::LineToElement) {
                append(p);
                current = p;
            } else if (e.type == QPainterPath::CurveToElement) {
                require(i + 2 < outline.elementCount(), "Malformed glyph curve");
                const auto c = outline.elementAt(++i), d = outline.elementAt(++i);
                const auto end = world({d.x, d.y});
                cubic(current, p, world({c.x, c.y}), end);
                current = end;
            } else
                throw std::runtime_error("Malformed glyph outline");
        }
        close();
        return paths;
    }
};
} // namespace
TextGeometry shapeTextGeometry(const TextGeometrySettings &s) {
    require(qobject_cast<QGuiApplication *>(QCoreApplication::instance()),
            "Text shaping requires a GUI application runtime");
    require(!s.text.isEmpty() && s.text.isValidUtf16() && s.text.toUtf8().size() <= 4096 &&
                !s.text.contains(QChar::Null),
            "Text must contain at most 4096 UTF-8 bytes");
    require(!s.family.isEmpty() && s.family.toUtf8().size() <= 256 &&
                s.style.toUtf8().size() <= 256,
            "Invalid font family or style");
    require(std::isfinite(s.height) && s.height >= .001 && s.height <= 1000,
            "Text em height must be in [0.001,1000] metres");
    require(std::isfinite(s.depth) && s.depth >= 0 && s.depth <= 1000 &&
                (s.depth == 0 || s.depth >= 1e-6),
            "Text depth must be zero or in [0.000001,1000] metres");
    require(std::isfinite(s.lineSpacing) && s.lineSpacing >= .5 && s.lineSpacing <= 10,
            "Text line spacing must be in [0.5,10] em");
    for (const auto c : s.text)
        require(c.unicode() >= 32 || c == '\n' || c == '\t', "Text contains unsupported controls");
    const auto families = QFontDatabase::families();
    const auto found = std::find_if(families.begin(), families.end(), [&](const auto &f) {
        return f.compare(s.family, Qt::CaseInsensitive) == 0;
    });
    const bool missing = found == families.end();
    require(
        !missing || s.allowSubstitution,
        "Requested font is missing; explicitly allow substitution or choose an installed family");
    const auto family = missing ? QString("DejaVu Sans") : *found;
    const auto styles = QFontDatabase::styles(family);
    const bool styleMissing = !s.style.isEmpty() && !styles.contains(s.style, Qt::CaseInsensitive);
    require(!styleMissing || s.allowSubstitution,
            "Requested font style is missing; explicitly allow substitution");
    QFont font(family);
    if (!s.style.isEmpty() && !styleMissing)
        font.setStyleName(s.style);
    font.setPixelSize(int(fontPixels));
    font.setHintingPreference(QFont::PreferNoHinting);
    const auto primary = QRawFont::fromFont(font);
    require(primary.isValid(), "No usable local outline font");
    TextGeometry result;
    result.requestedFamily = s.family;
    result.actualFamily = primary.familyName();
    result.actualStyle = primary.styleName();
    result.substituted =
        missing || styleMissing || primary.familyName().compare(family, Qt::CaseInsensitive) != 0 ||
        (!s.style.isEmpty() && primary.styleName().compare(s.style, Qt::CaseInsensitive) != 0);
    require(!result.substituted || s.allowSubstitution,
            "Font matching substituted the requested face; explicitly allow substitution");
    result.curveTolerance = std::max(1e-6, s.height / 2048.);
    Paths64 outlines;
    size_t points{}, fontBytes{};
    std::map<QString, size_t> fontIndices;
    const auto lines = s.text.split('\n');
    require(lines.size() <= 128, "Text exceeds 128 lines");
    result.lines = size_t(lines.size());
    for (qsizetype lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        QTextLayout layout(lines[lineNumber], font);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setAlignment(Qt::AlignLeft);
        layout.setTextOption(option);
        layout.beginLayout();
        auto line = layout.createLine();
        if (line.isValid()) {
            line.setLineWidth(1e9);
            line.setPosition({0, 0});
        }
        layout.endLayout();
        if (!line.isValid())
            continue;
        result.advanceWidth =
            std::max(result.advanceWidth, line.horizontalAdvance() * s.height / fontPixels);
        for (const auto &run : layout.glyphRuns(0, -1, QTextLayout::RetrieveAll)) {
            const auto raw = run.rawFont();
            require(raw.isValid(), "Shaped run has no usable font");
            const auto glyphs = run.glyphIndexes();
            const auto positions = run.positions();
            const auto indices = run.stringIndexes();
            require(glyphs.size() == positions.size() && glyphs.size() == indices.size(),
                    "Invalid shaped glyph positions");
            const auto key = raw.familyName() + QChar::Null + raw.styleName();
            if (!fontIndices.contains(key)) {
                require(fontIndices.size() < 64, "Text exceeds 64 resolved font faces");
                fontIndices[key] = result.fonts.size();
                result.fonts.push_back(
                    {raw.familyName(), raw.styleName(), fingerprint(raw, fontBytes), 0});
            }
            result.fallback |= raw.familyName() != primary.familyName();
            for (qsizetype i = 0; i < glyphs.size(); ++i) {
                require(++result.glyphs <= glyphLimit, "Text exceeds 1024 shaped glyphs");
                require(glyphs[i] != 0, "Font fallback cannot represent a requested glyph");
                ++result.fonts[fontIndices.at(key)].glyphs;
                const auto path = raw.pathForGlyph(glyphs[i]);
                if (path.isEmpty()) {
                    require(indices[i] >= 0 && indices[i] < lines[lineNumber].size(),
                            "Invalid glyph source index");
                    const auto character = lines[lineNumber][indices[i]];
                    require(character.isSpace() || character.category() == QChar::Other_Format ||
                                raw.boundingRect(glyphs[i]).isEmpty(),
                            "A requested visible glyph has no supported outline (bitmap/color "
                            "glyphs are unsupported)");
                    continue;
                }
                auto position = positions[i];
                position.ry() += lineNumber * s.lineSpacing * fontPixels - line.ascent();
                Flatten flatten{
                    s.height / fontPixels, result.curveTolerance, position, points, {}, {}, {}};
                const auto sampled = flatten.run(path);
                Clipper64 glyphClip;
                glyphClip.AddSubject(sampled);
                Paths64 normalized;
                require(glyphClip.Execute(ClipType::Union,
                                          path.fillRule() == Qt::OddEvenFill ? FillRule::EvenOdd
                                                                             : FillRule::NonZero,
                                          normalized),
                        "Glyph contour normalization failed");
                require(!normalized.empty(), "Glyph fill collapses below model precision");
                outlines.insert(outlines.end(), normalized.begin(), normalized.end());
                require(outlines.size() <= 4096, "Text exceeds contour limit");
            }
        }
    }
    require(!outlines.empty(), "Text has no supported outline geometry");
    result.points = points;
    result.contours = outlines.size();
    Clipper64 clip;
    clip.AddSubject(outlines);
    PolyTree64 tree;
    require(clip.Execute(ClipType::Union, FillRule::NonZero, tree), "Text contour union failed");
    size_t outputPoints{};
    auto loop = [&](Path64 path) {
        path = TrimCollinear(path);
        require(path.size() >= 3, "Text contour collapsed below model precision");
        outputPoints += path.size();
        require(outputPoints <= pointLimit, "Text union exceeds output point limit");
        std::rotate(path.begin(),
                    std::min_element(
                        path.begin(), path.end(),
                        [](auto a, auto b) { return std::tie(a.x, a.y) < std::tie(b.x, b.y); }),
                    path.end());
        std::vector<Vec3> points;
        for (auto p : path)
            points.push_back({double(p.x) / integerScale, double(p.y) / integerScale, 0});
        return points;
    };
    auto visit = [&](auto &&self, const PolyPath64 &parent, unsigned depth) -> void {
        require(depth <= 64, "Text contour nesting exceeds limit");
        for (const auto &node : parent) {
            if (!node->IsHole()) {
                require(result.regions.size() < regionLimit, "Text exceeds 1024 connected regions");
                std::vector<std::vector<Vec3>> loops{loop(node->Polygon())};
                for (const auto &hole : *node) {
                    require(hole->IsHole(), "Invalid text contour hierarchy");
                    loops.push_back(loop(hole->Polygon()));
                    ++result.holes;
                }
                Surface surface;
                const auto face = surface.addFace(loops);
                surface.validate();
                if (s.depth > 0)
                    surface.extrude(face, s.depth);
                surface.validate();
                result.regions.push_back(std::move(surface));
            }
            self(self, *node, depth + 1);
        }
    };
    visit(visit, tree, 0);
    require(!result.regions.empty(), "Text union has no surviving faces");
    return result;
}
} // namespace sketchy
