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

- **Elevation.** GeoTIFF rasters, single band, float32 or int16 samples, uncompressed, LZW-compressed or Deflate-compressed, stripped or tiled. 3DEP 1/3 arc-second and 1 arc-second, and GLO-30 at 30 m. (R107.b1.1, parent decision 2026-10-30: 3DEP is distributed LZW-compressed; owner decision 3 asks for 3DEP)
- **Images (R107.b1.2, parent decision 2026-10-30).** The reader reads the full-resolution image, which is the first image in the file. A further image marked reduced-resolution (TIFF NewSubfileType, tag 254, bit 0 set) is an overview: it is skipped, only its directory is read, and its blocks are never decoded. GDAL skips overviews by default too. A 3DEP tile as USGS distributes it holds three overviews after its full image. The reader walks at most 16 further images. Each link must lie inside the file, and a loop is refused by name. A further image that is not reduced-resolution is refused by name: a second full-resolution image, or a transparency mask (bit 2 without bit 0). The first image itself is refused by name if it is reduced-resolution or a mask.
- **Read in windows.** The importer reads a window the user chooses, within the triangle limit (§2.9). The window and spacing are in the report.
- **Labels.** Horizontal datum (WGS84 or NAD83 as a labelled approximation) and vertical reference (NAVD88, EGM2008, EGM96) are read from the file or chosen by the user, and recorded.
- **Report.** The source name, SHA-256, window, spacing, datum and vertical labels, the number of posts read, and any void or no-data posts.
- **Qt's TIFF plugin is not assumed.** Whether `QImageReader` reads 32-bit float TIFF samples was not verified. R107.b uses a bounded in-app reader for the subset, or a library if the owner prefers one (owner decision 3).

### 7.3 Refusals, each named

- Any GeoTIFF outside the subset: compressed formats other than LZW and Deflate (R107.b1.1, parent decision 2026-10-30: 3DEP is distributed LZW-compressed; owner decision 3 asks for 3DEP), multi-band or 64-bit samples, and more than one full-resolution image in a file (R107.b1.2, parent decision 2026-10-30: reduced-resolution overviews are skipped, and a second full-resolution image, a transparency mask without bit 0, a loop, a link outside the file, more than 16 further images, or a first image that is an overview or a mask is refused by name).
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

## Implementation notes (R107.a)

What R107.a built, and what it measured. Plain record of this slice; the sections above stay as the design.

**Coordinates (§2.2, §2.11 V3 and V4).** `src/geo/geographic.cpp` is an in-house Krüger series to order six, in the form Karney (2011) evaluates to nanometres. It uses no PROJ, no grid file and no network. The forward series uses Krüger's α coefficients and the inverse uses β. The first attempt used the wrong pair and was 4 m out; the reference test caught it. The reference values are Karney's published test data (Zenodo doi:10.5281/zenodo.32470, file TMcoords.dat, MD5 91b817eae34aef3cd8d67b5a4e8e798f, CC0 1.0). The fixture `tests/fixtures/geo/karney-tm-reference.txt` holds 25 published rows, with longitudes taken from the central meridian.

| Check | Result | Bound |
|---|---|---|
| Forward grid, published rows, zone 20 (63°W) | 1.9 nm (1.86e-9 m) | 5 nm (Karney 2011, within 3,900 km) |
| Forward grid, same rows in zones 31, 32 and 33 outside the exceptions | within the same 1.9 nm | 5 nm |
| Southern grid, mirrored rows in zone 56 | within the same 1.9 nm | 5 nm |
| Grid convergence against the published values | 1.3e-14 degrees | 1e-9 degrees |
| Round trip, published rows and a zone grid (zones 1, 31, 32, 33, 56, 60; latitudes 79.9°S to 83.9°N) | 3.7 nm (3.73e-9 m) | 1 µm |

- **Southern values are derived.** Karney's dataset has no southern row within 3° of a central meridian, so the southern cases mirror the northern rows (the UTM definition of the southern grid). The southern false northing of 10,000,000 m is therefore checked by construction, not against EPSG:32700. It is still unverified against that source.
- **Zone exceptions.** Norway (56° to 64°N, 3° to 12°E) and Svalbard (72° to 84°N, 0° to 42°E) are refused by name in both directions. Their boundaries here are rectangles drawn around the exception areas, not the exact zone outlines, so the refusal covers a little more ground than the real exceptions. The §2.2 note that the exceptions are unverified still holds.
- **Latitude band.** 80°S to 84°N is the standard limit, as §2.2 says. Its source was not read in this run.
- **Convergence sign.** Grid north lies γ clockwise (east) of true north. The sign was fixed by reasoning and then confirmed against the signed published values.
- **Ground distance.** `geoGroundDistance` uses the local radii at the midpoint, which is accurate for the 1 m test and for short separations. It is approximate over long distances.

**Datums (§2.3, decision 5).** WGS84 is accepted. NAD83 is accepted and warned as a labelled approximation. OSGB36 and ETRS89 are refused by name at the core and command level. The desktop dialog offers only WGS84 and NAD83, so the two refusals are reachable through the command interface, not the dialog.

**The geographic record (§2.1, decision 7).** The site origin may carry a `geographic` object, written only as schema 32 with required feature `geographic-site-v1`. Its fields are latitude, longitude, datum, UTM zone and hemisphere, the grid convergence (checked on read to 1e-6 degrees), a vertical reference and provenance (typed, geocoded or imported; imported needs a SHA-256). The record must sit at the origin's own UTM position, to 1 m (`siteGeographicToleranceMetres`). Re-anchor recomputes the record's position from the new origin in the same zone. A re-anchor that leaves the zone is refused by name.

**Schema 32 (§2.1, §9, V9).** A document without the record writes what it wrote before. A site without the record stays schema 29, and the bytes are unchanged. The schema-32 fixture `tests/fixtures/geographic-site-v32.sketchyup` reopens bit for bit. The released 1.3.0 reader (`/usr/bin/sketchyup-cli`, version 1.3.0) refuses it: exit 1, `Unsupported container field`, and the file's SHA-256 is unchanged. The schema-31 and schema-30 fixtures get the same refusal. The fuzz target `geographic-site` has four accepted and eleven rejected seeds. The seed generator was built with the dev toolchain and run, and every seed passed. The libFuzzer build was not run.

**Vertical reference (§2.7, decision 4).** §2.7 puts the vertical-reference label on the site origin's elevation. This slice puts it on the geographic record, because schema 32 is only written with that record. A site without a record has no label, as before. Mixing references and the terrain Z label belong to R107.b (V5).

**Commands and the solar study (§2.1, §2.6, decisions 6 and 7).** `site.set_geographic` and `site.clear_geographic` are one edit each. `site.describe` reports the record with its read-only convergence, and warns about NAD83. `solar.describe` reports the distance from the site location and warns beyond 1 m. In the sun panel, "Use site location" is one undoable edit. "Use grid north" is an explicit action in the sun editor that fills the north rotation with the negated convergence, and Save applies it. The sign follows from the solar rotation's clockwise convention. It was not checked against a sun path.

**Scale (decision 13).** Grid coordinates are stored. No geometry is scaled by the UTM factor. The site origin is still a translation.

**Verification run.** `geo_coordinates` (CTest), `geographic_site`, `native_format`, `style_bundle`, `terrain_io`, `component_parameter_io`, `drawing_sheet_io`, `presentation_io`, `command_tests`, `solar_command`, `inspection`, `terrain_tests`, `parser_fault_corpus`, `assistant_tool_surface`, `report_schedule`, `optional_capabilities`, `shipped_recipes` and `tool_reference_drift` all pass locally. The desktop `terrain_site_desktop` and `solar_input` binaries pass offscreen. `ci-plan.py check` and `ci-line-inventory.py check` pass.

**Gaps.** The sanitizer and libFuzzer builds ran only in CI, if at all. The full CI tier has not run for this PR. The site location dialog takes only WGS84 and NAD83 from the user. The solar sign has no sun-path check. Terrain Z labels and the mixed-reference refusal (V5) wait for R107.b. The geocoding and attribution slices are not started.

**Inventory (risk 12).** `dependencies.json` on `main` already lists Manifold and cgltf, so no change was needed here.

## Implementation notes (R107.b1)

What R107.b1 built, what it refused, and what it measured. The sections above stay as the design. The brief for this slice was the design's §2.8, §2.9, §3.2, §7.2, §7.3, §8 (V2, V3, V5, V7) and §9. Where the built slice differs from the design, the difference is named below.

**Licence reads (2026-10-30).** Owner decision 11 is replaced (no counsel). The two licences the product uses were re-read for this slice.

- **Copernicus DEM GLO-30.** The licence PDF, "Licence for Copernicus DEM instance COP-DEM-GLO-30-F Global 30m Full, Free & Open" (its body titled "Licence for the use of the Copernicus WorldDEM-30"), at `documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/DEM/resources/license/License-COPDEM-30.pdf`. The fetch tool could not read its compressed text, so the text layer was extracted with `pdftotext` from the copy the fetch saved. The PDF's metadata date is 2020-10-29; no version number appears in the text. Quoted sentences:
  - Article 2: "a Digital Surface Model providing a horizontal sampling of 30m". So the product is a surface model.
  - Article 3: "The rights granted under this Licence are worldwide and without limitation in time."
  - Article 5: "The use rights granted under this licence are free of charge to the User."
  - Article 6(a), on communicating or distributing to the General Public: "© DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved."
  - Article 6(b), for adapted or modified data: "produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved".
  - Article 6(c): the sentence "The organisations in charge of the Copernicus programme by law or by delegation do not incur any liability for any use of the Copernicus WorldDEM-30" must be added to a licence or notice on distribution or communication to the General Public.
  - Article 7: no liability for the Licensor and the Provider, and the user protects them against claims.
  - The licence does not state a commercial-use limit or a redistribution limit beyond Article 6. ADR 0181 §1.3 listed the commercial clauses as unverified; the text read here gives Article 3 and Article 5 for use and Article 6 for notices.
  - **Which notice the product uses.** The import resamples and crops the source, so the data are adapted, and the Article 6(b) notice applies. The record's credit is the 6(b) text, and the Article 6(c) sentence is the record's disclaimer.
- **USGS copyrights and credits page** (`usgs.gov/information-policies-and-instructions/copyrights-and-credits`, read 2026-10-30; no date shown). "USGS-authored or produced data and information are considered to be in the U.S. Public Domain." "When using information from USGS information products, publications, or websites, we ask that proper credit be given." "Credit can be provided by including a citation such as the following:" then "Credit: U.S. Geological Survey". The 3DEP record's credit is "Credit: U.S. Geological Survey".
- **USGS 3DEP program page** (`usgs.gov/3d-elevation-program`, read 2026-10-30; the free-of-charge sentence carries no date). "All 3DEP products are available free of charge and without use restrictions." **The page has no sentence that says all 3DEP products are public domain.** ADR 0181 §1.3 (read 2026-10-26) recorded "All 3DEP products are public domain" against this page, and that sentence is not on it. The record's licence text says what the two pages say: free of charge and without use restrictions, and USGS-produced data are U.S. Public Domain. The 2026-10-26 sentence must not be cited.

