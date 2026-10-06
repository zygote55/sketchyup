#pragma once
#include <QJsonArray>
#include <QJsonObject>
// Used only when a test deliberately constructs a pre-v16 document from the
// current writer. Historical byte fixtures remain untouched.
inline void removeTextureMappingFields(QJsonObject &document) {
    auto remove = [](QJsonArray bodies) {
        for (qsizetype i = 0; i < bodies.size(); ++i) {
            auto body = bodies[i].toObject();
            body.remove("faceTextureMappings");
            bodies[i] = body;
        }
        return bodies;
    };
    document["bodies"] = remove(document["bodies"].toArray());
    auto definitions = document["definitions"].toArray();
    for (qsizetype i = 0; i < definitions.size(); ++i) {
        auto definition = definitions[i].toObject();
        definition["members"] = remove(definition["members"].toArray());
        definitions[i] = definition;
    }
    document["definitions"] = definitions;
}
