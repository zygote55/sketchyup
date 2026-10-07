#include "io/obj_source.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <set>
namespace sketchy {
void ObjImportOptions::validate() const {
    if (!std::isfinite(metresPerUnit) || metresPerUnit < 1e-6 || metresPerUnit > 1e6 ||
        (up != ObjUpAxis::Y && up != ObjUpAxis::Z))
        throw std::runtime_error("OBJ import requires bounded units and Y or Z up");
}
namespace {
constexpr qsizetype byteLimit = 64 * 1024 * 1024, lineLimit = 64 * 1024;
struct Parser {
    ObjSource result;
    ObjImportOptions options;
    ObjState state;
    size_t line{}, corners{}, statementCount{}, stateIndex{};
    bool stateDirty{true};
    QJsonObject omitted;
    [[noreturn]] void fail(const char *message) const {
        throw std::runtime_error("OBJ line " + std::to_string(line) + ": " + message);
    }
    double number(const QString &token) const {
        bool ok{};
        const auto value = token.toDouble(&ok);
        if (!ok || !std::isfinite(value))
            fail("expected a finite number");
        return value;
    }
    Vec3 converted(Vec3 value, bool normal = false) const {
        if (options.up == ObjUpAxis::Y)
            value = {value.x, -value.z, value.y};
        if (normal)
            return normalized(value);
        value = value * options.metresPerUnit;
        checkPoint(value);
        return value;
    }
    size_t index(const QString &token, size_t available) const {
        bool ok{};
        const auto value = token.toLongLong(&ok);
        if (!ok || !value || value > 300000 || value < -qint64(available))
            fail("invalid vertex reference");
        return value < 0 ? size_t(qint64(available) + value) : size_t(value - 1);
    }
    ObjCorner corner(const QString &token, bool wire) const {
        const auto parts = token.split('/');
        if (parts.isEmpty() || parts.size() > (wire ? 2 : 3) || parts[0].isEmpty())
            fail("invalid slash reference");
        ObjCorner out;
        out.vertex = index(parts[0], result.vertices.size());
        if (parts.size() >= 2 && !parts[1].isEmpty())
            out.texture = index(parts[1], result.textures.size());
        if (parts.size() == 3 && !parts[2].isEmpty())
            out.normal = index(parts[2], result.normals.size());
        if ((parts.size() == 2 && !out.texture) || (parts.size() == 3 && !out.normal))
            fail("incomplete slash reference");
        return out;
    }
    size_t currentState() {
        if (stateDirty) {
            if (result.states.size() == 10000)
                fail("too many object/group/material states");
            result.states.push_back(state);
            stateIndex = result.states.size() - 1;
            stateDirty = false;
        }
        return stateIndex;
    }
    QString identifier(const QStringList &tokens, bool emptyAllowed = false) const {
        if (tokens.size() != 2 && !(emptyAllowed && tokens.size() == 1))
            fail("expected one name without whitespace");
        const auto text = tokens.size() == 2 ? tokens[1] : QString{};
        if (text.toUtf8().size() > 512)
            fail("name exceeds 512 bytes");
        return text;
    }
    void parse(QString text) {
        const auto comment = text.indexOf('#');
        if (comment >= 0)
            text.truncate(comment);
        text = text.trimmed();
        if (text.isEmpty())
            return;
        if (++statementCount > 1000000)
            fail("statement count exceeds one million");
        static const QRegularExpression whitespace("\\s+");
        const auto tokens = text.split(whitespace, Qt::SkipEmptyParts);
        const auto op = tokens[0];
        if (op == "csh" || op == "call")
            fail("executable commands and nested file calls are unsupported");
        if (op == "v") {
            if (tokens.size() != 4 && tokens.size() != 5)
                fail("supported vertices have x y z and optional unit weight");
            if (tokens.size() == 5 && number(tokens[4]) != 1)
                fail("weighted vertices are unsupported");
            if (result.vertices.size() == 100000)
                fail("vertex count exceeds 100000");
            result.vertices.push_back(
                converted({number(tokens[1]), number(tokens[2]), number(tokens[3])}));
        } else if (op == "vn") {
            if (tokens.size() != 4 || result.normals.size() == 300000)
                fail("invalid or oversized normal list");
            result.normals.push_back(
                converted({number(tokens[1]), number(tokens[2]), number(tokens[3])}, true));
        } else if (op == "vt") {
            if (tokens.size() < 2 || tokens.size() > 4 || result.textures.size() == 300000)
                fail("invalid or oversized texture coordinate list");
            if (tokens.size() == 4 && number(tokens[3]) != 0)
                fail("3D texture coordinates are unsupported");
            const auto u = number(tokens[1]), v = tokens.size() >= 3 ? number(tokens[2]) : 0;
            if (std::abs(u) > 1e9 || std::abs(v) > 1e9)
                fail("texture coordinates exceed bounds");
            result.textures.push_back({u, 1 - v});
        } else if (op == "f" || op == "l") {
            const bool wire = op == "l";
            if (tokens.size() < (wire ? 3 : 4) || tokens.size() > 257)
                fail("face/line requires 2/3 to 256 corners");
            corners += size_t(tokens.size() - 1);
            if (corners > 500000 || result.faces.size() + result.lines.size() >= 100000)
                fail("element expansion exceeds limits");
            std::vector<ObjCorner> refs;
            for (qsizetype i = 1; i < tokens.size(); ++i)
                refs.push_back(corner(tokens[i], wire));
            for (const auto &ref : refs)
                if (ref.texture.has_value() != refs[0].texture.has_value() ||
                    ref.normal.has_value() != refs[0].normal.has_value())
                    fail("inconsistent face/line slash attributes");
            if (wire)
                result.lines.push_back({std::move(refs), currentState(), line});
            else
                result.faces.push_back({std::move(refs), currentState(), line});
        } else if (op == "o") {
            state.object = identifier(tokens, true);
            stateDirty = true;
        } else if (op == "g") {
            if (tokens.size() > 17)
                fail("group membership exceeds 16 names");
            state.groups = tokens.mid(1);
            for (const auto &group : state.groups)
                if (group.toUtf8().size() > 512)
                    fail("group name exceeds bounds");
            std::sort(state.groups.begin(), state.groups.end());
            state.groups.removeDuplicates();
            stateDirty = true;
        } else if (op == "s") {
            const auto setting = identifier(tokens);
            bool ok = true;
            state.smoothing = setting == "off" || setting == "0" ? 0
                              : setting == "on"                  ? 1
                                                                 : setting.toULongLong(&ok);
            if (!ok || state.smoothing > 1000000000)
                fail("invalid smoothing group");
            stateDirty = true;
        } else if (op == "usemtl") {
            state.material = identifier(tokens, true);
            stateDirty = true;
        } else if (op == "mtllib") {
            if (tokens.size() < 2)
                fail("missing material library name");
            for (qsizetype i = 1; i < tokens.size(); ++i) {
                if (tokens[i].toUtf8().size() > 4096 || result.materialLibraries.size() == 64)
                    fail("material library list exceeds bounds");
                if (!result.materialLibraries.contains(tokens[i]))
                    result.materialLibraries.append(tokens[i]);
            }
        } else {
            if (!omitted.contains(op) && omitted.size() == 64)
                fail("too many unsupported statement kinds");
            omitted[op] = omitted[op].toInt() + 1;
        }
    }
    void validateReferences() const {
        const auto validate = [&](const auto &elements) {
            for (const auto &element : elements)
                for (const auto &ref : element.corners)
                    if (ref.vertex >= result.vertices.size() ||
                        (ref.texture && *ref.texture >= result.textures.size()) ||
                        (ref.normal && *ref.normal >= result.normals.size()))
                        throw std::runtime_error("OBJ line " + std::to_string(element.line) +
                                                 ": reference exceeds final vertex list");
        };
        validate(result.faces);
        validate(result.lines);
    }
};
} // namespace
ObjSource parseObj(const QByteArray &bytes, ObjImportOptions options) {
    options.validate();
    if (bytes.size() > byteLimit || bytes.contains('\0'))
        throw std::runtime_error("OBJ text exceeds 64 MiB or contains NUL");
    auto payload = bytes;
    if (payload.startsWith("\xef\xbb\xbf"))
        payload.remove(0, 3);
    const auto text = QString::fromUtf8(payload);
    if (text.toUtf8() != payload || !text.isValidUtf16())
        throw std::runtime_error("OBJ text must be valid UTF-8");
    Parser parser;
    parser.options = options;
    QString logical;
    size_t firstLine{};
    qsizetype offset{};
    while (offset < text.size()) {
        auto end = text.indexOf('\n', offset);
        if (end < 0)
            end = text.size();
        ++parser.line;
        if (parser.line > 1000000)
            parser.fail("physical line count exceeds one million");
        auto physical = text.mid(offset, end - offset).trimmed();
        offset = end + 1;
        if (parser.line == 1 && physical.startsWith(QChar(0xfeff)))
            physical.remove(0, 1);
        if (logical.isEmpty())
            firstLine = parser.line;
        const bool continued = physical.endsWith('\\');
        if (continued)
            physical.chop(1);
        if (logical.size() + physical.size() + 1 > lineLimit)
            parser.fail("logical line exceeds 64 KiB");
        logical += physical;
        logical += ' ';
        if (!continued) {
            const auto lastLine = parser.line;
            parser.line = firstLine;
            parser.parse(logical);
            parser.line = lastLine;
            logical.clear();
        }
    }
    if (!logical.isEmpty())
        parser.fail("unterminated continued line");
    parser.validateReferences();
    if (parser.result.faces.empty() && parser.result.lines.empty())
        throw std::runtime_error("OBJ contains no supported faces or lines");
    parser.result.report = {
        {"apiVersion", 1},
        {"sourceBytes", bytes.size()},
        {"sourceSha256",
         QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {"metresPerUnit", options.metresPerUnit},
        {"sourceUp", options.up == ObjUpAxis::Y ? "Y" : "Z"},
        {"nativeUp", "Z"},
        {"vertices", qint64(parser.result.vertices.size())},
        {"faces", qint64(parser.result.faces.size())},
        {"lines", qint64(parser.result.lines.size())},
        {"omittedStatements", parser.omitted}};
    return std::move(parser.result);
}
} // namespace sketchy
