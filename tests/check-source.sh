#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

check_source()
{
	source=$1

	grep -Fq 'return ec_transaction(cmd, data, ARRAY_SIZE(data), NULL, 0);' "$source" || return 1
	if grep -Eq '\<(inb|outb)\(' "$source"; then
		return 1
	fi
	[ "$(grep -Fc 'ec_read(first_offset' "$source")" -eq 2 ] || return 1
	[ "$(grep -Fc 'ec_read(second_offset' "$source")" -eq 1 ] || return 1
	grep -Fq 'if (first_before == first_after)' "$source" || return 1
	if grep -Fq 'DMI_MATCH(DMI_BOARD_VENDOR' "$source"; then
		return 1
	fi
	grep -Fq 'force_match < 0 || force_match > 3' "$source" || return 1
	if grep -Fq 'return 4;' "$source"; then
		return 1
	fi
	[ "$(grep -Fc 'GPU_FAN2_SPEED_OFFSET_0' "$source")" -eq 2 ] || return 1
	grep -Fq 'restore_ret = ec_io_do' "$source" || return 1
	[ "$(grep -Fc 'guard(mutex)(&fan_lock);' "$source")" -eq 2 ] || return 1
}

check_source "$repo/clevofan.c"

tmp=$(mktemp -d /tmp/clevofan-source-check.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i 's/return ec_transaction(cmd, data, ARRAY_SIZE(data), NULL, 0);/return 0;/' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'unsafe EC transaction mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i 's/if (first_before == first_after)/if (true)/' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'torn tachometer mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i '0,/DMI_BOARD_NAME/s//DMI_BOARD_VENDOR/' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'broad DMI mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i 's/^    return index + 1;$/    if (is_juno_v5() \&\& index == 1)\n        return 4;\n    return index + 1;/' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'Juno fan mapping mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i '/restore_ret = ec_io_do/d' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'auto-mode recovery mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i '0,/guard(mutex)(&fan_lock);/{/guard(mutex)(&fan_lock);/d}' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'unlocked sysfs write mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i '/int8_t i;/,/guard(mutex)(&fan_lock);/{/guard(mutex)(&fan_lock);/d}' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'unlocked PM notifier mutation passed' >&2
	exit 1
fi

echo 'clevofan source checks passed'
