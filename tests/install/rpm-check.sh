#!/bin/bash
# The Fedora package, the way COPR makes it and a stranger installs it: in a clean Fedora
# container, `make -f .copr/Makefile srpm` on a clone of this repository's HEAD (as COPR
# clones it), then the RPM built from that source package; then, in a second clean container
# with Plasma's KWin and Spectacle, the RPM installed with dnf and judged by rpm-inside.sh as
# a new user: a screenshot, a recording, `ectolens-shortcuts take` and `give-back`, Print
# Screen; then `dnf remove` and nothing of Ectolens left.
#
#   tests/install/rpm-check.sh        (commit first: COPR builds a commit, and so does this)
#   OUT=dir keeps the logs and packages there (default build/rpm-check)
#   PLANT="a.patch b.patch"  planted faults, applied to the clone and committed there before
#                     the source package is made; the run must fail where each patch says
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
OUT=$(realpath -m "${OUT:-$SRC/build/rpm-check}")
mkdir -p "$OUT/rpms" "$OUT/plants"
find "$OUT/rpms" -name '*.rpm' -delete
find "$OUT/plants" -name '*.patch' -delete
for plant in ${PLANT:-}; do cp "$(realpath "$plant")" "$OUT/plants/"; echo "planted: $plant"; done
[ -z "$(git -C "$SRC" status --porcelain --untracked-files=no)" ] || echo "note: uncommitted changes are not in the package; it is built from HEAD ($(git -C "$SRC" rev-parse --short HEAD))"

CPUS=$(nproc); [ "$CPUS" -gt 16 ] && CPUS=16
MEM_GB=$(awk '/^MemTotal:/ { g = int($2 * 0.4 / 1048576); print (g > 24 ? 24 : (g < 4 ? 4 : g)) }' /proc/meminfo)
GPU=()
for n in /sys/class/drm/renderD*; do
    [ "$(cat "$n/device/vendor" 2>/dev/null)" = 0x10de ] && continue
    GPU=(--device "/dev/dri/${n##*/}"); break
done

echo "== building the source package and the RPM (fedora:44)"
podman run --rm --memory=${MEM_GB}g --cpus="$CPUS" --cpu-shares=128 -v "$SRC:/repo:ro,Z" -v "$OUT/rpms:/out:Z" \
    -v "$OUT/plants:/plants:ro,Z" registry.fedoraproject.org/fedora:44 bash -c '
    set -e
    dnf -y -q install git rpm-build dnf-plugins-core make patch >/dev/null
    git config --global --add safe.directory "*"
    git clone -q /repo /build && cd /build
    for p in /plants/*.patch; do [ -e "$p" ] || continue; patch -s -p1 < "$p"; done
    [ -z "$(git status --porcelain)" ] || git -c user.name=plant -c user.email=plant@localhost commit -qam "planted faults"
    # as COPR calls it (its spec= is the checkout folder when no spec file is set)
    make -f .copr/Makefile srpm outdir=/out spec=/build
    dnf -y -q builddep /out/*.src.rpm >/dev/null
    rpmbuild --rebuild --define "_rpmdir /out" --define "_smp_mflags -j'"$CPUS"'" /out/*.src.rpm
    ' > "$OUT/build.log" 2>&1 || { echo "FAIL  the package didn't build ($OUT/build.log)"; tail -30 "$OUT/build.log"; exit 1; }
pkg=$(find "$OUT/rpms" -name 'ectolens-[0-9]*.x86_64.rpm' | head -1)
case "$pkg" in *-0.0.fc*) echo "FAIL  the RPM lost its snapshot release: $(basename "$pkg")"; exit 1;; esac
[ -n "$pkg" ] || { echo "FAIL  no RPM was made"; exit 1; }
echo "PASS  built $(basename "$(find "$OUT/rpms" -name '*.src.rpm' | head -1)") and $(basename "$pkg")"

echo "== installing it as a stranger would"
tag=$(podman images --format '{{.Repository}}:{{.Tag}}' | grep '^localhost/ectolens-stranger-fedora-44:' | grep -v ':desktop$' | head -1)
[ -n "$tag" ] || { echo "run tests/install/stranger-check.sh fedora-44 first: it makes the desktop image"; exit 1; }
rc=0
podman run --rm --memory=${MEM_GB}g --cpus="$CPUS" --cpu-shares=128 "${GPU[@]}" -v "$OUT/rpms:/rpms:ro,Z" \
    -v "$HERE/rpm-inside.sh:/rpm-inside.sh:ro,Z" --user root "$tag" bash -c '
    rpm -q spectacle >/dev/null || exit 3
    dnf -y -q install /rpms/x86_64/ectolens-[0-9]*.x86_64.rpm >/tmp/dnf.log 2>&1 && echo "PASS  dnf installed the package beside $(rpm -q spectacle)" || { echo "FAIL  dnf install"; tail -20 /tmp/dnf.log; exit 1; }
    # the removal is judged whatever the checks above found
    sudo -u stranger env -i HOME=/home/stranger USER=stranger LANG=C.UTF-8 PATH=/usr/bin:/bin bash /rpm-inside.sh || echo "FAIL  the checks as the stranger (above)"
    dnf -y -q remove ectolens >/dev/null 2>&1 && echo "PASS  dnf removed it"
    left=$(find / -xdev \( -path /proc -o -path /home -o -path /tmp -o -path /var/cache -o -path /var/lib/dnf -o -path /var/log \) -prune -o -iname "*ectolens*" -print 2>/dev/null | head)
    [ -z "$left" ] && echo "PASS  nothing of Ectolens is left in the system after dnf remove" || { echo "FAIL  left after removal:"; echo "$left"; }
    rpm -q spectacle >/dev/null && echo "PASS  Spectacle is still installed"
    ' > "$OUT/install.log" 2>&1 || rc=$?
cat "$OUT/install.log" | grep -E '^(PASS|FAIL)  |^  '
if [ "$rc" != 0 ] || grep -q '^FAIL ' "$OUT/install.log"; then echo "FAIL  the package (log $OUT/install.log)"; exit 1; fi
echo "PASS  the package"
