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

## Developer Hints

- The safety controller firmware uses internally the term 'inlet' for things related to the
  controlling of a locking motor for sockets. In EVerest and in most chargebyte's documents
  the term 'pluglock' was used traditionally on the EVSE side, too.
  This is why the YAML configuration files name the top-level configuration key also 'pluglock'
  and also the user-visible messages use this term, but in the source code, we kept
  the term 'inlet'.

## YAML Parameter Block Configuration

`ra-pb-create` reads a YAML file and converts it into a binary parameter block.
By default it writes the latest supported parameter block version.
The YAML `version` key is only used for output selection when `ra-pb-create` is called
with `--version-from-yaml`. The command line parameter `--version-override` takes precedence
over the YAML file when given.

The configuration uses a fixed hardware-oriented layout:

- `pt1000s`: exactly 4 entries are expected
- `contactors`: exactly 3 entries are expected
- `estops`: exactly 3 entries are expected

If fewer entries are provided, `ra-pb-create` prints a warning and leaves the missing
entries at their defaults (here this means disabled).
If more entries are provided, the surplus entries are ignored with a warning.

### Common Value Formats

- Temperatures use `°C`, for example `75.0 °C`. Accepted disable aliases: `disable`, `disabled`, `none`, `off`.
  Internally values are stored with 0.1 `°C` resolution and clamped to `-80.0 °C` to `200.0 °C`.
- Resistance offsets use `Ω` or `Ω`, for example `0.500 Ω`.
  Internally values are stored with 0.001 `Ω` resolution and clamped to `-32.000 Ω` to `32.000 Ω`.
- Contactor and pluglock times use `ms`, for example `100 ms`.
  They are quantized to 10 ms steps, so values should be provided in multiples of 10 ms.
  The maximum stored value is `2550 ms`.
- RCM times also use `ms`, for example `60 ms`.
  They are quantized to 20 ms steps, so values should be provided in multiples of 20 ms.
  The maximum stored value is `5100 ms`.
- Voltages use `mV`, for example `2200 mV`. Values above `3300 mV` are clamped to `3300 mV`.
- Hold duty cycle uses `%`, for example `55 %`. Allowed range is `0 %` to `100 %`.
- Pin polarity values are `disabled`, `active-low`, or `active-high`.
  `disable`, `none`, and `off` are accepted as aliases for `disabled`.

### Version 1

Parameter block version `v1` was the first versioned parameter block schema.
The safety controller firmware supported only temperatures, contactors, and estop inputs.

```yaml
version: 1

pt1000s:
  - abort-temperature: 75.0 °C
    resistance-offset: 0.500 Ω
  - disabled
  - disabled
  - disabled

contactors:
  - type: without-feedback
    close-time: 50 ms
    open-time: 60 ms
  - with-feedback-normally-open
  - disabled

estops:
  - active-low
  - active-high
  - disabled
```

Top-level keys in `v1`:

- `version`: positive integer.
- `pt1000s`: sequence of up to 4 PT1000 channel entries.
- `contactors`: sequence of up to 3 contactor entries.
- `estops`: sequence of up to 3 estop pin configurations.

`pt1000s` entries may be written in two forms:

- Scalar form: `disabled`, `disable`, `none`, or `off`
- Mapping form:
  - `abort-temperature`: temperature in `°C`
  - `resistance-offset`: resistance offset in `Ω` or `Ω`

`contactors` entries may be written in two forms:

- Scalar form:
  - `disabled` or `none`
  - `without-feedback`
  - `with-feedback-normally-open`
  - `with-feedback-normally-closed`
  - `with-feedback` is accepted as a legacy alias for `with-feedback-normally-closed`
- Mapping form:
  - `type`: one of the contactor type values above
  - `close-time`: time in `ms`
  - `open-time`: time in `ms`

`estops` entries are scalar pin configuration/polarity values:

- `disabled`
- `active-low`
- `active-high`

Notes for `v1`:

- A disabled PT1000 entry is emitted as `disabled` when dumping.
- Contactors may be given as scalar shorthand or as full mappings.

### Version 2

