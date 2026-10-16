#pragma once
#include "core/component_records.hpp"
#include <QJsonArray>
#include <QJsonValue>
namespace sketchy {
// Schema 28 component parameter tables (ADR 0165 §5). Records are written in
// ascending definition and placement order; every field is required, unknown fields
// are rejected and identities are canonical nonzero decimal strings. The tables are
// only shaped here: the records are checked (names, types, canonical expressions,
// the acyclic graph, driver targets, allocator) and evaluated by restore.
QJsonArray encodeComponentParameters(const ComponentDefinitions &definitions);
QJsonArray encodeComponentParameterOverrides(const ComponentInstances &instances);
// Attaches each stored parameter record to its definition. Every record must name a
// stored definition, once, in ascending order.
void decodeComponentParameters(const QJsonValue &value, ComponentDefinitions &definitions);
// Attaches each stored override set to its placement. Values are typed by the
// placement's definition's parameters, so the definitions must be decoded first.
void decodeComponentParameterOverrides(const QJsonValue &value,
                                       const ComponentDefinitions &definitions,
                                       ComponentInstances &instances);
} // namespace sketchy
