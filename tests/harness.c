// clevofan.c minus <linux/*> includes, on the stubs below, driven via hwmon, PM, init/exit.
// Each scenario runs in its own child process, so module state starts fresh.
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *scenario;
static int failures;
#define CHECK(c, ...)                                            \
	do {                                                         \
		if (!(c)) {                                              \
			printf("FAIL  %s:%d %s: ", __FILE__, __LINE__, scenario); \
			printf(__VA_ARGS__);                                 \
			printf("\n");                                        \
			failures++;                                          \
		}                                                        \
	} while (0)

/* --- kernel stubs ------------------------------------------------------ */
typedef uint8_t u8;
typedef uint32_t u32;
typedef unsigned short umode_t;
typedef void *acpi_handle;

#define KBUILD_MODNAME "clevofan"
#define pr_info(fmt, ...) ((void)(0 && printf(pr_fmt(fmt), ##__VA_ARGS__)))
#define pr_warn pr_info
#define pr_err pr_info
#define __init
#define __exit
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define S_IRUGO 0444
#define S_IWUSR 0200
#define MODULE_DEVICE_TABLE(type, name)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define MODULE_VERSION(x)
#define MODULE_PARM_DESC(n, d)
#define module_param(n, t, p)
#define module_init(f) static int (*const mod_init)(void) = f
#define module_exit(f) static void (*const mod_exit)(void) = f
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR_OR_ZERO(p) (IS_ERR(p) ? (int)PTR_ERR(p) : 0)

struct mutex { bool locked; };
#define DEFINE_MUTEX(m) struct mutex m = { false }
static struct mutex *guard_enter(struct mutex *m)
{
	CHECK(!m->locked, "fan_lock taken recursively");
	m->locked = true;
	return m;
}
static void guard_exit(struct mutex **m) { (*m)->locked = false; }
#define GUARD_CAT2(a, b) a##b
#define GUARD_CAT(a, b) GUARD_CAT2(a, b)
#define guard(type) struct mutex *__attribute__((cleanup(guard_exit))) GUARD_CAT(guard_, __COUNTER__) = guard_enter

enum dmi_field { DMI_NONE, DMI_BOARD_VENDOR, DMI_BOARD_NAME, DMI_FIELD_MAX };
struct dmi_strmatch { enum dmi_field slot; const char *substr; };
struct dmi_system_id { struct dmi_strmatch matches[4]; };
#define DMI_MATCH(a, b) { .slot = a, .substr = b }
static const char *dmi[DMI_FIELD_MAX];
static const char *dmi_get_system_info(int f) { return dmi[f]; }
static bool dmi_match(enum dmi_field f, const char *s) { return dmi[f] && !strcmp(dmi[f], s); } // exact, as dmi_scan.c
static const struct dmi_system_id *dmi_first_match(const struct dmi_system_id *l)
{
	for (; l->matches[0].slot != DMI_NONE; l++) // substring, as dmi_matches() for DMI_MATCH
		if (dmi[l->matches[0].slot] && strstr(dmi[l->matches[0].slot], l->matches[0].substr))
			return l;
	return NULL;
}

struct device { int unused; };
enum hwmon_sensor_types { hwmon_chip, hwmon_temp, hwmon_in, hwmon_curr, hwmon_power, hwmon_energy, hwmon_humidity,
			  hwmon_fan, hwmon_pwm };
