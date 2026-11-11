# Screenshot image for docs/images/ (see docs/images/README.md).
#
# It installs the SketchyUp package from the GitHub release, checks it against that
# release's SHA256SUMS, and adds the Xvfb, xdotool and ImageMagick tools that
# scripts/capture-screenshots.sh needs. Network access is needed for this build only.
#
# Build it with packaging/arch as the context, so the build sends only the mirror list:
#
#   docker build -f scripts/screenshots.Dockerfile --build-arg SKETCHYUP_VERSION=1.9.0 \
#     -t sketchyup-shots:1.9.0 packaging/arch
FROM archlinux:base-devel

ARG SKETCHYUP_VERSION=1.9.0

# The dated archive keeps the package set fixed; see packaging/arch/mirrorlist.
# --disable-download-timeout: the archive can stall a parallel download for more than
# pacman's 10-second low-speed limit, which failed this build on 2026-11-10.
COPY mirrorlist /etc/pacman.d/mirrorlist
RUN pacman -Syy --noconfirm --needed --disable-download-timeout xorg-server-xvfb xdotool imagemagick ttf-dejavu mesa curl python \
 && pacman -Scc --noconfirm

RUN set -eu; \
    base="https://github.com/zygote55/sketchyup/releases/download/v${SKETCHYUP_VERSION}"; \
    pkg="sketchyup-${SKETCHYUP_VERSION}-1-x86_64.pkg.tar.zst"; \
    mkdir -p /tmp/sketchyup-pkg; \
    cd /tmp/sketchyup-pkg; \
    curl -fsSL -o "$pkg" "$base/$pkg"; \
    curl -fsSL -o SHA256SUMS "$base/SHA256SUMS"; \
    grep -F "$pkg" SHA256SUMS > checksum.txt; \
    test -s checksum.txt; \
    sha256sum -c checksum.txt; \
    pacman -U --noconfirm --disable-download-timeout "$pkg"; \
    cd /; \
    rm -rf /tmp/sketchyup-pkg; \
    pacman -Scc --noconfirm
