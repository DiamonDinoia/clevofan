#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/suspend.h>

#define FAN_DUTY_CMD                          0x99
#define FAN_PORT_AUTO_MODE                    0xFF

#define EC_TICKS_PER_MINUTE     1966080
#define CPU_FAN_SPEED_OFFSET_0  0xD0
#define CPU_FAN_SPEED_OFFSET_1  0xD1
#define GPU_FAN_SPEED_OFFSET_0  0xD2
#define GPU_FAN_SPEED_OFFSET_1  0xD3
#define GPU_FAN2_SPEED_OFFSET_0  0xD4
#define GPU_FAN2_SPEED_OFFSET_1  0xD5

#define FAN_READ_RETRIES 3
#define MODVERS "1.1"

static int force_match = 0;
static uint8_t fan_count;
static uint8_t pwm_curr_value[3] = { -1, -1, -1 };
static uint8_t fan_auto[3] =       {  1,  1,  1 };

static const struct dmi_system_id clevo_dmi[] = 
{    
    { .matches = { DMI_MATCH(DMI_BOARD_NAME, "W35_37ET"), }, },
    { .matches = { DMI_MATCH(DMI_BOARD_NAME, "W350SS"), }, },
    { .matches = { DMI_MATCH(DMI_BOARD_NAME, "P170SM-A"), }, },
    { .matches = { DMI_MATCH(DMI_BOARD_NAME, "P65xHP"), }, },
    { .matches = { DMI_MATCH(DMI_BOARD_NAME, "V5xTNC_TND_TNE"), }, },
    {}
};
MODULE_DEVICE_TABLE(dmi, clevo_dmi);

static bool is_juno_v5(void)
{
    return dmi_match(DMI_BOARD_NAME, "V5xTNC_TND_TNE");
}

static uint8_t get_fan_count(void)
{
    if( dmi_match(DMI_BOARD_NAME, "W35_37ET") ||          //mainboards with 1 fan
        dmi_match(DMI_BOARD_NAME, "W350SS"  )  )
        return 1;
        
    else if( dmi_match(DMI_BOARD_NAME, "P170SM") ||
             is_juno_v5() )                                //mainboards with 2 fans
        return 2;
    
    else if( dmi_match(DMI_BOARD_NAME, "XXXXXXXX") ||     //mainboards with 3 fans
             dmi_match(DMI_BOARD_NAME, "XXXXXXXX") )        
        return 3;
        
        else return 1;
}

static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value) {
    u8 data[] = { port, value };

    return ec_transaction(cmd, data, ARRAY_SIZE(data), NULL, 0);
}

static uint8_t fan_control_index(uint8_t index)
{
    return index + 1;
}

static int fan_read_ticks(uint8_t first_offset, uint8_t second_offset, int *ticks)
{
    uint8_t first_before, first_after, second;
    int i, ret;

    for (i = 0; i < FAN_READ_RETRIES; i++) {
        ret = ec_read(first_offset, &first_before);
        if (ret)
            return ret;
        ret = ec_read(second_offset, &second);
        if (ret)
            return ret;
        ret = ec_read(first_offset, &first_after);
        if (ret)
            return ret;
        if (first_before == first_after) {
            *ticks = (first_before << 8) | second;
            return 0;
        }
    }

    return -EAGAIN;
}

static int fan_read_ticks_by_index(uint8_t index, int *ticks)
{
    if (index == 0)
        return fan_read_ticks(CPU_FAN_SPEED_OFFSET_0,
                              CPU_FAN_SPEED_OFFSET_1, ticks);
    if (index == 1)
        return fan_read_ticks(GPU_FAN_SPEED_OFFSET_0,
                              GPU_FAN_SPEED_OFFSET_1, ticks);
    if (index == 2)
        return fan_read_ticks(GPU_FAN2_SPEED_OFFSET_0,
                              GPU_FAN2_SPEED_OFFSET_1, ticks);

    return -EINVAL;
}

static int fan_set_pwm(uint8_t value, uint8_t index)
{
    int ret;
    ret = ec_io_do(FAN_DUTY_CMD, fan_control_index(index), value);
    if(ret != 0) 
        return ret;
    pwm_curr_value[index] = value;
    fan_auto[index] = 0;
    return 0;
}

