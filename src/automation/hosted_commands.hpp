#pragma once
#include "core/model.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonValue componentGlueDescription(const ComponentDefinition &definition);
QJsonValue attachmentDescription(const Document &doc, Id instance);
bool isHostedCommand(const QString &name);
struct HostedCommandResult {
    ChangeReport changes;
    QJsonObject operation;
};
HostedCommandResult executeHostedCommand(Document &doc, const QJsonObject &command);
} // namespace sketchy