enum { hwmon_fan_enable, hwmon_fan_input, hwmon_fan_label };
enum { hwmon_pwm_input, hwmon_pwm_enable };
#define HWMON_F_INPUT (1u << hwmon_fan_input)
#define HWMON_F_LABEL (1u << hwmon_fan_label)
#define HWMON_PWM_INPUT (1u << hwmon_pwm_input)
#define HWMON_PWM_ENABLE (1u << hwmon_pwm_enable)
struct hwmon_channel_info { enum hwmon_sensor_types type; const u32 *config; };
#define HWMON_CHANNEL_INFO(stype, ...) \
	(&(const struct hwmon_channel_info){ .type = hwmon_##stype, .config = (const u32[]){ __VA_ARGS__, 0 } })
struct hwmon_ops {
	umode_t (*is_visible)(const void *, enum hwmon_sensor_types, u32, int);
	int (*read)(struct device *, enum hwmon_sensor_types, u32, int, long *);
	int (*read_string)(struct device *, enum hwmon_sensor_types, u32, int, const char **);
	int (*write)(struct device *, enum hwmon_sensor_types, u32, int, long);
};
struct hwmon_chip_info { const struct hwmon_ops *ops; const struct hwmon_channel_info *const *info; };
static struct device hwmon_dev;
static const struct hwmon_chip_info *chip; // non-NULL while the hwmon sysfs attributes exist
static struct device *devm_hwmon_device_register_with_info(struct device *d, const char *name, void *drvdata,
							     const struct hwmon_chip_info *c, const void *groups)
{
	chip = c;
	return &hwmon_dev;
}

struct platform_device { struct device dev; };
struct platform_driver { struct { const char *name; } driver; };
static struct platform_device pdev;
static bool dev_registered, drv_registered;
static struct platform_device *platform_create_bundle(struct platform_driver *drv, int (*probe)(struct platform_device *),
						      void *res, unsigned int nres, const void *data, size_t size)
{
	int ret = probe(&pdev);

	if (ret)
		return ERR_PTR(ret);
	dev_registered = drv_registered = true;
	return &pdev;
}
static void platform_device_unregister(struct platform_device *p)
{
	dev_registered = false;
	chip = NULL;
}
static void platform_driver_unregister(struct platform_driver *d) { drv_registered = false; }

enum { PM_HIBERNATION_PREPARE = 1, PM_POST_HIBERNATION, PM_SUSPEND_PREPARE, PM_POST_SUSPEND, PM_RESTORE_PREPARE,
       PM_POST_RESTORE };
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };
static struct notifier_block *pm_nb;
static int register_pm_notifier(struct notifier_block *nb) { pm_nb = nb; return 0; }
static int unregister_pm_notifier(struct notifier_block *nb) { pm_nb = NULL; return 0; }

static acpi_handle ec_get_handle(void) { return &pdev; }
static void msleep(unsigned int ms) {}
static int ec_transaction(u8 command, const u8 *wdata, unsigned int wlen, u8 *rdata, unsigned int rlen);
static int ec_read(u8 addr, u8 *val);

#include "clevofan.c"

/* --- EC model ---------------------------------------------------------- */
struct ec_write { u8 cmd, port, value; };
static struct ec_write ec_log[64];
static int ec_n, ec_fail_at = -1; // ec_fail_at: index of the transaction that returns -EIO
static u8 ec_ram[256];
static int torn_updates, read_fail;

static int ec_transaction(u8 command, const u8 *wdata, unsigned int wlen, u8 *rdata, unsigned int rlen)
{
	CHECK(fan_lock.locked || !(chip || pm_nb || dev_registered || drv_registered),
	      "EC write without fan_lock while sysfs, the driver or the PM notifier is live");
	CHECK(wlen == 2 && rlen == 0 && ec_n < (int)ARRAY_SIZE(ec_log), "unexpected transaction shape");
	ec_log[ec_n] = (struct ec_write){ command, wdata[0], wdata[1] };
	return ec_n++ == ec_fail_at ? -EIO : 0;
}

static int ec_read(u8 addr, u8 *val)
{
	if (read_fail)
		return -EIO;
	*val = ec_ram[addr];
	if ((addr & 1) && torn_updates) { // EC bumps the high byte right after the low byte was sampled
		torn_updates--;
		ec_ram[addr - 1]++;
	}
	return 0;
}

/* --- helpers ----------------------------------------------------------- */
static int load(const char *vendor, const char *board, int force)
{
	dmi[DMI_BOARD_VENDOR] = vendor;
	dmi[DMI_BOARD_NAME] = board;
	force_match = force;
	return mod_init();
}
static int channels(enum hwmon_sensor_types t)
{
	int i, n = 0;

	for (i = 0; chip && chip->info[i]; i++)
		if (chip->info[i]->type == t)
			while (chip->info[i]->config[n])
				n++;
	return n;
}
static int wr(enum hwmon_sensor_types t, u32 attr, int ch, long val)
{
	CHECK(chip && ch < channels(t), "write to a channel that is not registered");
	return chip ? chip->ops->write(&hwmon_dev, t, attr, ch, val) : -ENODEV;
}
static long rd(enum hwmon_sensor_types t, u32 attr, int ch, int *ret)
{
	long v = -12345;
	int r;

	CHECK(chip && ch < channels(t), "read of a channel that is not registered");
	r = chip ? chip->ops->read(&hwmon_dev, t, attr, ch, &v) : -ENODEV;
	if (ret)
		*ret = r;
	else
		CHECK(r == 0, "read returned %d", r);
	return v;
}
static bool ec_is(int i, u8 port, u8 value)
{
	return i < ec_n && ec_log[i].cmd == FAN_DUTY_CMD && ec_log[i].port == port && ec_log[i].value == value;
}

/* --- scenarios --------------------------------------------------------- */
struct board { const char *vendor, *name; int force, ret, fans; };
static const struct board boards[] = {
	{ "CLEVO CO.", "W35_37ET", 0, 0, 1 },
	{ "CLEVO CO.", "W350SS", 0, 0, 1 },
	{ "CLEVO CO.", "P170SM-A", 0, 0, 2 },
	{ "CLEVO CO.", "P65xHP", 0, 0, 1 },
	{ "Notebook", "V5xTNC_TND_TNE", 0, 0, 2 },
	{ "CLEVO CO.", "UNKNOWN", 0, -ENODEV, 0 },
	{ "Notebook", "W35_37ET", 0, -ENODEV, 0 },
	{ NULL, "W35_37ET", 0, -ENODEV, 0 },
	{ "CLEVO CO.", "UNKNOWN", 3, 0, 3 },
	{ "CLEVO CO.", "UNKNOWN", 4, -EINVAL, 0 },
	{ "CLEVO CO.", "UNKNOWN", -1, -EINVAL, 0 },
};

static void board(const void *arg)
{
	const struct board *b = arg;
	int ret = load(b->vendor, b->name, b->force);

	CHECK(ret == b->ret, "init returned %d, want %d", ret, b->ret);
	if (ret) {
		CHECK(!chip && !pm_nb && !dev_registered && !drv_registered, "failed init left registrations behind");
		return;
	}
	CHECK(chip, "init succeeded without registering hwmon");
	if (!chip)
		return;
	CHECK(channels(hwmon_fan) == b->fans && channels(hwmon_pwm) == b->fans, "%d fan / %d pwm channels, want %d",
	      channels(hwmon_fan), channels(hwmon_pwm), b->fans);
	CHECK(chip->ops->is_visible(NULL, hwmon_fan, hwmon_fan_input, 0) == 0444 &&
	      chip->ops->is_visible(NULL, hwmon_pwm, hwmon_pwm_enable, 0) == 0644 &&
	      chip->ops->is_visible(NULL, hwmon_temp, 0, 0) == 0, "attribute modes");
	mod_exit();
	CHECK(ec_n == 0, "load/unload in auto mode wrote the EC");
}

static void tach(const void *arg)
{
	static const u8 pair[3][2] = { { 0x01, 0x00 }, { 0x02, 0x00 }, { 0x04, 0x00 } }; // 0xD0/1, 0xD2/3, 0xD4/5
	int ch, ret;

	CHECK(load("CLEVO CO.", "UNKNOWN", 3) == 0, "init");
	for (ch = 0; ch < 3; ch++)
		memcpy(&ec_ram[0xD0 + 2 * ch], pair[ch], 2);
	for (ch = 0; ch < 3; ch++)
		CHECK(rd(hwmon_fan, hwmon_fan_input, ch, NULL) == EC_TICKS_PER_MINUTE / (pair[ch][0] << 8),
		      "channel %d does not read its own tach pair", ch);
	torn_updates = 1; // 0x0200 -> 0x0300 between the two high-byte reads
	CHECK(rd(hwmon_fan, hwmon_fan_input, 1, NULL) == EC_TICKS_PER_MINUTE / 0x300, "torn read not retried");
	torn_updates = 3;
	rd(hwmon_fan, hwmon_fan_input, 1, &ret);
	CHECK(ret == -EAGAIN, "unstable tach returned %d, want -EAGAIN", ret);
	torn_updates = 0;
	ec_ram[0xD0] = 0;
	CHECK(rd(hwmon_fan, hwmon_fan_input, 0, NULL) == 0, "zero ticks is 0 rpm");
	read_fail = 1;
	rd(hwmon_fan, hwmon_fan_input, 0, &ret);
	CHECK(ret == -EIO, "ec_read error returned %d, want -EIO", ret);
	CHECK(ec_n == 0, "reads wrote the EC");
}

static void pwm(const void *arg)
{
	static const long bad_pwm[] = { -1, 256, 1000 }, bad_enable[] = { -1, 3, 1000 }, good_pwm[] = { 0, 255, 128 };
	unsigned int i;
	int n;

	CHECK(load("Notebook", "V5xTNC_TND_TNE", 0) == 0, "init");
	CHECK(wr(hwmon_pwm, hwmon_pwm_input, 1, 100) == -EOPNOTSUPP && ec_n == 0, "pwm write accepted in auto mode");
	CHECK(rd(hwmon_pwm, hwmon_pwm_enable, 1, NULL) == 2, "initial mode is auto");
	CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 1, 1) == 0 && ec_n == 0, "enable=1");
	CHECK(rd(hwmon_pwm, hwmon_pwm_enable, 1, NULL) == 1, "enable=1 readback");
	for (i = 0; i < ARRAY_SIZE(bad_pwm); i++)
		CHECK(wr(hwmon_pwm, hwmon_pwm_input, 1, bad_pwm[i]) == -EINVAL && ec_n == 0, "pwm %ld", bad_pwm[i]);
	for (i = 0; i < ARRAY_SIZE(good_pwm); i++) {
		CHECK(wr(hwmon_pwm, hwmon_pwm_input, 1, good_pwm[i]) == 0, "pwm %ld", good_pwm[i]);
		CHECK(ec_is(ec_n - 1, 2, good_pwm[i]), "pwm %ld not sent to control index 2", good_pwm[i]);
		CHECK(rd(hwmon_pwm, hwmon_pwm_input, 1, NULL) == good_pwm[i], "pwm readback");
	}
	n = ec_n;
	for (i = 0; i < ARRAY_SIZE(bad_enable); i++)
		CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 1, bad_enable[i]) == -EINVAL && ec_n == n, "enable %ld",
		      bad_enable[i]);
	CHECK(rd(hwmon_pwm, hwmon_pwm_enable, 1, NULL) == 1, "invalid enable changed the mode");
	for (i = 0; i < 2; i++) { // enable=2 and enable=0 both restore EC auto mode
		long mode = i ? 0 : 2;

		CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 1, 1) == 0 && wr(hwmon_pwm, hwmon_pwm_input, 1, 77) == 0, "manual");
		n = ec_n;
		CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 1, mode) == 0, "enable=%ld", mode);
		CHECK(ec_n == n + 2 && ec_is(n, 2, 0) && ec_is(n + 1, FAN_PORT_AUTO_MODE, 2), "enable=%ld EC sequence", mode);
		CHECK(rd(hwmon_pwm, hwmon_pwm_enable, 1, NULL) == 2, "enable=%ld readback", mode);
	}
	mod_exit();
}