**Real files read (2026-10-30).** The headers were read by HTTP range (the first 16 KiB of each file, parsed by hand). A GLO-30 block was also decoded in memory to settle the predictor. Nothing was saved.

| File | Format | Compression | Geo keys | Vertical key | Nodata |
|---|---|---|---|---|---|
| GLO-30, N51 E000 (`copernicus-dem-30m` bucket, COG) | classic, little-endian; 2400 by 3600 posts; float32; tiles 1024 by 1024 | Deflate (8), predictor 3 | model 2, raster type 2 (pixel-is-point), GeographicType 4326 | none | none |
| 3DEP 1/3 arc-second, n35w119 (`prd-tnm` bucket) | classic; 10812 by 10812; float32; tiles 512 by 512 | **LZW (5)**, predictor 3 | model 2, raster type 1, GeographicType 4269 (NAD83) | none | -999999 |
| 3DEP 1 arc-second, n35w119 (`prd-tnm` bucket) | classic; 3612 by 3612; float32; tiles 512 by 512 | **LZW (5)**, predictor 3 | as above | none | -999999 |

- The predictor 3 convention was checked on the GLO-30 block (a little-endian file): the bytes of each row are differenced across the row, and byte plane k holds byte k of each sample, most significant plane first. Most-significant-first gave 21.5 to 161.0 m with a mean step of 0.99 m between neighbours; the other order gave values near ±10⁶ m. The reader does this.
- **GLO-30 posts are 1 arc-second north-south and 1.5 arc-seconds east-west** (pixel scale 1/2400 by 1/3600 degree). ADR 0181 §1.3 and §2.8 said 1 arc-second. The reader accepts 1.5 arc-second east-west for GLO-30 only.
- **GLO-30 is pixel-is-point** (GeoKey 1025 = 2). Pixel-is-point is supported: the tie is the centre of the first post.
- **Neither product's files carry a vertical key.** The vertical reference must therefore come from `--dem-vertical` (NAVD88 for 3DEP over the conterminous US, EGM2008 for GLO-30). ADR 0181 §2.7 allowed this ("read from the file where present, or given by option").

**Conflict with ADR 0181 §7.2 (the most important point in this record).** The subset in §7.2 is "uncompressed or Deflate-compressed". **The USGS 3DEP tiles are LZW-compressed** (both the 1/3 and the 1 arc-second, as read above). As built, LZW is refused by name, as §7.3 requires. So **a 3DEP tile as USGS distributes it is refused** until it is converted, for example with `gdal_translate -co COMPRESS=DEFLATE` or `-co COMPRESS=NONE`. Adding LZW is a small reader change (TIFF LZW with early change), but it extends the subset past §7.2 and the brief, so it is left to the owner. The refusal message says what to do.

**The reader's subset as built** (`src/io/geotiff.cpp`; Qt-free; the vendored miniz `tinfl` inflater only, `sketchyup_tinfl`).

- Classic TIFF, little- or big-endian. The first image only. BigTIFF is refused by name.
- One sample per pixel; int16 (signed, 16-bit) or float32. 64-bit, unsigned and other sample formats are refused by name.
- Compression: none (1) or Deflate (8; Adobe Deflate, 32946, decodes the same way). LZW, JPEG, PackBits, ZSTD, WebP and LERC are refused by name.
- Predictor 1; predictor 2 for int16; predictor 3 for float32 (verified above). Any other combination is refused.
- Strips or tiles. A block holds at most 4,194,304 samples (16 MiB of float32), and the file has at most 1,000,000 blocks. Every block's offset and byte count is checked against the file before it is read, and a stored size must match the uncompressed size (Deflate may be a little larger than that size, to a fixed bound).
- Georeferencing: one pixel scale and one tie point; a transformation matrix (rotated raster) is refused. The model must be geographic (a projected model is refused by name). GeographicType 4326 (WGS84) or 4269 (NAD83, a labelled approximation with a notice); 4258 (ETRS89) and 4277 (OSGB36) are refused by name, as ADR 0181 §2.3 requires; others are refused. Pixel-is-area and pixel-is-point are both supported. Vertical units other than metres (9001) are refused.
- Vertical GeoKey: 5703 is NAVD88, 3855 is EGM2008, 5773 is EGM96. Any other code is refused by name. No key means the option must give one.
- GDAL_NODATA is read as an ASCII number. A NaN or infinite post is void as well.
- Sizes: 2 to 1,000,000 posts per side.

**Post size and product rules** (`src/io/geotiff_terrain.cpp`). 3DEP is accepted at 1/3 or 1 arc-second, north-south and east-west. GLO-30 is accepted at 1 arc-second north-south and at 1 or 1.5 arc-seconds east-west. Any other post is refused by name, so a 3DEP option on a GLO-30 file is refused (the test checks this).

**Refusals by name** (each tested, and each a named message in the CLI): multi-band; 64-bit; LZW; more than one image; projected CRS; transformation matrix; OSGB36; ETRS89; a vertical reference the file does not name, when no option gives one (named as an INPUT_ERROR that says which option to give); a vertical code the subset does not know (5714 is refused); BigTIFF; a non-TIFF file; a directory outside the file. A corrupted or truncated file is refused by name or read; it never crashes (the test makes 400 random edits and truncates the file every 37 bytes).

**Window, grid and resampling** (ADR 0181 §2.8, §2.9).

- The window is in UTM metres: `--dem-window E0,N0,E1,N1` with `--dem-zone` (for example `30N`). **The design left the window's coordinate system open** (lat/lon or UTM, "whichever matches the existing site tools"). The site and the terrain origin are projected metres, so UTM was chosen. A lat/lon window is not built.
- Each window corner must convert with R107.a's conversion. A window outside the zone's 6° band or the UTM latitude band is refused by that conversion's own message (the test uses a window at about 85°N).
- The grid is regular in UTM. Its posts are at whole multiples of the spacing from the window's west and south edges. Each post is converted to WGS84 with R107.a, and its value comes from the file by **bilinear interpolation at pixel centres**.
- **Nominal post size.** The file's north-south and east-west steps are converted to metres at the window's centre with the WGS84 radii, and the larger of the two is rounded to whole metres. The same formula gives 31 m for 1 arc-second (GLO-30 and 3DEP 1 arc-second) and about 10 m for 1/3 arc-second (computed at 51.6°N; no 1/3 arc-second file was imported).
- **Spacing.** The default is the nominal post size. A spacing finer than the nominal size is refused, since it would only interpolate.
- **Decimation.** A grid whose triangles (2 per cell) exceed 250,000, or whose posts exceed 250,000, is decimated to the smallest whole-metre spacing that fits. The report names it, and the notice "spacing-decimated" says the spacing used. There is no silent truncation.
- **Void policy (chosen: refuse, do not fill).** A grid post is void when a neighbour with weight in its bilinear interpolation is void (no-data, NaN or infinite). Any void post in the window refuses the import, and the message gives the count. Holes were considered, but the existing triangulation cannot cut interior holes (its maximum-edge rule only removes triangles on the outer boundary), and a hole needs a boundary polygon per void. Filling would invent elevations (ADR 0168 owner decision 6). A window must be chosen without voids.
- **Outside the file.** A grid post outside the file's post centres refuses the window, with the count. A window is never silently clipped.
- The grid is triangulated as ordinary terrain points with the existing triangulation (ConvexHull mode, which for a full grid is the rectangle), so the terrain's regenerate and edit paths are unchanged.
- The terrain is placed relative to the site origin: local = UTM − origin. The origin is the site's, or a rounded proposed origin when the document has none, as for other sources.

**Vertical references and zones** (ADR 0181 §2.7, V5).

- The label is the file's key or the option's; a file key and a different option are refused ("the file's vertical reference is X; the option names Y").
- If the site has a geographic record: a zone or hemisphere that differs from the site's is refused ("zone-mismatch"). A vertical reference that differs from the site's (a site label other than "unknown") is refused, unless an offset and a note are given. The refusal names both references ("NAVD88 terrain, EGM2008 site"). A site with the label "unknown" is accepted with the notice "site-vertical-unlabelled".
- The offset is added to the file's heights before the origin is subtracted, and the note is stored with it. The note is required with a non-zero offset. The reader checks this, and so does the record.
- A site with no geographic record has no label, so nothing is checked and no label is compared. The terrain's label is recorded only. This is the same as ADR 0181 §2.7 ("a site without a record has no label").

**Schema step (ADR 0162 rules).** The GeoTIFF source record is stored with the terrain's provenance (`provenance.dem`), with the file's SHA-256 as the provenance digest. The record has the product, the file name (not its path), the licence and credit texts, the disclaimer, the datum, the vertical reference and its offset and note, the elevation kind (bare earth or surface), the UTM zone and hemisphere, the window, the spacing, the nominal spacing and the grid size. Each text is at most 256 bytes, and `validateTerrainDemSource` checks every field on read and on write.

- **Schema 34**, required feature **`terrain-dem-v1`**. A document with a GeoTIFF terrain is written as schema 34 (chunk encoding `json-v34`, payload version 34); a document without one is written as before (verified: a terrain without the record stays schema 29, and the texture-size documents stay 33). The container refuses the feature with the same newer-format path as schemas 30 to 33, and the decoder refuses the record in a document older than 34.
- Fixtures: the round trip is in `geotiff_tests` (a terrain built from the plane fixture is saved, reopened and compared, and its record is equal); the CLI output of a GeoTIFF import is a schema-34 file (`plane.sketchyup`, checked by hand).
- **Downgrade check.** `/usr/bin/sketchyup-cli` is the released 1.4.0. `sketchyup-cli --validate-native` on the schema-34 file exits 1 with `Unsupported container field`, and the file's SHA-256 is unchanged (`e20b7bbe…`). The current build validates the same file with exit 0.
- `docs/api/native-format-v1.json` is regenerated from `--format-capabilities`: `documentVersion` 34, `publicSchema` `sketchyup-document-v34`, and 34 added to `readDocumentVersions`. Only those three keys changed. The contract test (`native_format`) checks the shipped file against the build.
- `docs/PR_ROADMAP.md` and `dependencies.json` record the miniz use: the inflater (`miniz_tinfl.c`) is now linked into `sketchyup_io`, and the rest of miniz is not.

**Fuzzing (ADR 0181 §8 V7).** `parser_fuzz_geotiff` is registered with the other parser targets (`tests/fuzz/parser_harness.hpp`, `tests/fuzz/parser_seeds.cpp`). Its seeds are the 21 synthetic fixtures: 11 accepted and 10 refused by name.

