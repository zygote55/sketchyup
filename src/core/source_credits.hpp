#pragma once
// R107.c1 (ADR 0181 §3.3, §3.4, owner decision 8): the credit text the data exports write for the
// DEM sources they carry. A notice is the source record's own text, never paraphrased. An export
// collects the notices of the content it writes, each distinct notice once, in the order met.
#include "core/model.hpp"

#include <string>
#include <vector>

namespace sketchy {

struct TerrainDemSource;

// The notice of one DEM source: its credit, then its no-liability sentence when it has one (GLO-30,
// ADR 0181 §3.1 and the licence's Article 6(c)). A 3DEP source's notice is its credit alone.
// Control characters become spaces, so a notice is always one line.
std::string demSourceNotice(const TerrainDemSource &dem);

// The DEM source a body is part of: the GeoTIFF record of the terrain above it, or nullptr.
const TerrainDemSource *demSourceOf(const Document &document, Id body);

// The DEM source the site origin was established from (ADR 0168 §4): the GeoTIFF terrain whose file
// digest is the site's, or nullptr. The data that an IFC georeference carries come from that file.
const TerrainDemSource *demSourceOfSite(const Document &document);

// The notices one export writes. Each distinct notice once; an empty set means the export carries
// no source-derived content, and it must then be byte-identical to an export without this record.
class SourceCredits {
  public:
    void add(const TerrainDemSource &dem);
    bool empty() const { return notices_.empty(); }
    const std::vector<std::string> &notices() const { return notices_; }
    // The notices joined by "; ", for a single text field (GLB asset.copyright, an IFC property).
    std::string joined() const;

  private:
    std::vector<std::string> notices_;
};

} // namespace sketchy
