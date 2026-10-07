#include "io/obj_material.hpp"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <cmath>
#include <optional>
#include <set>
#include <stdexcept>
namespace sketchy {
namespace {
struct Parser {
    ObjMaterialLibrary result;
    QJsonObject omitted;
    std::set<QString> names;
    size_t line{};
    std::optional<double> dissolve, transparency;
    [[noreturn]] void fail(const char *message) const {
        throw std::runtime_error("MTL line " + std::to_string(line) + ": " + message);
    }
    double number(const QString &text, double minimum, double maximum) const {
        bool ok{};
        const auto value = text.toDouble(&ok);
        if (!ok || !std::isfinite(value) || value < minimum || value > maximum)
            fail("number outside supported range");
        return value;
    }
    void omission(const QString &name) {
        if (!omitted.contains(name) && omitted.size() == 64)
            fail("too many unsupported statement kinds");
        omitted[name] = omitted[name].toInt() + 1;
    }
    void texture(const QStringList &tokens) {
        ObjTextureReference value;
        qsizetype i = 1;
        while (i < tokens.size() && tokens[i].startsWith('-')) {
            const auto option = tokens[i++];
            if (option == "-s" || option == "-o") {
                std::array<double, 3> vector = option == "-s" ? std::array<double, 3>{1, 1, 1}
                                                              : std::array<double, 3>{0, 0, 0};
                int count = 0;
                while (i < tokens.size() && count < 3) {
                    bool ok{};
                    tokens[i].toDouble(&ok);
                    if (!ok)
                        break;
                    vector[count++] = number(tokens[i++], -1e6, 1e6);
                }
                if (!count)
                    fail("texture scale/offset requires a numeric argument");
                if (vector[2] != (option == "-s" ? 1 : 0)) {
                    omission("map_Kd_3d_transform");
                    return;
                }
                if (option == "-s")
                    value.scale = {vector[0], vector[1]};
                else
                    value.offset = {vector[0], vector[1]};
            } else if (option == "-clamp") {
                if (i == tokens.size() || (tokens[i] != "on" && tokens[i] != "off"))
                    fail("invalid texture clamp option");
                if (tokens[i++] == "on") {
                    omission("map_Kd_clamp");
                    return;
                }
            } else {
                omission("map_Kd_unsupported_options");
                return;
            }
        }
        if (i == tokens.size())
            fail("missing diffuse texture path");
        value.path = tokens.mid(i).join(' ');
        if (value.path.toUtf8().size() > 4096)
            fail("texture path exceeds 4096 bytes");
        result.materials.back().texture = std::move(value);
    }
    void parse(QString text) {
        const auto comment = text.indexOf('#');
        if (comment >= 0)
            text.truncate(comment);
        text = text.trimmed();
        if (text.isEmpty())
            return;
        static const QRegularExpression whitespace("\\s+");
        const auto tokens = text.split(whitespace, Qt::SkipEmptyParts);
        const auto op = tokens[0];
        if (op == "csh" || op == "call")
            fail("executable commands and nested calls are unsupported");
        if (op == "newmtl") {
            if (tokens.size() != 2 || tokens[1].toUtf8().size() > 512 ||
                !names.insert(tokens[1]).second || result.materials.size() == 1024)
                fail("invalid, duplicate or excessive material names");
            ObjMaterial m;
            m.name = tokens[1];
            result.materials.push_back(std::move(m));
            dissolve.reset();
            transparency.reset();
            return;
        }
        if (result.materials.empty())
            fail("material properties require newmtl first");
        auto &material = result.materials.back();
        if (op == "Kd") {
            if (tokens.size() != 2 && tokens.size() != 4)
                fail("diffuse color requires one or three RGB values");
            material.diffuse = {number(tokens[1], 0, 1),
                                number(tokens.size() == 4 ? tokens[2] : tokens[1], 0, 1),
                                number(tokens.size() == 4 ? tokens[3] : tokens[1], 0, 1)};
        } else if (op == "d" || op == "Tr") {
            if (tokens.size() != 2)
                fail("only uniform material opacity is supported");
            const auto value = number(tokens[1], 0, 1);
            if (op == "d")
                dissolve = value;
            else
                transparency = value;
            if (dissolve && transparency && std::abs(*dissolve - (1 - *transparency)) > 1e-6)
                fail("conflicting d and Tr opacity");
            material.opacity = dissolve ? *dissolve : 1 - *transparency;
        } else if (op == "map_Kd") {
            material.texture = {};
            texture(tokens);
        } else
            omission(op);
    }
};
} // namespace
ObjMaterialLibrary parseObjMaterials(const QByteArray &bytes) {
    if (bytes.size() > 4 * 1024 * 1024 || bytes.contains('\0'))
        throw std::runtime_error("MTL exceeds 4 MiB or contains NUL");
    auto payload = bytes;
    if (payload.startsWith("\xef\xbb\xbf"))
        payload.remove(0, 3);
    const auto text = QString::fromUtf8(payload);
    if (text.toUtf8() != payload || !text.isValidUtf16())
        throw std::runtime_error("MTL must be valid UTF-8");
    Parser parser;
    QString logical;
    size_t first{};
    qsizetype offset{};
    while (offset < text.size()) {
        auto end = text.indexOf('\n', offset);
        if (end < 0)
            end = text.size();
        if (++parser.line > 100000)
            parser.fail("physical line count exceeds 100000");
        auto physical = text.mid(offset, end - offset).trimmed();
        offset = end + 1;
        if (logical.isEmpty())
            first = parser.line;
        const bool continued = physical.endsWith('\\');
        if (continued)
            physical.chop(1);
        if (logical.size() + physical.size() + 1 > 64 * 1024)
            parser.fail("logical line exceeds 64 KiB");
        logical += physical;
        logical += ' ';
        if (!continued) {
            const auto last = parser.line;
            parser.line = first;
            parser.parse(logical);
            parser.line = last;
            logical.clear();
        }
    }
    if (!logical.isEmpty())
        parser.fail("unterminated continued line");
    parser.result.report = {
        {"apiVersion", 1},
        {"sourceBytes", bytes.size()},
        {"sourceSha256",
         QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {"materials", qint64(parser.result.materials.size())},
        {"omittedStatements", parser.omitted}};
    return std::move(parser.result);
}
} // namespace sketchy
