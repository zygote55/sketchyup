#include "text/text_worker.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void fields(const QJsonObject &o, const QStringList &keys) {
    require(o.size() == keys.size(), "Text protocol has missing or unknown fields");
    for (const auto &key : keys)
        require(o.contains(key), "Text protocol is missing a required field");
}
QString text(const QJsonValue &v, int limit) {
    require(v.isString() && v.toString().toUtf8().size() <= limit, "Invalid text protocol string");
    return v.toString();
}
double number(const QJsonValue &v, double low, double high) {
    require(v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= low &&
                v.toDouble() <= high,
            "Invalid text protocol number");
    return v.toDouble();
}
size_t count(const QJsonValue &v, size_t maximum) {
    const auto n = number(v, 0, double(maximum));
    require(n == std::floor(n), "Invalid text protocol count");
    return size_t(n);
}
bool flag(const QJsonValue &v) {
    require(v.isBool(), "Invalid text protocol boolean");
    return v.toBool();
}
QString executable() {
    const auto directory = QCoreApplication::applicationDirPath();
    for (const auto &candidate : {directory + "/sketchyup-text-worker",
                                  directory + "/../lib/sketchyup/sketchyup-text-worker"}) {
        const QFileInfo file(candidate);
        if (file.isFile() && file.isExecutable())
            return file.canonicalFilePath();
    }
    throw std::runtime_error("Text geometry worker is missing from this installation");
}
} // namespace
QJsonObject encodeTextSettings(const TextGeometrySettings &s) {
    return {{"text", s.text},
            {"family", s.family},
            {"style", s.style},
            {"height", s.height},
            {"depth", s.depth},
            {"lineSpacing", s.lineSpacing},
            {"allowSubstitution", s.allowSubstitution}};
}
TextGeometrySettings decodeTextSettings(const QJsonObject &o) {
    fields(o, {"text", "family", "style", "height", "depth", "lineSpacing", "allowSubstitution"});
    TextGeometrySettings s;
    s.text = text(o["text"], 4096);
    s.family = text(o["family"], 256);
    s.style = text(o["style"], 256);
    s.height = number(o["height"], .001, 1000);
    s.depth = number(o["depth"], 0, 1000);
    s.lineSpacing = number(o["lineSpacing"], .5, 10);
    s.allowSubstitution = flag(o["allowSubstitution"]);
    return s;
}
QJsonObject encodeTextGeometry(const TextGeometry &g) {
    Document doc;
    Edit edit{"Generate text geometry", {}};
    for (const auto &surface : g.regions) {
        auto body = std::make_shared<Body>();
        body->id = edit.changes.size() + 1;
        body->name = "Glyph region";
        body->surface = surface;
        edit.changes.push_back({body->id, nullptr, body});
    }
    doc.apply(std::move(edit), doc.revision());
    QJsonArray fonts;
    for (const auto &font : g.fonts)
        fonts.append(QJsonObject{{"family", font.family},
                                 {"style", font.style},
                                 {"fingerprint", font.fingerprint},
                                 {"glyphs", int(font.glyphs)}});
    return {{"requestedFamily", g.requestedFamily},
            {"actualFamily", g.actualFamily},
            {"actualStyle", g.actualStyle},
            {"substituted", g.substituted},
            {"fallback", g.fallback},
            {"fonts", fonts},
            {"glyphs", int(g.glyphs)},
            {"contours", int(g.contours)},
            {"points", int(g.points)},
            {"holes", int(g.holes)},
            {"lines", int(g.lines)},
            {"curveTolerance", g.curveTolerance},
            {"advanceWidth", g.advanceWidth},
            {"document", QJsonDocument::fromJson(encodeDocument(doc)).object()}};
}
TextGeometry decodeTextGeometry(const QJsonObject &o) {
    fields(o, {"requestedFamily", "actualFamily", "actualStyle", "substituted", "fallback", "fonts",
               "glyphs", "contours", "points", "holes", "lines", "curveTolerance", "advanceWidth",
               "document"});
    TextGeometry g;
    g.requestedFamily = text(o["requestedFamily"], 256);
    g.actualFamily = text(o["actualFamily"], 256);
    g.actualStyle = text(o["actualStyle"], 256);
    require(!g.actualFamily.isEmpty(), "Worker omitted resolved font family");
    g.substituted = flag(o["substituted"]);
    g.fallback = flag(o["fallback"]);
    g.glyphs = count(o["glyphs"], 1024);
    g.contours = count(o["contours"], 4096);
    g.points = count(o["points"], 32768);
    g.holes = count(o["holes"], 4096);
    g.lines = count(o["lines"], 128);
    require(g.glyphs && g.contours && g.points && g.lines, "Worker returned empty text statistics");
    g.curveTolerance = number(o["curveTolerance"], 1e-6, 1);
    g.advanceWidth = number(o["advanceWidth"], 0, 1e9);
    require(o["fonts"].isArray() && !o["fonts"].toArray().isEmpty() &&
                o["fonts"].toArray().size() <= 64,
            "Invalid resolved font table");
    size_t glyphCount{};
    for (const auto v : o["fonts"].toArray()) {
        require(v.isObject(), "Invalid font descriptor");
        const auto f = v.toObject();
        fields(f, {"family", "style", "fingerprint", "glyphs"});
        TextFontUse font{text(f["family"], 256), text(f["style"], 256), text(f["fingerprint"], 64),
                         count(f["glyphs"], 1024)};
        require(!font.family.isEmpty() &&
                    QRegularExpression("^[0-9a-f]{64}$").match(font.fingerprint).hasMatch(),
                "Invalid resolved font identity");
        glyphCount += font.glyphs;
        g.fonts.push_back(std::move(font));
    }
    require(glyphCount == g.glyphs, "Font glyph counts disagree");
    require(o["document"].isObject(), "Worker omitted native geometry document");
    auto geometryDocument = o["document"].toObject();
    auto emptyDocument = QJsonDocument::fromJson(encodeDocument(Document{})).object();
    for (const auto &key : {"documentId", "revision", "nextId", "bodies"}) {
        geometryDocument.remove(key);
        emptyDocument.remove(key);
    }
    require(geometryDocument == emptyDocument, "Worker returned non-geometry document metadata");
    const auto doc =
        decodeDocument(QJsonDocument(o["document"].toObject()).toJson(QJsonDocument::Compact));
    require(!doc.bodies().empty() && doc.bodies().size() <= 1024 && doc.definitions().empty() &&
                doc.instances().empty() && doc.tags().empty() && doc.materials().empty() &&
                doc.assets().empty() && doc.scenes().empty() && doc.sections().empty() &&
                doc.annotations().empty(),
            "Worker returned unsupported document resources");
    size_t vertices{}, faces{};
    for (const auto &[id, body] : doc.bodies()) {
        require(body->kind == BodyKind::Geometry && body->name == "Glyph region" &&
                    body->color == Body{}.color && !body->parent &&
                    body->transform == Transform{} && !body->tag && !body->hidden &&
                    !body->locked && body->surface.wires.empty() && body->curves.empty() &&
                    body->guides.empty() && body->properties.empty() &&
                    body->edgeAppearances.empty() && body->faceColors.empty() &&
                    body->materials == MaterialSides{} && body->faceMaterials.empty() &&
                    body->faceTextureMappings.empty(),
                "Worker returned unsupported body state");
        vertices += body->surface.vertices.size();
        faces += body->surface.faces.size();
        require(vertices <= 65536 && faces <= 65536, "Worker geometry exceeds output bounds");
        require(!body->surface.faces.empty(), "Worker returned empty glyph region");
        g.regions.push_back(body->surface);
    }
    return g;
}
TextGeometry runTextWorker(const TextGeometrySettings &settings, const TextWorkerOptions &options) {
    require(options.timeoutMs >= 1 && options.timeoutMs <= 120000 && options.responseBytes >= 1 &&
                options.responseBytes <= textWorkerResponseLimit,
            "Invalid text worker limits");
    const auto encoded = encodeTextSettings(settings);
    (void)decodeTextSettings(encoded);
    const auto request = QJsonDocument(QJsonObject{{"protocol", 1}, {"settings", encoded}})
                             .toJson(QJsonDocument::Compact);
    require(request.size() <= textWorkerRequestLimit, "Text worker request exceeds 32 KiB");
    const auto program = options.executable.isEmpty() ? executable() : options.executable;
    require(QFileInfo(program).isAbsolute(), "Text worker path must be absolute");
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QElapsedTimer timer;
    timer.start();
    process.start();
    require(process.waitForStarted(std::min(options.timeoutMs, 5000)),
            "Text geometry worker failed to start");
    QByteArray output;
    auto stop = [&] {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        }
    };
    try {
        require(process.write(request) == request.size(), "Text worker input write failed");
        process.closeWriteChannel();
        while (true) {
            process.waitForReadyRead(20);
            output += process.readAllStandardOutput();
            process.readAllStandardError();
            require(output.size() <= options.responseBytes,
                    "Text worker output exceeds byte limit");
            require(timer.elapsed() <= options.timeoutMs, "Text geometry worker timed out");
            if (process.state() == QProcess::NotRunning)
                break;
        }
        require(process.exitStatus() == QProcess::NormalExit, "Text geometry worker crashed");
        QJsonParseError parse;
        const auto reply = QJsonDocument::fromJson(output, &parse);
        require(parse.error == QJsonParseError::NoError && reply.isObject(),
                "Text worker returned invalid JSON");
        const auto root = reply.object();
        require(root["protocol"] == 1 && root["ok"].isBool(), "Unsupported text worker response");
        if (!root["ok"].toBool()) {
            fields(root, {"protocol", "ok", "error"});
            throw std::runtime_error(text(root["error"], 4096).toStdString());
        }
        fields(root, {"protocol", "ok", "geometry"});
        require(process.exitCode() == 0 && root["geometry"].isObject(),
                "Text worker exited without a valid result");
        auto result = decodeTextGeometry(root["geometry"].toObject());
        require(result.requestedFamily == settings.family &&
                    (!result.substituted || settings.allowSubstitution),
                "Text worker result does not match requested font policy");
        require(result.curveTolerance == std::max(1e-6, settings.height / 2048.),
                "Text worker returned inconsistent curve precision");
        return result;
    } catch (...) {
        stop();
        throw;
    }
}
} // namespace sketchy
