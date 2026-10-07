# Bounded local library catalog

R079.d, 2026-10-07. A library is a user-selected flat local folder of `.sketchylib`
files. Moving that folder does not break bundle dependencies: each file contains
its native records, thumbnail and assets. Subfolders are not scanned recursively,
hidden entries are omitted, and file links are reported as invalid rather than
followed. No install, executable content, remote fetch or automatic synchronization
is implied by selecting a library folder.

Scanning examines at most 4096 directory entries and retains at most 128 candidate
bundles, 256 MiB of cumulative read bytes and 16 MiB of compressed thumbnails.
Each candidate undergoes complete template/component decoding and native record
validation before becoming selectable. Only metadata, paths and thumbnails are
retained, not every decoded document. Invalid files retain a bounded 512-character
error and no thumbnail; one bad bundle does not prevent browsing the other entries.
Scan limits produce explicit notices/errors rather than silently trusting partial
payloads. Selection must reload and validate the chosen file before use because
files can change after a scan.

Entries sort by case-insensitive display name with a path tie-breaker. Search is
case-insensitive and requires every whitespace-separated term to match the combined
name, description, labels or filename. Query length is limited to 1024 characters.
Native UI applies an explicit template/component filter and keeps invalid entries
inspectable but unavailable for insertion.

Tests include mixed template/component libraries, labels plus description search,
empty and unmatched queries, invalid bytes, symlinks and missing folders. Existing
bundle tests cover relocation and source immutability. Native keyboard selection
and workflow acceptance follow separately.
