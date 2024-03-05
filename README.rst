ZephyrH4HciTransport
####################


Overview
*********

This package provides a raw Ble HCI Controller which can be accessed using a H4 UART connection which will be routed via the ``zephyr_console``. Hosts might be e.g. ``BlueZ``.
This code was based on the zephyr ``_bluetooth-hci-uart-sample``

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


Supported Ble features
*********
* BLE Central
* Extended Advertisement Support (BLE 5.X)
