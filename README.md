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

## Dependencies

Some tools depend on [libgpiod](git://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git),
the currently used/tested version is v2.0.1 of the library.

To parse YAML files, the library [libyaml](https://pyyaml.org/wiki/LibYAML)
is used, at time of writing v0.2.5.

## Compatibility

Unless stated otherwise, please use the tagged ra-utils version only with
the safety firmware version included in the actual tag.

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

## Using the UART Trace Feature

During testing and bugfixing it is sometimes desired to create a communication protocol
trace of the messages exchanged between the host and the safety controller.
Since the UART protocol is derived from and looks like a CAN protocol, this idea is picked
up again for tracing: instead of fiddling around with yet another UART trace format and to
develop custom tools for analyzing, let's simply dump all UART frames as CAN ones on
a CAN interface (for example a VCAN interface) so that existing tools can be re-used.

On Charge SOM for example, you can create such a VCAN interface as follows:

    cat <<EOF > /etc/systemd/network/vcan0.netdev
    [NetDev]
    Name=vcan0
    Kind=vcan
    EOF

    cat <<EOF > /etc/systemd/network/vcan0.network
    [Match]
    Name=vcan0

    [Link]
    RequiredForOnline=no

    [CAN]
    BitRate=1M
    EOF

    networkctl reload
    networkctl reconfigure vcan0

This configures systemd-networkd to create such a VCAN interface also during boot.
If you prefer it manually and non-persistent (only until reboot), just run this:

    ip link add dev vcan0 type vcan

    ip link set dev vcan0 up

Then you can start ``ra-raw -M vcan0`` in a first SSH session which dumps all UART frames
to this virtual interface.
In a second, parallel SSH session, use for example ``candump -t A vcan0`` to generate
a textual traffic dump.
It is also possible to capture the CAN traffic into a pcap trace, then download this
trace file to your PC and analyze it offline using e.g. Wireshark.
