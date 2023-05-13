# Data Acquisition - Backplane

| :warning: WARNING|
|:---|
| This design is an unverified work in progress (as of May 2023).|

## Overview

The backplane is is a passive motherboard that accepts plugin boards with the functionality of the DAQ system.

![Alt](./kicad/backplane.png "Title")

## Design decisions

* Passive only, all components are on plugin boards.
* Using common PCIE X8 98 pins connectors but backplane and plugin boards are not PCIE compatible. 
* Symmetric. Slots are interchangeable and boards can be plugged in any socket. Conflict resolution is delegated to the plugin boards.
* Scalable. Versions with more/less slots can be easily derived.
* External power provided via a two pio connectors. Conversion to working voltages such as 5V/3.3V is done on the plugin boards (either centrally by a single power board or by a converter on each board.)
* Auxiliary connector with 4+GND pins on board is available.
* Using through hole connectors for mechanical rigidity but can be converted to a SMD version if desired.
* Sufficient signal integrity and bandwidth. The backplane is intended for relatively slow signals at the order of 1Mhz,that are driven directly by an MCU and peripherals,  and thus uses only 4 PCB layers for connectivity with no ground planes.

## Mechanical Dimensions

TBD

## Signals Assignment

TBD

Signal assignments for two connectors and for PCIE slots.


## BOM

TBD
