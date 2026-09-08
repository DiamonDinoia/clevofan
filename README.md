# clevofan - Clevo Fan control module

Kernel module that provides fan control for various Clevo mainboards via standard hwmon interfaces (to use with generic fan control softwares like lm-sensors, pwmconfig, fancontrol, ...).

## Supported models
```bash
W35_37ET: supported and completely tested
W350SS:   not tested
P170SM:   not tested
```
* Teoretically supports all Clevo laptops but I have to add correct DMI_BOARD_NAME string and fan number to make module load without force_match parameter. Please open an issue "Add support for *model*" and attach the output file of *dmi_info_dump* script, then I'll add support (look "parameters" section for tests).

## Features

* Up to 3 fans supported

* FAN Speed Reading
  - Exposed hwmon device for reading fan speed provided by EC

* Fan Speed Control
  - Exposed hwmon PWM interface to make every fan control software capable of controlling the fan speed
  - pwmX-enable possible values are <br>
  1 -> manual speed <br>
  2 -> default EC automatic speed

## Parameters

* force_match [default:0] force driver to match with non-compatible mainboard, set number of fans to enable
  
## Please read CAREFULLY
This fork sends each fan command through the Linux ACPI EC transaction API.
The API serializes the command with other ACPI EC requests.
The original driver wrote the EC ports directly.
Direct writes could interleave with ACPI requests and select the wrong fan or duty.

## Install
```bash
make
make install
```
## Load
```bash
modprobe clevofan
```
## Contributing
Advices and pull requests are welcome. You can contact me at pilia.simone96@gmail.com 