- First run, 345,000 executions: an AddressSanitizer **heap-buffer-overflow** in `tagDoubles`. A tag typed SHORT (a pixel scale of three SHORT values) was read as doubles. The double reader now checks the tag type, as the integer reader already did. The crashing input is kept as a corpus seed.
- Second run after the fix, 9 minutes, 2,317,626 executions, no crash, no invariant failure.
- Non-finite posts are void, which the fuzzer also checks: a sampled post is finite or a named void.
- The sanitized failure matrix of R083 (ADR 0181 §8 V7, "runs on it") was not run. The campaign's other targets were not run either; only this target was.

**Measurements (ADR 0181 §9).** A GLO-30-shaped file: 2400 by 3600 posts, float32, Deflate, predictor 3, 1024 tiles, pixel-is-point, 10 MB (a smooth synthetic surface, from `make_geotiff_fixtures.py --budget`; not committed). Release build, `tests/geotiff_budget_benchmark.cpp`, three runs each, on a machine with a load average near 15 from other workers. Parse includes the file's SHA-256 and the block decodes; import is the whole `runTerrainImport`.

| Window | Posts | Spacing | Triangles | Parse | Import | Total | Raster held (peak) | Process max RSS |
|---|---|---|---|---|---|---|---|---|
| 1 km at 1 arc-second | 1,089 | 31 m | 2,048 | 16 ms | 2 ms | 18 ms | 9.1 MiB (1 block) | 35 MiB |
| 12 km, decimated | 124,609 | 34 m (nominal 31 m) | 247,808 | 74 ms | 285 ms | 0.36 s | 13.1 MiB (2 blocks) | 97 MiB |

- Budgets: 1 km at 1 arc-second under 2 s (met: 18 ms); at most 64 MiB of raster held (met: 13.1 MiB peak); at most 1 GiB of application memory (met: 97 MiB).
- The synthetic file's block sizes and compression are close to a real GLO-30 tile (1024 tiles, Deflate, predictor 3). The real tile's decode speed was not measured beyond the one block decoded above.

**Tests.** `geotiff` (CTest, `tests/geotiff_tests.cpp`) passes: the reader on every positive fixture, exact at every post; voids; every named refusal; corruption; the analytic plane at every post of a window (V3: worst position 0 m, worst elevation 2.8e-14 m); the GLO-30 record and its credit wording; the NAD83 notice; the refused windows (outside the file, finer than the post, out of the UTM band, void posts, unlabelled vertical, conflicting vertical, product post, zone, offset without a note); the V5 offset; decimation (12 km at 34 m, 247,808 triangles); a missing file; and the save-and-reopen round trip (V9). It also passes in the sanitize preset (`SKETCHYUP_SANITIZE=ON`: AddressSanitizer and UBSan, shared libraries; 12 cases, exit 0, no sanitizer report), run under the cgroup cap because ASan cannot run under `ulimit -v`.

**V2 (offline).** `unshare -n` is not permitted in this sandbox ("Operation not permitted"), so V2 was shown another way. An `LD_PRELOAD` guard (`socket`, `connect`, `getaddrinfo`, `sendto` abort the process) was tested with `getent hosts example.com`, which it stopped with exit 99. Then the GLO-30-shaped import and the save and validation of its native file ran under the guard: exit 0, no guard message. The import path has no socket or QtNetwork include. The CLI binary also links `libcrypto`; this slice does not change that link, and the import path was not checked for calls into it.

**Not done in this slice.** The desktop import dialog (the UX section comes first; roadmap R107 says so). LZW (§7.2 conflict above). Lat/lon windows. Holes for voids. The R083 sanitized failure matrix for the new target. The Google, Esri, Ordnance Survey, NAIP and SRTM rechecks in ADR 0181 §11 (those are for later rows; R107.b1 uses only the two licences above). The export credits (R107.c). The user guide and the support matrix (R107.e).

**Conflicts with ADR 0181 and ADR 0168.**

- ADR 0181 §7.2: the subset is "uncompressed or Deflate". The USGS 3DEP tiles use LZW (see above). Owner decision needed.
- ADR 0181 §1.3 and §2.8: GLO-30 posts are 1 arc-second. Its east-west step is 1.5 arc-seconds, and it is pixel-is-point.
- ADR 0181 §1.3: "All 3DEP products are public domain" was recorded against the 3DEP program page. The page does not say it (read 2026-10-30).
- ADR 0181 §2.8: the void policy and the window coordinate system were open. Chosen here: refuse voids; UTM window.
- ADR 0168 §1 deferred GeoTIFF rasters to a later row. The terrain importer now takes GeoTIFF (through `--terrain-format geotiff`, which defaults from the `.tif` extension), and the terrain record carries a source record (schema 34). The DXF and point-file paths are unchanged. ADR 0168 §2's record format is extended by `provenance.dem` only when a GeoTIFF terrain is made.
- ADR 0181 §7.3 names "unlabelled vertical" as a refusal of the DEM. As built, an unlabelled file is refused unless the option gives a label, which is the same rule (§2.7: labels "read from the file where present, or given by option").

## Implementation notes (R107.b1.1)

What R107.b1.1 built, what it refused, and what it measured. The sections above stay as the design, and §7.2 and §7.3 are amended in place. The brief was the parent's decision of 2026-10-30 to add LZW, so that owner decision 3 (3DEP) can be met.

**Why LZW.** USGS distributes 3DEP tiles LZW-compressed with predictor 3 (R107.b1 found this; the two tiles below). The R107.b1 subset refused LZW, so a 3DEP tile was refused as downloaded. LZW is now in the subset (§7.2). The reader still refuses every other compression by name, including JPEG (7).

**The decoder** (`src/io/geotiff.cpp`, `decodeLzw`; Qt-free; no new library; the repository has no vendored LZW decoder, so none was reused).

- TIFF 6.0 LZW: MSB-first codes of 9 to 12 bits, ClearCode 256, EndOfInformation 257, and the first free code 258. The early change: the width grows when the next free code is 511, 1023 or 2047.
- Output is exactly the block's expected size, which comes from the block's shape. The output buffer is never grown from the input. A stream that would write past that size is refused; so is a stream that ends short of it, and a stream that ends before its EndOfInformation code.
- Refused by name, when the block is decoded (the header of such a file is accepted): a code beyond the table; a table that would pass 4,096 codes without a ClearCode; a truncated stream; output short of the block or past it; and old-style LZW, which uses least-significant-bit codes and is detected as libtiff detects it (the stream starts 0x00 and then a byte with its low bit set).
- Stored-size bound (the Deflate check's counterpart): a compressed block may store 1.5 times its output plus 1 KiB. LZW spends at most 12 bits on a code, and each code that writes output writes at least one byte, so a legal block without redundant ClearCodes stores at most 1.5 bytes per byte of output. The 1 KiB covers the first ClearCode and the end code. Deflate's bound (a sixteenth and 1 KiB) is unchanged.
- Predictors 2 (int16) and 3 (float32) run after decoding, as for Deflate.
- Memory. The table is 4,096 entries (about 32 KiB), held for one block. The largest block (4,194,304 float32 samples) decodes to 16 MiB and stores at most 24 MiB, so with its samples it needs 56 MiB of the 64 MiB raster budget (ADR 0181 §9).

**Fixtures** (`tests/fixtures/geotiff`, from `make_geotiff_fixtures.py`).

- LZW positives: int16 with predictor 2 and float32 with predictor 3, stripped and tiled, in both byte orders (8 files, the plane surface). `noise-i16-strip-lzw-pred2-le.tif` is 96 by 96 int16 with a hash surface. Its one block (25,196 bytes) uses 1,270 nine-bit codes, 2,560 ten-bit, 5,120 eleven-bit and 9,012 twelve-bit codes, and holds five ClearCodes, one at its start and four in the middle. The plane fixtures use only nine-bit codes, so the noise fixture is what exercises the wider codes and the clear.
- Refusals: `refuse-jpeg.tif` (compression 7), and six corrupt LZW streams, each a single 64 by 64 int16 tile unless noted: `refuse-lzw-bad-code.tif`, `refuse-lzw-table-overflow.tif` (4,200 literal codes with no ClearCode), `refuse-lzw-truncated.tif`, `refuse-lzw-short.tif`, `refuse-lzw-more-output.tif` (a 32 by 32 tile, with 2,100 literal codes), and `refuse-lzw-old-style.tif`. `refuse-lzw.tif`, a zero-filled stand-in, is removed.
- `dep13-usgs-scale-f32.tif`: the 1/3 arc-second step as USGS writes it (see the post-step tolerance below).
- **How the encoder was checked.** The generator's `lzw_encode` is written from the TIFF 6.0 specification. Its streams decode back exactly under `imagecodecs` 2026.10.10 (libtiff's LZW decoder) for 18 inputs, from 0 to 200,000 bytes, random and repetitive. Every LZW fixture is read by `tifffile` 2026.10.15 and matches its analytic surface exactly. Pillow is not installed in the CI image or here, so it was not used. The CI image has no tifffile, imagecodecs or GDAL either, so both were installed in a venv under the worktree's ignored `build/` directory, for these checks only.

**The real 3DEP tiles.** Two tiles from the USGS TNM bucket (`prd-tnm`), downloaded on 2026-10-30 into the worktree's ignored `build/` directory and not committed. Both are LZW, predictor 3, 512-pixel tiles, NAD83 (GeographicType 4269), nodata -999999, pixel-is-area, with a tie at the top-left corner. The metadata extent of the 1 arc-second tile (33.998 to 35.002 N, 119.002 to 117.998 W) agrees with its tie point.

| File | Bytes | Size | Pixel scale | As distributed |
|---|---|---|---|---|
| `USGS_1_n35w119.tif` (1 arc-second) | 54,875,665 | 3612 by 3612 | 1/3600 | **Refused**: "More than one image in the file" |
| `USGS_13_n35w119.tif` (1/3 arc-second) | 454,839,551 | 10812 by 10812 | 1/10800 | **Refused**, for the same reason |

**The as-distributed refusal (not resolved here).** Each tile holds four IFDs: the full-resolution image, and three reduced-resolution overview images (NewSubfileType 1), each tiled and LZW. The reader refuses any file whose first IFD links to a second one, and ADR 0181 §7.3 lists "more than one image in a file" as a refusal. R107.b1 read only the header, so it did not see the links. Whether overviews count as images is a decision about the subset, and it was not made here. **Recommendation:** read the full-resolution image, and skip reduced-resolution subfiles (NewSubfileType bit 0), refusing any second full-resolution image. GDAL reads a file this way by default. Until the owner or parent decides, a 3DEP tile as downloaded is refused.

