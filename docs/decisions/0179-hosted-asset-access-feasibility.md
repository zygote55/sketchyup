# 0179 — Hosted asset access: feasibility

**Status: Accepted (2026-10-27).** The owner approved decisions 1, 2, 4, 5, 9 and 10 as recommended; approved decisions 3 and 7 as design only (R105.b and R105.c are not scheduled before 1.x); deferred decision 6 until a hosted adapter exists (a user-mediated download carries no provenance, so the refusal cannot be enforced yet); and decided decision 8 without counsel: the owner does not plan to consult counsel, so no product, feature or package name contains "SketchUp" or a Trimble mark, services are named only to refer to them, and every such mention carries the non-affiliation note. Originally proposed 2026-10-26. This is the feasibility design for [R105](../PR_ROADMAP.md#r105) ("Investigate supported hosted asset access"), scope row C04, which [SCOPE](../SCOPE.md) keeps at "Investigate". It is research and design only: no product code, no dependency, no account, no credential and no network call from the product. No undocumented endpoint was called. **Verdict: user-mediated downloads only for 1.x. There is no 3D Warehouse adapter. One documented CC0 source, Poly Haven, is viable and is held for an owner decision; it is not scheduled.** The owner decides on that verdict and on the open questions in [Owner decisions](#owner-decisions). ADR 0174 leaves hosted catalogs to C04. ADR 0034's rule that credentials live in the OS store stands. ADR 0160's remote-provider scope stands.

## Verdict in brief

- **3D Warehouse has no documented API for search or download.** The Trimble developer hub lists SketchUp extension APIs and a Desktop SDK "available by request". It lists no model-library API. A 2019 forum reply says "there does not (yet) exist a public API for 3D Warehouse". Forum replies are not Trimble statements, and no later source contradicts that.
- **The Warehouse terms bar an adapter as well.** They allow use "to view, search, locate, download, and Distribute Models". They also bar "Scraping information from 3D Warehouse" and any load that is "unreasonable or disproportionately large". A program that searches and downloads falls under those rules unless Trimble authorises it in writing.
- **The user-mediated path works, and it is the honest answer for 1.x.** The user downloads in a browser and imports the file. Each format has a loss report. For Warehouse files that means `.skp` import, which is not released yet (ADR 0176, SUPPORT_MATRIX row 74). The Warehouse's COLLADA (`.dae`) fallback has no importer.
- **Poly Haven is the one documented hosted source that fits.** It has a public API with no key (`GET /assets`, `GET /files/{id}`). Its assets are CC0. Its API page says the API is free "for everyone - including commercial use". It needs a User-Agent header and a visible "assets came from Poly Haven" credit. If its models are glTF, as a 2021 search summary says and I could not confirm, they import today (ADR 0111). It is the candidate for R105.b, gated by owner decisions 3 and 4. I did not measure what share of its catalogue is 3D models.
- **Sketchfab is documented but gated.** Downloads need the end user's own Sketchfab login (OAuth 2.0). Licences are per model. The author can change a model's licence at any time, and attribution must stay with the asset. Deferred (R105.d).
- **ambientCG and BIMobject are not recommended now.** ambientCG is CC0 with a documented API menu, but its pages do not say what the endpoints return. BIMobject's APIs are available only on application, and its FAQ states no licence for downloaded objects.

## Context

- **What exists today (native).**
  - [ADR 0176](0176-skp-import.md): `.skp` import, import only, through the MIT OpenSKP reader in `sketchyup-skp-worker`. Accepted 2026-10-25. Unreleased (SUPPORT_MATRIX row 74).
  - [ADR 0111](0111-native-gltf-import-workflow.md) and [ADR 0109](0109-bounded-gltf-packages.md): glTF and GLB import with a native loss report. ADR 0109 says "No parser filesystem callback or network loader is enabled", and external resources are refused.
  - OBJ/MTL ([ADR 0115](0115-native-obj-interchange-workflow.md)), STL ([ADR 0119](0119-native-stl-interchange-workflow.md)), IFC ([ADR 0171](0171-ifc-exchange-subset.md)) and STEP (SUPPORT_MATRIX row 73). All import with a report.
  - The local library: [ADR 0129](0129-self-contained-template-bundles.md), [ADR 0130](0130-contained-component-bundles.md) and [ADR 0131](0131-atomic-library-insertion.md). `.sketchylib` single-file bundles with SHA-256 checks. "A hash match alone never substitutes a resource." "Missing referenced assets reject."
  - The R100 catalog: [ADR 0174](0174-custom-styles-asset-authoring.md) §4. A `.sketchycat` has `license` (text plus a licence identifier), `author`, `version` and a SHA-256 per asset. §11 leaves "remote or hosted catalogs, authentication or automatic synchronization" to C04. Catalog provenance is not persisted in the document.
  - The network surface today: the assistant providers only (§6).
- **The R105 Build line:** "Keep the local library independent."

**Alternatives considered:**

| Route | Access | Verdict |
|---|---|---|
| 3D Warehouse adapter (search and download through an API) | No documented API (§1.2) | Rejected: no API, and the terms bar automated access |
| 3D Warehouse in an embedded web view, intercepting the download (a suggestion in a forum thread, 2019) | Scraping of the site | Rejected: "Scraping information from 3D Warehouse" is barred, and intercepting downloads is the same act |
| 3D Warehouse, user-mediated download | A browser, with the user as the actor | **Chosen for 1.x (R105.a)** |
| Poly Haven API | Documented, no key | Viable; held for owner decision (R105.b) |
| Sketchfab Download API | Documented, per-user OAuth | Viable in principle; deferred (R105.d) |
| ambientCG | Documented API menu; CC0 | Not recommended now |
| BIMobject | Application-only APIs; no public reference read | Not recommended now |
| Bundle a CC0 set in the installer (no network) | None | Not evaluated. ADR 0174 allows CC0 textures "whose licence and source are recorded per asset", and "Textures whose licence is not verified are not bundled" (lines 217–221, §3). Noted as a no-network alternative |

## 1. The services compared

### 1.1 Method

- **Sources.** Official pages from help.sketchup.com, developer.sketchup.com, trimble.com, sketchfab.com, polyhaven.com, docs.ambientcg.com and developer.bimobject.com. Community forum threads, labelled as such. Search summaries, labelled and not relied on. Every page was read on 2026-10-26. Each citation gives the date the page shows, or "no date shown".
- **How pages were read.** Each page went through a summarising reader. Quotes are the reader's exact quotes from the page. I did not check each line against the live page. Section 11 of this ADR asks for a re-read of the cited sentences before R105.a or R105.b is written.
- **Undocumented endpoints.** No HTTP request was made to any hosted service during this research. Endpoints named below are names from documentation pages, not observed responses. Where a source only said that an internal endpoint exists, I record that and nothing more.

### 1.2 3D Warehouse

- **API.** The Trimble developer docs (`developer.trimble.com/docs`, redirected to `www.trimble.com/en/developer/docs`, read 2026-10-26) list 25 entries in the visible part of the page. The SketchUp entry says "Create extensions that add functionality and custom capabilities" and "Make importers and exporters that can read and write the .skp file format". No 3D Warehouse or model-library API is listed. Trimble Identity is listed for authenticating users and apps.
- **SketchUp developer hub** ([developer.sketchup.com](https://developer.sketchup.com/), read 2026-10-26). It offers the Ruby API, the Extension Warehouse (which distributes extensions, not models) and a "Desktop SDK: Available by request ... which allows you to read and write SKP files." It names no 3D Warehouse, model search or model download API.
- **Forum evidence (not Trimble statements).**
  - [Integrate Warehouse using APIs](https://forums.sketchup.com/t/integrate-warehouse-using-apis/98593), read 2026-10-26. A reply on July 9, 2019 says "there does not (yet) exist a public API for 3D Warehouse (although often requested!)". The same reply says an internal data structure "is not officially released for public usage" and that using it without authorisation could breach the Terms of Service. I record that such a structure exists, and I did not call it.
  - [3D Warehouse API](https://forums.sketchup.com/t/3d-warehouse-api/47436), read 2026-10-26. Replies from June 2017 describe manual bulk uploads. None says whether an API exists.
  - [3Dwarehouse API](https://forums.sketchup.com/t/3dwarehouse-api/201774), read 2026-10-26. A reply on June 23, 2022 says "If it's not documented, it's probably not a public API."
- **Conclusion.** No documented, supported API exists for search or download.
- **Terms of Use** (the embed copy, [tos](https://embed-3dwarehouse-classic.sketchup.com/tos/), "Last updated: April 1, 2017"; the canonical page at [3dwarehouse.sketchup.com/tos](https://3dwarehouse.sketchup.com/tos/) returned only its title):
  - Use: "you may use 3D Warehouse to view, search, locate, download, and Distribute Models."
  - Conduct: "Scraping information from 3D Warehouse;" and "Taking any action that imposes an unreasonable or disproportionately large load on 3D Warehouse infrastructure."
  - "Redistribute or transfer 3D Warehouse Materials to another person except as expressly authorized."
  - Other uses: "you must submit a request to Trimble at 3dwarehouse-tou@sketchup.com."
  - The text mentions no API, bot or crawler. It says "Models ... are licensed, not sold". It says Trimble "does not endorse, guarantee or provide a warranty for the third party 3D Warehouse Materials".
  - General Model License (on the same page; "Last updated: March 18, 2015"): a "limited, worldwide, royalty-free, non-transferable and non-exclusive license". Derivative works are allowed. "Distributing Models, Creations, and Combined Works to third parties for your business purposes (including for commercial purposes)" is allowed. A combined work needs "substantial additional content". Barred: "sell, offer to sell, make or distribute any individual Model on a standalone basis", "use ... in a manner that competes with 3D Warehouse", and "aggregate any content (including Models) obtained from 3D Warehouse for redistribution".
- **Terms FAQ** ([3D Warehouse Terms of Use FAQ](https://help.sketchup.com/en/3d-warehouse/3d-warehouse-terms-use-faq), "Last updated: Aug 21, 2026"; I read the first 100,000 of 153,004 characters):
  - Uploaders "retain all intellectual property rights ... in models you create and upload". Downloaders receive "a royalty-free license to each user and may not charge a fee to download a model at this time."
  - "You can use models from 3D Warehouse to populate construction scenes."
  - "Incorporating models into a website for members or into a published research project: 'No, this is impermissible aggregation.'"
  - "Selling designs that include models: 'As long as the models you downloaded from 3D Warehouse are not transferred or sold as stand-alone items.'"
  - Printing is allowed, but "the commercial sale of exact, physical reproductions of models is not permitted."
  - The excerpt I read does not address attribution or use inside third-party software.
- **Download help** ([Downloading models in 3D Warehouse: file formats](https://help.sketchup.com/en/3d-warehouse/downloading-model-file-formats), "Last updated: Aug 21, 2026" as fetched; a search summary said Jul 1, 2026, and I used the fetched date):
  - Downloadable files are "the current version, the previous two versions, and the version the model was originally uploaded in."
  - "2017 and 2018 models ... unless you are the uploader of that model."
  - "If you are using an older version of SketchUp, you can still download models in a COLLADA (.DAE) format."
  - "After the download completes, the file may be in a ZIP file format and will need to be extracted."
  - The Download button offers format choices, but they are in a screenshot and not named in the text.

### 1.3 Alternatives

**Sketchfab**

- **Download API** ([guidelines](https://sketchfab.com/developers/download-api/guidelines), read 2026-10-26; the api.sketchfab.com address redirects here; no date shown, © 2026 footer). "Downloading models requires users to be authenticated with a Sketchfab account." End users "must be able to log in or create a Sketchfab account inside your application." For downloads without end-user login, "contact us."
- **Attribution.** "Your app should clearly display the license of the model as well as author attribution ... the creator's username and a link to the Sketchfab model." "Your app must clearly mention that downloadable models are provided by Sketchfab." "The Creative Commons license and attribution must follow the asset everywhere it is used."
- **Formats** ([Download API](https://sketchfab.com/developers/download-api), read 2026-10-26): "Models are available in glTF, GLB, and USDZ formats." FBX and OBJ are not available through the API.
- **Licence per model** ([Licences for downloadable models](https://help.sketchfab.com/en/articles/16152220), read 2026-10-26, no date shown). The Standard licence allows "all types of use", including derivative works. The Creative Commons choices are CC BY, CC BY-NC, CC BY-ND and CC BY-SA. "You can change the license at any time from the model page." ND and SA are "mutually exclusive".
- **Not stated** on the guidelines or the Download API page: rate limits, caching, storage, offline use, and whether models may only be displayed. The page shows no date.
- **Conflicts.** The guidelines say "more than 700,000" models are CC-licensed. A search summary of the landing page says "over 1 million" and was not opened. A search summary calls the Download API a beta extension of Data API v3. The page I read does not say so. The fetch of `sketchfab.com/licenses` returned an unrelated licence page, so I did not use it.

**Poly Haven**

- **API** ([Poly Haven API](https://polyhaven.com/our-api), "18 July 2026" as shown; read 2026-10-26):
  - Base address `https://api.polyhaven.com`. "no login or API key required for the default endpoints." The endpoints named are `GET /assets` (list and metadata) and `GET /files/{id}` (download links).
  - "All requests to the API require a unique User-Agent header."
  - "The Poly Haven API is now free for everyone - including commercial use." "There's no premium tier, no revenue share, and no login or API key required for the default endpoints."
  - "If you use the live API in your product, website, or service ... make it clear to your users that the assets came from Poly Haven."
  - "We primarily maintain the API for our own purposes and may occasionally change endpoints and authentication methods."
  - "If you are integrating our API in your tool on the user side, please contact us."
- **Corporate page** ([corporate](https://polyhaven.com/corporate), no date shown; read 2026-10-26): "There is no license to arrange and no fee for access." "Our live API is free for everyone, for any purpose, including commercial use - forever."
- **Public API README** ([Public-API](https://github.com/Poly-Haven/Public-API), no date shown; read 2026-10-26): "Free to use for any purpose, personal or commercial, forever." Building on the live API "requires a small 'Powered by Poly Haven' credit." It does not mention the User-Agent header.
- **Licence** ([license](https://polyhaven.com/license), no date shown; read 2026-10-26): "Our assets are all licensed as CC0." Credit is not required. Assets may be redistributed, "including ... a product you sell". Site content is excluded from CC0 and may not be reproduced "without prior written permission". "Web scraping or data mining without express permission" appears in the Terms' list of disruptive activities. "The use of the public API hosted at api.polyhaven.com is governed by separate API Terms." I did not retrieve the API Terms.
- **Conflict.** A search snippet said commercial API use needs a custom licence and a 5% revenue share. The two fetched pages above say there is no fee and no revenue share. I treat the snippet as stale. It is recorded as a risk (§10).
- **Formats.** The API page names no file formats. A search summary of a 2021 launch post, not opened, says glTF is offered for models and textures and that FBX was left out. The current formats are unverified.

**ambientCG**

- **Licence** ([licence](https://docs.ambientcg.com/license/), no date shown; read 2026-10-26): "All ambientCG assets are provided under the Creative Commons CC0 1.0 Universal License." Credit is optional. Redistribution is allowed, "even for commercial purposes". No rule on automated downloading is stated.
- **API** ([API](https://docs.ambientcg.com/api/), no date shown; read 2026-10-26). The navigation lists v1 `/downloads_csv` and `/full_json`, v2 with `/categories_json` and `/releases_rss` added, and v3 `/assets`, `/categories`, `/collections` and `/rss`. The page says nothing about authentication, rate limits or caching. It warns that the API "is therefore potentially not as reliable or stable as needed for an 'enterprise-level' application."
- **Site** ([ambientcg.com](https://ambientcg.com/), read 2026-10-26). The sections are PBR Surfaces, HDRIs, Substances and 3D Assets. Whether the 3D Assets section holds models, and in which formats, is unverified. A third-party guide said no 3D models are offered. I did not rely on it.

**BIMobject**

- **Developer portal** ([developer.bimobject.com](https://developer.bimobject.com), read 2026-10-26). The fetch returned only the heading "BIMobject Developer". A search summary says applications are invited for Search, Admin and Developer APIs, with onboarding "within twenty-four hours". That is not on the fetched page and is unverified.
- **FAQ** ([business.bimobject.com/faq](https://business.bimobject.com/faq/), read 2026-10-26, no date shown). Downloads are offered to AEC professionals, and "manufacturers retain full ownership of their product data." The page states no licence or use terms for downloaded objects. Its footer links to an EULA page that I did not read. The APIs let you "embed BIM objects directly into your website, PIM system, or partner platforms."
- **Verdict.** No public API reference was read. The content is manufacturer product data for building workflows, which is not a general model source. Not recommended now.

### 1.4 Comparison

| Service | API and access | Auth | Licence per asset | Formats | Desktop-client terms | Cache and offline | Verdict |
|---|---|---|---|---|---|---|---|
| 3D Warehouse | None documented (§1.2) | A Trimble account; no third-party auth documented | General Model License by default, per the Terms; a CC choice is unverified | `.skp` in a version window; COLLADA `.dae` for older SketchUp; ZIP wrapper | Automated access and aggregation barred; written consent route by email | Not stated; no cache in 1.x (§5) | **No adapter. User-mediated only.** |
| Sketchfab | Download API, per user | OAuth 2.0 end-user login; "contact us" for app-level download | Per model: Standard, or CC BY, BY-NC, BY-ND, BY-SA; the author may change it | glTF, GLB, USDZ | Attribution displayed; "provided by Sketchfab" | Not stated | Deferred (R105.d) |
| Poly Haven | Public REST API at `api.polyhaven.com` | None; User-Agent required | CC0; credit optional for assets; credit required for live API use | Models per `/files`; glTF likely (unverified) | Free, including commercial; "assets came from Poly Haven" credit; separate API Terms (not read) | Not stated; CC0 assets may be stored | **Viable; held (R105.b)** |
| ambientCG | Documented API menu (v1–v3) | Not stated | CC0; credit optional | Textures, HDRIs, substances; 3D Assets unverified | Not stated; reliability warning | CC0 assets may be stored; API rules not stated | Not recommended now |
| BIMobject | Application-only APIs; no public reference read | Not stated | Not stated for downloads | Not stated | Not stated | Not stated | Not recommended now |

## 2. Auth and terms

- **3D Warehouse.** No third-party authentication is documented. A Warehouse account belongs to the user. The product must never ask for, store or handle a Trimble or Warehouse password. Trimble Identity is a developer-platform service, and nothing documents it for Warehouse downloads.
- **Sketchfab.** Downloads need the end user's own login through OAuth 2.0. This would be the product's first desktop OAuth flow. There is a precedent: ADR 0160 describes the assistant's "own browser sign-in with PKCE, OS credential storage". [ADR 0041](0041-chatgpt-plan.md) covers that flow. A Sketchfab adapter would follow it. R105.d is deferred, so this is not built now.
- **Poly Haven.** No account. The User-Agent identifies the application. It does not identify a user.
- **Desktop-client terms, in short.**
  - Warehouse: automated access barred; aggregation barred; redistribution barred; a written consent route exists.
  - Sketchfab: attribution text required; the app must say the models come from Sketchfab; app-level download needs contact.
  - Poly Haven: credit for the live API; User-Agent required; contact requested for user-side integrations.
  - ambientCG and BIMobject: none stated.

## 3. Per-asset licence and attribution

These rules apply to any hosted asset that enters the product. They are proposed for R105.a and R105.b.

- **L1. Record at import.** Each hosted asset records its source service, asset id, the licence as stated on its page at download time, the author name, the source page address, and the attribution text shown. The record is stored in the library entry, in the manifest `license` and `author` fields that [ADR 0174](0174-custom-styles-asset-authoring.md) §4.1 already defines. It is not stored in the document, because ADR 0174 keeps catalog provenance out of documents (owner decision 10).
- **L2. Warehouse.** The default licence is the General Model License, per the Terms ("you will make your Models available to end users pursuant to the General Model License"). The excerpts I read require no attribution. Record "Trimble 3D Warehouse; General Model License, per the Terms", with the uploader's name as shown. If a model page states another licence, record that text as stated.
- **L3. Show, do not hide.** A licence that restricts the user's use is shown next to the asset, for example CC BY-NC for commercial work. Import is not blocked by the licence. Catalog export is refused for restricted items (owner decision 6).
- **L4. Keep the credit with the asset.** Sketchfab requires it wherever the asset appears. Poly Haven requires the live-API credit. A CC0 credit is optional, but the product should show one.
- **L5. Licences can change.** A Sketchfab author can change a model's licence at any time. The product keeps the licence as it was at download. A refresh never changes the record. A change applies only when the user asks for it.
- **L6. No aggregation.** The Warehouse terms bar aggregating Warehouse content for redistribution, and the FAQ calls aggregation into a website or publication "impermissible". The product does not publish, sync or export a catalog that contains Warehouse-origin entries (owner decision 6).

## 4. The user-mediated download path and its loss reports

**The flow.**

1. The user opens the source in a browser, signs in as themselves if the source needs it, and downloads the file.
2. If the file is a ZIP, the user extracts it. The product refuses the ZIP and says how to extract it (§7.3).
3. The user chooses File › Import, then the file.
4. The product checks the file type. The importer runs in its helper, where the format has one. The result is an unsaved native document with a loss report.

The product never fetches, scrapes or intercepts the download. The help text links to the source and says the file is the user's own download. It does not describe a service integration.

**Per-format.** Nothing is dropped silently. Each report keeps the accounting identity from ADR 0176 §4 (counts present equal counts imported plus reported).

| Format | Offered by | Reader and status | Report and losses |
|---|---|---|---|
| `.skp` | 3D Warehouse (current, previous two, and first-upload versions) | [ADR 0176](0176-skp-import.md), R102. Unreleased. 2021 and later supported (tested with a 2024 model); 2013–2020 best effort; before 2013 refused | Format version and container; source SHA-256; units; counts; notices (`invalidFaces`, `truncatedNames`, `omittedScenes`); a plain line for best-effort files. Not carried: scenes, styles, section planes, text, dimensions, guides, the camera, and dynamic-component behaviour. Groups import as components used once (SUPPORT_MATRIX row 74; ADR 0176 §5) |
| COLLADA `.dae` | 3D Warehouse, for older SketchUp | No importer. No code match in `src/`, and no SUPPORT_MATRIX row | Refused, with the reason and the advice to download a newer version |
| ZIP | 3D Warehouse download wrapper | Not an import type | Refused, with the advice to extract and import the contained file |
| glTF, GLB | Sketchfab (glTF, GLB); Poly Haven (glTF likely, unverified) | ADR 0111 and ADR 0109 | Triangle, instance and camera counts; metre and axis conversion; preserved features; a notice for every recorded loss. History, topology and analytic semantics do not round-trip. Import capture is limited to 128 MiB (ADR 0111) |
| USDZ | Sketchfab | No importer | Refused |
| FBX | Not offered by Sketchfab's API; unverified for Poly Haven | No importer | Refused |
| OBJ/MTL, STL, STEP, IFC | None of the sources read offers these | Existing importers (ADR 0115, ADR 0119, SUPPORT_MATRIX row 73, ADR 0171) | Existing reports, unchanged |

**The gap to state plainly.** Until R102 ships, the Warehouse's native format has no working importer. The user-mediated path for `.skp` therefore depends on R102 shipping.

## 5. Cache and offline

**Cache rules by source.**

- **3D Warehouse.** No product-side cache in 1.x. The user's file belongs to the user. The document keeps only the geometry it imports. Offline use works once the file is on disk.
- **Sketchfab.** Caching is not stated. No cache until the full terms are read. If the adapter is reopened, every cache entry keeps its licence record and credit.
- **Poly Haven.** CC0 assets may be stored. The API's caching is not stated, so the cache is keyed by asset id and file hash, so that an endpoint change does not corrupt it.
- **ambientCG.** CC0 assets may be stored. The 3D model path is unverified.

**Cache design, for R105.c (Poly Haven only; proposed).**

- **Location.** `AppDataLocation/hosted-assets/`. The library, catalogs and extensions already sit under `AppDataLocation` (`src/app/window_library.cpp:23`). A separate folder keeps the local library independent.
- **Layout.** One directory per source and asset id. Each file is named by its SHA-256. Each entry has a manifest with the source, asset id, file name, SHA-256, size, licence identifier, author, attribution text, UTC fetch time, and the API host. The query is not stored.
- **Integrity.** SHA-256 is checked on every read. A mismatch deletes the entry and the file is fetched again. The hash is only an index. As ADR 0131 puts it, "a hash match alone never substitutes a resource."
- **Permissions.** Directories 0700, files 0600. This follows the private-copy rule in ADR 0176 §1 (proposed).
- **Writes.** A temporary file, then a rename, using the atomic publisher of ADR 0129 and ADR 0131.
- **Bounds.** Each file is at most 128 MiB, the glTF capture limit (ADR 0111). The total is at most 2 GiB by default, with least-recently-used eviction. The bound is a proposal. ADR 0164's 64 MiB LRU image cache is a precedent for the design.
- **User control.** A "Clear hosted asset cache" control in Preferences. The cache is never synced, never shared, and never included in a catalog export.
- **Search results.** Listings and search results are not persisted. Stale listings are worse than a fresh request.

**Offline behaviour.**

- A cached asset imports with no network. This is to be verified in R105.c with `unshare -n`.
- A new search, a new download, and any licence refresh show a named offline state. The rest of the application is unaffected (UX_DESIGN principle 5).
- For the Warehouse and Sketchfab, offline means only that the user already has the file.

## 6. Network and privacy stance

**What the product does today** (verified in code, read 2026-10-26).

- The only network code is in `src/integrations/` and `src/app/assistant_panel.cpp`.
  - The OpenAI Responses endpoint, `https://api.openai.com/v1/responses` (`src/integrations/openai_provider.cpp:358`).
  - ChatGPT sign-in and token endpoints under `https://auth.openai.com`, and `https://api.openai.com/v1/models` (`src/integrations/chatgpt_auth.cpp`).
  - The ChatGPT usage link (`src/app/assistant_panel.cpp:135`, `:793`).
  - A loopback Ollama endpoint at `127.0.0.1` (`src/integrations/ollama_provider.hpp:5`; ADR 0036).
- No update check, telemetry, analytics or asset download code exists in `src/`.
- Credentials use the Secret Service with no plaintext fallback (ADR 0034, ADR 0041).
- The first remote request for each provider shows "what is sent" and asks for consent once (UX_DESIGN §8.5).
- **The brief's premise, checked.** A hosted asset adapter would be the product's first network feature for asset downloads, which is true. It would not be the first network feature. The assistant providers already make network requests. A hosted adapter would add the first non-assistant network client.

**The parser helpers have network access.**

- The `.skp`, IFC and STEP helpers run with the user's network access. Their isolation is resource limits, `setsid` and `PR_SET_PDEATHSIG` (`src/io/skp_worker.cpp:74–87`, `src/io/ifc_worker.cpp:148–163`, `src/io/step_worker.cpp:248–261`). No seccomp, Landlock or network-namespace code exists in `src/`. ADR 0171 calls that hardening possible on current kernels and makes it part of decision 19 (its §1.6 text).
- **Correction.** ADR 0176 calls the helper "sandboxed as in ADR 0171 §1.6" (its owner decisions, decision 2). In this codebase that word means resource limits only. This ADR does not change ADR 0176. It records the actual isolation.

**Stance, for owner acceptance.**

- **N1. Only on user action.** A request is made only after the user clicks Search or Download. There are no background requests.
- **N2. No telemetry.** The product sends no usage data to any hosted service. A search sends its query to that service and nothing else.
- **N3. In the app process.** A hosted client uses the transport pattern of `src/integrations/provider_transport.hpp` (QtNetwork, TLS verification, a bounded timeout of 30 s at line 50). It never runs inside a parser helper.
- **N4. No hosted bytes to a network-capable helper.** Bytes from a hosted service do not reach a helper that has network access until the ADR 0171 decision 19 hardening exists. GlTF import is in-app and has no network loader (ADR 0109), so the first hosted format is unaffected.
- **N5. The User-Agent names the application.** It carries no user identifier.
- **N6. Sign-in only where a service requires it.** Tokens go in the OS store. Nothing appears in documents, logs or screenshots (UX_DESIGN §8.5).
- **N7. Named failure states.** Offline, refused by the service (403 or 429), not found, and licence unknown are each named. Retries are limited to one (ADR 0034 adds no separate retry loop).
- **N8. Nothing core depends on a hosted service.** This is UX_DESIGN principle 5 and BUILD_PLAN line 135.
- **N9. Nothing about the document is sent.** A request carries only an asset id or a query.

**Privacy disclosure.** A request to Poly Haven or Sketchfab shows the user's IP address and the query to that service. The consent text should say so, as the assistant consent does (UX_DESIGN §8.5).

## 7. Verdict and the supported subset

### 7.1 Verdict

1. **3D Warehouse: user-mediated only (R105.a).** No adapter, no scraping, no web-view download interception, no bot. The reasons are the lack of a documented API (§1.2) and the Terms' bar on automated access (§1.2).
2. **Poly Haven: viable, not scheduled (R105.b).** It is the only documented, keyless, CC0 source that serves glTF. It starts only if owner decisions 3 and 4 are accepted.
3. **Sketchfab: deferred (R105.d).** It is documented but needs per-user OAuth and per-model licences that can change. Its caching rules and rate limits are not stated.
4. **ambientCG and BIMobject: not recommended now.**
5. **The local library is unchanged.** A hosted adapter writes only to the hosted cache. Anything reaches a document only through an explicit user insertion (ADR 0131's atomic insertion).

### 7.2 The subset R105.b would support, if approved

- Poly Haven only.
- Read-only: list assets and fetch download links. No account, upload or write.
- glTF and GLB only. Other types are refused with the reason.
- Each asset carries its licence (CC0) and its credit.
- If a response includes a hash, verify it. If not, record the SHA-256 that the product computes and treat it as the identity from first download. (A search summary says the API gives hashes; that is unverified.)
- The report shows the source, asset id, licence, credit, bytes, SHA-256 and any refused file types.

### 7.3 Refusals, each named

- Any Warehouse API call, or any automated Warehouse access.
- Scraping or reading the HTML of any hosted site.
- Sketchfab downloads, until R105.d.
- COLLADA `.dae`, USDZ, FBX, Blender `.blend` and ZIP, each as an import type.
- Any hosted asset in a catalog export (owner decision 6).
- Any request that carries document content.

## 8. Verification

The Verify line asks for sign-in, denied access, import losses and offline cache where an adapter is viable. Here only Poly Haven could be viable, so those checks apply to R105.b. R105.a is verified as a user-mediated path.

**R105.a (user-mediated, no adapter).**

- **V1. Refusal fixtures.** A `.dae`, a `.usdz`, a `.fbx` and a `.zip` each refuse with a named reason and change nothing in the document, as ADR 0176 §5 requires.
- **V2. Warehouse-origin `.skp` report.** The report shows the counts and notices of ADR 0176 S1. This depends on R102.
- **V3. External references.** A glTF with external buffers is refused, as ADR 0109 requires.
- **V4. No network.** The import path runs under `unshare -n` and succeeds. This is the same test as ADR 0178 D1.9.
- **V5. Wording.** The user guide and support matrix say the file was downloaded by the user, and that SketchyUp is not affiliated with or endorsed by Trimble (ADR 0176 §8). No product name contains "SketchUp" (ADR 0177 §3, item 6).

**R105.b (if approved), for Poly Haven.**

- **Sign-in.** None. The test confirms that no credential is read or stored.
- **Denied access.** A fixture server returns 403 and 429. The product shows the named state and retries at most once.
- **Import losses.** The ADR 0111 report for each downloaded file.
- **Offline cache.** A cached asset imports under `unshare -n`. An uncached asset shows the offline state.
- **Live checks.** None during this feasibility work. Any live check in R105.b runs on the owner's machine, with a bounded number of requests set in advance.

## 9. Budgets (proposed; R105.b measures)

| Item | Budget | Basis |
|---|---|---|
| Network traffic, R105.a | 0 bytes | No code path |
| Network traffic, R105.b | Only per user action; measured per action | N1 |
| Request timeout | 30 s | `src/integrations/provider_transport.hpp:50` |
| Retries | 1 | ADR 0034 |
| Asset list response | At most 4 MiB | Proposed; R105.b measures |
| One download | At most 128 MiB | ADR 0111 capture limit; ADR 0109 |
| Cache, total | At most 2 GiB, least recently used first | Proposed; owner decision 7 |
| Cache entry | At most 128 MiB | As for one download |
| Manifest per entry | At most 32 KiB | ADR 0174 §4.1 |
| Licence and credit text | At most 256 bytes per field | ADR 0176 §5 message limit |
| Import time, cached glTF | No slower than the ADR 0111 figure | R105.b measures |
| Application memory during import | At most 1 GiB | ADR 0176 §6 |

## 10. Risks

1. **Automation breaches the Warehouse terms.** A search tool, a web-view interception or a bot would breach the scraping and load rules. Mitigation: no adapter (§7.3), and a check that the Warehouse help text names no API.
2. **Warehouse terms change.** The Terms are dated 2017, and the FAQ is dated 21 August 2026. I read two-thirds of the FAQ and none of the canonical Terms page. Changes may be in the parts I did not read. Mitigation: owner decision 5.
3. **Licence drift.** A Sketchfab author can change a model's licence at any time. Mitigation: L5. The licence is recorded on download and never refreshed silently.
4. **Poly Haven terms.** Endpoints and authentication "may" change. The API Terms were not retrieved. A stale snippet reported a revenue share that the fetched pages do not support. Mitigation: contact Poly Haven before R105.b, as its page asks (owner decision 3).
5. **Format gaps.** `.dae`, USDZ, FBX and ZIP have no importer. Warehouse models from 2017 and 2018 are downloadable only by their uploader. Older-SketchUp users get DAE. Mitigation: named refusals and help text (§7.3).
6. **Reader coverage for newer files.** The `.skp` reader was tested with a 2024 model. The Warehouse keeps the current, previous two and first-upload versions, so 2025 and 2026 files are likely. Not verified (ADR 0176 §7).
7. **The network surface.** The product gains its first non-assistant network client. The parser helpers keep network access until ADR 0171 decision 19 hardening. Mitigation: N3 and N4.
8. **Trademark and wording.** "3D Warehouse", "Sketchfab" and "Poly Haven" will appear in the UI. Trimble's guidelines bar using Trimble marks in product names (§4) and any suggestion of sponsorship (§7). Use the names only as nominative references, "appropriate and consistent" with the guidelines (§1). Mitigation: a non-affiliation note (ADR 0176 §8) and counsel review (owner decision 8).
9. **Catalog aggregation.** An exported `.sketchycat` that contains a Warehouse model is the aggregation the Terms bar. Mitigation: owner decision 6.
10. **Scope creep.** A hosted catalog implies sync and accounts. Mitigation: the R105 Build line ("Keep the local library independent"). No slice adds sync.
11. **Lost features.** Hosted glTF and `.skp` lose history, topology, scenes and dynamic-component behaviour (§4). Mitigation: the report.
12. **Sketchfab's status is unverified.** The beta claim comes from a search summary. R105.d must check it.
13. **Source uncertainty.** Pages went through a summariser (§1.1). Mitigation: re-read the cited sentences before R105.a or R105.b is written.

## 11. Implementation split

- **R105.a: user-mediated downloads (for 1.x).** A user guide page on getting models from the 3D Warehouse: the download, the ZIP step, the version window, and the non-affiliation note. Named refusals for `.dae`, `.usdz`, `.fbx` and `.zip` in the import dialog. Fixtures V1 to V5. Gate: owner decisions 1, 2, 5 and 8. Dependency: R102 ships for `.skp`. Until then the help text says `.skp` import is not yet released. Network: none.
- **R105.b: Poly Haven CC0 adapter (conditional; not scheduled).** A read-only client (§7.2) in the app process, on the transport pattern of N3. An attribution display and the licence record. Gate: owner decisions 3, 4, 6 and 7, and the Poly Haven contact in §10 item 4. Tests: the §8 checks, for Poly Haven.
- **R105.c: bounded private cache (for R105.b).** Layout, integrity checks, LRU eviction, the clear-cache control, and the offline tests. Gate: R105.b.
- **R105.d: Sketchfab adapter (deferred).** Per-user OAuth 2.0 on the PKCE pattern of ADR 0160 and ADR 0041, per-model licence display, and attribution. Gate: the owner reopens it, the Sketchfab beta status and full terms are checked, and app-level download is requested from Sketchfab if wanted.
- **R105.e: documentation and claims.** SCOPE C04 stays at "Investigate". SUPPORT_MATRIX row, USER_GUIDE, RELEASE_NOTES if a claim is made, and the trademark wording. Counts from R105.b go in its implementation notes.

## Owner decisions

1. **Verdict: 3D Warehouse is user-mediated only. No adapter, no scraping, no web-view download interception.** *Recommended: yes.* The Terms bar automated access, and no documented API exists (§1.2).
2. **Ship the user-mediated path first (R105.a), with its help text and named refusals.** *Recommended: yes.* It is the only path the evidence supports for the Warehouse, and it adds no network code.
3. **Poly Haven CC0 adapter: viable. Approve the design as a later, separately gated slice (R105.b), not for 1.x.** *Recommended: approve the design; schedule it after 1.x.* It is the only documented, keyless CC0 source that serves glTF. It is also the product's first non-assistant network feature, so it should not ride a release.
4. **Network stance, N1 to N9.** Hosted requests only on user action; no telemetry; no hosted bytes to a network-capable helper before ADR 0171 decision 19 hardening. *Recommended: yes.* It keeps the offline rule and the existing consent model (UX_DESIGN §8.5).
5. **Read the rest of the Warehouse Terms, FAQ and help pages before R105.a is written.** Check the parts I did not read, the canonical Terms page, the attribution question, and whether a Creative Commons choice exists. *Recommended: yes.* The excerpts do not cover the whole pages.
6. **Catalog export: refuse any `.sketchycat` that contains a Warehouse-origin entry, or any entry under a restrictive licence (NC, ND or SA).** *Recommended: yes.* The Warehouse bars aggregation for redistribution, and the licence must stay with the asset (ADR 0174 §4.1). This needs a provenance field on catalog entries, which ADR 0174 does not have today.
7. **Cache: a private cache for R105.b, proposed at 2 GiB total and 128 MiB per file, keeping each licence record.** *Recommended: yes, as a proposal that R105.c measures.* It bounds disk use and keeps the attribution with the file.
8. **Naming and wording: "3D Warehouse", "Sketchfab" and "Poly Haven" as nominative references, with a non-affiliation note. No product name contains "SketchUp" or a Trimble mark. Counsel reviews the wording before 1.x.** *Recommended: yes.* Trimble's guidelines §4 and §7 bar these uses (ADR 0177 §3).
9. **Sketchfab and the others: defer Sketchfab (R105.d). Do not pursue ambientCG or BIMobject now.** *Recommended: defer.* Sketchfab needs per-user OAuth, licences can change, and its status and limits are unverified. ambientCG's 3D content is unverified, and BIMobject offers no public reference.
10. **Licence record location: in the library entry, not in the document.** *Recommended: yes.* It keeps ADR 0174's rule that catalog provenance is not persisted in documents, so relocating or deleting a library entry breaks nothing.

## Sources

Each page below was read on 2026-10-26. "Last updated" dates are the page's own, as shown.

**Trimble and SketchUp (official)**

- 3D Warehouse Terms of Use, embed copy ("Last updated: April 1, 2017"). The General Model License ("Last updated: March 18, 2015") is on the same page. <https://embed-3dwarehouse-classic.sketchup.com/tos/>. The canonical address <https://3dwarehouse.sketchup.com/tos/> returned only its title.
- 3D Warehouse Terms of Use FAQ ("Last updated: Aug 21, 2026"; the first 100,000 of 153,004 characters read). <https://help.sketchup.com/en/3d-warehouse/3d-warehouse-terms-use-faq>
- Downloading models in 3D Warehouse: file formats ("Last updated: Aug 21, 2026" as fetched; the first 100,000 of 151,161 characters read). <https://help.sketchup.com/en/3d-warehouse/downloading-model-file-formats>
- Trimble developer docs (redirected from `developer.trimble.com/docs`; the visible part of the page). <https://www.trimble.com/en/developer/docs>
- SketchUp developer hub. <https://developer.sketchup.com/>
- Trimble trademark guidelines, V2.0 ("Last Updated: July 5, 2023"). <https://www.trimble.com/legal/trademark-guidelines>

**SketchUp community forums (not Trimble statements)**

- "Integrate Warehouse using APIs" (reply of July 9, 2019). <https://forums.sketchup.com/t/integrate-warehouse-using-apis/98593>
- "3D Warehouse API" (replies of June 2017). <https://forums.sketchup.com/t/3d-warehouse-api/47436>
- "3Dwarehouse API" (reply of June 23, 2022). <https://forums.sketchup.com/t/3dwarehouse-api/201774>

**Sketchfab (official)**

- Download API integration guidelines (no date; © 2026 footer). The api.sketchfab.com address redirects here. <https://sketchfab.com/developers/download-api/guidelines>
- Download API (© 2026 footer). <https://sketchfab.com/developers/download-api>
- Licences for downloadable models (help article; "Updated over 2 weeks ago", no date). <https://help.sketchfab.com/en/articles/16152220>

**Poly Haven (official)**

- Poly Haven API ("18 July 2026" as shown). <https://polyhaven.com/our-api>
- Corporate page (no date). <https://polyhaven.com/corporate>
- Licence (no date). <https://polyhaven.com/license>
- Public API README (no date; read 2026-10-26). <https://github.com/Poly-Haven/Public-API>

**ambientCG (official)**

- Licence (no date). <https://docs.ambientcg.com/license/>
- API (no date). <https://docs.ambientcg.com/api/>
- Site. <https://ambientcg.com/>

**BIMobject (official)**

- Developer portal (the fetch returned only its heading). <https://developer.bimobject.com>
- FAQ (no date). <https://business.bimobject.com/faq/>

**Repository** (`origin/main` at `e51f5959`, read 2026-10-26)

- ADRs: [0034](0034-openai-provider.md), [0036](0036-local-provider.md), [0041](0041-chatgpt-plan.md), [0109](0109-bounded-gltf-packages.md), [0111](0111-native-gltf-import-workflow.md), [0115](0115-native-obj-interchange-workflow.md), [0119](0119-native-stl-interchange-workflow.md), [0129](0129-self-contained-template-bundles.md), [0130](0130-contained-component-bundles.md), [0131](0131-atomic-library-insertion.md), [0160](0160-provider-scope-for-1-0.md), [0164](0164-sheet-layout-interface.md), [0171](0171-ifc-exchange-subset.md), [0174](0174-custom-styles-asset-authoring.md), [0176](0176-skp-import.md), 0177 and 0178, which are on branches `docs/r103-ruby-feasibility` and `docs/r104-dc-lc-feasibility` and not yet on `main`.
- Code: `src/integrations/provider_transport.hpp:50`; `src/integrations/openai_provider.cpp:358`; `src/integrations/chatgpt_auth.cpp` (lines 28–29, 352, 384, 606, 629, 633); `src/integrations/ollama_provider.hpp:5`; `src/app/assistant_panel.cpp` (lines 135, 523, 732, 793); `src/io/skp_worker.cpp:74–87`; `src/io/ifc_worker.cpp:148–163`; `src/io/step_worker.cpp:248–261`; `src/app/window_library.cpp:23`.
- Docs: `docs/SUPPORT_MATRIX.md` rows 68–74; `docs/SCOPE.md` row C04 (line 130); `docs/PR_ROADMAP.md` R102 (line 1472) and R105 (line 1506); `docs/UX_DESIGN.md` §1 principle 5 and §8.5; `docs/BUILD_PLAN.md` line 135.

**Search summaries (not opened; not relied on)**

- The Poly Haven "5% revenue share" snippet (contradicted by the fetched pages).
- The Sketchfab "over 1 million" claim, its "beta" status, and the rate-limit claims from low-quality sites.
- The Poly Haven 2021 launch post's format claims, and the claim that the API gives hashes.
- The BIMobject "twenty-four hours" onboarding.
- A mirrored Warehouse listing that shows a Creative Commons choice.
- A search summary that says a Creative Commons choice does not cancel the Warehouse grant.

**Not verified**

- Whether a 3D Warehouse uploader can choose a Creative Commons licence. The mirrored listing suggests so, but I did not open it.
- The parts of the Warehouse Terms, FAQ and help pages I did not read (about a third of the FAQ and help pages, and the canonical Terms page).
- Which formats the Warehouse Download button offers. The choices are in a screenshot.
- Whether Trimble requires attribution for Warehouse models. The FAQ excerpt does not address it.
- The rest of the Trimble developer list. The page was truncated after the Agriculture section.
- Any Trimble statement after 2019 on a Warehouse API. A targeted search found none.
- Sketchfab's Download API beta status, rate limits, and caching and offline rules.
- Poly Haven's current model formats, whether its API returns hashes, and the API Terms document.
- ambientCG's 3D Assets formats and what its endpoints return.
- BIMobject's API terms and access.
- Whether the `.skp` reader handles 2025 and 2026 files. It was tested with a 2024 model only.
- Counsel's view on any point in §10 and owner decision 8.
- No live API call was made, so no rate limit, response shape or latency was observed.
