#pragma once
#include "core/model.hpp"
#include <QJsonArray>
namespace sketchy {
inline constexpr size_t maxMeasurementAssertions = 32;
inline constexpr size_t maxAssertionTargets = 16;
inline constexpr size_t maxAssertionRecords = 256;
inline constexpr size_t maxAssertionVertices = 20000;
inline constexpr size_t maxAssertionEdges = 40000;
inline constexpr size_t maxAssertionFaces = 2000;
inline constexpr size_t maxAssertionCorners = 64000;
QJsonObject measurementAssertionCapabilities();
// Read-only postconditions over the final private batch candidate. Throws before
// publication on mismatches, unavailable measurements or bounded-work rejection.
QJsonArray evaluateMeasurementAssertions(const Document &candidate, const QJsonArray &assertions);
} // namespace sketchy
