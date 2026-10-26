#pragma once
// R101.a (ADR 0175 §1–§4): the report engine. A report is a pure function of its request and the
// materialized document at one revision; it never mutates the document and keeps no state.
#include "core/model.hpp"
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sketchy {

enum class ReportGroup { None, Kind, Tag, Material, Definition };
enum class ReportVisibility { All, Visible };

// The request object of ADR 0175 §1. `volumes` also emits the invalid table (§1, §6.1).
struct ReportRequest {
    bool counts{true}, areas{true}, volumes{true};
    ReportGroup groupBy{ReportGroup::None};
    ReportVisibility visibility{ReportVisibility::All};
    // Tag IDs (a folder includes every descendant tag), material IDs (areas rows only) and
    // definition IDs. Each ID must exist; the caller checks that.
    std::set<Id> tags, materials, definitions;
    // The session's current selection. Applied only when runReport receives its bodies.
    bool selection{};
    // Decimal places for the output (§3): empty means the document's displayPrecision.
    std::optional<int> precision;
};

struct ReportCountRow {
    std::string key;
    std::size_t contexts{}, instances{}, definitions{};
};

// Areas in square metres (the internal unit; §3 converts at output). Material 0 is "no material".
struct ReportAreaRow {
    std::string key;
    Id material{};
    double area{};
};

// Volumes in cubic metres: known volume only (§2.4). `unvalidated` counts the contexts of the group
// that are not known volumes.
struct ReportVolumeRow {
    std::string key;
    double volume{};
    std::size_t unvalidated{};
};

struct ReportInvalidRow {
    std::string status;
    std::size_t contexts{};
};

struct ReportTerrainRow {
    Id terrain{};
    std::size_t contexts{}, triangles{};
    double area{};
};

// The four tables of §1 and the terrain table of §2.5, plus the stamp (§4) and the applied request.
struct ReportResult {
    std::string documentId;
    std::uint64_t revision{};
    ReportRequest request;
    int precision{};
    std::vector<ReportCountRow> counts;
    std::vector<ReportAreaRow> areas;
    std::vector<ReportVolumeRow> volumes;
    std::vector<ReportInvalidRow> invalid;
    std::vector<ReportTerrainRow> terrain;
    std::size_t sheets{}, views{}, items{};
};

// `selectedBodies` is the session's selection expanded to bodies; it is required when
// request.selection is set and ignored otherwise. Throws std::invalid_argument for a request the
// caller must not have made (no metric, or a selection without editor state).
ReportResult runReport(const Document &doc, const ReportRequest &request,
                       const std::set<Id> *selectedBodies = nullptr);

} // namespace sketchy
