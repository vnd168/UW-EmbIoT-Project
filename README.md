# UW IOT Embsys Assignments

## Description
This is the assignment repository for the UW "Embedded Systems For IoT" course. Throughout the course,
you will be creating an emdedded system, where each assignment builds upon the previous one. To make
this easier, each new assignment will be able to build and run out of the box, and will have a label
like `IOTEMBSYS#` where `#` is the assignment number.

When creating the assignments, we built the "end result", which is the application that all assignments build towards. Then, to create the assignments, created a new branch, removed some code, and left a comment indicating where students must insert their implementation.

If you would like to reuse your own implementation of the prior solution, instead of the fresh one provided with the assignment, you are more than welcome to do so by copy/pasting. We made this easier by keeping the entire application (mostly) in a single source file, with the exception of configuration files.

## Getting Started

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

### Initialization

The first step is to initialize the workspace folder (``workspace``) where
the ``embsys-firmware`` (i.e. each assignment and project) and all Zephyr modules will be cloned. Run the following
command:

```shell
# install the west tool
# https://docs.zephyrproject.org/latest/develop/west/install.html

# Linux
pip3 install --user -U west

# Windows/macOS
pip3 install -U west

# initialize embsys-workspace for the embsys-firmware (main branch)
west init -m https://github.com/kail/embsys-firmware --mr main embsys-workspace
# update Zephyr modules
cd embsys-workspace
west update

# Export the zephyr cmake package and requirements
west zephyr-export

# Install the requirements for building
pip3 install -r zephyr/scripts/requirements.txt
```

### Initialization for each new assignment
```shell
cd embsys-workspace

# Clone the repo into a new assignment directory
git clone https://github.com/kail/embsys-firmware assignment_n
# Note: do this manually
# update the embsys-workspace/.west/config file so the manifest path points to the new assignment

# Update the workspace, just to be safe
west update
```

### Project structure
- `workspace`: this contains the zephyr workspace, which will contain all assignments, the bootloader, and the zephyr source, once cloned
- `workspace/.west/config`: this is where the current application (assignment) can be selected
- `workspace/bootloader`: this will be where the MCUboot repository resides
- `workspace/zephyr`: this is where the Zephyr source ends up after initializing the workspace
- `workspace/assignment_n`: each assignment will get its own directory in the workspace
- `workspace/assignment_n/.github`: this is where Github dotfiles go for Github Actions, when implementing continuous integration
- `workspace/assignment_n/app`: this is where the main application and its configs are
- `workspace/assignment_n/boards`: this is where the board definition files are
- `workspace/assignment_n/drivers`: this is where the modem driver (and any you choose to implement) are located


### Common commands
Zephyr has ample documentation, so when in doubt, see the official repository. At the time of this
writing, this project is using Zephyr 3.4.0, so be sure to select that version when looking at documentation!

Useful flags:
- `-p`: Does a "pristine" build, by building fresh from source
- `-d`: Allows you to specify a build directory other than `build`
- `-DEXTRA_CONF_FILE=<file>`: Allows you to pass additional configuration files to the build

Commands:
`west update`: Update all zephyr tools and sources; used when starting on a new assignment
`west build -b stm32l496_cell app`: Likely the most common command that will be used throughout the course. This builds the application
`west flash`: Flash the application to the MCU
`west debug`: Flash the application to the MCU, and start debugging
`west build -p -s ../bootloader/mcuboot/boot/zephyr -d build-mcuboot -b stm32l496_cell -- -DCONF_FILE="<path to bootloader.conf>"`: Build the bootloader for the application
- `west build -b stm32l496_cell app -p -d build -- -DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE=\"embsys-firmware/conf/root-rsa-2048.pem\" -DEXTRA_CONF_FILE=mcumgr.conf`: Build the application that can be launched by the bootloader

## Final application
This is a list of items that the end result is capable of, and what the assignments are building towards.

- Handling button presses and shell commands (assignment 2)
- Booting the modem and connecting to the network (assignment 3)
- Creating a TCP connection and using the sockets API (assignment 4)
- Making HTTP requests (assignment 5)
- Encoding and decoding protocol buffers (assignment 7)
- Storing provisioning data internally (assignment 7)
- Using the MCUboot bootloader w/ partitions and mcumgr (assignment 8)
- Downloading OTA (over the air) updates (assignment 9)
- Github actions for testing and continuous integration (assignment 10)
- Sending internal telemetry information to the server (assignment 11)

### What doesn't work
It's also good to know the limitations of the project, so you don't get stuck going down a path which is not supported.

- SSL/TLS: The current modem driver implementation does not support TLS, so all requests must use pure TCP
- Push releases to S3 with CI: Configuring github actions to push releases to AWS with Vocareum (the AWS provider) is just a configuration problem, and does not provide a lot of value to students, so it isn't implemented.
- This project doesn't use a true random generator, so you may see warnings about that

### Zephyr Useful Links

- Basic [Zephyr application][app_dev] skeleton
- [Zephyr workspace applications][workspace_app]
- [West T2 topology][west_t2]
- [Custom boards][board_porting]
- Custom [devicetree bindings][bindings]
- Out-of-tree [drivers][drivers]
- Out-of-tree libraries
- Example CI configuration (using Github Actions)
- Custom [west extension][west_ext]

[app_dev]: https://docs.zephyrproject.org/latest/develop/application/index.html
[workspace_app]: https://docs.zephyrproject.org/latest/develop/application/index.html#zephyr-workspace-app
[west_t2]: https://docs.zephyrproject.org/latest/develop/west/workspaces.html#west-t2
[board_porting]: https://docs.zephyrproject.org/latest/guides/porting/board_porting.html
[bindings]: https://docs.zephyrproject.org/latest/guides/dts/bindings.html
[drivers]: https://docs.zephyrproject.org/latest/reference/drivers/index.html
[zephyr]: https://github.com/zephyrproject-rtos/zephyr
[west_ext]: https://docs.zephyrproject.org/latest/develop/west/extensions.html
