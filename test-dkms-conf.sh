#!/bin/sh
# Static consistency checks on dkms.conf: the debian version and the dkms
# module version must agree, and BUILT_MODULE_NAME must name the object the
# Makefile's obj-m actually builds. Either drift breaks dh_dkms silently
# (wrong /usr/src/<name>-<version> path, or a .ko dkms never finds).
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

check_dkms_conf()
{
	dkmsconf=$1
	changelog=$2
	makefile=$3

	pkgver=$(sed -n 's/^PACKAGE_VERSION=//p' "$dkmsconf")
	chlogver=$(dpkg-parsechangelog -l "$changelog" -SVersion)
	[ -n "$pkgver" ] || return 1
	[ "$pkgver" = "$chlogver" ] || return 1

	builtname=$(sed -n 's/^BUILT_MODULE_NAME\[0\]="\(.*\)"$/\1/p' "$dkmsconf")
	objtarget=$(sed -n 's/^obj-m := \(.*\)\.o$/\1/p' "$makefile")
	name=$(sed -n 's/^name := \(.*\)$/\1/p' "$makefile")
	objname=$(basename "$objtarget" | sed "s/\\\$(name)/$name/")
	[ -n "$builtname" ] && [ -n "$objname" ] || return 1
	[ "$builtname" = "$objname" ] || return 1
}

check_dkms_conf "$root/dkms.conf" "$root/debian/changelog" "$root/Makefile"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/clevofan-dkms-conf-check.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cp "$root/dkms.conf" "$tmp/version-drift.conf"
sed -i 's/^PACKAGE_VERSION=.*/PACKAGE_VERSION=9.9.9/' "$tmp/version-drift.conf"
if check_dkms_conf "$tmp/version-drift.conf" "$root/debian/changelog" "$root/Makefile"; then
	echo "FAIL  positive control: a mismatched PACKAGE_VERSION passed the check"; exit 1
fi

cp "$root/dkms.conf" "$tmp/name-drift.conf"
sed -i 's/^BUILT_MODULE_NAME\[0\]=.*/BUILT_MODULE_NAME[0]="wrong_name"/' "$tmp/name-drift.conf"
if check_dkms_conf "$tmp/name-drift.conf" "$root/debian/changelog" "$root/Makefile"; then
	echo "FAIL  positive control: a mismatched BUILT_MODULE_NAME passed the check"; exit 1
fi

echo "ok    dkms.conf PACKAGE_VERSION matches debian/changelog, BUILT_MODULE_NAME matches obj-m"
echo "ok    positive controls: version drift and BUILT_MODULE_NAME drift both fail the check"
