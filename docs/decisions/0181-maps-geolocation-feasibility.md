# 0181 — Maps and geolocation: feasibility

**Status: Accepted (2026-10-27).** The owner approved the full scope ("Real-world terrain matters to me"): decisions 1 to 10, 12 and 13 as recommended, with R107.a, R107.b, R107.b2 (optional), R107.c and R107.e scheduled and R107.d (Nominatim geocoding) held. Decision 11 is replaced: the owner does not consult counsel, so before R107.b ships a worker re-reads the licence pages this ADR could not read in full (the Copernicus licence first) and records them, and the product uses only USGS 3DEP and Copernicus GLO-30, each with its required credit, and no Google, Esri or Mapbox service; provider names are used only to refer to them, with the non-affiliation note. Originally proposed 2026-10-26. This is the feasibility design for [R107](../PR_ROADMAP.md#r107) ("Investigate and conditionally add maps/geolocation services"), scope row C06, which [SCOPE](../SCOPE.md) keeps at "Investigate". It is research and design only: no product code, no dependency, no account, no API key and no network call, from the product or from the research. No provider endpoint was called. **Verdict: user-mediated data import only for 1.x. The user downloads an elevation file (USGS 3DEP or Copernicus GLO-30) in a browser and SketchyUp imports it. No basemap or imagery adapter. One online lookup, geocoding through Nominatim, is viable under its policy and is held for an owner decision (R107.d). It is not scheduled.** Manual coordinates stay the primary input and work offline. The owner decides on that verdict and on the open questions in [Owner decisions](#owner-decisions). ADR 0168's translation-only site origin stands, and so does ADR 0179's network stance N1–N9, with the differences named in [§4](#4-network-stance).

Note on links: ADRs 0177 to 0180 are on their own branches and are not on `main` at `e51f5959`. Links to them resolve after those branches merge.

## Verdict in brief

- **Online basemap tiles are not viable as an adapter for 1.x.** The OpenStreetMap tile policy bars offline use and any pre-fetching of tiles the user is not viewing. Google's tile policy bars pre-fetching and caching outside limited conditions it names, and bars offline use. Google geocoding results must be shown on a Google map. MapTiler's free plan is non-commercial and its terms bar commercial derivative works. Mapbox's terms set no tile caching limit, but its offline use is a separate licence. Esri's terms were not retrieved.
- **Elevation: the user downloads, SketchyUp imports.** Two sources fit. USGS 3DEP is free, has no use restrictions, and is public domain; its tiles are NAD83 geographic and NAVD88 over the conterminous US. Copernicus GLO-30 is free, but its licence requires a notice to the public and its heights are EGM2008 (a surface model, not bare earth). A 1° GeoTIFF tile is too large to read whole, so R107.b needs a bounded reader and windowing: a 1° tile at 1 arc-second is about 49 MiB as float32, and one at 1/3 arc-second is about 445 MiB, both before compression.
- **Coordinates: an in-house WGS84 ↔ UTM conversion, offline, with no PROJ.** The Krüger series (Karney 2011) gives errors under 5 nm within 3,900 km of the central meridian. Typed coordinates at six decimal places are about 0.11 m north–south. The datum is labelled, not assumed. OSGB36 coordinates are refused, since the product has no national grid transformation.
- **Attribution: a source record, carried into every export that shows source-derived geometry or values.** GLB has `asset.copyright`. PDF sheets get a credit line. The batch manifest carries the credit list. OBJ and DXF get comment lines (a proposal, see [§3](#3-attribution)).
- **Geocoding through Nominatim is the one online adapter the evidence supports.** The policy allows 1 request per second, requires caching, requires an identifying User-Agent, and bars client-side autocomplete. It is held for R107.d.

## Context

**What exists today (verified in code and docs, 2026-10-26).**

- **Site origin.** `SiteOrigin` holds easting, northing and elevation in projected metres, a CRS label of at most 64 bytes, and provenance (`src/core/site_origin.hpp`). The label is never used to transform. Coordinates are bounded by `siteCoordinateLimit` = 1e8 m. The site stores no latitude or longitude.
- **Solar location.** `SolarSettings` holds latitude (±90°), longitude (±180°), north rotation (±180°) and a civil time (`src/solar/solar.hpp`; validated at `src/solar/solar.cpp:35–42`). The solar panel edits them (`src/app/solar_panel.cpp:100–149`). **The document can therefore hold two locations, the site origin and the solar latitude and longitude, and nothing checks that they agree.** ADR 0168 kept them separate "without PROJ".
- **Site commands.** `site.describe` reports the origin, the label and provenance, and says "label only; coordinates are never reprojected" (`src/automation/terrain_commands.cpp:3096–3110`). `site.set`, `site.reanchor` and `site.convert` are in `src/automation/inspection.cpp:280–287` and `terrain_commands.cpp:2825–2842`.
- **IFC georeference (ADR 0171 §3.6).** The importer reads `IfcMapConversion` and `IfcProjectedCRS`. A rotation cannot be held by the translation-only site origin, so by default the importer keeps the project axes and reports the conversion. Its opt-in grid mode rotates the model. `IfcSite` `RefLatitude` and `RefLongitude` are kept as properties and do not change the solar settings.
- **Terrain (ADR 0168, ADR 0169).** Import takes DXF and point files (CSV, XYZ, PNEZD) only (`src/automation/terrain_commands.cpp:114`, `:2689`). Limits per document: 250,000 triangles, 250,000 source vertices, 100,000 sources (`src/core/terrain_records.hpp:240–245`). Import runs in the application process; `src/io/terrain_import.cpp` has no worker. ADR 0168 §1 deferred GeoTIFF and DEM rasters to a later row.
- **Reference images.** `ReferenceImage` holds an asset, width, height and opacity, and is placed by a world transform (`src/core/reference_image.hpp`, `src/io/reference_image_io.cpp`). It carries no geographic reference. Images decode through `QImageReader` (`src/io/texture_image.cpp`, ADR 0063).
- **Network surface.** The only network code is in `src/integrations/` and `src/app/assistant_panel.cpp` (assistant providers, ChatGPT sign-in, the Ollama loopback). The transport timeout is 30,000 ms (`src/integrations/provider_transport.hpp:50`). This matches ADR 0179.
- **Exports.** The PDF sheets set a creator and a title and attach a JSON report (`src/io/sheet_export.cpp:809–812`). The measured PDF sets the same (`src/io/measured_export.cpp:315–319`) and the measured PNG is a raster (`:281`). The GLB `asset` block holds only a version and a generator (`src/integrations/glb_export.cpp:548`). DXF writes a HEADER with `$ACADVER` and `$INSUNITS` and no comment lines (`src/io/dxf_export.cpp:236–239`). STL writes the name `solid sketchyup` (`src/io/stl_export.cpp:55`). The presentation batch writes `manifest.json` (`src/integrations/presentation_batch.cpp:250`). The drawing title block holds project, drawing title, date and revision and no credit field (`src/core/drawing_item_records.hpp:86–88`).
- **Dependencies.** `dependencies.json` lists Qt, Clipper2, CDT, IfcOpenShell, OCCT, Boost, Eigen, OpenSKP, miniz and OpenGL. It does not list `third_party/manifold` or `third_party/cgltf`, which CMake builds into the application (`CMakeLists.txt:48–50`, `:62`). The inventory is incomplete, and any R107 slice that adds a dependency must fix it first.
- **Native schema.** The newest document schema is 31 (`src/io/native_format.hpp:10`; ADR 0174 §2). A geographic record on the site needs schema 32.
- **Prerequisites.** [R089](../PR_ROADMAP.md#r089) is delivered (1.0.0 released). [R067](../PR_ROADMAP.md#r067) is delivered in the accepted M7 checkpoint, with the solar algorithm and the sun controls. [R095](../PR_ROADMAP.md#r095) is delivered (accepted 2026-10-19), with the site origin and terrain import. R107's Requires line is met.

**Alternatives considered:**

| Route | Access and cost | Verdict |
|---|---|---|
| OpenStreetMap tile server, as a viewport basemap | No key. Free, but bulk use and offline use are barred | **Rejected.** Offline use is not permitted on the tile server, and pre-fetching is barred (§1.2) |
| Google Map Tiles API, as a basemap | Key and billing. Essentials: $0.60 per 1,000 events after 100,000 free a month (§1.2) | **Rejected.** Caching and offline are barred; exports outside a Google display are not permitted (§1.2) |
| Mapbox, as a basemap | Token. Raster tiles: 750,000 free a month, then $0.25 per 1,000 (§1.2) | **Not recommended now.** The terms set no tile caching limit, but offline is a separate licence and the desktop terms (Product Terms) were not read |
| MapTiler, as a basemap | Key. Free plan: 5,000 map loads and 100,000 tile requests a month, non-commercial (§1.2) | **Not recommended now.** Commercial derivative works are barred, and the free plan is non-commercial |
| Esri World Imagery | Esri account (terms not retrieved) | **Not verified.** The item page points away from offline export (search summary only) |
| Sentinel-2 imagery (user download) | Free, per search summaries; official page not read (§1.2) | **Reference only.** Its 10 m resolution is too coarse to model a site; not a modelling input |
| National orthophoto (USGS NAIP, user download) | Free to download; public-domain status conflicting across sources | **Held.** Not relied on until the status is read from the source |
| Online elevation API (Google Elevation and others) | Key and billing | **Not evaluated.** No source read |
| **Elevation file, user-mediated (3DEP, GLO-30)** | Browser download by the user; no key | **Chosen (R107.b).** Free and documented, with a clear licence notice for each |
| Nominatim geocoding | No key; User-Agent; 1 request per second; caching required | **Viable under its policy; held for R107.d** |
| Google or Mapbox geocoding | Key and billing | **Not recommended.** Google results display only on a Google map; permanent storage is limited to place IDs (§1.4) |
| PROJ for coordinate transforms | MIT-style licence; network grids fetched only when authorised; cache up to 300 MB (§2.2) | **Not for 1.x.** The product needs WGS84 and UTM only, which a bounded in-house series covers. Revisit for State Plane or OSGB, which need grids |
| In-house Krüger series for WGS84 ↔ UTM | None | **Chosen (R107.a).** Offline, bounded, and tested against reference points |
| Bundling a DEM or imagery in the installer | None | **Rejected.** The 250,000-triangle budget and the licence record make it unworkable, and bundled data would be unrecorded |

## 1. Sources compared

### 1.1 Method

- **Sources.** Official pages and policies, read on 2026-10-26. Each citation gives the date the page shows, or "no date shown". Quotes are the reading tool's exact quotes from the page. I did not check each quote against the live page. Section 11 of this ADR asks for the cited sentences to be re-read before R107.b or R107.c is written.
- **Not retrieved.** Google's Maps Platform Service Specific Terms (the fetch returned a truncated page). Esri's terms (the item page returned only its title). The Copernicus DEM licence PDF (the binary could not be read). The Copernicus Sentinel legal page and the Copernicus access conditions (HTTP 404). The Mapbox Product Terms. The Ordnance Survey OpenData page (HTTP 404). The USGS 3DEP lidar format pages. The GPS.gov accuracy page (HTTP 404 on every URL tried).
- **Search summaries.** Where a figure came only from a search summary, the text says so and the figure is not relied on for a decision. They are listed in [Sources](#sources).
- **No endpoint called.** Endpoints named below come from documentation. No request was made to any provider, and no key or account was used.

### 1.2 Imagery and base maps

**OpenStreetMap tiles.** [Tile usage policy](https://operations.osmfoundation.org/policies/tiles/), read 2026-10-26, no date shown.

- "Bulk downloading is any pre-emptive fetching of tiles other than those a user is actively viewing."
- "Offline use is not permitted on tile.openstreetmap.org."
- "Any 'download for offline' button or background job that fetches tiles a user is not currently viewing" is prohibited.
- "Cache tiles locally according to HTTP caching headers (or at least 7 days if your cache cannot read them)."
- "Show OpenStreetMap licence attribution clearly on the map (typically bottom-right)." "Do not hide attribution beneath UI, behind toggles, or off-screen."
- "We may block access, without notice, if your usage degrades the service."

**Google Map Tiles API.** [Tile policies](https://developers.google.com/maps/documentation/tile/policies), read 2026-10-26; "Last updated 2026-10-23 UTC."

- "you must not pre-fetch, index, store, or cache any Content except under the limited conditions stated in the terms." The client must respect the `max-age` and `stale-while-revalidate` values.
- Offline use is listed among the prohibited non-visualization uses: "Offline uses, including for any of the above".
- "You must include clear Google Maps attribution when displaying content from Google Maps Platform APIs." The logo is preferred, and "the attribution of third-party data must clearly be disassociated from Google's data attributions."
- **Cost.** [Pricing](https://developers.google.com/maps/billing-and-pricing/pricing), read 2026-10-26; "Last updated 2026-10-23 UTC." 2D Map Tiles (Essentials): $0.60 per 1,000 events for the first tier, with 100,000 events free a month. A stale $200 monthly credit that ended on 2025-02-28 also appears on the page and is not relied on.
- The Service Specific Terms (the page that governs export and printing) were not retrieved.

**Mapbox.** [Terms of Service](https://www.mapbox.com/legal/tos), read 2026-10-26; "Last updated March 31, 2024."

- No caching limit on map tiles is set. "caching of or references to the content may not be immediately removed." On termination: "you agree to immediately destroy any materials or other data or content."
- Offline use is not addressed in the Terms. Services may be incorporated into "Licensed Applications", which the Product Terms define. The Product Terms were not read.
- "You may not remove any proprietary notices or product identification labels from the Services." No separate attribution text is set in the Terms.
- **Cost.** [Pricing](https://www.mapbox.com/pricing), read 2026-10-26; no effective date shown. Map loads: 50,000 free a month, then $5.00 per 1,000 for the next 50,000. Raster tile requests: 750,000 free a month, then $0.25 per 1,000. Offline pricing is in separate SDK guides, not on this page.

**MapTiler.** [Terms](https://www.maptiler.com/terms/), read 2026-10-26; no date shown. [Pricing](https://www.maptiler.com/cloud/pricing/), read 2026-10-26; no date shown.

- "© MapTiler" is required, and "© OpenStreetMap" for OpenStreetMap-based data.
- For "printed maps, images or video recording", the credit must be "in a clearly visible textual description near the map."
- "you may not produce commercial derivative works from MapTiler Services." "The production of derivative vector datasets for non-commercial purposes or for OpenStreetMap is permitted."
- Caching, offline use and desktop use are not addressed.
- **Cost.** Free plan: 5,000 map loads, 100,000 tile requests and 1,000 geocoding requests a month. "Suitable for testing, PoC, prototyping, personal, or non-commercial use." Flex: $30 a month, with overage at $2.50 per 1,000 sessions and $0.15 per 1,000 API requests.

**Esri World Imagery.** Not verified. The item page redirected to an address that returned only the word "ArcGIS" on 2026-10-26. A search summary says the layer "is not meant for exporting tiles for offline use" and is licensed under the Esri Master License Agreement. That is secondary and is not relied on.

**Sentinel-2 (open, user download).** No official page was retrieved. The Copernicus legal page returned HTTP 404. Search summaries say the 10 m bands are B2, B3, B4 and B8 and that the constellation revisit is about 5 days. Summaries of the reuse terms say the data are free and that attribution takes the form "Contains modified Copernicus Sentinel data" with the year. These are secondary. Treat the attribution text as unverified until the official page is read.

**USGS public domain and credit.** [Copyrights and credits](https://www.usgs.gov/information-policies-and-instructions/copyrights-and-credits), read 2026-10-26; no date shown. "USGS-authored or produced data and information are considered to be in the U.S. Public Domain." "When using information from USGS information products, publications, or websites, we ask that proper credit be given." The sample credit line is "Credit: U.S. Geological Survey."

**National orthophotos (USDA NAIP).** Not read from a primary source. Search summaries disagree on whether imagery from 2020 onward is public domain (one says public domain for imagery through 2019, another says public domain without a date). Not relied on.

### 1.3 Elevation

**Copernicus DEM GLO-30.** [Copernicus Data Space collection page](https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM), read 2026-10-26.

- Absolute vertical accuracy "< 4m (90% linear error)". Relative vertical: "< 2m for slopes below or equal 20%" and "< 4m for slopes above 20%" (90% linear point-to-point error within 1° × 1°). Absolute horizontal: "< 6m (90% circular error)".
- GLO-30 resolution 30 m. "The GLO-30 and GLO-90 datasets are available worldwide with a free license."
- Attribution for the GLO-30 data: "© DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018", followed by "provided under COPERNICUS by the European Union and ESA; all rights reserved." Adapted or modified data needs "produced using Copernicus WorldDEM-30" before that notice.

[Earth Engine catalogue entry](https://developers.google.com/earth-engine/datasets/catalog/COPERNICUS_DEM_GLO30_2024_1), read 2026-10-26; the release is 2024_1.

- "The DSM uses the EGM2008 vertical datum: EPSG:3855." The entry calls it a DSM: "orthometric height (H) in meters above EGM2008."
- The attribution requirement applies to anyone "distributing the data or communicating it to the general public". The redistributor must add a sentence that the Copernicus organisations accept no liability, quoted as "The organisations in charge of the Copernicus programme by law or by delegation do not incur any liability."
- Water pixels are flagged, and "Void (no data)" appears in the masks. The entry gives no numeric no-data value for the elevation band.
- The licence is named "Licence for the use of the Copernicus WorldDEM-30". **Not read in full** (the PDF was binary). Its clauses on commercial use and redistribution are unverified.

**SRTM (USGS).** [SRTM 1 arc-second page](https://www.usgs.gov/centers/eros/science/usgs-eros-archive-digital-elevation-shuttle-radar-topography-mission-srtm-1), read 2026-10-26; page date 2018-07-30 as shown.

- "a resolution of 1 arc-second (30 meters)" for void-filled data, with "Some tiles may still contain voids." The page also says "tiles above 50° north and below 50° south latitude are sampled at a resolution of 2 arc-second by 1 arc-second."
- Vertical datum: EGM96. Horizontal datum: WGS84.
- Formats: DTED, BIL and GeoTIFF.
- "Sources/Usage: Public Domain" is printed on an example image. The page does not say whether the elevation data are public domain. **The licence is unverified for the data.**
- No vertical accuracy is stated on the page. A search summary gives the SRTM design requirement as 16 m at 90% (Rodriguez et al. 2006, cited second-hand). Not relied on.

**USGS 3D Elevation Program (3DEP).** [Program page](https://www.usgs.gov/3d-elevation-program), read 2026-10-26; no date shown. "All 3DEP products are available free of charge and without use restrictions." [1/3 arc-second DEM record](https://www.sciencebase.gov/catalog/item/4f70aa9fe4b058caae3f8de5), read 2026-10-26; item publication date 2024.

- Resolution "approximately 10 m". Coordinates "distributed in geographic coordinates in units of decimal degrees" in NAD83. Elevations over the conterminous US "are referenced to the North American Vertical Datum of 1988 (NAVD88)." Elevations are bare earth.
- "The seamless 1/3arc-second DEM is available as pre-staged current and historical products tiled in GeoTIFF format."
- "All 3DEP products are public domain."
- [Quality levels](https://www.usgs.gov/3d-elevation-program/topographic-data-quality-levels-qls), read 2026-10-26; no date shown. RMSEz is 10 cm for QL1 and QL2 and 5 cm for QL0. Pulse spacing is 0.35 m or finer for QL1 and 0.71 m or finer for QL2. Point density is at least 8 points per m² for QL1 and 2 for QL2. The page says the NVA and VVA requirements of QL0 are "two times better than that of QL1 data." The page cites the 3DEP Lidar Base Specification without a version. A 2014 ASPRS standard is referenced.
- Lidar point-cloud formats (LAZ, COPC) were not confirmed.

### 1.4 Geocoding

**Nominatim.** [Usage policy](https://operations.osmfoundation.org/policies/nominatim/), read 2026-10-26; no date shown.

- "No heavy uses (an absolute maximum of 1 request per second)."
- "Results must be cached on your side." "Clients sending repeatedly the same query may be classified as faulty and blocked."
- "Provide a valid HTTP Referer or User-Agent identifying the application (stock User-Agents as set by http libraries will not do)."
- Autocomplete: "This is not yet supported by Nominatim and you must not implement such a service on the client side using the API."
- Bulk geocoding: "As a general rule, bulk geocoding of larger amounts of data is not encouraged." Scripts running longer than a day, or on a schedule, are "restricted to 4 requests per minute."
- Attribution: the data are ODbL (see §3).

**Google Geocoding API.** [Policies](https://developers.google.com/maps/documentation/geocoding/policies), read 2026-10-26; "Last updated 2026-10-26 UTC."

- "Content pre-fetching, caching, or storage is generally restricted." The place ID is exempt: "You can therefore store place ID values indefinitely."
- "Geocoding API results displayed on a map must be shown on a Google Map." "attribution is required when displaying data without a Google Map."
- **Cost.** Geocoding (Essentials): $5.00 per 1,000 events, with 10,000 free a month ([pricing](https://developers.google.com/maps/billing-and-pricing/pricing), read 2026-10-26).

**Mapbox geocoding.** Pricing only ([pricing](https://www.mapbox.com/pricing), read 2026-10-26). Temporary geocoding: 100,000 free a month, then $0.75 per 1,000. Permanent geocoding: $5.00 per 1,000 with no free tier. "Stored results cannot be used for distribution or sublicense" for permanent geocoding. No caching limit is stated for temporary results.

### 1.5 Comparison

| Service | Access and auth | Licence and attribution | Cost (read) | Caching and offline | Export and print | Verdict |
|---|---|---|---|---|---|---|
| OpenStreetMap tiles | No key | ODbL; visible attribution | Free, subject to the policy | HTTP headers, or 7 days; offline barred | Not addressed | **Rejected** for the viewport |
| Google Map Tiles | Key; billing | Google attribution, logo | $0.60 per 1,000 after 100,000 free a month | Pre-fetch, store and cache barred; offline barred | Not permitted outside Google display (not verified in the Service Terms) | **Rejected** |
| Mapbox | Token | Proprietary notices kept; attribution text unverified | 50,000 map loads and 750,000 raster requests free a month | No tile limit in Terms; offline separate | Not addressed in Terms | **Not recommended now** |
| MapTiler | Key | "© MapTiler"; "© OpenStreetMap"; printed credit | Free plan non-commercial | Not addressed | Printed credit required | **Not recommended now** |
| Esri | Account | Not verified | Not verified | Not verified | Not verified | **Not verified** |
| Copernicus GLO-30 | None (download) | Licence notice; "produced using" for derived data | Free | Licence PDF not read | Notice required when distributed | **Chosen for R107.b** |
| USGS 3DEP | None (download) | Public domain; credit requested | Free, no restrictions | Public domain | Credit requested | **Chosen for R107.b** |
| SRTM (USGS) | None (download) | Public domain shown on example only | Free | Not stated | Not stated | **Held** until its data licence is read |
| Nominatim | None; User-Agent | ODbL | Free, 1 request per second | Caching required | ODbL notice when publicly used | **Viable; held (R107.d)** |
| Google geocoding | Key; billing | Google map display required | $5.00 per 1,000 after 10,000 free a month | Restricted; place IDs exempt | Google map required | **Not recommended** |

## 2. Coordinates and accuracy design

### 2.1 Stored form

- **The site origin does not change.** It stays `SiteOrigin` (ADR 0168 §4): projected metres, a translation only, and a CRS label that is never used to transform.
- **New optional geographic record.** The site gains an optional `geographic` record: latitude and longitude in decimal degrees (WGS84), a datum label (`WGS84` in 1.x, see §2.3), the UTM zone and hemisphere used, the grid convergence computed at the origin (§2.6), and provenance (typed, geocoded, or imported with its file SHA-256). This is schema 32 (the newest is 31). It is owner decision 7.
- **The solar study reads from the site record on user action.** A "Use site location" control copies the site's latitude and longitude into the solar settings, as one undoable edit. Without that action the two stay separate, and the solar panel shows a mismatch warning when the site record and the solar study differ by more than 1 m on the ground (proposed).

### 2.2 Conversion

- **Method.** A transverse Mercator series in the form given by Karney (2011): "the errors in the series method are less than 5 nm within 3900 km of the central meridian" (abstract, [arXiv:1002.1417](https://arxiv.org/abs/1002.1417), read 2026-10-26; published in *J. Geodesy* 85(8), 475–485). The series is written in-house, in the application process, with no network and no dependency. The series order is fixed in R107.a and tested against reference points.
- **Scope.** WGS84 geographic to UTM, and back, for the standard zones. Zone from longitude, `zone = floor((lon + 180) / 6) + 1`, unless the user chooses another zone, which is then checked against the 6° band. The northern limit is 84°N: EPSG:32633 is "northern hemisphere between equator and 84°N" ([epsg.io/32633](https://epsg.io/32633), read 2026-10-26). The southern limit of 80°S is the standard figure and was not read in this run. Points outside the range are refused by name. Zone exceptions in the standard grid, such as those for Norway and Svalbard, are not verified in this run. R107.a refuses them with a named reason until they are checked (proposed).
- **Constants.** Scale factor at the central meridian 0.9996; false easting 500,000 m; false northing 0 m in the north and 10,000,000 m in the south. EPSG:32633 has these values with its central meridian at 15°E ([epsg.io/32633](https://epsg.io/32633), read 2026-10-26). The southern false northing is stated by [epsg.io/32700](https://epsg.io/32700) (search summary, not opened) and by PROJ's `+south` flag (search summary); R107.a checks it with a fixture.
- **PROJ.** PROJ is MIT-style licensed ([COPYING](https://github.com/OSGeo/PROJ/blob/master/COPYING), read 2026-10-26; the page does not name the licence, and ADR 0168 §4 says MIT). Its network grids are used only when the user authorises network access, through `PROJ_NETWORK`, and the cache is a SQLite file of up to 300 MB ([network docs](https://proj.org/en/stable/usage/network.html), read 2026-10-26; the page documents PROJ 9.9). PROJ is not needed for 1.x. Revisit it for State Plane or OSGB, which need grid shifts.

### 2.3 Datums

- **WGS84.** The only datum typed in 1.x. The site, the solar study and the typed coordinates use it.
- **NAD83 (3DEP tiles).** Treated as WGS84 with a visible label and a warning. The size of the difference is not settled by the sources read. Search summaries say "roughly a metre", and one vendor argues the gap now exceeds a metre. Sources disagree, and the figure is a risk (§10).
- **ETRS89.** Not verified. Refused as a typed datum until a source is read.
- **OSGB36.** Refused. The product has no national grid transformation. The Ordnance Survey's approximate WGS84 to OSGB36 Helmert transformation carries "up to 3.5 metres (95%) both horizontally and vertically", and "Do not use it for points outside Britain" ([OS guide](https://docs.os.uk/more-than-maps/deep-dive/a-guide-to-coordinate-systems-in-great-britain/from-one-coordinate-system-to-another-geodetic-transformations/approximate-wgs84-to-osgb36-odn-transformation), read 2026-10-26; no date shown). A search summary reports a forum case where a missing shift gave errors above 90 m (secondary).

### 2.4 Precision of typed coordinates

- Six decimal places in degrees, which the entry accepts and stores in double precision, gives about 0.111 m in latitude everywhere. In longitude it gives 0.111 m at the equator and 0.072 m at 50°N (computed with a spherical metres-per-degree figure; not measured against PROJ).
- The solar limits (±90° and ±180°) are the bounds, and a value outside them is refused (`src/solar/solar.cpp:35–42`).

### 2.5 Scale

- The spherical approximation of the UTM scale, `k = 0.9996 × (1 + (cos φ · sin Δλ)² / 2)`, gives these values (computed in a stdlib script, not checked against PROJ; R107.a checks against the series):

| Position | Scale factor | Error |
|---|---|---|
| Central meridian | 0.9996 | −0.04 % |
| 180 km from the central meridian | 1.0000 | 0 |
| Zone edge (3° from the central meridian), equator | 1.00097 | +0.097 % |
| Zone edge, 30°N | 1.00063 | +0.063 % |
| Zone edge, 50°N | 1.00017 | +0.017 % |

- At the equator at a zone edge, a 1 km ground distance is about 0.97 m longer in grid coordinates. At 50°N it is about 0.17 m longer.
- **Decision.** Store grid coordinates. Do not scale model geometry by the scale factor. The site origin stays a translation (ADR 0168 §4). Grid and ground distances can be shown side by side in a later slice (owner decision 13).

### 2.6 Grid convergence and north

- The convergence is `γ ≈ atan(tan Δλ · sin φ)`, computed in the same stdlib script. At 50°N, 3° from the central meridian, it is 2.30°. At 50°N, 1° from it, it is 0.77°. At 30°N, 3° from it, it is 1.5°.
- The site origin has no rotation, so the model's +Y is grid north, not true north. This is the case without any R107 change. The convergence is shown read-only in the site panel, and the solar north can be set from it on user action. **The model is never rotated by convergence automatically.** ADR 0171's grid mode is an explicit opt-in, and this design keeps that rule (owner decision 6).

### 2.7 Vertical references

- The vertical reference of each source differs: GLO-30 uses EGM2008 (EPSG:3855), SRTM uses EGM96, and 3DEP uses NAVD88 over the conterminous US. The geoid separation between two of these varies by place. This ADR does not quantify it.
- The site origin's elevation and the terrain's Z carry a vertical-reference label (new field, schema 32). Mixing two references is refused unless the user enters an offset and a note that says so (owner decision 4).
- No geoid model is added in 1.x. The labels are the protection.

### 2.8 DEM geometry

- 3DEP is geographic (NAD83 decimal degrees), as is the GLO-30 product on Earth Engine (its CRS was not read in this run; treat it as unverified).
- A 1 arc-second post is about 30.9 m north–south and about 19.9 m east–west at 50°N (computed). A grid in degrees therefore has non-square cells in metres.
- **Decision.** The importer resamples the DEM onto a regular UTM grid at an explicit spacing, which is shown in the import dialog and in the report. It does not triangulate the post positions silently. The default spacing is the source's nominal size in metres, rounded and reported (owner decision 12).

### 2.9 Sizing against the terrain limits

The terrain limits are 250,000 triangles and 250,000 source vertices per document (`src/core/terrain_records.hpp:240–245`). A regular grid of cells uses two triangles per cell. The largest square window at full resolution, computed with the stdlib script (approximate):

| Source | Cell size | Largest window, equator | Largest window, 50°N |
|---|---|---|---|
| 1 m lidar | 1 m | 0.35 km | 0.35 km |
| 3DEP 1/3 arc-second | about 10.3 m N–S; 6.6 m E–W at 50°N | 3.6 km | 2.9 km |
| GLO-30 (1 arc-second) | about 30.9 m N–S; 19.9 m E–W at 50°N | 10.9 km | 8.8 km |
| 3 arc-second | about 92 m N–S; 59.6 m E–W at 50°N | about 33 km | about 26 km |

- A 1 km square window at GLO-30 is about 34 posts a side and about 2,200 triangles. At 3DEP 1/3 arc-second it is about 101 posts and about 20,000 triangles.
- Windows above the limit are decimated, and the report names the spacing used. There is no silent truncation.

### 2.10 Accuracy budget

| Error source | Stated figure | Probability | Status |
|---|---|---|---|
| Typed lat/long at 6 dp | 0.111 m N–S; 0.072 m E–W at 50°N | — | Computed |
| UTM scale, zone edge | +0.017 % (50°N) to +0.097 % (equator) | — | Computed; spherical |
| Grid convergence | 2.30° at 50°N, 3° from the central meridian | — | Computed; spherical |
| GLO-30 absolute vertical | < 4 m | 90% linear | Read (Data Space page) |
| GLO-30 relative vertical | < 2 m (slopes ≤ 20%); < 4 m (> 20%) | 90% point to point, 1° × 1° | Read (Data Space page) |
| GLO-30 absolute horizontal | < 6 m | 90% circular | Read (Data Space page) |
| 3DEP QL1 and QL2 absolute | RMSEz 10 cm | RMSE | Read (USGS QL page; no version) |
| 3DEP QL0 absolute | RMSEz 5 cm | RMSE | Read (USGS QL page) |
| SRTM absolute vertical | 16 m design requirement | 90% | Search summary only |
| OSGB36 Helmert approximation | up to 3.5 m | 95% | Read (OS guide); refused in 1.x anyway |
| NAD83 vs WGS84 | about 1 m | — | Search summaries disagree |
| Consumer GNSS (horizontal) | about 1.8 m median (2016 federal report card); 3 m (2007 standard) | 95% | Search summary only (secondary) |

- **The site cannot be more accurate than its source.** A typed coordinate from a phone or a handheld receiver is metres-level. The geographic record shows its provenance so that the user can see which of these applies.

### 2.11 Alignment test

"Tested source data aligns with local geometry" (the Verify line) has three parts.

1. **Round trip.** A typed WGS84 point converts to UTM and back within 1 µm, across zones 31N, 32N and 33N, a southern zone, and a zone edge. Reference values come from an external tool used only in tests (not a dependency). The reference values are themselves unverified until R107.a records them.
2. **Analytic surface.** A plane and a known slope, written as a GeoTIFF fixture, import with their elevations preserved within 1e-6 m, and each post's model position equals its source position less the origin within 1e-9 m. This is the ADR 0168 §4 precision test applied to a geographic grid.
3. **Control points.** An owner-supplied real tile is compared against a few published control heights. Each control must fall within the source's stated accuracy (for GLO-30, under 4 m at 90%; for 3DEP QL1, 10 cm RMSEz). This is the only check that proves alignment to real ground. It needs a sample the owner supplies, and it is not run in this research.

## 3. Attribution

### 3.1 Rules per source

| Source | Notice required | When it applies | Status |
|---|---|---|---|
| Copernicus GLO-30 | "© DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved". For modified data, "produced using Copernicus WorldDEM-30" before it. The no-liability sentence | On distribution or communication to the public (any export that leaves the machine); the product keeps it in every export containing GLO-30 values | Read (Data Space and Earth Engine pages); licence PDF not read |
| USGS 3DEP and SRTM | "Credit: U.S. Geological Survey" requested | Recommended in every export with USGS-derived values; not legally required by the USGS page | Read (USGS credits page); not legally required |
| OpenStreetMap or Nominatim (ODbL) | "Contains information from [database], which is made available here under the Open Database License (ODbL)" for a publicly used produced work; "© OpenStreetMap contributors" as the common form | Any publicly used output containing geocoding results | Read (ODbL §4.3; OSM copyright page); exact credit text not read |
| Open Government Licence (OS OpenData, if used) | The provider's statement; otherwise "Contains public sector information licensed under the Open Government Licence v3.0" | Any output using OGL data | Read (OGL v3); the OS statement itself not read |
| Sentinel-2 | "Contains modified Copernicus Sentinel data [year]" (secondary) | Any imagery export | Not verified against the official page |
| Google | Google Maps attribution, logo, and separation from third-party data | Any display of Google content | Read (tile policies); not used in 1.x |
| MapTiler | "© MapTiler"; printed credit | Any display | Read; not used in 1.x |

### 3.2 The source record

Each source used in a document has a record, stored with the terrain or the site, not in a library:

- provider and dataset name; licence identifier; the notice text exactly as the provider gives it; the source URL; the date the user downloaded the file; the file's SHA-256; and the transform applied (window, resampling spacing, zone, datum label).
- The record extends the existing site provenance (`SiteProvenance`: source and SHA-256) and the terrain's own source records. It is written when the file is imported, and Undo removes it with the import.
- The record is not catalog provenance, so ADR 0174's rule that catalog provenance stays out of documents does not apply. Its text is at most 256 bytes per field, the same limit as ADR 0179 uses.

### 3.3 Exports

| Export | Today | Where the credit goes (proposed) | Notes |
|---|---|---|---|
| PDF sheets | Creator and title only; a JSON report is attached (`src/io/sheet_export.cpp:809–812`) | A "Sources" line on each page that shows source-derived content, generated from the views on that page; the title block gains no field | Required when GLO-30 or 3DEP values appear on the page |
| Measured PDF and PNG raster | Creator and title (`src/io/measured_export.cpp:281`, `:315–319`) | A credit strip under the raster when source-derived geometry is in the picture | |
| Terrain or site data in the native file | Site provenance only | The source record (§3.2) | Stays with the document; saved and reopened |
| GLB (glTF) | `asset` holds version and generator (`src/integrations/glb_export.cpp:548`) | `asset.copyright` with the joined credit text. glTF 2.0 lists `copyright` as an optional asset property ([spec §3.2](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html), version 2.0.1, 2021-10-11, read 2026-10-26) | Read |
| OBJ | No header found (not verified in full) | A `#` comment line at the top of the file | Proposal; OBJ comment syntax to be checked in R107.c |
| DXF | HEADER only; no comment lines (`src/io/dxf_export.cpp:236–239`) | A group-code 999 comment line (DXF comment convention) | Proposal; the group code is not verified in this run |
| STL | `solid sketchyup` (`src/io/stl_export.cpp:55`); no metadata | No field in the format. The credit goes in the export report, not in the file | Limitation stated |
| IFC | `IfcProjectedCRS` and `IfcMapConversion` (ADR 0171 §3.6) | A property set on the site, carrying the credit text. No IFC attribute for imagery copyright was checked | Not verified against the IFC schema |
| STEP | Worker-written header; not inspected | Not decided | Open |
| Presentation batch | `manifest.json` (`src/integrations/presentation_batch.cpp:250`) | A `credits` array in the manifest; a burned-in credit on frames that show source-derived terrain (owner decision 8) | |

### 3.4 Rules

- A credit cannot be removed from an export while its source is in that export. The user can add text but cannot delete a required credit.
- A credit appears where the source-derived content appears, not only in the document.
- No provider imagery from Google, Mapbox, MapTiler or Esri is written into an export in 1.x, because the terms bar or restrict it and the product has no basemap (§1.2).

## 4. Network stance

This section applies ADR 0179's N1–N9 to R107 and names where R107 differs.

| Item | ADR 0179 | R107 |
|---|---|---|
| N1. Only on user action | Yes | **Same.** Map views fetch tiles only while the user views them, and nothing is pre-fetched. In 1.x R107 makes no tile requests at all (no basemap adapter). A geocoding request follows a click on Search. |
| N2. No telemetry | Yes | **Same.** A geocoding request sends the query text and the User-Agent. Nothing about the document is sent. |
| N3. In the app process | Yes | **Same.** The geocoding client, if R107.d is approved, uses the transport pattern of `src/integrations/provider_transport.hpp` (30 s timeout). |
| N4. No hosted bytes to a network-capable helper | Yes | **Same, and it matters more.** The DEM reader runs in the application process, not in a helper, so the DEM bytes never reach a network-capable helper. The reader is a new parser and joins the fuzzing campaign (the ten parser formats in [R083](../PR_ROADMAP.md#r083), line 1217). |
| N5. User-Agent names the application | Yes | **Same.** Nominatim requires it. |
| N6. Sign-in only where a service requires it | Yes | **Same.** Keys are stored in the OS store (ADR 0034). The product ships with no key. Each user supplies their own (owner decision 10). |
| N7. Named failure states | Yes | **Same.** Offline, 403 or 429, timeout and not found each have a name. One retry at most. Nominatim's 1 request per second is enforced in the client. |
| N8. Nothing core depends on a hosted service | Yes | **Same.** Manual coordinates, the UTM conversion, and file import need no network. |
| N9. Nothing about the document is sent | Yes | **Same.** |

**Where R107 differs from ADR 0179 (stated plainly):**

- **Caching of geocoding results (contradicts ADR 0179 §5 for search results).** ADR 0179 kept search results out of storage. Nominatim's policy requires that "results must be cached on your side." So R107.d keeps a small geocode cache: bounded at 10 MiB, least-recently-used, keyed by the query, holding the ODbL attribution, and kept out of documents and exports. This is a deliberate change, and owner decision 9 asks for it.
- **Size of the data.** A 1° GeoTIFF at 1/3 arc-second is about 445 MiB before compression. ADR 0179's 128 MiB per-download limit does not hold for this file. R107.b reads a file in windows and never loads the whole file (§9).
- **No tile cache.** ADR 0179's cache design is not used, because R107 has no basemap. The only stored copy of source data is the user's file, and the document's own terrain.
- **Viewport disclosure.** Any tile request would show the viewport to the tile operator. R107 makes no tile requests, so this is a stated risk and not a live one.
- **Keys.** ADR 0179 had no keys. Any commercial provider would need a key, which is bring-your-own-key only (owner decision 10).

## 5. Cache design

- **Tiles.** None in R107. There is no basemap adapter. If a basemap is ever reopened, it must follow its provider's caching rules (OpenStreetMap: HTTP headers or 7 days; Google: barred except its conditions; the others: unverified).
- **Imported DEM and imagery.** The user's file is the cache. The import stores the file's SHA-256, the window, the spacing and the source record (§3.2). The file is not copied into the application's data directory. If the file moves, the document keeps its record and the terrain it made, and it reports the missing file.
- **Geocode cache (R107.d only).** Keyed by the normalised query and the provider. Bounded at 10 MiB, least-recently-used. Each entry stores the attribution text and the time it was fetched. It lives in `AppDataLocation/geocode-cache/`, is cleared by a Preferences control, is never synchronised, and is never written into a document or an export. Directories are 0700 and files 0600, as in ADR 0179 §5.
- **Integrity.** Each entry is checked on read against its stored hash. A mismatch deletes the entry and the query is sent again, once.

## 6. Offline guarantees

- **O1. Modelling never needs the network.** Typed coordinates, the UTM conversion, the site origin, the solar study, the DEM import from a file, and every export work offline. The test runs under `unshare -n` (V2).
- **O2. Denied access does not block.** A refusal, a timeout or a rate limit shows a named state and changes nothing in the document (R107.d only; V1).
- **O3. No startup or background request.** The application makes no network request when it starts, when a document opens, or in the background.
- **O4. Lookup results change the document only on confirmation.** A geocoded location is applied through one undoable edit, after the user confirms it.
- **O5. Named offline state.** The site panel says "Offline: manual coordinates work" when a lookup cannot run, and the rest of the application stays usable (UX principle 5, as cited by ADR 0179 §5).
- **O6. Offline import of an imported file is always possible.** A DEM imported once remains usable with the network off, and its record stays.

## 7. Verdict and the supported subset

### 7.1 Verdict

1. **Elevation: user-mediated file import (R107.b).** 3DEP and GLO-30, read-only, from a file the user downloaded. No DEM request from the product.
2. **Imagery: reference only for 1.x.** Sentinel-2 and national orthophotos are not modelling inputs. A user-supplied orthophoto may be placed as a reference image by its corners, which needs a new placement record (R107.b2, after the DEM slice; owner decision 3).
3. **Coordinates: in-house WGS84 ↔ UTM, offline (R107.a).** No PROJ.
4. **Attribution: source records and export credits (R107.c).**
5. **Geocoding: Nominatim only, optional, held (R107.d).**
6. **No basemap or imagery adapter, and no commercial elevation or geocoding adapter.** The terms for those services bar the uses the product needs (offline, export, storage) or need a key and billing that the owner would have to sign.

### 7.2 The subset R107.b would support, if approved

- **Elevation.** GeoTIFF rasters, single band, float32 or int16 samples, uncompressed or Deflate-compressed, stripped or tiled. 3DEP 1/3 arc-second and 1 arc-second, and GLO-30 at 30 m.
- **Read in windows.** The importer reads a window the user chooses, within the triangle limit (§2.9). The window and spacing are in the report.
- **Labels.** Horizontal datum (WGS84 or NAD83 as a labelled approximation) and vertical reference (NAVD88, EGM2008, EGM96) are read from the file or chosen by the user, and recorded.
- **Report.** The source name, SHA-256, window, spacing, datum and vertical labels, the number of posts read, and any void or no-data posts.
- **Qt's TIFF plugin is not assumed.** Whether `QImageReader` reads 32-bit float TIFF samples was not verified. R107.b uses a bounded in-app reader for the subset, or a library if the owner prefers one (owner decision 3).

### 7.3 Refusals, each named

- Any GeoTIFF outside the subset: compressed formats other than Deflate, multi-band or 64-bit samples, and more than one image in a file.
- Formats: DTED, BIL, ASCII grid, LAZ and COPC point clouds, LandXML, GeoJSON, and LAS (each is a later row; ADR 0168 §1).
- Any reprojection beyond WGS84 ↔ UTM (State Plane, OSGB, ETRS89 transforms).
- OSGB36 coordinates, and any coordinate outside UTM's range.
- Any DEM whose vertical reference cannot be labelled.
- Any online request to fetch a DEM.

## 8. Verification

The Verify line asks that denied access or offline use not block modelling, that tested source data align with local geometry, and that attribution survive applicable exports.

- **V1. Denied access (R107.d).** A fixture server returns 403, 429 and a timeout. The product shows each named state, retries at most once, and changes nothing in the document.
- **V2. Offline modelling.** The full path (typed coordinates, UTM conversion, site origin, solar study, DEM import from a fixture file, and the exports) runs under `unshare -n` and succeeds.
- **V3. Alignment.** The three parts of §2.11: round trip within 1 µm, analytic surface within 1e-6 m (elevation) and 1e-9 m (position), and owner-supplied control points within the stated source accuracy.
- **V4. Coordinate conventions.** Zones 31N, 32N and 33N, a southern zone with the 10,000,000 m false northing, a zone edge, and the refusals for OSGB36 and for a point outside UTM's range.
- **V5. Vertical labels.** A mixed-reference site (NAVD88 terrain on an EGM2008 origin) is refused unless an offset is entered, and the refusal names both references.
- **V6. Attribution survives exports.** Each export that carries source-derived data contains the credit text: the PDF sheet's text layer, the measured PDF and PNG, the GLB `asset.copyright`, the batch manifest, and the OBJ and DXF comment lines once adopted. Each is checked by reading the file back.
- **V7. Parser robustness.** The GeoTIFF reader joins the R083 fuzzing campaign as an eleventh parser target, and the R083 sanitized failure matrix runs on it.
- **V8. Wording.** The user guide and SUPPORT_MATRIX say that the file was downloaded by the user, name each source's credit, and say SketchyUp is not affiliated with any provider (as ADR 0176 §8 and ADR 0179 §8, V5, require).
- **V9. Schema.** Schema 32 reads 31 and round-trips; the geographic record survives save, reopen and undo.

## 9. Budgets (proposed; R107.a and R107.b measure)

| Item | Budget | Basis |
|---|---|---|
| Network traffic, R107.a, R107.b | 0 bytes | No code path |
| Network traffic, R107.d | One request per user action, at most 1 per second | Nominatim policy |
| Request timeout | 30 s | `src/integrations/provider_transport.hpp:50` |
| Retries | One, as ADR 0179 §9 sets it | ADR 0034 (bounded task retry policy, lines 42–43) |
| Geocode response | At most 64 KiB | Proposed |
| DEM file read | Windowed; at most 64 MiB of raster held at once | Proposed; R107.b measures |
| DEM window, full resolution | At most 250,000 triangles and 250,000 vertices | `src/core/terrain_records.hpp:240–245` |
| Geocode cache | At most 10 MiB, least-recently-used | Proposed (§5) |
| Geographic record | At most 256 bytes per text field; schema 32 | §3.2; ADR 0168 §4 |
| Coordinate conversion | Round trip within 1 µm | Karney's figure is 5 nm (2011); the product target is looser (proposed) |
| Application memory during DEM import | At most 1 GiB | ADR 0176 budget table (the R082.w budget, line 211) |
| Import time, 1 km at 1 arc-second | Under 2 s on the reference machine | Proposed; R107.b measures |

## 10. Risks

1. **Provider terms change.** Google's terms were updated on 2026-10-23 and 2026-10-26 (the pricing and the policy pages). Other terms are undated. Mitigation: no adapter in 1.x (§7.1), and the rechecking step in §11.
2. **The Copernicus licence is not read in full.** Its clauses on commercial use and redistribution are unverified. Mitigation: owner decision 11 (counsel review before R107.b ships), and the notice is carried in every export.
3. **GLO-30 is a surface model.** The entry calls it a DSM. Buildings and trees are in its heights, so a pad or a terrain placed on it can carry them. Mitigation: 3DEP is bare earth and is the default for US sites. GLO-30 is labelled "surface" in the record, and the terrain report states it.
4. **Vertical reference confusion.** EGM96, EGM2008 and NAVD88 differ by place. Mitigation: labels and refusals (§2.7, V5).
5. **Datum confusion.** NAD83 and WGS84 differ by a figure the sources do not agree on, and OSGB36 needs a grid. Mitigation: labels, a warning, and a refusal (§2.3).
6. **Grid north is mistaken for true north.** A user who sets the solar north from grid convergence, or who assumes model north is true north, gets a sun off by the convergence (up to about 2.3° at 50°N, 3° from the central meridian). Mitigation: read-only display, and no automatic rotation (§2.6).
7. **Two locations disagree.** The site origin and the solar latitude and longitude are separate today. Mitigation: the optional geographic record and the mismatch warning (§2.1).
8. **The DEM reader is a new parser.** It handles untrusted files in the application process. Mitigation: windowed reads, bounded allocations, and the fuzzing target (V7).
9. **Accuracy is overstated.** GLO-30 at under 4 m (90%) and a typed point at metres are both meaningful only where stated. Mitigation: the accuracy table (§2.10) travels with the record, and no field claims more.
10. **Geocoding is misused.** Nominatim bars autocomplete and bulk use. Mitigation: a user-action-only client with 1 request per second and the cache (§4, R107.d). Autocomplete is not built.
11. **Trademark wording.** "Google", "Mapbox", "MapTiler", "Esri", "Sentinel" and "Copernicus" appear as names of sources. Mitigation: nominative use only, a non-affiliation note, and counsel review (owner decision 11).
12. **Inventory gap.** `manifold` and `cgltf` are missing from `dependencies.json`. Mitigation: fix the inventory before any slice that adds a dependency.
13. **Sources were read through a summariser.** Quotes are the tool's exact quotes and were not checked line by line. Mitigation: §11 re-reads.
14. **Specific gaps.** The Esri terms, the SRTM accuracy and licence, the NAIP status, the Sentinel attribution, NAD83 and ETRS89 sizes, and the 3DEP lidar formats are unverified. None is needed for the verdict, but each is needed before its row is built.

## 11. Implementation split

- **R107.a: offline coordinates.** The in-house WGS84 ↔ UTM series, the zone and range rules, the datum labels and refusals, the grid convergence readout, the optional geographic record on the site (schema 32), the solar "Use site location" action and the mismatch warning. Fixtures for V3 (round trip and zones) and V4. No network. Gate: owner decisions 1, 5, 6 and 7. Also fixes the `dependencies.json` inventory (risk 12).
- **R107.b: user-mediated DEM import.** The bounded GeoTIFF reader for the §7.2 subset, windowing and decimation, the vertical and datum labels, the source record, the import dialog and the report. The reader joins the fuzz campaign. Fixtures for V3 (analytic surface and control points) and V5 and V7. Gate: owner decisions 2, 3, 4, 11 and 12. Depends on R107.a.
- **R107.b2: user-supplied orthophoto as a reference image (optional, after R107.b).** A placement record for `ReferenceImage` (corners in model coordinates, from a world file or GeoTIFF tags), with the same source record. Gate: owner decision 3.
- **R107.c: attribution in exports.** The credit text in the PDF sheets (a per-page "Sources" line), the measured PDF and PNG, GLB `asset.copyright`, the batch manifest, and the OBJ and DXF comment lines. Fixtures for V6. No network. Gate: owner decision 8. Depends on R107.b for source-derived content, though the credit mechanism can land first.
- **R107.d: optional geocoding (held, not scheduled).** A Nominatim client, user action only, 1 request per second, the geocode cache, the User-Agent, and the consent text. Fixtures for V1 and O2. Gate: owner decisions 2 and 9, and R107.a to R107.c done.
- **R107.e: documentation and claims.** SCOPE C06 stays at "Investigate" until R107.d is accepted. SUPPORT_MATRIX row, USER_GUIDE page on getting DEMs from USGS and Copernicus (with the file-download step and the credits), RELEASE_NOTES if a claim is made, and the non-affiliation wording (V8). Counsel review (owner decision 11).
- **Recheck before R107.b.** Re-read the cited sentences of the Copernicus licence PDF, the Google Service Specific Terms, the Esri terms, the OS attribution wording, the NAIP status, and the SRTM data licence. Record the reads in the R107.b implementation notes.

## Owner decisions

1. **Verdict: user-mediated data import only for 1.x. No basemap, imagery or commercial adapter.** *Recommended: yes.* The terms for those services bar offline use, storage or export (§1.2), and manual coordinates already work.
2. **Optional geocoding through Nominatim, held as R107.d, not scheduled.** *Recommended: approve the design, schedule after R107.c.* It is the one online adapter the evidence supports, and the policy is clear (§1.4).
3. **Elevation and imagery scope for R107.b.** Support 3DEP and GLO-30 GeoTIFF only. Defer SRTM until its data licence is read in full. Defer orthophoto placement to R107.b2. Decide the reader: a bounded in-app reader for the §7.2 subset, which is my recommendation, or a library. *Recommended: yes to both, in-app reader.* It avoids a new dependency, and the subset is small.
4. **Vertical reference policy.** Label every site elevation and terrain Z with its vertical reference. Refuse silent mixing, allow an offset only with a note. Add no geoid model in 1.x. *Recommended: yes.*
5. **Datum policy.** WGS84 is the only typed datum. NAD83 is accepted only as a labelled approximation with a warning. OSGB36 and ETRS89 are refused until a transformation is read and approved. *Recommended: yes.*
6. **Grid north.** Show the convergence read-only. Never rotate the model by it. Allow the solar north to be set from it on user action. *Recommended: yes, consistent with ADR 0168 §4 and ADR 0171 decision 6.*
7. **Geographic record on the site, schema 32.** Store latitude, longitude, datum label and provenance, with a "Use site location" action and a mismatch warning. *Recommended: yes.* It is the only way the two locations can agree, and it needs one schema step.
8. **Export credits.** A required credit (GLO-30 notice, ODbL notice where geocoding results are publicly used) cannot be removed from an export that carries its source. Credits in the batch manifest are always written, and burned-in credits on frames are on by default. *Recommended: yes.*
9. **Geocode cache.** Keep a bounded geocode cache for R107.d, because Nominatim requires caching. It is outside documents and exports. *Recommended: yes, with R107.d only.* This is the one deliberate change from ADR 0179 §5.
10. **No product-owned keys.** The product ships with no provider key. Each user brings their own, stored in the OS store (ADR 0034). *Recommended: yes.*
11. *Replaced 2026-10-27 (no counsel): a recorded re-read of the licence texts before R107.b, and only 3DEP and GLO-30 with their credits.* **Counsel review before R107.b ships.** Covers the Copernicus licence text, the Google and Esri terms, the OS wording, the NAIP status, the nominative use of provider names, and the ODbL position for geocoding results. *Recommended: yes.*
12. **DEM windowing and decimation.** Full resolution up to the triangle limit, and decimation beyond it with the spacing reported. No silent truncation. The default spacing is the source's nominal size in metres. *Recommended: yes.*
13. **Scale.** Store grid coordinates and do not scale model geometry by the UTM scale factor. Grid and ground distances can be shown side by side in a later slice. *Recommended: yes.*

## Sources

Each source was read on 2026-10-26 unless marked otherwise. "Date shown" is the page's own date, or "no date shown". Search summaries are labelled and not relied on for a decision.

**Read directly (WebFetch), 2026-10-26**

- OpenStreetMap tile usage policy. <https://operations.osmfoundation.org/policies/tiles/>. No date shown.
- OpenStreetMap Nominatim usage policy. <https://operations.osmfoundation.org/policies/nominatim/>. No date shown.
- OpenStreetMap copyright and licence. <https://www.openstreetmap.org/copyright>. No date shown.
- Google Maps Platform, tile policies. <https://developers.google.com/maps/documentation/tile/policies>. "Last updated 2026-10-23 UTC."
- Google Maps Platform, geocoding policies. <https://developers.google.com/maps/documentation/geocoding/policies>. "Last updated 2026-10-26 UTC."
- Google Maps Platform, pricing. <https://developers.google.com/maps/billing-and-pricing/pricing>. "Last updated 2026-10-23 UTC."
- Mapbox Terms of Service. <https://www.mapbox.com/legal/tos>. "Last updated March 31, 2024."
- Mapbox pricing. <https://www.mapbox.com/pricing>. No date shown.
- MapTiler terms. <https://www.maptiler.com/terms/>. No date shown.
- MapTiler pricing. <https://www.maptiler.com/cloud/pricing/>. No date shown.
- Copernicus Data Space, Copernicus DEM collection. <https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM>. No date shown; the data availability dates are as the page gives them.
- Earth Engine catalogue, Copernicus DEM GLO-30 (2024_1). <https://developers.google.com/earth-engine/datasets/catalog/COPERNICUS_DEM_GLO30_2024_1>. Availability 2010-12-01 to 2020-11-13 as shown; release 2024_1.
- USGS, SRTM 1 arc-second. <https://www.usgs.gov/centers/eros/science/usgs-eros-archive-digital-elevation-shuttle-radar-topography-mission-srtm-1>. Page date 2018-07-30 as shown.
- USGS, copyrights and credits. <https://www.usgs.gov/information-policies-and-instructions/copyrights-and-credits>. No date shown.
- USGS, 3D Elevation Program. <https://www.usgs.gov/3d-elevation-program>. No date shown.
- USGS, topographic data quality levels. <https://www.usgs.gov/3d-elevation-program/topographic-data-quality-levels-qls>. No date shown.
- ScienceBase, 1/3 arc-second DEM (3DEP). <https://www.sciencebase.gov/catalog/item/4f70aa9fe4b058caae3f8de5>. Item publication date 2024.
- PROJ, network usage. <https://proj.org/en/stable/usage/network.html>. No date shown; documents PROJ 9.9.
- PROJ, Mercator and Web Mercator pages. <https://proj.org/en/stable/operations/projections/merc.html> and <https://proj.org/en/stable/operations/projections/webmerc.html>. No date shown.
- PROJ, UTM. <https://proj.org/en/stable/operations/projections/utm.html>. No date shown.
- PROJ, COPYING. <https://github.com/OSGeo/PROJ/blob/master/COPYING>. No date shown.
- epsg.io, EPSG:32633. <https://epsg.io/32633>. Revision 2022-12-12 as shown.
- Karney, C. F. F., "Transverse Mercator with an accuracy of a few nanometers", *J. Geodesy* 85(8), 475–485 (2011). arXiv abstract <https://arxiv.org/abs/1002.1417>, read 2026-10-26.
- Ordnance Survey, approximate WGS84 to OSGB36 transformation. <https://docs.os.uk/more-than-maps/deep-dive/a-guide-to-coordinate-systems-in-great-britain/from-one-coordinate-system-to-another-geodetic-transformations/approximate-wgs84-to-osgb36-odn-transformation>. No date shown.
- National Archives, Open Government Licence v3.0. <https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/>. Version 3.0; no date shown.
- Open Data Commons, ODbL 1.0. <https://opendatacommons.org/licenses/odbl/1-0/>. §4.3 and §4.4 read.
- Khronos, glTF 2.0 specification (version 2.0.1, 2021-10-11). <https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html>. §3.2 read; the first 100,000 characters.
- GPS.gov FAQ. <http://www.gps.gov/faq> (redirected from `https://www.gps.gov/faq/`). It refers readers to the accuracy page and gives no figure. Read 2026-10-26.

**Search summaries (secondary; not opened unless stated)**

- Copernicus DEM licence wording and "free basis" summary, from the Earth Engine and Data Space results. The licence PDF itself was not readable.
- Copernicus Product Handbook i5.0 (2022-11-29), for the accuracy sections. Not opened; its contents match the Data Space figures.
- Sentinel-2 bands, 5-day revisit and reuse wording, from Wikipedia and ESA documentation search results. The official legal page returned HTTP 404.
- NAIP public-domain status and resolution, from search results (Wikipedia; FSA; Google Earth Engine).
- Esri World Imagery item page, summarised by a search result (the official page returned only its title).
- SRTM 16 m (90%) design requirement, Rodriguez et al. 2006, from search results (not opened).
- GPS accuracy, from search results: the 2007 SPS performance standard (3 m horizontal, 5 m vertical, 95%) and the FAA WAAS report card (2016; median 1.81 m horizontal and 3.95 m vertical at 95%). The gps.gov accuracy page returned HTTP 404.
- NAD83 and WGS84 difference, from vendor and forum results. These sources disagree.
- OSGB36 errors from a missing datum shift, from a PROJ mailing-list result.
- Ordnance Survey OpenData attribution wording, from council and third-party pages. The OS acknowledgements page was not opened.
- USGS 3DEP lidar formats (LAZ, COPC). No page found.
- epsg.io/32700, for the southern-hemisphere false northing. Not opened.

**Repository** (`origin/main` at `e51f5959`, read 2026-10-26)

- ADRs: [0034](0034-openai-provider.md), [0063](0063-texture-image-decoding.md), [0164](0164-sheet-layout-interface.md), [0168](0168-terrain-import-and-surfaces.md), [0169](0169-terrain-editing-drape-stamp.md), [0171](0171-ifc-exchange-subset.md), [0174](0174-custom-styles-asset-authoring.md), [0176](0176-skp-import.md). ADR 0177 to 0180 are on their branches (`docs/r103-ruby-feasibility`, `docs/r104-dc-lc-feasibility`, `docs/r105-hosted-assets-feasibility`, `docs/r106-dwg-feasibility`), and ADR 0179 is cited for N1–N9 and §5 and §8.
- Code: `src/core/site_origin.hpp`; `src/solar/solar.hpp`, `src/solar/solar.cpp:35–42`; `src/app/solar_panel.cpp:100–149`; `src/automation/inspection.cpp:280–287`; `src/automation/terrain_commands.cpp:114`, `:2689`, `:2825–2842`, `:3096–3110`; `src/core/terrain_records.hpp:240–245`; `src/core/reference_image.hpp`; `src/io/reference_image_io.cpp`; `src/io/texture_image.cpp`; `src/io/native_format.hpp:10`; `src/io/sheet_export.cpp:809–812`; `src/io/measured_export.cpp:281`, `:315–319`; `src/integrations/glb_export.cpp:548`; `src/io/dxf_export.cpp:236–239`; `src/io/stl_export.cpp:55`; `src/integrations/presentation_batch.cpp:250`; `src/core/drawing_item_records.hpp:86–88`; `src/integrations/provider_transport.hpp:50`; `CMakeLists.txt:48–50`, `:62`; `dependencies.json`.
- Docs: `docs/PR_ROADMAP.md` R067 (line 1012), R083 (line 1217), R089 (line 1287), R095 (line 1386), R107 (line 1530); `docs/SCOPE.md` row C06 (line 132).

**Computed (stdlib arithmetic, not checked against PROJ or GeographicLib)**

- UTM scale, grid convergence, metre-per-degree cell sizes, typed-coordinate precision, Web Mercator scale (`sec φ`, from the sphere computation the PROJ page describes), and the terrain window sizes (§2.4–§2.9). The script is kept outside the repository.

## Not verified

- The Copernicus DEM licence PDF, read in full. Its commercial-use and redistribution clauses.
- The Google Maps Platform Service Specific Terms, including what they say about export and printing, and the offline pricing.
- The Esri terms, caching, export and attribution.
- Mapbox Product Terms (desktop and offline use) and Mapbox's own attribution text.
- The MapTiler caching and offline terms.
- Sentinel-2 official attribution and licence text.
- NAIP public-domain status for imagery from 2020 onward.
- SRTM elevation data licence, and SRTM vertical accuracy from a primary source.
- The Ordnance Survey acknowledgement wording and any OS licence beyond OGL v3.
- NAD83 to WGS84 and ETRS89 to WGS84 differences, from a primary source.
- The Copernicus DEM product handbook's accuracy sections, read directly.
- The 3DEP Lidar Base Specification version, and its point-cloud formats (LAZ, COPC).
- Whether the GLO-30 product's grid is geographic WGS84, read directly.
- The GeoTIFF tags, sample formats and compression that the reader needs, and whether `QImageReader` reads float TIFF.
- The IFC attribute or property that should carry imagery copyright, and the DXF group code for comments, both against their specifications.
- The OBJ comment syntax and the STEP header in this repository's writers.
- The vertical geoid difference between EGM96, EGM2008 and NAVD88 at any given site.
- The UTM and grid-convergence values against PROJ or GeographicLib. The figures here are spherical approximations.
- The accuracy of any real DEM tile against control points. This needs an owner-supplied sample and was not run.
- Google Elevation and other commercial DEM services. Not researched.
- Counsel's view on every point in §10, and owner decision 11.
