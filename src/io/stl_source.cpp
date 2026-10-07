#include "io/stl_source.hpp"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QtEndian>
#include <bit>
namespace sketchy {
void StlCoordinateOptions::validate() const {
    if (!std::isfinite(metresPerUnit) || metresPerUnit < 1e-6 || metresPerUnit > 1e6 ||
        (up != StlUpAxis::Y && up != StlUpAxis::Z))
        throw std::runtime_error("STL requires bounded explicit units and Y or Z up");
}
namespace {
constexpr qsizetype byteLimit = 64 * 1024 * 1024;
constexpr size_t facetLimit = 100000;
void require(bool valid, const char *message) {
    if (!valid)
        throw std::runtime_error(message);
}
struct Parser {
    StlCoordinateOptions options;
    StlSource result;
    size_t attributes{}, zeroNormals{}, opposedNormals{}, degenerate{};
    Vec3 axis(Vec3 p) const { return options.up == StlUpAxis::Y ? Vec3{p.x, -p.z, p.y} : p; }
    void add(StlFacet facet) {
        require(result.facets.size() < facetLimit, "STL exceeds 100000 facets");
        for (auto &p : facet.vertices) {
            p = axis(p) * options.metresPerUnit;
            checkPoint(p);
        }
        auto n = axis(facet.normal);
        require(std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z),
                "STL normal must be finite");
        const auto magnitude = std::hypot(n.x, n.y, n.z);
        require(std::isfinite(magnitude), "STL normal magnitude exceeds numeric bounds");
        facet.normal = magnitude ? Vec3{n.x / magnitude, n.y / magnitude, n.z / magnitude} : Vec3{};
        if (!magnitude)
            ++zeroNormals;
        const auto geometric =
            cross(facet.vertices[1] - facet.vertices[0], facet.vertices[2] - facet.vertices[0]);
        const auto geometricMagnitude = length(geometric);
        if (geometricMagnitude < 2 * tolerance * tolerance)
            ++degenerate;
        else if (magnitude && dot(geometric * (1 / geometricMagnitude), facet.normal) < .9999)
            ++opposedNormals;
        attributes += facet.attribute != 0;
        result.facets.push_back(facet);
    }
    void binary(const QByteArray &bytes, size_t count) {
        require(count > 0 && count <= facetLimit, "STL binary facet count must be 1..100000");
        require(bytes.size() == qsizetype(84 + count * 50),
                "STL binary count and byte length disagree");
        result.solids.append("STL mesh");
        result.facets.reserve(count);
        auto scalar = [](const char *p) {
            return double(std::bit_cast<float>(qFromLittleEndian<quint32>(p)));
        };
        auto vector = [&](const char *p) { return Vec3{scalar(p), scalar(p + 4), scalar(p + 8)}; };
        for (size_t i = 0; i < count; ++i) {
            const auto *p = bytes.constData() + 84 + i * 50;
            add({{vector(p + 12), vector(p + 24), vector(p + 36)},
                 vector(p),
                 0,
                 qFromLittleEndian<quint16>(p + 48)});
        }
    }
    void ascii(QByteArray bytes) {
        if (bytes.startsWith(QByteArray::fromHex("efbbbf")))
            bytes.remove(0, 3);
        require(!bytes.contains('\0'), "ASCII STL cannot contain NUL");
        const auto text = QString::fromUtf8(bytes);
        require(text.toUtf8() == bytes, "ASCII STL requires valid UTF-8");
        enum class State { Solid, Facet, Outer, Vertex1, Vertex2, Vertex3, EndLoop, EndFacet };
        State state = State::Solid;
        StlFacet facet;
        QString solidName;
        qsizetype offset = 0;
        size_t lines = 0;
        static const QRegularExpression whitespace("\\s+");
        auto number = [](const QString &token) {
            bool ok{};
            const double value = token.toDouble(&ok);
            require(ok && std::isfinite(value), "ASCII STL requires finite decimal numbers");
            return value;
        };
        while (offset < text.size()) {
            require(++lines <= 1000000, "ASCII STL exceeds one million lines");
            auto end = text.indexOf('\n', offset);
            if (end < 0)
                end = text.size();
            require(end - offset <= 4096, "ASCII STL line exceeds 4096 characters");
            const auto line = text.mid(offset, end - offset).trimmed();
            offset = end + 1;
            if (line.isEmpty())
                continue;
            const auto words = line.split(whitespace, Qt::SkipEmptyParts);
            if (state == State::Solid) {
                require(words[0] == "solid", "ASCII STL requires solid before facets");
                solidName = line.mid(5).trimmed();
                require(solidName.toUtf8().size() <= 512, "STL solid name exceeds 512 bytes");
                require(result.solids.size() < 256, "ASCII STL exceeds 256 solids");
                result.solids.append(solidName.isEmpty()
                                         ? QString("STL mesh %1").arg(result.solids.size() + 1)
                                         : solidName);
                state = State::Facet;
            } else if (state == State::Facet) {
                if (words[0] == "endsolid") {
                    const auto closing = line.mid(8).trimmed();
                    require(closing.isEmpty() || closing == solidName,
                            "ASCII STL closing solid name disagrees");
                    state = State::Solid;
                } else {
                    require(words.size() == 5 && words[0] == "facet" && words[1] == "normal",
                            "ASCII STL requires facet normal x y z");
                    facet = {};
                    facet.solid = size_t(result.solids.size() - 1);
                    facet.normal = {number(words[2]), number(words[3]), number(words[4])};
                    state = State::Outer;
                }
            } else if (state == State::Outer) {
                require(words == QStringList{"outer", "loop"}, "ASCII STL requires outer loop");
                state = State::Vertex1;
            } else if (state == State::Vertex1 || state == State::Vertex2 ||
                       state == State::Vertex3) {
                require(words.size() == 4 && words[0] == "vertex",
                        "ASCII STL requires exactly three vertex records");
                const size_t index = state == State::Vertex1 ? 0 : state == State::Vertex2 ? 1 : 2;
                facet.vertices[index] = {number(words[1]), number(words[2]), number(words[3])};
                state = index == 0 ? State::Vertex2 : index == 1 ? State::Vertex3 : State::EndLoop;
            } else if (state == State::EndLoop) {
                require(words == QStringList{"endloop"},
                        "ASCII STL requires endloop after three vertices");
                state = State::EndFacet;
            } else {
                require(words == QStringList{"endfacet"}, "ASCII STL requires endfacet");
                add(facet);
                state = State::Facet;
            }
        }
        require(state == State::Solid && !result.facets.empty(),
                "ASCII STL is incomplete or has no facets");
    }
};
} // namespace
StlSource parseStl(const QByteArray &bytes, StlCoordinateOptions options) {
    options.validate();
    require(!bytes.isEmpty() && bytes.size() <= byteLimit,
            "STL input must contain 1 byte to 64 MiB");
    Parser parser{options, {}};
    const auto count = bytes.size() >= 84 ? qFromLittleEndian<quint32>(bytes.constData() + 80) : 0;
    const bool binary = bytes.size() >= 84 && quint64(bytes.size()) == 84 + quint64(count) * 50;
    if (binary)
        parser.binary(bytes, count);
    else
        parser.ascii(bytes);
    parser.result.report = {
        {"apiVersion", 1},
        {"format", binary ? "binary STL" : "ASCII STL"},
        {"sourceBytes", bytes.size()},
        {"sourceSha256",
         QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {"metresPerUnit", options.metresPerUnit},
        {"sourceUp", options.up == StlUpAxis::Y ? "Y" : "Z"},
        {"nativeUp", "Z"},
        {"facets", qint64(parser.result.facets.size())},
        {"solids", parser.result.solids.size()},
        {"nonzeroAttributeWords", qint64(parser.attributes)},
        {"zeroNormals", qint64(parser.zeroNormals)},
        {"normalsDisagreeWithWinding", qint64(parser.opposedNormals)},
        {"degenerateFacets", qint64(parser.degenerate)}};
    return std::move(parser.result);
}
} // namespace sketchy
