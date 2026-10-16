#!/usr/bin/env bash
# Pack a build tree for the native test jobs: everything they run or read, without
# objects, static archives or moc/rcc sources. ELF files lose their DWARF debug
# sections (strip --strip-debug): a Debug desktop executable is about 275 MB, almost
# all of it .debug_info, and the full tree would be a 14 GB artifact. Symbol tables
# stay, so gdb stacks from scripts/test-deadline.sh and scripts/crash-trace.c still name
# every function, but without source lines, arguments or inlined frames.
# Usage: scripts/ci-pack-build.sh BUILD_DIR ARCHIVE.tar.zst
set -euo pipefail
if (( $# != 2 )); then
  echo "Usage: $0 BUILD_DIR ARCHIVE.tar.zst" >&2
  exit 2
fi
build=$1
archive=$(realpath -m -- "$2")
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
list=$stage/files
find "$build" \( -name '*.o' -o -name '*.a' -o -name '*_autogen' -o -name .ninja_log \) -prune \
  -o \( -type f -o -type l \) -print0 > "$list"
mkdir -p "$stage/tree"
elf=$stage/elf
: > "$elf"
while IFS= read -r -d '' path; do
  mkdir -p "$stage/tree/$(dirname -- "$path")"
  if [[ ! -L $path && $(head -c4 -- "$path" | od -An -tx1 | tr -d ' \n') == 7f454c46 ]]; then
    printf '%s\0' "$path" >> "$elf"
  else
    cp -a -- "$path" "$stage/tree/$path"
  fi
done < "$list"
# strip -o writes the new file with the original's mode; -p keeps its timestamps.
xargs -0 -P "$(nproc)" -I{} strip --strip-debug -p -o "$stage/tree/{}" {} < "$elf"
tar -C "$stage/tree" --use-compress-program='zstd -T0 -3' -cf "$archive" "$build"
printf 'Packed %s ELF files (debug sections stripped) and %s other files: %s\n' \
  "$(tr -cd '\0' < "$elf" | wc -c)" \
  "$(( $(tr -cd '\0' < "$list" | wc -c) - $(tr -cd '\0' < "$elf" | wc -c) ))" \
  "$(du -h "$archive" | cut -f1)"
