# Building

This project is split into 3 folders:
- __common__: common files used by both collector and node
- __collector__: ESP-IDF project for the collector (carried by the UAV)
- __node__: ESP-IDF project for the node (ground)

The instructions on this page have been tested on Ubuntu 20.04 LTS and Debian 10 (but has no OS specific requirements).

# System Requirements:
- Docker
- Git

# Checkout

Adapted from [this](https://git-scm.com/book/en/v2/Git-Tools-Submodules) page

Checkout project and modules:

```git clone https://github.com/PS9888-2020-Sensors/Firmware-HW3.git --recurse-submodules```

If this repository has already been cloned without the `--recurse-submodules flag`, run `git submodule update --init` to update the submodules

# Environment

To avoid having to install and configure the ESP-IDF environment, we use the [Docker image provided by Espressif](https://hub.docker.com/r/espressif/idf/).

The following command is used to create and drop into a shell on the container:

```docker run --rm -v $PWD:/project -w /project --device "/dev/ttyUSB0" -it espressif/idf:release-v4.2```

The flags do a few things:
- `--rm`: remove the container upon exiting
- `-v $PWD:/project`: mount the current folder at `/project` in the container
- `-w /project`: set the working directory to `/project`
- `--device "/dev/ttyUSB0"`: pass the `/dev/ttyUSB0` device from the host to the container
- `-it`: allocate tty for container
- `espressif/idf:release-v4.2` specifies the image and tag to run

For convenience, the following lines are added to `~/.bash_aliases` (note that you need to source the file `source ~/.bash_aliases` for them to apply):

```
alias idf='docker run --rm -v $PWD:/project -w /project -it espressif/idf:release-v4.2'
alias idf1='docker run --rm -v $PWD:/project -w /project --device "/dev/ttyUSB0" -it espressif/idf:release-v4.2'
alias idf2='docker run --rm -v $PWD:/project -w /project --device "/dev/ttyUSB1" -it espressif/idf:release-v4.2'
```

This creates the following aliases:
- `idf`: create a container, mounting the current directory. Used when no hardware is available
- `idf1`: same as `idf`, except `/dev/ttyUSB0` is passed to the container
- `idf2`: same as `idf`, except `/dev/ttyUSB1` is passed to the container

`idf1` and `idf2` are useful when multiple boards are connected to the host at the same time.

### Example:

```
justin@ws1:~/ps9888/Firmware-HW3$ idf1
Adding ESP-IDF tools to PATH...
Using Python interpreter in /opt/esp/python_env/idf4.2_py3.6_env/bin/python
Checking if Python packages are up to date...
Python requirements from /opt/esp/idf/requirements.txt are satisfied.
Added the following directories to PATH:
  /opt/esp/idf/components/esptool_py/esptool
  /opt/esp/idf/components/espcoredump
  /opt/esp/idf/components/partition_table
  /opt/esp/idf/components/app_update
  /opt/esp/tools/xtensa-esp32-elf/esp-2020r2-8.2.0/xtensa-esp32-elf/bin
  /opt/esp/tools/xtensa-esp32s2-elf/esp-2020r2-8.2.0/xtensa-esp32s2-elf/bin
  /opt/esp/tools/esp32ulp-elf/2.28.51-esp-20191205/esp32ulp-elf-binutils/bin
  /opt/esp/tools/esp32s2ulp-elf/2.28.51-esp-20191205/esp32s2ulp-elf-binutils/bin
  /opt/esp/tools/cmake/3.16.4/bin
  /opt/esp/tools/openocd-esp32/v0.10.0-esp32-20200420/openocd-esp32/bin
  /opt/esp/python_env/idf4.2_py3.6_env/bin
  /opt/esp/idf/tools
Done! You can now compile ESP-IDF projects.
Go to the project directory and run:

  idf.py build

root@bbe6717e68b7:/project# cd node/
root@bbe6717e68b7:/project/node# idf.py build flash monitor
Your environment is not configured to handle unicode filenames outside of ASCII range. Environment variable LC_ALL is temporary set to C.UTF-8 for unicode support.
Executing action: all (aliases: build)
Running ninja in directory /project/node/build
Executing "ninja all"...
[1/3] Performing build step for 'bootloader'
ninja: no work to do.
Executing action: flash
Choosing default port b'/dev/ttyUSB0' (use '-p PORT' option to set a specific serial port)
Running ninja in directory /project/node/build
Executing "ninja flash"...
[1/4] Performing build step for 'bootloader'
ninja: no work to do.
[1/2] cd /opt/esp/idf/components/esptool_py && /opt/esp/tools/cmake/3.16.4/bin/cmake ...RECTORY="/project/node/build" -P /opt/esp/idf/components/esptool_py/run_esptool.cmake
esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 2MB 0x8000 partition_table/partition-table.bin 0x1000 bootloader/bootloader.bin 0x10000 firmware-hw3-node.bin
esptool.py v3.0-dev
Serial port /dev/ttyUSB0
Connecting........_
Chip is ESP32D0WDQ5 (revision 1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
Crystal is 40MHz
MAC: f0:08:d1:7e:71:14
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 460800
Changed.
Configuring flash size...
Compressed 3072 bytes to 103...
Writing at 0x00008000... (100 %)
Wrote 3072 bytes (103 compressed) at 0x00008000 in 0.0 seconds (effective 3626.7 kbit/s)...
Hash of data verified.
Compressed 24816 bytes to 15235...
Writing at 0x00001000... (100 %)
Wrote 24816 bytes (15235 compressed) at 0x00001000 in 0.4 seconds (effective 533.0 kbit/s)...
Hash of data verified.
Compressed 750656 bytes to 469536...
Writing at 0x00010000... (3 %)
...
Writing at 0x00080000... (100 %)
Wrote 750656 bytes (469536 compressed) at 0x00010000 in 11.7 seconds (effective 512.8 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
Executing action: monitor
Running idf_monitor in directory /project/node
Executing "/opt/esp/python_env/idf4.2_py3.6_env/bin/python /opt/esp/idf/tools/idf_monitor.py -p /dev/ttyUSB0 -b 921600 --toolchain-prefix xtensa-esp32-elf- /project/node/build/firmware-hw3-node.elf -m '/opt/esp/python_env/idf4.2_py3.6_env/bin/python' '/opt/esp/idf/tools/idf.py'"...
--- idf_monitor on /dev/ttyUSB0 921600 ---
--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---
I (12) boot: ESP-IDF v4.2-dev-1856-g00148cd0c 2nd stage bootloader
I (12) boot: compile time 09:56:58
I (12) boot: chip revision: 1

<output trimmed>
```
