# Data Acquisition - Controller board.

| :warning: WARNING|
|:---|
| This design is an unverified work in progress (as of May 2023).|

## Overview

This plugin board contains the microcontroller that controls the system. In the current design, we
use WeAct STM32H750 module. Future designs may have the STM32H750 MCU directly on the board.

![Alt](./kicad/power_supply.png)

## Design decisions

* All the MCU signals are routed to the bus, to provide flexibility for future designs.
* All signals are protected against ESD using TVS diodes.
* Controller can be powered from the bus or from the USB port, with power arbitration using OR diodes. However, the USB power does not power the bus to avoid overloading the host computer.
* The WeAct module can be soldered to the board using pin header or can be removable using a set of male/female headers.

> **_NOTE:_**  If powering the bus from the USB connector will be needed, short the diode Schottky by populating jumper R1 and make that sure any additional 5V power supply is connected to the bus via a Schottky diode. The WeAct module already contains a Schottky diode between the USB connector the +5V.

## BOM

Key components:

* WeAct Studio STM32H750 module.