**Measurements on the real tiles, with the overview links removed.** For each measurement, a copy was made in which the first IFD's next-IFD pointer is zero. The image data are unchanged, so this is the reader's path for the full-resolution image. The copies are in the ignored `build/` directory and are not committed. The record's SHA-256 is the copy's.

| Window (UTM 11N) | File | Posts | Voids | Blocks decoded | Triangles | Wall time | Peak CLI RSS | Peak raster |
|---|---|---|---|---|---|---|---|---|
| 1 km, 361789.4,3818118.4 to 362789.4,3819118.4 | 1 arc-second | 33 by 33 at 31 m (1,089) | 0 | 1 | 2,048 | 0.159 s | 36.9 MiB | 2.8 MiB |
| 4 km, 366308.9,3809143.7 to 370308.9,3813143.7 | 1 arc-second | 130 by 130 at 31 m (16,900) | 0 | 4 | 33,282 | 1.44 s | 79.2 MiB | 5.8 MiB |
| 1 km, the same corners | 1/3 arc-second | 101 by 101 at 10 m (10,201) | 0 | 1 | 20,000 | 1.03 s | 59.1 MiB | 2.8 MiB |

- **Wall time includes the whole-file SHA-256** (`fileSha256`, 1 MiB reads), so it covers the whole 54.9 MB or 454.8 MB file.
- **Whole-file SHA-256 cost.** `hashlib` (OpenSSL) over the same bytes takes 0.031 s for the 1 arc-second file and 0.243 s for the 454.8 MB 1/3 arc-second file. Both digests match the CLI's `sha256` field. The product's own hash was not timed alone, so the wall times above are the measured cost. The 450 MB tile's cost, which R107.b1 left unmeasured, is about a quarter of a second.
- **Heights against tifffile.** Each post was sampled from tifffile's decode of the same file, bilinearly at its pixel centre, as the reader does. The minimum and maximum post heights agree with the CLI's reported extents to within 2.3 µm (1 km at 1 arc-second: -2.2 µm and -0.7 µm; 4 km at 1 arc-second: -0.2 µm and +1.1 µm). This comparison checks the decoded values across four blocks. The residual of about a micrometre comes from the UTM inversion in the check script, which I did not verify separately.
- **The 1/3 arc-second import** was run on the copy, not the file as distributed. It exercises the post-step tolerance below, which the real tile needed.

**Post-step tolerance** (`src/io/geotiff_terrain.cpp`). The subset's post steps were compared to their nominal values within 1e-9 relative. USGS writes its 1/3 arc-second step as `9.25925927753796e-05`, which is 2e-9 from 1/10800, so the real 1/3 arc-second tile was refused. The 1 arc-second step is 9e-10 from 1/3600, which passed by a hair. The bound is now 1e-6 relative. Any step that is not 1/10800 or 1/3600 (or 1/2400 for GLO-30) is still refused. The fixture `dep13-usgs-scale-f32.tif` and the test `USGS 1/3 arc-second step` cover it.

**Fuzzing (ADR 0181 §8 V7).** `parser_fuzz_geotiff` built with clang (RelWithDebInfo, the fuzzer and ASan and UBSan options of `docs/PARSER_FUZZING.md`), from the 36 GeoTIFF seeds: 20 accepted (the 11 earlier positives and the 9 LZW ones), 6 corrupt LZW streams that the header accepts (the harness samples them, and they are refused by name), and 10 refused at the header (including JPEG). The run lasted 661 seconds, which is over the 10 minutes the brief asks for: 8,519,954 executions, no crash, no sanitizer report, no invariant failure, and no artifact. Coverage grew from 1,173 edges (the seeds) to 1,630. Peak RSS was 584 MB under the 3 GB limit. The campaign found nothing to fix, so no crash input was kept as a seed.

**Harness change.** The six corrupt LZW streams are seeds the reader accepts at the header, so they are accepted seeds (`publish`), not rejected ones. Calling them rejected failed the seed generator's invariant. The harness samples them, and their named refusals are caught there.

**Tests.** `geotiff` (CTest, `tests/geotiff_tests.cpp`) passes in the headless build, with 16 cases: the LZW positives read exactly at every post; the noise surface; the named refusals (including JPEG); the six LZW stream refusals, named when their block is decoded; corruption (cuts and random edits) on the LZW files as well as the Deflate file; and the USGS 1/3 arc-second step. It also passes under the sanitize preset (ASan and UBSan, shared libraries), with no sanitizer report. `terrain_import`, `terrain_io` and `parser_fault_corpus` pass. `terrain_io` prints that the prior-release CLI check was not run (`SKETCHYUP_PRIOR_CLI` is not set); that check is not exercised here.

**Not done in this slice.**

- Reduced-resolution overviews are refused, so a 3DEP tile as downloaded is still refused (see above; a decision for the owner or parent).
- The R083 sanitized failure matrix was not run for this target (as for R107.b1).
- The desktop import dialog (as for R107.b1).
- Other compressions remain refused by name: JPEG, PackBits, ZSTD, WebP and LERC.
- Old-style detection reads only the first two bytes, as libtiff does. A new-style stream that starts with a literal 0 instead of ClearCode can match it. TIFF writers start with ClearCode, so none is expected to.
- GLO-30 is unchanged (Deflate, predictor 3, pixel-is-point).

**Contradictions with the ADR.**

- §7.3 refuses "more than one image in a file". The 3DEP tiles contain overview images. Stated above; a decision is needed.
- §7.2 and §2.8: the post-step bound (1e-9) refused the real 1/3 arc-second step. The ADR's own subset includes 1/3 arc-second, so this was a defect against the ADR, now fixed.

## Implementation notes (R107.b1.2)

What R107.b1.2 built, what it refused, and what it measured. The sections above stay as the design, and §7.2 and §7.3 are amended in place (marked "R107.b1.2, parent decision 2026-10-30"). The brief was the parent's decision of 2026-10-30, which delivers owner decision 3 for 3DEP: read the full-resolution image, skip the reduced-resolution overviews as GDAL does by default, and keep refusing any further image that is not reduced-resolution.

**The rule as built** (`src/io/geotiff.cpp`: `readDirectory`, `readLaterImage` and `checkFurtherImages`).

- **The first image** is refused by name if its NewSubfileType has bit 0 set (an overview) or bit 2 set (a transparency mask). The brief asked for the bit 0 check only. The bit 2 check is an addition: a mask read as elevation would give wrong heights with no error. The parent should confirm it.
- **Each later image** is walked by its directory only. The reader reads its link to the next directory and its NewSubfileType (tag 254), and no other tag value. Its blocks are never read or decoded.
  - Bit 0 set: the image is an overview. It is skipped.
  - Bit 0 clear: it is refused by name, with its number in the file and the reason. A second full-resolution image is "a second full-resolution image (NewSubfileType N, bit 0 clear)". A mask without bit 0 is "a transparency mask without the reduced-resolution bit (NewSubfileType N)".
- **The bound.** At most 16 later images are walked. The 17th link is refused by name before its directory is read.
- **Links.** A link below offset 8, or at or past the end of the file, is refused by name. A link to a directory already walked is a loop, refused by name.
- A NewSubfileType tag that is not one SHORT or LONG value is refused as malformed.

**The real 3DEP tile, as downloaded.** USGS `n35w119`, 1 arc-second, from the `prd-tnm` bucket, downloaded 2026-10-30 into the worktree's ignored `build/` and not committed. It is 54,875,665 bytes with SHA-256 `7e378d44621ce06260c81b3d6330f4e03d5f3caed9559323a959eb035130cba8`. It holds four images: the full image (3612 by 3612 posts, no NewSubfileType) and three overviews (1806, 903 and 451 posts a side, NewSubfileType 1). All four are tiled LZW. tifffile 2026.10.15 reads the same four pages.

Command, on the file unmodified:

```
sketchyup-cli --import-terrain USGS_1_n35w119.tif --dem-source 3dep \
  --dem-window 361789.4,3818118.4,362789.4,3819118.4 --dem-zone 11N \
  --dem-vertical NAVD88 --output window-1km.sketchyup
```

| Measure | Result |
|---|---|
| Outcome | `importedWithNotices`: the NAD83 datum approximation and the vertical label from the option |
| Posts read | 1,089 (33 by 33 at 31 m, the nominal size); 0 voids; 0 outside the file |
| Blocks decoded | 1 |
| Triangles | 2,048 |
| Heights (the file's heights, since the origin's elevation is 0) | 500.44 to 717.46 m |
| Peak raster held | 2,982,695 bytes (2.8 MiB) |
| Wall time | 0.160 s on the first run (the file just downloaded); 0.064 to 0.081 s on three warm runs |
| Peak RSS | 29.6 MiB (first run); 30.5 to 31.2 MiB (warm runs) |
| Native file | schema 34, 119,358 bytes; `--validate-native` reports `valid: true` |

These match the R107.b1.1 measurements of the same window on the copy with the overview links removed: 33 by 33 posts, 2,048 triangles, one block, no voids and a 2.8 MiB peak raster. The reader decodes the same full-resolution blocks as before; only the overviews are new to the walk.

**Fixtures** (`tests/fixtures/geotiff`, from `make_geotiff_fixtures.py`). The generator writes further images from its `overviews` key. The 38 committed fixtures regenerate byte for byte.

- Accepted: `plane-f32-tile-lzw-pred3-overviews-le.tif` (the plane, with two LZW overviews of 24 by 20 and 12 by 10 posts, as in the real tile); `plane-f32-tile-lzw-pred3-overviews-undecodable-le.tif` (the same, but its overview blocks are undecodable LZW, so the file is read only if no overview is decoded); `plane-f32-strip-none-overviews16-le.tif` (sixteen overviews, the bound).
- Refused: `refuse-mask-image.tif` (a mask after the full image); `refuse-ifd-loop.tif` (the overview links back to the full image); `refuse-overviews-17.tif` (seventeen overviews); `refuse-reduced-first.tif` (the first image is an overview); `refuse-mask-first.tif` (the first image is a mask).
- Kept: `refuse-two-images.tif`, a second full-resolution image. Its refusal now names image 2 and the reason.

**Tests.** `geotiff` gets the case `overviews and further images (R107.b1.2)`: the plane is exact at every post of each accepted file; the full image keeps its 1 arc-second scale and its nine tiles; the second image is named by number and reason; and a link past the end of the file is refused by name. The refusal list covers the five new refusals. The corruption sweep also edits the overview file's links, and no edit reaches a crash or an exception other than `GeoTiffRefusal`.

- Release (GCC, CLI on, desktop off): `geotiff`, `terrain_import` and `parser_fault_corpus` pass.
- The sanitize preset (AddressSanitizer and UBSan): `geotiff`, `terrain_import` and `parser_fault_corpus` pass. Running `geotiff_tests` directly reports no sanitizer message. The sanitize preset builds `geotiff_tests` only with the CLI on, so the configure adds `-DSKETCHYUP_BUILD_CLI=ON`.

