#!/bin/bash
# install.sh and uninstall.sh on other people's computers: for each distribution, a clean
# container with KDE Plasma's KWin and Spectacle, a new user ("stranger") who has never
# run Ectolens, and this source folder as a fresh copy. The stranger runs ./install.sh --yes
# inside a headless KWin session; inside.sh judges what it did, then uninstalls and judges
# that too. Too-old distributions must be refused with nothing changed.
#
#   tests/install/stranger-check.sh [DISTRO ...]     (default: all of them)
#   OUT=dir  keeps the logs there (default build/stranger-check)
#   PLANT="a.patch b.patch"  applies planted faults to the copy first (tests/install/plants/),
#                     to prove a check catches each: the run must then fail where each patch says
#
# Needs rootless podman and network access for the packages. Nothing runs on this desktop,
# and nothing of this computer's user goes in: the copy holds only the files git tracks.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
OUT=$(realpath -m "${OUT:-$SRC/build/stranger-check}")
mkdir -p "$OUT"

# name | image | how to install the desktop (KWin, Spectacle, Mesa, PipeWire; FFmpeg's ffprobe
# to read the recording back) and sudo | expect. Tumbleweed's container image carries BusyBox's
# diffutils, which no Tumbleweed desktop has; GNU's replaces it, as on a desktop.
DISTROS=(
  "fedora-44|registry.fedoraproject.org/fedora:44|dnf -y install kwin spectacle sudo procps-ng glib2 dbus-daemon findutils mesa-dri-drivers pipewire wireplumber ffmpeg-free|installs"
  "tumbleweed|registry.opensuse.org/opensuse/tumbleweed:latest|zypper -n refresh && zypper -n install --force-resolution kwin6 spectacle sudo procps glib2-tools dbus-1-daemon findutils Mesa-dri pipewire wireplumber ffmpeg-7 diffutils|installs"
  "arch|docker.io/library/archlinux:latest|pacman -Syu --noconfirm kwin spectacle sudo procps-ng glib2 dbus mesa pipewire wireplumber ffmpeg|installs"
  "debian-testing|docker.io/library/debian:testing|apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y kwin-wayland kde-spectacle sudo procps libglib2.0-bin dbus-daemon libgl1-mesa-dri libegl-mesa0 pipewire wireplumber ffmpeg|installs"
  "neon|invent-registry.kde.org/neon/docker-images/plasma:user|apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y sudo procps libglib2.0-bin libgl1-mesa-dri libegl-mesa0 pipewire wireplumber ffmpeg|installs"
  "debian-13|docker.io/library/debian:trixie|apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y kwin-wayland kde-spectacle sudo procps libglib2.0-bin dbus-daemon|refused"
  "kubuntu-26.10|docker.io/library/ubuntu:26.10|apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y kwin-wayland kde-spectacle sudo procps libglib2.0-bin dbus-daemon|refused"
)
want=("$@")
for w in "${want[@]}"; do
    printf '%s\n' "${DISTROS[@]}" | cut -d'|' -f1 | grep -qxF "$w" \
        || { echo "no distribution called $w; the names: $(printf '%s\n' "${DISTROS[@]}" | cut -d'|' -f1 | tr '\n' ' ')" >&2; exit 1; }
done

# a share of this computer, at low priority: up to 16 processors and 40% of its memory (at
# most 24 GB), so a compile that runs out fails in the container instead of pushing the
# desktop into swap
CPUS=$(nproc); [ "$CPUS" -gt 16 ] && CPUS=16
MEM_GB=$(awk '/^MemTotal:/ { g = int($2 * 0.4 / 1048576); print (g > 24 ? 24 : (g < 4 ? 4 : g)) }' /proc/meminfo)

# KWin draws on this computer's integrated graphics, as tests/e2e/overlay/run.sh does: a render
# node that isn't NVIDIA's (its card stays out of every test). Without one, KWin's software
# path crashed on the first screenshot, for the distribution's own Spectacle too.
GPU=()
for n in /sys/class/drm/renderD*; do
    [ "$(cat "$n/device/vendor" 2>/dev/null)" = 0x10de ] && continue
    GPU=(--device "/dev/dri/${n##*/}"); break
