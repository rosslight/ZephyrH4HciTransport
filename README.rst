ZephyrH4HciTransport
####################


Overview
*********

This package provides a raw Ble HCI Controller which can be accessed using a H4 UART connection which will be routed via the ``zephyr_console``. Hosts might be e.g. ``BlueZ``.
This code was based on the zephyr ``_bluetooth-hci-uart-sample``

Supported Ble features
*********
* BLE Central
* Extended Advertisement Support (BLE 5.X)

Compatible devices
*********
* `nRF52840 dongle <https://www.nordicsemi.com/Products/Development-hardware/nRF52840-Dongle>`_

Installation for Nordic
*********
* Install and open the `nRF Connect Programmer <https://infocenter.nordicsemi.com/topic/ug_nc_programmer/UG/nrf_connect_programmer/ncp_introduction.html>`_ which is part of the ``nRF Connect for Desktop`` app
* Download the ``zephyr.hex`` from the latest release
* Flash the Nordic device by following `this guide <https://infocenter.nordicsemi.com/topic/ug_nc_programmer/UG/nrf_connect_programmer/ncp_programming_dongle.html>`_
* Configure the connection of the Host

   * Baudrate: 1Mbit/s
   * 8 bits, no parity, 1 stop bit
   * Hardware Flow Control (RTS/CTS) enabled
* Connect via USB: You can find the dongle with the default vendorId and ``0x0004`` as productId

Buttonless updates on the nRF52840 dongle
*****************************************

Use the physical RESET button once to install a build with the Nordic USB DFU
trigger. The default Zephyr USB ID appears as a serial device in Programmer.
Use the RESET button to enter the bootloader when using this USB ID.

For local development on a PCA10059, build with the Nordic USB ID supported by
Programmer by passing ``-- -DEXTRA_CONF_FILE=overlay-nordic-programmer.conf`` to
``west build``. With that image installed, check application-mode discovery
with ``nrfutil device list --traits nordicDfu`` before updating from Programmer.
Windows may assign a different COM port after the USB ID changes.

The Nordic USB DFU Version String request (``0x08``) returns the firmware name
and version, for example ``ZephyrH4HciTransport 1.2.0``. The release build uses
the Git tag without the leading ``v``. Local builds report ``0.0.0-dev`` by
default. Pass ``-- -DFIRMWARE_VERSION=1.2.0`` to ``west build`` to override it.

Runtime advertising transmit power
**********************************

The nRF52840 dongle firmware starts with a 0 dBm transmit-power default.
After creating a set with LE Set Extended Advertising Parameters, the host can
change its transmit power using Zephyr's vendor-specific HCI commands over
the existing H4 connection.

* **Write TX Power Level** (``0xFC0E``) takes the advertising handle type
  (``0x00``), the advertising set handle as a 16-bit little-endian value, and
  the requested power as a signed byte in dBm. To request +8 dBm for set 0,
  send ``01 0E FC 04 00 00 00 08`` over H4.
* **Read TX Power Level** (``0xFC0F``) takes the same handle type and handle.
  To read the current power for set 0, send ``01 0F FC 03 00 00 00`` over H4.

Check the status and the returned signed power value in the Command Complete
event. The controller can select a different power level based on the
hardware's supported values. Apply the requested power again if the host
resets the controller and recreates the advertising set.

Building from Source
*********
* Install the nRF Connect SDK stack following the `offical documentation <https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/installation.html>`_
* Use ``west build --build-dir build . --board nrf52840dongle_nrf52840`` to build for e.g. the nRF52840 dongle