static void ec_errors(const void *arg)
{
	int n;

	CHECK(load("Notebook", "V5xTNC_TND_TNE", 0) == 0, "init");
	CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 0, 1) == 0 && wr(hwmon_pwm, hwmon_pwm_input, 0, 60) == 0, "manual 60");
	ec_fail_at = ec_n;
	CHECK(wr(hwmon_pwm, hwmon_pwm_input, 0, 90) == -EIO, "ec_transaction error not propagated by the pwm write");
	CHECK(rd(hwmon_pwm, hwmon_pwm_input, 0, NULL) == 60, "failed pwm write changed the cached duty");
	n = ec_n;
	ec_fail_at = n + 1; // duty 0 succeeds, the auto-mode command fails
	CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 0, 2) == -EIO, "auto-mode error not propagated");
	CHECK(ec_n == n + 3 && ec_is(n, 1, 0) && ec_is(n + 1, FAN_PORT_AUTO_MODE, 1) && ec_is(n + 2, 1, 60),
	      "failed auto mode did not restore duty 60");
	CHECK(rd(hwmon_pwm, hwmon_pwm_enable, 0, NULL) == 1 && rd(hwmon_pwm, hwmon_pwm_input, 0, NULL) == 60,
	      "failed auto mode changed the state");
	ec_fail_at = -1;
	mod_exit();
}

