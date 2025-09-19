# chargebyte's RA Utilities

This repository contains some command line tools for operating the so called
safety controller on chargebyte's products like Charge SOM.
It is a Renesas MCU of the RA family, hence the 'RA' in the repository name.

Tools included here:

- **ra-update**: This tool allows to flash the safety firmware into the
  safety controller MCU, using Renesas' UART bootloader protocol.
  It is also possible to write and/or read the so called parameter block
  from the safety controller's data flash area.
- **ra-raw**: This tools uses chargebyte's UART protocol to control the
  MCU and/or check the current state. It is only intended for testing and
  debug purposes.
- **ra-pb-create**: This tools creates a binary parameter block file
  from a YAML file/stdin.
- **ra-pb-dump**: This tools dumps a binary parameter block file as YAML.
- **ra-gen-param-block**: This tools allows to generate a parameter block
  which is used as parameterization for the safety controller.
  Please don't use this tool anymore and prefer the YAML tools above.

## Dependencies

Some tools depend on [libgpiod](git://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git),
the currently used/tested version is v2.0.1 of the library.

To parse YAML files, the library [libyaml](https://pyyaml.org/wiki/LibYAML)
is used, at time of writing v0.2.5.

## Building and Installation on the Target

Since this project is quite small and only has few dependencies, it is possible
to compile it on the target itself. Here is an example transcript:

    git clone https://github.com/chargebyte/ra-utils.git

    mkdir ra-utils/build

    cd ra-utils/build

    CMAKE_INSTALL_PATH_DEFINES=" \
          -DCMAKE_INSTALL_PREFIX:PATH=/usr \
          -DCMAKE_INSTALL_BINDIR:PATH=/usr/bin \
          -DCMAKE_INSTALL_SBINDIR:PATH=/usr/sbin \
          -DCMAKE_INSTALL_LIBEXECDIR:PATH=/usr/libexec  \
          -DCMAKE_INSTALL_SYSCONFDIR:PATH=/etc \
          -DCMAKE_INSTALL_SHAREDSTATEDIR:PATH=/var/share \
          -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=/var \
          -DCMAKE_INSTALL_LIBDIR:PATH=/usr/lib \
    "
    export CMAKE_INSTALL_PATH_DEFINES

    cmake \
          $CMAKE_INSTALL_PATH_DEFINES \
          ..

    make -j$(nproc)

    make install

Note: After `make install` multiple firmware files are placed in `/usr/share/ra-utils`
but the update script only expects a single firmware file matching the platform it
runs on. So just delete the files manually which are not needed in your setup (this
platform selection is done automatically in chargebyte's Yocto recipe).

Remember, that the tool is already pre-installed on chargebyte's distributions.
The very same procedure can be used on a host system, e.g. when the tools are
needed to create parameter block files on the host system.