**Fuzzing** (ADR 0181 §8 V7). `parser_fuzz_geotiff` was built with clang (RelWithDebInfo, `-O1 -g`, with the libFuzzer, AddressSanitizer and UBSan options of `docs/PARSER_FUZZING.md`). Its seeds are 44: the 36 from R107.b1.1 and the 8 new fixtures. The run lasted 661 seconds with `-max_len=1048576 -timeout=10 -rss_limit_mb=3072`: 8,406,500 executions, no crash, no sanitizer report, no artifact, and a peak RSS of 597 MB. Coverage grew from 1,380 to 1,873 edges and the corpus from 40 to 526 units.

**Contradictions and open points.**

- §7.3 refused "more than one image in a file". The 3DEP tiles hold overviews, so the refusal now names only a second full-resolution image or a mask. This closes the question R107.b1.1 left open.
- The first-image mask refusal (bit 2) goes beyond the parent's decision. See above.
- The sanitized failure matrix of R083 was not run for this target, as for R107.b1 and R107.b1.1.
- The full CI tier was not waited for; the fast tier is the gate for this PR.

**Not done in this slice.** The desktop import dialog, lat/lon windows and holes for voids (all as in R107.b1.1). Heights were not compared pixel by pixel with tifffile in this run. The structure of the real file was checked with tifffile, and the decode path is the one R107.b1.1 checked.

## Implementation notes (R107.b, UX design)

What this record adds: the desktop import dialog's design, [UX_DESIGN §20](UX_DESIGN.md#20-terrain-from-a-downloaded-elevation-file-desktop), and a read of the code that dialog extends. No code was changed, and nothing was measured. The figures are the ones in the R107.b1.1 and R107.b1.2 notes above. The owner's approval of §20 is the UX gate for R107.b. The owner decisions of this ADR stand unchanged.

Where the built code differs from this ADR (each is a gap §20.7 asks to close before the dialog imports a GeoTIFF):

1. **The desktop cannot read a 3DEP tile of the size the ADR names.** §9 says "windowed; at most 64 MiB of raster held at once" and §4 says the reader "never loads the whole file". That holds for the command line. The dialog takes the file as bytes, and `Window::importTerrainPath` refuses any file over 64 MiB (`src/app/window_terrain.cpp:71`). The 1/3 arc-second tile is 454,839,551 bytes.
2. **The V5 escape accepts a zero offset.** The mismatch check in `src/io/terrain_import.cpp` passes when the note is non-empty, whatever the offset. V5 says "an offset and a note". The design requires a non-zero offset.
3. **Decimation does not know the model's room.** `readGeoTiffTerrain` decimates each grid to one terrain's limit. The model's total is checked afterwards (`triangle-budget`, `src/io/terrain_import.cpp`), so a window can be refused when a coarser spacing would have fitted. §2.9 does not cover this case.
4. **A DEM import writes no geographic record.** §2.1 allows the record, with provenance "imported" and the file's SHA-256. Without it the zone and vertical checks of §2.7 never run for that site. The import also sets the site's CRS label (EPSG:326nn or EPSG:327nn, `src/automation/terrain_commands.cpp`, `importGeoTiffTerrain`) with no position behind it.
5. **The SHA-256 comes from a second pass.** `readGeoTiffTerrain` hashes the file after it reads the posts. §3.2 needs the digest of the bytes that were read, so the two can disagree if the file changes between them.
6. **Void posts are counted, not located.** Any void refuses the window, so the report's `voids` is 0 for every import. The refusal gives a count only, so the preview cannot show where the voids are. §20.8 question 3 depends on this.
7. **GeoTIFF is not a command.** `terrain.import` accepts `dxf` and `points` only (`src/automation/terrain_commands.cpp`). The command line is the only caller of `importGeoTiffTerrain`. ADR 0181 does not say whether the agent surface should get it.
8. **Reader texts cite this ADR and command-line options.** For example "Give --dem-vertical NAVD88" and "(ADR 0181 §2.8)". The dialog replaces them, and the command line keeps them.