done
[ ${#GPU[@]} -gt 0 ] || { echo "no render node other than NVIDIA's; KWin can't draw in the containers" >&2; exit 1; }

# the source as a stranger would clone it: the files git tracks (submodule included), no build
COPY=$(mktemp -d "${TMPDIR:-/tmp}/ectolens-stranger.XXXXXX"); trap 'rm -rf "$COPY"' EXIT
# (files not committed yet count too, so a change is checked before it is committed)
list() {
    git -C "$SRC" ls-files -z --cached --others --exclude-standard
    git -C "$SRC" submodule foreach --quiet 'git ls-files -z --cached --others --exclude-standard | while IFS= read -r -d "" f; do printf "%s/%s\0" "$sm_path" "$f"; done'
}
( cd "$SRC" && list | while IFS= read -r -d '' f; do [ -f "$f" ] && printf '%s\0' "$f"; done \
    | tar -cf - --null --no-recursion -T - ) | tar -xf - -C "$COPY"
for plant in ${PLANT:-}; do
    patch -s -p1 -d "$COPY" < "$(realpath "$plant")" || { echo "the plant $plant doesn't apply" >&2; exit 1; }
    echo "planted: $plant"
done
chmod -R a+rX "$COPY"   # the container's stranger is another user id
[ -f "$COPY/install.sh" ] && [ -f "$COPY/3rdparty/kpipewire/CMakeLists.txt" ] || { echo "copy is incomplete" >&2; exit 1; }

overall=0 ran=0
for row in "${DISTROS[@]}"; do
    IFS='|' read -r name image desktop expect <<<"$row"
    if [ ${#want[@]} -gt 0 ] && ! printf '%s\n' "${want[@]}" | grep -qxF "$name"; then continue; fi
    log=$OUT/$name.log
    ran=$((ran + 1))
    echo "== $name ($image), expected: $expect"
    # a desktop image per distribution, kept so a second run starts quicker (named by its
    # recipe, so a changed recipe builds a new one); the build's own packages are never in
    # it: install.sh has to find and install them every time
    # (sudo without PAM's account check: a rootless container can't run the helper that reads
    # /etc/shadow, which needs file capabilities; a real computer's sudo is the person's own)
    containerfile="FROM $image
USER root
RUN $desktop
RUN useradd -m -s /bin/bash stranger && printf 'Defaults:stranger !pam_acct_mgmt\\nstranger ALL=(ALL) NOPASSWD: ALL\\n' > /etc/sudoers.d/stranger && chmod 440 /etc/sudoers.d/stranger"
    tag=localhost/ectolens-stranger-$name:$(printf '%s' "$containerfile" | sha1sum | cut -c1-12)
    if ! podman image exists "$tag"; then
        printf '%s\n' "$containerfile" | podman build -t "$tag" -f - "$HERE" >"$OUT/$name.image.log" 2>&1 \
            || { echo "FAIL  $name: the desktop image didn't build ($OUT/$name.image.log)"; overall=1; continue; }
    fi
    rc=0
    podman run --rm --memory=${MEM_GB}g --memory-swap=${MEM_GB}g --cpus="$CPUS" --cpu-shares=128 "${GPU[@]}" \
        --user stranger --workdir /home/stranger -v "$COPY:/src:ro,Z" -v "$HERE/inside.sh:/inside.sh:ro,Z" "$tag" \
        bash -c 'cp -r /src /home/stranger/Ectolens &&
                 exec env -i HOME=/home/stranger USER=stranger LOGNAME=stranger LANG=C.UTF-8 PATH=/usr/local/bin:/usr/bin:/bin CMAKE_BUILD_PARALLEL_LEVEL='"$CPUS"'  \
                     bash /inside.sh '"$expect" >"$log" 2>&1 || rc=$?
    grep -E '^(PASS|FAIL) ' "$log" | sed "s/^/  /" || true
    if [ "$rc" != 0 ] || grep -q '^FAIL ' "$log" || ! grep -q '^PASS ' "$log"; then
        echo "FAIL  $name (exit $rc, log $log)"; overall=1
    else
        echo "PASS  $name"
    fi
done
[ "$ran" -gt 0 ] || { echo "nothing was checked" >&2; exit 1; }
exit $overall