static int fan_auto_mode(uint8_t index)
{
    if(fan_auto[index] == 0) {
        int restore_ret, ret;
        uint8_t previous_pwm = pwm_curr_value[index];

        ret = ec_io_do(FAN_DUTY_CMD, fan_control_index(index), 0); //seems put fan off before setting auto mode is necessary
        if(ret != 0) 
            return ret;
        msleep(100); //value found with tests
        ret = ec_io_do(FAN_DUTY_CMD, FAN_PORT_AUTO_MODE,
                       fan_control_index(index));
        if(ret != 0) {
            restore_ret = ec_io_do(FAN_DUTY_CMD,
                                   fan_control_index(index), previous_pwm);
            if (restore_ret)
                pr_err("failed to restore fan %u after auto-mode error: %d\n",
                       index, restore_ret);
            return ret;
        }
        fan_auto[index] = 1;
        pwm_curr_value[index] = -1;
    }
    return 0;
}

static umode_t clevo_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
    if(type == hwmon_fan) {
        switch (attr) {
        case hwmon_fan_input:
        case hwmon_fan_label:
            return S_IRUGO;
        default:
            return 0;
        }
    }
    else if(type == hwmon_pwm) {
        switch (attr) {
        case hwmon_pwm_input:
        case hwmon_pwm_enable: 
            return (S_IRUGO | S_IWUSR);
        default:
            return 0;
        }
    }
    return 0;
}

static int clevo_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val)
{
    if(type == hwmon_fan)
    {
        int ec_ticks_per_rotation = 0;
        int ret;

        ret = fan_read_ticks_by_index(channel, &ec_ticks_per_rotation);
        if (ret)
            return ret;
        if (ec_ticks_per_rotation == 0)
            *val = 0;
        else
            *val = (EC_TICKS_PER_MINUTE/ec_ticks_per_rotation);
        return 0;
    }
    else if(type == hwmon_pwm)
    {
        if(attr == hwmon_pwm_input) {
            *val = pwm_curr_value[channel];
            return 0;
        }
        else if(attr == hwmon_pwm_enable) {
            if(fan_auto[channel] == 1) 
                *val = 2;
            else 
                *val = 1;
            return 0;
        }
        else return -EOPNOTSUPP;
    }
    return -EOPNOTSUPP;
}

static int clevo_hwmon_read_label(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, const char **str)
{
    if(type == hwmon_fan && attr == hwmon_fan_label) {
        if     (channel == 0) *str = "CPU Fan";
        else if(channel == 1) *str = "GPU Fan 1";
        else if(channel == 2) *str = "GPU Fan 2";
        return 0;
    }
    else return -EOPNOTSUPP;
}

static int clevo_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long val)
{
    if(type == hwmon_pwm) 
    {
        if(attr == hwmon_pwm_input) 
        {
            if (val < 0 || val > 255)
                return -EINVAL;
            if(fan_auto[channel] == 0)
                return fan_set_pwm(val, channel);
            else return -EOPNOTSUPP;
        }
        else if(attr == hwmon_pwm_enable)
        {
             if(val == 1)             
                 fan_auto[channel] = 0;
             else if(val == 2 || val == 0) 
                return fan_auto_mode(channel);
            return 0;
        }
        else return -EOPNOTSUPP;
    }
    return -EOPNOTSUPP;
}

static int clevo_pm_handler(struct notifier_block *nbp, unsigned long event_type, void *p) 
{
    switch (event_type) {
        case PM_POST_HIBERNATION:
        case PM_POST_SUSPEND:
        case PM_POST_RESTORE:
        {
            int8_t i;
            for(i=0; i<fan_count;i++) {
                if(fan_auto[i] == 0) 
                    fan_set_pwm(pwm_curr_value[i], i);
            }
        }
    }
    return 0;
}

static struct notifier_block nb = {
    .notifier_call = &clevo_pm_handler
};

static const struct hwmon_ops clevo_hwmon_ops = {
    .is_visible = clevo_hwmon_is_visible,
    .read = clevo_hwmon_read,
    .read_string = clevo_hwmon_read_label,
    .write = clevo_hwmon_write,
};