Safety controller firmware v0.4.x added RCM support. So parameter block version
`v2` extends `v1` by adding optional RCM configuration.

```yaml
version: 2

...

rcm:
  fault-polarity: active-low
  test-polarity: active-high
  test-trigger-time: 60 ms
  test-check-tripped-time: 80 ms
  test-check-normal-time: 100 ms
```

The new `rcm` top-level key  supports two forms:

- Scalar form:
  - `disabled`, `disable`, `none`, or `off`
- Mapping form:
  - `fault-polarity`: `disabled`, `active-low`, or `active-high`
  - `test-polarity`: `disabled`, `active-low`, or `active-high`
  - `test-trigger-time`: time in `ms`
  - `test-check-tripped-time`: time in `ms`
  - `test-check-normal-time`: time in `ms`

Rules for `rcm`:

- If `fault-polarity` is set to `disabled`, `test-polarity` must also be `disabled`.
- If `fault-polarity` is enabled, then `test-polarity` is required.
- If RCM is enabled, all three timing values are required and must not resolve to zero.
- A scalar `rcm: disabled` is the explicit way to document that RCM is intentionally unused.

### Version 3

The safety controller firmware was extended to use a PWM signal to control the contactors.
This feature is only usable with newer Charge SOM platforms (hardware revision >= V1R2a), or
on carrier boards with the according safety controller pin wiring.

Also the firmware gained support for controlling pluglock motors (aka inlet support).
This also requires additional configuration.

The parameter block version `v3` is required for these firmwares and extends `v2`
in the mentioned two areas:

- each contactor mapping entry can now have a `hold-duty-cycle` key

  If `hold-duty-cycle` is omitted, it defaults to `100 %`.

  Otherwise this is the PWM duty cycle which is applied after `close-time` elapsed.
  This feature can be used to reduce the energy consumption (and thus also the temperature)
  when holding the contactor closed.

- `pluglock` is a new top-level key and bundles various aspects of the connected plug lock motor.

  If not used, it can be given in the short-hand scalar form and accepts the usual disabled strings.
  Otherwise the mapping form accepts:
  - `type`: `none`, `without-feedback`, or `with-feedback`
  - `close-time`: time in `ms`
  - `open-time`: time in `ms`
  - `feedback-open-voltage-min`: voltage in `mV`
  - `feedback-open-voltage-max`: voltage in `mV`
  - `feedback-closed-voltage-min`: voltage in `mV`
  - `feedback-closed-voltage-max`: voltage in `mV`

  Rules to respect:
    - If `type` is `without-feedback` or `with-feedback`, `close-time` and `open-time` are required.
    - If `type` is `with-feedback`, all four feedback voltage keys are required.
    - If `type` is `none`, additional timing and voltage keys are accepted and stored, but the type
      still disables pluglock behavior.
    - If the entire `pluglock` key is omitted, the default is no pluglock configured.
    - The feedback voltage ranges must not overlap! While it is possible to create such a parameter
      block, it will not be accepted by the safety controller firmware.

- `motor-driver-fault` is a new top-level scalar which enables evaluation of the motor driver
  feedback pin. This depends on the hardware platform and can take the following values:
  - `disabled`
  - `active-low`
  - `active-high`

```yaml
version: 3

...

pluglock:
  type: with-feedback
  close-time: 100 ms
  open-time: 110 ms
  feedback-open-voltage-min: 2200 mV
  feedback-open-voltage-max: 2800 mV
  feedback-closed-voltage-min: 1700 mV
  feedback-closed-voltage-max: 2000 mV

motor-driver-fault: disabled
```

### Cross-Version Behavior

- Without `--version-from-yaml`, `ra-pb-create` writes the latest supported version, regardless of the YAML `version` field.
- With `--version-from-yaml`, the YAML `version` field selects the output format if that version is supported.
- With `--version-override`, the command line version wins over the YAML `version` field.
- When newer version YAML files are converted/used to generate an older parameter block version, then
  unsupported elements are dropped and generate a warning.
- `ra-pb-dump` always emits the schema version that matches the binary parameter block version it reads.
