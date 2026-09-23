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
	# hwmon channel -> tach register pair: 0 CPU 0xD0/1, 1 GPU 0xD2/3, 2 GPU2 0xD4/5
	[ "$(grep -A1 -E '^    if \(index == [0-9]\)$' "$source" | grep -oE 'index == [0-9]|[A-Z0-9_]+_SPEED_OFFSET_0' | tr '\n' ' ')" = \
	  'index == 0 CPU_FAN_SPEED_OFFSET_0 index == 1 GPU_FAN_SPEED_OFFSET_0 index == 2 GPU_FAN2_SPEED_OFFSET_0 ' ] || return 1
	awk '/attr == hwmon_pwm_enable\)$/ { f = 1 } f && /return -EINVAL;/ { ok = 1 } f && /return 0;/ { exit !ok }' "$source" || return 1
	# module exit: notifier and sysfs gone before auto mode is restored
	awk '/^static void __exit clevofan_exit/ { f = 1 }
	     f && /unregister_pm_notifier\(&nb\);/ { pm = NR }
	     f && /platform_device_unregister\(clevo_platdvc\);/ { dev = NR }
	     f && /fan_auto_mode\(i\);/ { fan = NR }
	     f && /^}/ { exit !(pm && dev && fan && pm < fan && dev < fan) }' "$source" || return 1
}

check_source "$repo/clevofan.c" || { echo "clevofan.c failed: $( (set -x; check_source "$repo/clevofan.c") 2>&1 | grep -v "^+ return" | grep "^+ " | tail -1)" >&2; exit 1; }

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

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i '/^static void __exit clevofan_exit/,/^}/{/platform_device_unregister(clevo_platdvc);/d;s/^    pr_info("exiting module\\n");$/&\n    platform_device_unregister(clevo_platdvc);/}' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'exit restores auto mode before sysfs removal mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i '/attr == hwmon_pwm_enable)$/,/return 0;/{/return -EINVAL;/d}' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'pwm_enable accepts any value mutation passed' >&2
	exit 1
fi

cp "$repo/clevofan.c" "$tmp/clevofan.c"
sed -i 's/CPU_FAN_SPEED_OFFSET_0,$/GPU_FAN_SPEED_OFFSET_0,/;t;s/        return fan_read_ticks(GPU_FAN_SPEED_OFFSET_0,$/        return fan_read_ticks(CPU_FAN_SPEED_OFFSET_0,/' "$tmp/clevofan.c"
if check_source "$tmp/clevofan.c"; then
	echo 'swapped CPU/GPU tachometer mutation passed' >&2
	exit 1
fi

echo 'clevofan source checks passed'