static const struct hwmon_channel_info *clevo_hwmon_info[] = {
    HWMON_CHANNEL_INFO(fan, 
        HWMON_F_LABEL | HWMON_F_INPUT),
    HWMON_CHANNEL_INFO(pwm, 
        HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
    NULL
};
static const struct hwmon_channel_info *clevo_hwmon_info2[] = {
    HWMON_CHANNEL_INFO(fan, 
        HWMON_F_LABEL | HWMON_F_INPUT,
        HWMON_F_LABEL | HWMON_F_INPUT),
    HWMON_CHANNEL_INFO(pwm, 
        HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
        HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
    NULL
};
static const struct hwmon_channel_info *clevo_hwmon_info3[] = {
    HWMON_CHANNEL_INFO(fan, 
        HWMON_F_LABEL | HWMON_F_INPUT,
        HWMON_F_LABEL | HWMON_F_INPUT,
        HWMON_F_LABEL | HWMON_F_INPUT),
    HWMON_CHANNEL_INFO(pwm, 
        HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
        HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
        HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
    NULL
};
static const struct hwmon_chip_info clevo_hwmon_chip_info = {
    .ops = &clevo_hwmon_ops,
    .info = clevo_hwmon_info,
};
static const struct hwmon_chip_info clevo_hwmon_chip_info2 = {
    .ops = &clevo_hwmon_ops,
    .info = clevo_hwmon_info2,
};
static const struct hwmon_chip_info clevo_hwmon_chip_info3 = {
    .ops = &clevo_hwmon_ops,
    .info = clevo_hwmon_info3,
};

static struct platform_driver clevo_platdrv = {
    .driver = {
        .name = "clevofan",
    },
};

static int __init clevo_platform_probe(struct platform_device *pdev)
{
    struct device *hwmon_dev;

    if (force_match < 0 || force_match > 3)
        return -EINVAL;

    fan_count = force_match;
    if(fan_count == 0)
        fan_count = get_fan_count();
    pr_info("assuming %d FAN(s) to control\n", fan_count);
    if(fan_count == 1)
        hwmon_dev = 
        devm_hwmon_device_register_with_info(&pdev->dev, 
        dmi_get_system_info(DMI_BOARD_NAME), NULL, &clevo_hwmon_chip_info, NULL);

    else if(fan_count == 2)
        hwmon_dev = 
        devm_hwmon_device_register_with_info(&pdev->dev, 
        dmi_get_system_info(DMI_BOARD_NAME), NULL, &clevo_hwmon_chip_info2, NULL);

    else if(fan_count == 3)
        hwmon_dev = 
        devm_hwmon_device_register_with_info(&pdev->dev, 
        dmi_get_system_info(DMI_BOARD_NAME), NULL, &clevo_hwmon_chip_info3, NULL);
        
    return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_device *clevo_platdvc;

static int __init clevofan_init(void)
{
    acpi_handle ec_handle;
    const char *board_vendor;
    int ret;

    board_vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
    if(!is_juno_v5() &&
       (!board_vendor || strncmp(board_vendor, "CLEVO CO.", 9) != 0))
        return -ENODEV;
    if (!dmi_first_match(clevo_dmi) && force_match == 0)
        return -ENODEV;
    
    ec_handle = ec_get_handle();
    if (!ec_handle)
        return -ENODEV;
    
    pr_info("Found CLEVO %s, creating hwmon interfaces\n", dmi_get_system_info(DMI_BOARD_NAME));
    clevo_platdvc = platform_create_bundle(&clevo_platdrv, clevo_platform_probe, NULL, 0, NULL, 0);
    if (IS_ERR(clevo_platdvc))
        return PTR_ERR(clevo_platdvc);

    ret = register_pm_notifier(&nb);
    if (ret) {
        platform_device_unregister(clevo_platdvc);
        platform_driver_unregister(&clevo_platdrv);
        return ret;
    }

    return 0;
}

static void __exit clevofan_exit(void)
{
    uint8_t i;
    for(i=0; i<fan_count;i++) {
        if(fan_auto[i] == 0) {
            fan_auto_mode(i);
            pr_info("Setting Fan auto mode(FAN_%d)\n", i);
        }
    }
    pr_info("exiting module\n");
    platform_device_unregister(clevo_platdvc);
    platform_driver_unregister(&clevo_platdrv);
    unregister_pm_notifier(&nb);
}

module_init(clevofan_init);
module_exit(clevofan_exit);

MODULE_AUTHOR("simopil");
MODULE_DESCRIPTION("Fan control module for Clevo mainboards");
MODULE_LICENSE("GPL");
MODULE_VERSION(MODVERS);

MODULE_PARM_DESC(force_match, "Force loading despite not-matching mainboard - set number of fans");
module_param(force_match, int, 0600);