static void resume(const void *arg)
{
	int n;

	CHECK(load("Notebook", "V5xTNC_TND_TNE", 0) == 0, "init");
	CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 0, 1) == 0 && wr(hwmon_pwm, hwmon_pwm_input, 0, 50) == 0, "manual 50");
	n = ec_n;
	CHECK(pm_nb && pm_nb->notifier_call(pm_nb, PM_SUSPEND_PREPARE, NULL) == 0 && ec_n == n, "suspend wrote the EC");
	CHECK(pm_nb->notifier_call(pm_nb, PM_POST_SUSPEND, NULL) == 0, "resume");
	CHECK(ec_n == n + 1 && ec_is(n, 1, 50), "resume did not re-drive only the manual fan to 50");
	CHECK(!fan_lock.locked, "fan_lock leaked");
	mod_exit();
}

static void unload(const void *arg)
{
	int n;

	CHECK(load("Notebook", "V5xTNC_TND_TNE", 0) == 0, "init");
	CHECK(wr(hwmon_pwm, hwmon_pwm_enable, 1, 1) == 0 && wr(hwmon_pwm, hwmon_pwm_input, 1, 200) == 0, "manual 200");
	n = ec_n;
	mod_exit(); // the EC write check rejects a restore that runs while anything is still registered
	CHECK(!chip && !pm_nb && !dev_registered && !drv_registered, "exit left registrations behind");
	CHECK(ec_n == n + 2 && ec_is(n, 2, 0) && ec_is(n + 1, FAN_PORT_AUTO_MODE, 2), "exit did not restore auto mode");
}

