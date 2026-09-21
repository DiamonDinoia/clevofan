#!/bin/bash
# Builds this fork's clevofan-dkms deb and installs it in a clean debian:sid
# container. Checks: (a) the package installs alone (no dependency cycle,
# unlike the sibling forks); (b) dkms actually builds clevofan.ko against
# real kernel headers and modinfo reports the module dkms.conf describes;
# (c) purge leaves no dkms registration and no /usr/src payload.
#
# The container shares the host kernel, whose exact version apt's
# linux-headers-amd64 cannot match unless the host runs stock Debian, so (b)
# reads the kernelver dkms actually built for back out of `dkms status`
# rather than assuming the container's own `uname -r`.
#
# Needs podman or docker. With no argument it builds first; a built .deb path
# can be passed to skip the build.
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
engine=$(command -v podman || command -v docker) || {
  echo "FAIL  no podman or docker; this check cannot run"; exit 1; }

"$root/test-dkms-conf.sh"

if [ $# -eq 0 ]; then
  (cd "$root" && dpkg-buildpackage -b -uc -us >/dev/null)
  deb="$root/../clevofan-dkms_$(dpkg-parsechangelog -l "$root/debian/changelog" -SVersion)_amd64.deb"
else
  deb=$1
fi
[ -f "$deb" ] || { echo "FAIL  no built .deb at $deb"; exit 1; }
version=$(dpkg-parsechangelog -l "$root/debian/changelog" -SVersion)

build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cp "$deb" "$build/"

"$engine" run --rm -i -v "$build:/build:ro" -e "VERSION=$version" \
    debian:sid bash -eo pipefail <<'SCRIPT'
sed -i 's/^Components: main$/Components: main contrib non-free non-free-firmware/' \
    /etc/apt/sources.list.d/debian.sources
apt-get update -qq
apt-get install -y --no-install-recommends dkms linux-headers-amd64 >/dev/null

# --- (a) installs alone -----------------------------------------------------
apt-get install -y /build/clevofan-dkms_*.deb </dev/null >/tmp/install.log 2>&1 ||
  { echo "FAIL  clevofan-dkms did not install alone"; tail -20 /tmp/install.log; exit 1; }
st=$(dpkg-query -W -f '${db:Status-Abbrev}' clevofan-dkms)
[[ "$st" == ii* ]] || { echo "FAIL  clevofan-dkms dpkg state '$st', not ii"; exit 1; }
echo "ok    clevofan-dkms installed alone (ii)"

# --- (b) dkms actually built clevofan.ko, modinfo matches ------------------
status=$(dkms status -m clevofan)
echo "$status" | grep -q installed ||
  { echo "FAIL  dkms status does not report installed: $status"; exit 1; }
echo "ok    dkms status: $status"
hdrver=$(sed -n 's/^clevofan\/[^,]*, \([^,]*\),.*/\1/p' <<<"$status" | head -1)
[ -n "$hdrver" ] || { echo "FAIL  could not parse a kernelver out of dkms status"; exit 1; }

ko="/lib/modules/$hdrver/updates/dkms/clevofan.ko"
[ -f "$ko" ] || { echo "FAIL  $ko not present after dkms install"; exit 1; }
info=$(modinfo "$ko")
echo "$info" | grep -qE '^license: *GPL$' ||
  { echo "FAIL  modinfo license mismatch: $info"; exit 1; }
echo "$info" | grep -qE '^name: *clevofan$' ||
  { echo "FAIL  modinfo name is not clevofan: $info"; exit 1; }
echo "$info" | grep -q '^version: *1.1$' ||
  { echo "FAIL  modinfo version is not 1.1: $info"; exit 1; }
echo "ok    modinfo $ko: license GPL, name clevofan, version 1.1"

# --- (c) purge leaves no trace ----------------------------------------------
apt-get purge -y clevofan-dkms >/dev/null </dev/null
[ -z "$(dkms status clevofan 2>/dev/null || true)" ] ||
  { echo "FAIL  dkms still tracks clevofan after purge"; exit 1; }
[ ! -e "/usr/src/clevofan-$VERSION" ] ||
  { echo "FAIL  /usr/src/clevofan-$VERSION survived purge"; exit 1; }
echo "ok    purge leaves no dkms registration and no /usr/src payload"

echo "ok    clevofan-dkms $VERSION: standalone install, dkms build, purge all verified"
SCRIPT