Owner questions raised by this record are in [UX_DESIGN §20.8](UX_DESIGN.md#208-open-questions-for-the-owner). Question 5 would make the DEM import write the site record that item 4 says is missing.

## Implementation notes (R107.c1)

What R107.c1 built, what each export carries, and what it does not. The sections above stay as the design. This is the first half of §3.3 and §3.4: the data exports. The visible credits (the PDF sheet's "Sources" line, the measured PDF and PNG strip, and the burned-in credit on frames) are R107.c2.

**The notice.** Each DEM source has one notice (`src/core/source_credits.cpp`, `demSourceNotice`). It is the record's `credit`, then its `disclaimer` when the record has one. The GLO-30 notice therefore carries its no-liability sentence (Article 6(c) of the licence, and §3.1's "The no-liability sentence"), joined by a full stop. The 3DEP notice is its credit alone. Control characters become spaces, so a notice is one line. Several sources are joined with "; " (GLB), and line-based carriers write one line per notice. The GLO-30 text already contains "; " before "all rights reserved", so the join is readable but not unique. Each distinct notice is written once, in document order.

**Carriers, as built:**

| Export | Carrier | Written when | Written text (example) |
|---|---|---|---|
| GLB (`src/integrations/glb_export.cpp`) | `asset.copyright` (glTF 2.0 optional asset property, §3.3). The manifest gets `credits` | a visible DEM terrain tile emits at least one face | `Credit: U.S. Geological Survey` |
| OBJ (`src/io/obj_export.cpp`) | `#` comment lines after the first header line, before `mtllib`. The manifest gets `credits` | a DEM tile contributes faces (the OBJ writer ignores visibility, as before) | `# Credit: U.S. Geological Survey` |
| DXF (`src/io/dxf_export.cpp`) | group-code 999 comments before `0 SECTION` / `2 HEADER`. The report gets `credits` | a DEM tile's edges are written (see the conflict below: only a terrain with every height zero) | `999` / `Credit: U.S. Geological Survey` |
| STL (`src/io/stl_export.cpp`) | no field in the format. The report gets `credits` and a notice that says the credit is in the report only | a DEM tile contributes facets | report only |
| IFC (`src/io/ifc_export.cpp`) | property set `SketchyUp_SourceCredits`, property `Credits`, on the site. The report gets `credits` | the site origin was established from a DEM file (see below) | `'Credit: U.S. Geological Survey'` as an `IfcLabel` (an `IfcText` above 255 code points) |
| STEP | none | not written (see below) | none |
| Presentation batch (`src/integrations/presentation_batch.cpp`) | `credits` in `manifest.json` | a keyframe scene shows DEM terrain. Computed once when the capture is made. Each frame's GLB carries `asset.copyright` through the GLB writer | the same notices |
| CLI | prints each exporter's report, so the keys above reach the CLI output. No CLI code changed | | |
| MCP | no export tool exists (`src/automation/mcp.cpp` has no export call), so there is no MCP export report to change | | |

**Format facts checked.**

- OBJ comments. The Wavefront OBJ description on paulbourke.net (read 2026-10-30) says "Comments can appear anywhere in an .obj file", with a hash in its example. The OBJ importer reads the written file back in the test, so the comment lines do not break it.
- DXF group 999. The Autodesk group-code value table is the reference ADR 0120 cites. The search summary (2026-10-30; the page itself was not opened) says 999 is a comment string, and that its line holds commentary. ezdxf 1.4.4 (the version the oracle pins) defines `COMMENT_MARKER = 999` and reads the file. Its reader treats 999 as a comment.
- DXF risk. A search result cites a KLayout forum thread in which a DXF with code 999 stopped importing in one third-party reader. It was not reproduced here. The product's own reader (`dxf_source.cpp` skips code 999) and ezdxf read the files. The owner should know this before the 999 lines reach users.
- glTF `asset.copyright` is the spec property ADR §3.3 cites (read 2026-10-26).
- IFC property sets. The name `SketchyUp_SourceCredits` is not a `Pset_` name, so no reader can take it for a standard set. No IFC attribute for imagery copyright was checked (§3.3's note still stands).

**Conflicts with the design and the decisions in this section.**

1. **DXF carries no DEM content for real terrain.** The planar DXF writer requires every vertex at height zero within its tolerance and refuses anything else (`DXF export supports world XY only`, which ADR 0122's planar export rule gives). A DEM terrain has heights, so its DXF export is refused and no DEM geometry reaches a file. The 999 path is written only for a terrain whose every height is zero. The test builds such a terrain by zeroing the posts of a real fixture window. The §3.3 proposal of a 999 comment for DEM content therefore has nothing to carry until the planar export takes terrain. The refusal is tested.
2. **IFC writes no terrain.** The IFC export skips terrain subtrees (`Disposition::Terrain` and `TerrainTile`), so no DEM geometry is in an IFC file. The only DEM-derived content is the georeference, when the site origin was established from a DEM file (the import sets the site's provenance to the file's name and digest, and the terrain's digest is the same). The notice is written on the site in that case, and only then. A document whose site was typed, with a DEM terrain added later, gets no IFC notice. This is my reading of §3.3's "a property set on the site", and the owner should confirm it.
3. **A synthesized site.** A document with buildings and no site, whose site came from a DEM file, gets an `IfcSite` above its buildings (identity placement, so no building moves). It is added only for such documents, and only for the notice. It is counted as `synthesizedSite`, as the no-building case already was.
4. **STEP: decided, no notice.** The STEP export skips terrain bodies (`src/io/step_export.cpp`, the `Terrain` and `ReferenceImage` check) and writes no georeference, so no DEM content is in a STEP file. §3.3 asked the choice between the `FILE_DESCRIPTION` header and the export report. The answer is neither, because the rule ("a credit appears in every export that carries source-derived content") does not apply. The header was not touched, and no test was added for the STEP export. If terrain ever reaches STEP, the notice goes into the header, through the STEP writer's header, which this slice did not inspect.
5. **The GLO-30 notice includes the no-liability sentence.** §3.1 lists it as part of the notice, and the R107.b1 record stores it separately as `disclaimer`. The export writes both, so the Article 6(c) sentence is never dropped.
6. **Binary STL refuses DEM terrain.** A terrain at UTM scale, exported as binary STL (float32), breaks the one-micrometre precision bound that the STL writer applies. The test writes the terrain as ASCII. The product behaviour is unchanged and is noted here, since the STL report's credit cannot be reached in binary for such a terrain.

**Byte identity.** A document with no DEM content writes no `credits` key, no `copyright`, no comment line and no 999 line. The existing goldens and fixtures pass without regeneration: `glb_export`, `obj_export`, `dxf_export` (with the ezdxf oracle `dxf_export_real`, run against ezdxf 1.4.4), `stl_export`, `presentation_export`, and the IFC export suite `ifc_export_tests` (its goldens, its `ifc_export_tests: ok` run). No golden file was regenerated.

**Tests.** `tests/dem_export_fixture.hpp` builds a terrain from the committed fixtures (the 3DEP plane, and the GLO-30-like file, both at the tie point the R107.b1 tests use). Each export has a check that reads the file back:

- GLB: `asset.copyright` equals the 3DEP notice exactly; two 3DEP terrains and one GLO-30 terrain give the 3DEP notice once and the two notices joined; a hidden terrain gives no copyright; a scene with no terrain has none; the CLI's printed GLB manifest lists the notice.
- OBJ: the comment lines sit before `mtllib`, each notice once; the file imports back with `loadObj`.
- DXF: a real terrain is refused; a zero-height terrain writes the 999 line first, the report lists it, the file reads back, and a document without DEM writes no 999 line. `ezdxf` read a zero-height file as a probe (258 model-space entities, no audit errors, AC1032).
- STL: the report lists the notice and states that the file holds none; the file bytes have no credit text (ASCII, see conflict 6).
- Presentation: the capture's `credits()` holds the notice once; a stand-in batch's manifest lists it.
- IFC: the site carries the notice, the file checks with the helper's `--check`, and the notice reads back as a property of the site. A building with no site gets a synthesized site that carries it. A user's site carries it, and nothing is synthesized. A document without DEM writes no notice.

**Not run.** `ifcopenshell.validate` on the IFC output. The external validator in `scripts/ifc-external-validators` is not a CI gate (ADR 0171 §6.2 point 5). The helper's own `--check` ran on each file the tests wrote. The fast-tier CI build was not run here in full: only the targets above were built locally, in the CI image `sketchyup-ci-pinned:r097a-ifc` (the `ci-fast` preset with the system linker, because the image has no mold, and the `ifc` and `ifc-app` presets for IFC).

**Not done in this slice.** The visible credits (R107.c2). The STEP header (decided not to write, conflict 4). The planar DXF export of terrain (a separate decision for the owner: it would need a terrain-aware writer, which ADR 0122 does not give). Lat/lon windows and holes, as before.

## Implementation notes (R107.b3)

What this record adds: the engine side of the desktop DEM import. It closes [UX_DESIGN §20.7](UX_DESIGN.md#207-engine-and-command-gaps-the-design-found) items 1 to 7, 9 and 11, so that R107.b4 can build the dialog. Not in this record: the dialog, the report text, the Credit line, the preview overlays, the latitude and longitude entry, the default window and the "Nothing was imported." banners (all R107.b4). Measured on the committed fixtures in `tests/geotiff_tests.cpp`, and run under ASan and UBSan (`geotiff_tests`, `terrain_io_tests`).

Item by item, numbered as in §20.7:

1. **Path-based dry run and import.** `parseGeoTiffTerrainRequest` (`src/automation/terrain_commands.hpp`) reads a GeoTIFF from its path, in windows, through the same `readGeoTiffTerrain` as the command line. The dialog then calls `runTerrainImport` for the dry run and `applyTerrainImport` for the edit, as it does for DXF and point files. A GeoTIFF is never read whole, so the 64 MiB rule of `Window::importTerrainPath` does not apply to it; the rule still applies to DXF and point files. `src/app/window_terrain.cpp` is unchanged.
2. **No GeoTIFF in `terrain.import`.** The command still accepts `dxf` and `points`. MCP and the assistant have no GeoTIFF import, and this is a decision: a GeoTIFF command changes the assistant's pinned tool surface, which needs a corpus re-pin and an owner-approved live run. The desktop calls the engine directly, and the command line is the only other caller.
3. **Facts from the path.** `geoTiffFacts` (`src/io/geotiff_terrain.hpp`) reads the header with the bounded `readGeoTiffHeader` and no post. It returns the post steps (arc-seconds, and nominal metres at the extent's centre latitude), the products whose post the file has, the datum key and label, the vertical key (GeoKey 4096) and label, the compression, pixel-is-area or pixel-is-point, the latitude and longitude extent, the UTM zones the extent touches, and the image size. The extent is the centres of the first and last posts, because those are the posts the reader samples. For a pixel-is-area tile that is half a post inside the tile's outer edge (test: 47.5 and 0.5 arc-second offsets in the fixture).
4. **Decimation to the room.** `GeoTiffTerrainOptions::otherTerrainTriangles` is the room the model's other terrains leave. The spacing is the smallest whole-metre spacing from the request that fits the room and the terrain's own limits. A window is refused (`triangle-room`) only when no spacing fits, which is when the room holds fewer than two triangles or no spacing up to the window's width fits. Measured on the 12 km square of the decimation test: with the terrain limit alone it is 34 m (247,808 triangles); with 50,000 triangles of room it is 76 m (49,298 triangles); with two triangles of room it is one cell of four posts. The notice names the room when the room set the spacing ("The model's other terrains leave room for 50,000 triangles, so the spacing is 76 m, not 31 m"), and keeps the existing wording when the terrain limit did.
5. **Zero offset.** Both places now refuse it. The reader refuses any offset of zero, with or without a note (`vertical-offset-zero`): an offset is a correction, and a zero one corrects nothing. The import's mismatch check requires a non-zero offset and a note, so a mismatch with a zero offset and a note is refused (`vertical-mismatch`). The test builds that case directly, because the reader now refuses it first.
6. **Geographic record.** A GeoTIFF import that establishes the origin writes the site's geographic record (schema 32; no new schema). The record sits at the origin's own UTM position in the file's zone, so the 1 m check of `validateSiteOrigin` holds. Its datum is the file's, its vertical reference is the chosen one, and its provenance is kind `imported`, with the file's name and SHA-256. An import into a site that already exists writes nothing, so an existing record is never overwritten. An origin outside the file's zone band cannot take a record, so the import is refused (`origin-outside-zone`) rather than written without one.
7. **Digest bound to the read (choice: hash first, then re-check).** The reader records the file's size and modification time before it reads the header, takes the SHA-256 of the whole file, reads the posts, and then checks the size and modification time again. A change in between refuses the import by name (`file-changed`), and nothing is imported. A single pass was not chosen: the windowed reader skips most of the file, so a single pass would need a second, sequential reader of the whole file. The file is still read twice, as the old code read it (once for the windows, once for the digest), so the cost is unchanged. The residual risk is a rewrite that keeps both the size and the modification time; the check does not catch it. Test: `testDigestBindsTheRead` appends a byte between the digest and the read, and the import is refused.
9. **Reader texts.** Each refusal of the reader and of the window has a stable code and a plain sentence: `TerrainError::refusal()` and `sentence()`, and `GeoTiffRefusal::code()` and `sentence()`. The command line still prints the message, which cites this ADR; the dialog shows the sentence. The reader's codes are `not-tiff`, `bigtiff`, `damaged`, `compression`, `image-layout`, `bands`, `sample-type`, `projected`, `national-grid`, `coordinate-system`, `vertical-units`, `vertical-reference`, `not-georeferenced` and `unsupported`. The window's and options' are `window-zone`, `window-coordinates`, `window-order`, `window-place`, `spacing`, `spacing-fine`, `window-small`, `window-outside`, `window-void`, `triangle-room`, `vertical-note`, `vertical-note-length`, `vertical-offset-value`, `vertical-offset-zero`, `vertical-choice`, `vertical-choice-differs`, `vertical-not-chosen`, `post-size`, `file-changed` and `unreadable`. The import's are `zone-mismatch`, `vertical-mismatch`, `origin-outside-zone` and `triangle-budget`; any other refusal carries its first report kind as its code and no sentence. A block that could not be decoded during the posts used to escape as an uncoded `GeoTiffRefusal`; it is now a `damaged` refusal.
11. **Located voids.** A window refused for void posts throws `GeoTiffVoidRefusal`, a `TerrainError` with code `window-void`. It carries the count and the UTM positions (easting and northing, in metres) of the first 4,096 void posts in grid order (`geoTiffVoidsListed`). The fixture window has two void posts, both listed. The 4,096 bound is by inspection, not by a test, since no fixture has more void posts than that.

Found while doing this, not listed in §20.7:

- The decimation loop never ends for a window narrower than its spacing. `fits` needs two posts across, so with a width under the spacing (31 m at 1 arc-second) the loop raised the spacing forever. This is reachable from the command line: `--dem-window` narrower than the nominal post, with no `--dem-spacing`. Found by reading the loop, not by running the old binary. The window is now refused as `window-small`.

Where this differs from ADR 0181 and UX §20:

- **V5 is applied more strictly.** V5 says "an offset and a note". The reader now refuses a zero offset even with a note, since such a note corrects nothing.
- **§20.7 item 4 asks for the triangle and vertex room.** The model tracks triangles only, and the dialog passes only the triangle count, so the spacing uses the triangle room. The vertex limit stays the terrain's own.
- **Which extent "Fit to the file" uses.** §20.2 describes a 1 by 1 degree file. This record's extent is the posts' centres, which is a little less than the tile's outer edges. R107.b4 must choose which one the window's "Fit to the file" uses; the centres are the ones the reader can sample.
- **Command-line behaviour, changed where the old behaviour was wrong or unsafe.** (a) A GeoTIFF imported into a model that has terrains is now decimated to the room, and its dry run counts that room; before, the room was not counted when the spacing was chosen. (b) An `--origin` outside the file's zone is refused, because no record can be written for it. (c) A zero offset with a note is refused. (d) A window narrower than its spacing is refused, instead of looping. The command line's existing outputs are unchanged for every other input: `geotiff_tests`, `terrain_import_tests`, `terrain_io_tests` and `geographic_site_tests` pass without change to their expected output, except the zero-offset case that now gives its own refusal.

## Implementation notes (R107.c2)

What R107.c2 built: the visible credits of §3.3, the second half of R107.c (owner decision 8). The data exports are R107.c1 (above). The design sections above stay as they were.

**Where each credit goes.** The notice text is the shared helper's (`src/core/source_credits.cpp`). Each output places it as follows.

| Output | Placement | Example | Image or page size |
|---|---|---|---|
| PDF sheets (`src/io/sheet_export.cpp`) | A "Sources" line in the bottom page margin, below the frame, left-aligned with the frame and centred in the margin. Printing (`printSheets`) paints the same line | `Sources: Credit: U.S. Geological Survey` | Unchanged. The default margin is 10 mm |
| Measured PDF and SVG, vector (`src/io/measured_export.cpp`) | The notice as real text in the bottom page margin, outside the drawing's clip. In the PDF it is in the text layer | `Credit: U.S. Geological Survey` | Unchanged |
| Measured PDF and SVG, raster appearance | The image is composed: white outside the drawing's content rectangle, the drawing in that rectangle, and the notice painted into the image's bottom margin. The SVG's group is not clipped in this case | The notice is pixels in the image | Unchanged: the image keeps its width and height, and the strip sits inside the existing size |
| Presentation frames (`src/integrations/blender_worker.py`) | Blender's own text stamp: `render.use_stamp`, `use_stamp_note` and `stamp_note_text`, with `stamp_background` (0, 0, 0, 0.5). Blender draws the note at the top left and wraps it | The notice, burned into the frame's top rows | Unchanged. Video is encoded from these stills, so it carries the note |

**How it looks.** The line is set in DejaVu Sans, which the measured annotations use. The size starts at 2.5 mm (about 7 pt) and steps down through 2.2, 2.0, 1.8, 1.6 and 1.4 mm (about 4 pt) until the notice fits the margin's width in whole words. A notice that cannot fit at 1.4 mm is refused, never cut. The GLO-30 notice, with its no-liability sentence, fits a 10 mm margin on a 128 mm wide page (a test). The line is labelled "Sources:"; the notice's own words follow unchanged.

**Which content counts** (a rule, not a test).
- A to-scale view or a measured drawing counts a DEM terrain when one of its own lines reaches the page's content rectangle. A raster appearance counts a DEM terrain when one of its opaque surfaces reaches it. The lines are the vector credit; the surfaces are the raster credit. A terrain outside the picture adds no notice.
- A perspective (not-to-scale) view has no drawing. It counts any DEM terrain its scene shows.
- A frame counts a DEM terrain when its scene manifest lists one. The GLB writer lists `credits` only when a visible DEM terrain tile emits faces (R107.c1). This rule does not test whether the camera sees the terrain (see gaps).

**Preflight.** A page that shows DEM terrain and cannot print its line blocks the export (no opt-in, because the credit is required). `sourcesNoRoom` means the margin cannot hold the line at any size, which includes a zero margin. `sourcesOverlap` means page content (a view, an item or the title block) reaches into the bottom margin. The line never overlaps the frame, the views or the title block, because each of those must stay out of the margin.

**Reports.** Each export's report lists the notices it wrote. The PDF sheet report has a top-level `credits` and `pages[].credits`. The measured report has `credits` and `creditPlacement` (`bottomMargin` or `composedRasterMargin`). The batch manifest keeps `credits` (R107.c1), and each frame's record now carries the `credits` its scene manifest listed. A document without DEM content writes none of these keys.

**Frame method and the Blender facts checked.**
- Blender's stamp is on only for a frame whose scene manifest lists credits. Every other stamp flag stays off, so a frame without DEM content is unchanged. A handoff keeps the scene's own settings, as before.
- Blender's stamp draws the note in its own font at the top left, with a translucent box. It does not change the image size.
- Blender keeps 767 bytes of a stamp note. A probe in the CI image (Blender 5.2.2) set notes of 700, 767, 768, 769 and 1000 bytes and read each back: 700 and 767 were kept whole, and longer notes were cut to 767 bytes. The worker therefore refuses a credit note longer than 700 bytes, so a required notice is never cut short.

**Determinism evidence.** The same DEM scene rendered twice by the real worker gives identical PNG bytes (`blender_job_tests --real`, "The same DEM frame renders the same PNG bytes"). A 400 × 300 probe scene with a note rendered twice in the CI image gave the same sha256 both times (`1dbbe4d7…`). The stamp draws the same text and the stripped PNG text chunks are the same, as ADR 0173 §6 requires.

**The tests (V6).**
- Sheets: a DEM sheet's report lists the 3DEP notice once, on its page; a page without DEM names no credit; a zero margin and a crowded margin block the export. `pdftotext` finds `Sources: Credit: U.S. Geological Survey` on the page (`sheet_export_tests --pdf-real`). `qpdf --check` passes on the same file, run on the host (qpdf is not in the CI image).
- Measured: the SVG's `<text>` sits in the bottom margin and reads the notice exactly; the PDF's text layer carries it (`pdftotext`); a 1000 × 800 raster keeps its size, its drawing stays in the margins and its top margin stays white, and its bottom margin holds more than 50 dark pixels of text. Without DEM the raster is not composed and the SVG has no `<text>`.
- Frames: the real worker renders a DEM frame with the credit in its manifest and a scene without DEM without it; the DEM frame's top 24 rows hold at least 50 text-bright pixels (lightness above 180, where the scene behind is 138) and the other frame fewer than 50.

**Unchanged output.** A document with no DEM terrain writes no `credits` key, no notice text, no `<text>` element and no composed raster. The code paths for those outputs return early on an empty credit set: `paintSources` (`src/io/sheet_export.cpp`) and the measured strip (`src/io/measured_export.cpp`). The worker's stamp stays off unless a manifest lists credits. No golden or fixture file was changed or regenerated. PDF bytes are not identical from run to run in any case: the creation date is set when the file is written. For PDFs, the proof is the code path above plus the existing suites. Raster pixels and SVG bytes are the same as before for such a document.

**Limits and gaps.**
- The frame credit is a visibility rule. A DEM terrain that is visible in a camera's scene but outside its frame adds a notice to that frame. The R107.c1 rule is carried over unchanged.
- The stamp note sits at the top left of a frame and covers part of the picture. Blender places it, and the layout cannot be moved. The owner should decide whether that is acceptable.
- UX section 17 designs no control for the frame credit, so the credit is always on, as decision 8 says. No control was added. The owner may want a way to turn it off, which would need a UX decision.
- Printing a sheet paints the line too, but only the PDF output is tested.
- The batch record's `credits` key and the video output are not covered by a test of their own. The frame's stamp is covered by the real-worker test, and video is encoded from those same stills.
- The native (X11 and Wayland) suites that cover the sheet and measured dialogs were not run here, because no dialog changed. The full CI tier runs them.
- SVG is not named in §3.3 for the measured output. It shares the export path, so it gets the same line. The owner should confirm that.

**Contradictions with the design.** None of the design rules are contradicted. Two points go beyond §3.3. The raster credit does not shrink the image; it keeps the image size and uses the bottom margin. The raster and vector credits follow different content rules (a surface or a line), because a vector drawing shows only its lines.

## Implementation notes (R107.b4)

What this record adds: the desktop GeoTIFF page of the terrain import dialog, [UX_DESIGN §20](UX_DESIGN.md#20-terrain-from-a-downloaded-elevation-file-desktop) 20.1 to 20.6, built on the R107.b3 engine, which is unchanged. It closes UX §20.7 items 8, 10 and 12. The page is `src/app/terrain_import_dialog_dem.cpp`; its pure rules and texts (the footprint, the fit window, the refusal next steps, the notice wording) are `src/app/terrain_dem_window.cpp`; the dry run's struct, which both files use, is `src/app/terrain_import_dry_run.hpp`; the overlays and the summary are in `src/app/terrain_preview.cpp`. `src/app/window_terrain.cpp` reads a `.tif` from its path, not whole, and `src/app/terrain_section.cpp` adds the Credit row.

Measured on the committed fixtures in `tests/fixtures/geotiff`: the 700 by 700 plane tile (1 arc-second, near 51.5 N, 0.1 W), with a default window of 1,000 m on the file's centre, gives a 33 by 33 grid (1,089 posts) at 31 m, not decimated, and 2,048 triangles, the figures of §20.4's example. Fitted to the whole tile, the grid is decimated, and the amber line of §20.2 names both spacings. The 48 by 40 plane tile fits both products, so neither is chosen until the person chooses; a window wider than it is refused by name.

Tests: `terrain_import_dialog_tests` passes on X11 at 1× (Xvfb), on Wayland at 2× (`scripts/test-wayland.sh`) and offscreen. `native_accessibility_audit --dialogs-keyboard=import` passes all four terrain import cases, the new GeoTIFF one included. `terrain_desktop_tests`, `entity_info_input_tests`, `geotiff_tests` and `terrain_import_tests` pass under Xvfb.

Item by item, in the order of §20:

- **20.1 Entry.** The chooser's terrain filter gains "GeoTIFF elevation (*.tif, *.tiff)". A `.tif` or `.tiff` opens the dialog with GeoTIFF as its only format. The product radios are enabled only for the products the posts fit (`geoTiffFacts`): a 1 arc-second file fits both, so neither is chosen and the caption says why; a 1/3 arc-second file is 3DEP only. A file that fits neither is refused as `post-size`, in the words the reader would give.
- **20.2 Window.** The default is 1,000 m square on the site origin when the site has one (in the zone of its geographic record or of its UTM CRS label), and otherwise on the file's centre (see the differences below). The window takes corners or a centre and size in UTM, and latitude and longitude beside each point, converted in the window's zone with R107.a's series. A position outside the zone is named under the window. Fit to the file sets the corners to the file's post-centre extent, as the facts call returns it. The live grid line gives the posts, the spacing with its nominal size, decimation, the triangles and the room left in the model; the amber line of §20.2 is under the spacing when the grid is decimated.
- **20.3 Labels.** The datum reads "NAD83, labelled approximation" with ⚠ Approximation and the NAD83 caption. The vertical reference menu starts at "Choose…" (or "From the file: …", read-only, when the file names one), and Import waits for a choice. The mismatch block appears when the site's record names a different reference: it names both, and Import waits for a non-zero offset and a note. The engine's own refusal of the mismatch is shown too, and it names both references. A site with no record says its zone and reference are not checked.
- **20.4 Report.** `reportText()` has a GeoTIFF branch: the source and the post, the licence, the SHA-256 in full, the window, the spacing, the grid with its voids and outside counts, the datum, the heights with the offset, the triangles, the credit (with the no-liability line for GLO-30), the geographic record, and the notices once each in §20.4's words. Copy report and Save report use the same text and `writeReportBytes`.
- **20.5 Refusals.** Every refusal of this dialog starts "✕ Refused. Nothing was imported. Your model is unchanged.", then the reader's or the engine's plain sentence (by refusal code, never the command line's text), then numbered next steps from the table. A file-level refusal (`demFileRefusal`) greys the settings and shows Choose another file…. The DXF and point-file banners say "Refused. Nothing was imported." too (decision 6). Voids are refused and listed, up to 4,096 in the engine, and the preview shows them as crosses.
- **20.6 Keyboard and names.** The tab order follows the section's list: product radios, window radios, the window fields, Zone, Fit, Spacing, Vertical reference, Offset, Why, Site settings…, then Show report, Cancel, Import. Radio groups are one Tab stop. The accessible names are §20.6's. The preview's description gives the window, the file and the grid in words. Not run: the 200 % text-size layout.
- **§20.7 item 10 (credit).** The Terrain section shows a "Credit" row under Source for a DEM terrain: the file's name, then the credit, then the GLO-30 disclaimer, from the source record (decision 4).
- **§20.7 item 12 (overlays).** The preview draws the file's extent dashed, the window solid (the failure colour when it leaves the file), the grid as a light fill, the origin as a cross, and the void posts as crosses, each with a label.
- **Decision 5 (geographic record).** The engine writes the record when the import establishes the origin (R107.b3). The report and the outcome line say so; when the site already has its origin, the report says the site keeps its record.

Applied owner decisions: 1 (latitude and longitude in the dialog only), 2 (1,000 m default), 3 (voids refused and located), 4 (Credit line), 5 (record written and reported, never overwritten), 6 ("Nothing was imported." in every refusal of the dialog).

Where this differs from ADR 0181 and UX §20:

- **Default window with no site origin.** The brief sets the file's centre, so the window is 1,000 m on the centre. §20.2 says "Centre and size" with an empty centre, and Import waits. The brief's choice is built.
- **Fit to the file.** The brief asks for the post-centre extent the facts call returns. Its UTM bounding box is not inside the file, since a UTM rectangle's corners fall outside a footprint rotated by grid convergence. So the window is that box inset, by bisection, until every sampled point of its perimeter lies in the file, plus one metre so that rounding the spin boxes to a tenth of a metre cannot push a corner out. The fitted window is smaller than the box.
- **Refusal wording.** The banner's first words are "✕ Refused. Nothing was imported." (§20.5 and §20.6). The brief's form, which begins with "Nothing was imported.", is kept as the sentence that follows.
- **Vertical reference position.** The row sits below Spacing, where §20.6's Tab order puts it, so the reading order and the Tab order agree. §20.3 draws it in the source block.
- **User-guide link.** Left out. `docs/USER_GUIDE.md` has no anchor for the download (R107.e writes it).
- **Choose another file….** On a refused file it closes the dialog, and the window opens the chooser again. §20.5 asks for focus to move there instead.
- **Site settings….** The window passes its Site settings dialog to the import dialog, which has no viewport of its own.
- **Notice wording.** The decimation line is §20.2's. The other kinds use §20.4's wording. An engine message with no wording of its own has its ADR citation removed.
- **Keyboard audit.** `tests/native_dialog_audit.hpp` took an unchecked radio group as unreachable by Tab when no button of the group was checked. Both products fit a 1 arc-second file, so neither is checked, and Qt enters such a group at its first button. The audit now does the same. This changes a shared test helper; the import group and the existing cases pass with it.

Risks: the file's outline in the preview is drawn from the samples that fall in the window's zone, so a file that crosses a zone edge is drawn approximately. The page reads the file's header on the UI thread (bounded by the reader). Not run: the 200 % layout, a dialog screenshot, and the full tier (`full-ci`), which the parent adds.

## Implementation notes (R107.e)

What R107.e wrote, and the sources it read. Documents only: no code, and no test was run. The sections above stay as the design.

**Pages written.** The user guide page [Terrain from a downloaded elevation file](../USER_GUIDE.md#terrain-from-a-downloaded-elevation-file) (anchor `#terrain-from-a-downloaded-elevation-file`, which the dialog's "Open the user guide" link must use); the [support matrix](../SUPPORT_MATRIX.md) row "Elevation file import"; one line in the terrain-related SCOPE row (C06, class unchanged at Investigate until R107.d); one sentence in the R107 status; one line in the changelog's Unreleased section. [RELEASE_NOTES.md](../RELEASE_NOTES.md) is unchanged, as the release preparation writes it.

**Sources read (2026-10-30, WebFetch unless marked).** Each cited page loads.

- USGS 3D Elevation Program page, `https://www.usgs.gov/3d-elevation-program`. "All 3DEP products are available free of charge and without use restrictions." This is the only 3DEP licence sentence the page gives. Its "Sources/Usage: Public Domain" text is on three map items, not on 3DEP products. The sentence "All 3DEP products are public domain" (§1.3, 2026-10-26) is not on the page, and the guide does not say it.
- ScienceBase record, `https://www.sciencebase.gov/catalog/item/4f70aa9fe4b058caae3f8de5`, "1/3rd arc-second Digital Elevation Models (DEMs) - USGS National Map 3DEP Downloadable Data Collection", publication 2024. "The seamless 1/3arc-second DEM is available as pre-staged current and historical products tiled in GeoTIFF format." Its download links point to the staged-products listing and to the data delivery page.
- The National Map downloader, `https://apps.nationalmap.gov/downloader`, linked from the data delivery page (`https://www.usgs.gov/the-national-map-data-delivery`). The downloader loads with the title "TNM Downloader - The National Map". Its product and format choices were not visible to the fetch, so the guide says only "choose GeoTIFF where the page offers a choice". The staged-products listing (`https://prd-tnm.s3.amazonaws.com/...StagedProducts/Elevation/13/TIFF/`) did not render, and the guide does not cite it.
- USGS copyrights and credits, `https://www.usgs.gov/information-policies-and-instructions/copyrights-and-credits`. "USGS-authored or produced data and information are considered to be in the U.S. Public Domain." "When using information from USGS information products, publications, or websites, we ask that proper credit be given." Credit line: "Credit: U.S. Geological Survey".
- Copernicus Data Space collection page for COP-DEM, `https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM`. It names the products COP-DEM_GLO-30-DGED and COP-DEM_GLO-30-DTED, names GeoTIFF among the formats, says that registration is needed to download the 30 m and 90 m instances, and gives the notice for distribution to the public. It does not say which product has which format, so the guide names both products and does not pair them with a format. The sample types that the fetch summary gave (DGED float32, DTED 16-bit signed) were not stated on the page itself, and the guide does not use them.
- Data Space browser, `https://browser.dataspace.copernicus.eu/`, linked from the collection page. The fetch returned only the title "Copernicus Browser". Whether it needs sign-in was not confirmed, and the guide relies on the collection page's registration statement.
- The GLO-30 licence, "Licence for Copernicus DEM instance COP-DEM-GLO-30-F", `https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/DEM/resources/license/License-COPDEM-30.pdf`. The fetch returned the binary only, so its text was extracted with `pdftotext` from the saved copy, to standard output. Article 6(a) gives the notice for distribution to the public, 6(b) the notice for adapted or modified data (the import's case: "produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved"), 6(c) the no-liability sentence, and 6(d) the rule against suggesting official endorsement. The commercial-use clauses are as R107.b1 recorded them. Counsel's view is not sought (owner decision 11, replaced).

**Wording decisions.**

- The guide's credit for 3DEP is the requested credit and the licence text the USGS pages give. It does not state a public-domain status for 3DEP products.
- The guide's credit for GLO-30 is the 6(b) notice, with the 6(c) sentence as its disclaimer, as R107.c1 writes them.
- The guide's download steps name the products, the GeoTIFF file type and the pages above. It gives no URL that was not read.
- Measured numbers in the guide: a 1 km window from the 1 arc-second tile took 0.16 s with the file's SHA-256 (R107.b1.2, the file as downloaded). A one-degree 1/3 arc-second tile is about 434 MiB and a one-degree 1 arc-second tile about 52 MiB (the two tiles' byte counts in R107.b1.1 and R107.b1.2).
- The support-matrix row states the 1/3 arc-second tile only with its overview links removed, as R107.b1.1 measured it, and the 1 arc-second tile as downloaded, as R107.b1.2 measured it. GLO-30 is stated as tested with synthetic files and one decoded block.

**Contradictions and gaps.**

- **The brief's "R107.a to R107.c are delivered" is not true on main.** R107.c2 (PR #535) and R107.b4 (PR #536, the dialog) are open. The SCOPE sentence says what is merged: R107.a, R107.b1 to R107.b3 and R107.c1.
- **Dialog and §20.** §20.2 gives the corner latitudes and longitudes as read-only. §20.8 question 1, approved on 2026-10-30 with the other five answers, lets the dialog's Centre and Corners take latitude and longitude as well as UTM. The guide follows §20.8. The owner should confirm the two read together.
- **The guide describes the dialog as §20 specifies it.** Until PR #536 merges, the dialog's behaviour in the guide is design, not shipped code. The page must not ship before the dialog's link (the "Open the user guide" button) and its anchor agree with it.
- **Visible credits.** The guide says a visible credit line appears on sheets, measured drawings and frames. That is R107.c2, which is not merged. The guide's credit section is true only once PR #535 merges. The change must not ship before it.
- **1 arc-second download route.** The guide sends 1 arc-second users to the National Map downloader. The fetch did not show the downloader's product list, so the route to a 1 arc-second GeoTIFF was not confirmed from a page. The 1 arc-second file that R107.b1 read did come from the National Map bucket.
- **Not checked.** The 3DEP bare-earth statement (§1.3) was not re-read and is not in the guide. The GLO-30 product-to-format pairing is not stated.

## Implementation notes (R107.b4 link)

PR #538. The GeoTIFF page of the terrain import dialog has the "Open the user guide" button that UX §20.1 asks for. UX §20.7 item 13 is closed.

- **Nothing opened the guide before.** A search of `src/app` found no reference to `USER_GUIDE`, no Help menu, and no `QDesktopServices` call for a document page. The nearest patterns are `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` for output files (`presentation_results.cpp`, `render_jobs.cpp`) and the lookup of installed resources beside the binary (`components_tab.cpp`, `assemblyFolder`). The package installs the guide at `share/doc/sketchyup/USER_GUIDE.md` (the `install(FILES docs/USER_GUIDE.md ...)` line in `CMakeLists.txt`), and the application binary goes to `bin/`.
- **Mechanism.** `TerrainImportDialog::Setup` takes `openUserGuide`, a callback like `openSiteSettings`. The dialog builds `file://…/share/doc/sketchyup/USER_GUIDE.md#terrain-from-a-downloaded-elevation-file` from `applicationDirPath()/../share/doc/sketchyup`, and `Window::importTerrainPath` opens it with `QDesktopServices::openUrl`. No network address is used, and the product sends no request.
- **Test.** `geotiffUserGuide` in `tests/terrain_import_dialog_tests.cpp`. The button is named and enabled. Tab from the product radios reaches it, and the next Tab reaches the checked window radio, which is Centre and size because the demo site has no origin. Space and a click each request the file and the anchor once. A refused open leaves its message in the footer.
- **Measured.** `terrain_import_dialog_tests` passes on X11 at 1× and on Wayland at 2×. `native_accessibility_audit --dialogs-keyboard=import` passes on X11 at 2× and on Wayland at 1× with no findings, and `terrain-import-geotiff` passes on both.
- **Contradiction with UX §20.1.** The section says the button "opens the guide page in the system browser". The installed guide is Markdown, and no HTML copy is installed. A desktop's default viewer for `.md` may not be a browser and may not scroll to the anchor. This PR does not fix that. Owner decision needed: (a) install a rendered HTML copy and open that, or (b) open the guide's page on GitHub, which the browser requests, not the product. Until then the link opens the file, and the section may not be shown.