static int run(const char *name, void (*fn)(const void *), const void *arg, const char *claim)
{
	int status;
	pid_t pid;

	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		scenario = name;
		fn(arg);
		if (!failures)
			printf("ok    %s: %s\n", name, claim);
		exit(failures != 0);
	}
	return waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status);
}

int main(void)
{
	char name[64];
	unsigned int i;
	int bad = 0;

	for (i = 0; i < ARRAY_SIZE(boards); i++) {
		snprintf(name, sizeof(name), "board %s/%s force=%d", boards[i].vendor ? boards[i].vendor : "(null)",
			 boards[i].name, boards[i].force);
		bad |= run(name, board, &boards[i], "init result, fan/pwm channel count, attribute modes");
	}
	bad |= run("tach", tach, NULL, "channel 0/1/2 read 0xD0/0xD2/0xD4, torn read retried, -EAGAIN and -EIO propagated");
	bad |= run("pwm", pwm, NULL, "pwm 0..255 to control index+1, pwm_enable 0/1/2 only, auto-mode EC sequence");
	bad |= run("ec_errors", ec_errors, NULL, "EC write errors propagated, failed auto mode restores the duty");
	bad |= run("resume", resume, NULL, "PM_POST_SUSPEND re-drives manual fans under fan_lock");
	bad |= run("unload", unload, NULL, "exit restores auto mode only after every registration is gone");
	printf(bad ? "FAIL  clevofan harness\n" : "ok    clevofan harness: every EC write held fan_lock or ran after teardown\n");
	return bad;
}
