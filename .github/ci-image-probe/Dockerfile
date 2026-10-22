FROM mirror.gcr.io/library/archlinux:base-devel@sha256:51dd3d24f7fba779e7c471caeee7804c50e8c134ad948e19685a1c83a42facc3
COPY mirrorlist /etc/pacman.d/mirrorlist
ARG PACKAGES
RUN for attempt in 1 2 3; do pacman -Syyuu --noconfirm --needed $PACKAGES && break; sleep 30; done && pacman -Scc --noconfirm
