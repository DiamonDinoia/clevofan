#!/bin/sh
# Builds tests/harness.c around clevofan.c with ASan/UBSan and runs it, then requires every mutant
# below to compile cleanly and fail the harness.
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/clevofan-harness.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

build()
{
	mkdir -p "$tmp/$1"
	sed '/^#include <linux\//d' "$2" > "$tmp/$1/clevofan.c"
	${CC:-cc} -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Werror -fno-omit-frame-pointer \
		-fsanitize=address,undefined -fno-sanitize-recover=all -I"$tmp/$1" \
		-o "$tmp/$1/harness" "$repo/tests/harness.c"
}

build head "$repo/clevofan.c"
"$tmp/head/harness"

bad=0
while IFS='|' read -r name expr; do
	perl -0pe "$expr" "$repo/clevofan.c" > "$tmp/$name.c"
	if cmp -s "$repo/clevofan.c" "$tmp/$name.c"; then
		echo "FAIL  mutant $name: substitution did not apply"; bad=1; continue
	fi
	if ! build "$name" "$tmp/$name.c" > "$tmp/$name.log" 2>&1; then
		echo "FAIL  mutant $name: does not compile"; cat "$tmp/$name.log"; bad=1; continue
	fi
	if "$tmp/$name/harness" > "$tmp/$name.log" 2>&1; then
		echo "FAIL  mutant $name: harness passed"; bad=1
	else
		echo "ok    mutant $name fails: $(grep -m1 -E '^FAIL  .*:[0-9]|runtime error|ERROR: AddressSanitizer' "$tmp/$name.log" | sed 's/^FAIL  [^ ]* //')"
	fi
done <<'EOF'
pm-guard-after-loop|s/\n            guard\(mutex\)\(&fan_lock\);\n(            for\(i=0.*?\n            \}\n)/\n$1            guard(mutex)(&fan_lock);\n/s
write-guard-in-pwm-input|s/(long val\)\n\{\n)    guard\(mutex\)\(&fan_lock\);\n(.*?attr == hwmon_pwm_input\) \n        \{\n)/$1$2            guard(mutex)(&fan_lock);\n/s
fan-count-1-2-swapped|s/(W350SS"  \)  \)\n        return )1;/${1}2;/; s/(is_juno_v5\(\) \)[^\n]*\n        return )2;/${1}1;/
pwm-enable-0-rejected|s/val == 2 \|\| val == 0/val == 2/
driver-unregister-after-restore|s/\n    platform_driver_unregister\(&clevo_platdrv\);\n(    for\(i=0; i<fan_count;i\+\+\) \{\n.*?\n    \}\n)/\n$1    platform_driver_unregister(&clevo_platdrv);\n/s
ec-error-ignored-in-fan_set_pwm|s/(fan_control_index\(index\), value\);\n    if\(ret != 0\) \n        )return ret;/${1}ret = 0;/
device-unregister-after-restore|s/\n    platform_device_unregister\(clevo_platdvc\);[^\n]*\n(    platform_driver_unregister\(&clevo_platdrv\);\n    for\(i=0; i<fan_count;i\+\+\) \{\n.*?\n    \}\n)/\n$1    platform_device_unregister(clevo_platdvc);\n/s
pm-notifier-unregister-last|s/\n    unregister_pm_notifier\(&nb\);\n(.*?pr_info\("exiting module\\n"\);\n)/\n$1    unregister_pm_notifier(&nb);\n/s
pm-guard-removed|s/\n            guard\(mutex\)\(&fan_lock\);\n/\n/
write-guard-removed|s/(long val\)\n\{\n)    guard\(mutex\)\(&fan_lock\);\n/$1/
pwm-enable-accepts-any|s/\n             else\n                return -EINVAL;\n/\n/
pwm-range-unchecked|s/val < 0 \|\| val > 255/val < 0/
tach-cpu-gpu-swapped|s/(index == 0\)\n        return fan_read_ticks\()CPU(.*?index == 1\)\n        return fan_read_ticks\()GPU/${1}GPU${2}CPU/s
tach-torn-read-accepted|s/first_before == first_after/1/
tach-error-dropped|s/(ret = fan_read_ticks_by_index\(channel, &ec_ticks_per_rotation\);\n        if \(ret\)\n            return )ret;/${1}0;/
auto-mode-restores-zero|s/fan_control_index\(index\), previous_pwm\)/fan_control_index(index), previous_pwm * 0)/
p170sm-exact-name|s/dmi_match\(DMI_BOARD_NAME, "P170SM-A"\)/dmi_match(DMI_BOARD_NAME, "P170SM")/
juno-vendor-bypass-removed|s/if\(!is_juno_v5\(\) &&\n\s*/if(/
force-match-unbounded|s/force_match < 0 \|\| force_match > 3/force_match < 0/
control-index-off-by-one|s/return index \+ 1;/return index;/
EOF
[ "$bad" -eq 0 ] || exit 1
echo "ok    positive controls: all mutants compile and fail the harness"
