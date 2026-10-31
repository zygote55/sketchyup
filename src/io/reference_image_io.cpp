#include "io/reference_image_io.hpp"

#include <stdexcept>
#include <string>

namespace sketchy {
namespace {

// The georeference record (R107.b2, schema 35): zone and hemisphere, datum, corner, pixel sizes,
// pixel counts, the source, and the optional credit. Every key is required but "credit".
QJsonObject encodeGeoreference(const ImageGeoreference &geo) {
    QJsonObject source{{"format", QString::fromStdString(geo.source.format)},
                       {"file", QString::fromStdString(geo.source.file)},
                       {"sha256", QString::fromStdString(geo.source.sha256)}};
    if (!geo.source.worldFile.empty()) {
        source["worldFile"] = QString::fromStdString(geo.source.worldFile);
        source["worldSha256"] = QString::fromStdString(geo.source.worldSha256);
    }
    QJsonObject record{{"zone", geo.zone},
                       {"hemisphere", geo.north ? "N" : "S"},
                       {"datum", QString::fromStdString(geo.datum)},
                       {"easting", geo.easting},
                       {"northing", geo.northing},
                       {"pixelWidth", geo.pixelWidth},
                       {"pixelHeight", geo.pixelHeight},
                       {"columns", geo.columns},
                       {"rows", geo.rows},
                       {"source", source}};
    if (!geo.credit.empty())
        record["credit"] = QString::fromStdString(geo.credit);
    return record;
}

ImageGeoreference decodeGeoreference(const QJsonValue &value) {
    const auto object = value.toObject();
    const auto text = [&object](const char *key) {
        if (!object[key].isString())
            throw std::runtime_error(std::string("Reference image georeference needs text ") + key);
        return object[key].toString().toStdString();
    };
    const auto number = [&object](const char *key) {
        if (!object[key].isDouble())
            throw std::runtime_error(std::string("Reference image georeference needs a number ") + key);
        return object[key].toDouble();
    };
    const auto integer = [&number](const char *key) {
        const auto value = number(key);
        if (value != static_cast<int>(value))
            throw std::runtime_error(std::string("Reference image georeference needs a whole number ") + key);
        return static_cast<int>(value);
    };
    const auto hemisphere = text("hemisphere");
    if (hemisphere != "N" && hemisphere != "S")
        throw std::runtime_error("Reference image hemisphere must be N or S");
    const auto sourceObject = object["source"].toObject();
    if (!object["source"].isObject())
        throw std::runtime_error("Reference image georeference needs its source");
    const auto sourceText = [&sourceObject](const char *key) {
        if (!sourceObject[key].isString())
            throw std::runtime_error(std::string("Reference image source needs text ") + key);
        return sourceObject[key].toString().toStdString();
    };
    ImageGeoreference geo;
    geo.zone = integer("zone");
    geo.north = hemisphere == "N";
    geo.datum = text("datum");
    geo.easting = number("easting");
    geo.northing = number("northing");
    geo.pixelWidth = number("pixelWidth");
    geo.pixelHeight = number("pixelHeight");
    geo.columns = integer("columns");
    geo.rows = integer("rows");
    geo.source.format = sourceText("format");
    geo.source.file = sourceText("file");
    geo.source.sha256 = sourceText("sha256");
    if (sourceObject.contains("worldFile")) {
        geo.source.worldFile = sourceText("worldFile");
        geo.source.worldSha256 = sourceText("worldSha256");
    }
    if (object.contains("credit"))
        geo.credit = text("credit");
    return geo;
}

} // namespace

QJsonObject encodeReferenceImage(const ReferenceImage &image) {
    image.validate();
    QJsonObject record{{"asset", QString::number(image.asset)},
                       {"width", image.width},
                       {"height", image.height},
                       {"opacity", image.opacity}};
    if (image.georeference)
        record["georeference"] = encodeGeoreference(*image.georeference);
    return record;
}

ReferenceImage decodeReferenceImage(const QJsonObject &object, bool georeferenced) {
    const bool withGeoreference = object.contains("georeference");
    if (!(object.size() == 4 || (object.size() == 5 && withGeoreference)) ||
        !object["asset"].isString() || !object["width"].isDouble() ||
        !object["height"].isDouble() || !object["opacity"].isDouble())
        throw std::runtime_error("Reference image requires asset, width, height and opacity");
    if (withGeoreference && !georeferenced)
        throw std::runtime_error("A georeferenced reference image needs format 35 or later");
    const auto token = object["asset"].toString();
    bool valid{};
    const auto asset = token.toULongLong(&valid);
    if (!valid || QString::number(asset) != token)
        throw std::runtime_error("Reference image asset must be a canonical decimal identity");
    ReferenceImage result{asset, object["width"].toDouble(), object["height"].toDouble(),
                          object["opacity"].toDouble(), std::nullopt};
    if (withGeoreference)
        result.georeference = decodeGeoreference(object["georeference"]);
    result.validate();
    return result;
}

} // namespace sketchy
