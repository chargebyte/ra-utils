# chargebyte's RA Utilities

This repository contains some command line tools for operating the so called
safety controller on chargebyte's products like Charge SOM.
It is a Renesas MCU of the RA family, hence the 'RA' in the repository name.

Tools included here:

- **ra-update**: This tool allows to flash the safety firmware into the
  safety controller MCU, using Renesas' UART bootloader protocol.
- **ra-raw**: This tools uses chargebyte's UART protocol to control the
  MCU and/or check the current state. It is only intended for testing and
  debug purposes.
- **ra-gen-param-block**: This tools allows to generate a parameter block
  which is used as parameterization for the safety controller.
