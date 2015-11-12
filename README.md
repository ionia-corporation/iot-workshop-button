Setting up the Arduino environment
==================================

There are 3 items required to upload this program to an ESP8266:

- The Arduino IDE -- The application used to write code, compile it, and upload
  it to the board.
- The ESP8266 Board Manager -- The Arduino IDE does not support the ESP8266 board
  by default. We need to install the 'Board Manager' so we can program it in the
  Arduino language and upload the code using the IDE.
- Adafruit MQTT Library -- This allows the device to communicate with the server
  using the MQTT protocol.

### Using the Arduino IDE for the ESP8266

You can find very detailed instructions on how to install these 2 components here:
https://learn.adafruit.com/adafruit-huzzah-esp8266-breakout/using-arduino-ide

WARNING: Do not download the latest version of the Arduino IDE (1.6.6). There's
an issue with it. Instead, select the version 1.6.5 when downloading.

More info about the issue with 1.6.6 here: https://github.com/esp8266/Arduino/issues/965

### Adafruit MQTT library

Using the same Arduino Package Manager as before, find and install the library
called **Adafruit MQTT Library**.

For more information and examples regarding this library, check out
[its GitHub repository](https://github.com/adafruit/Adafruit_MQTT_Library)

Note: This project has been tested using the version 0.10.0

--------------------------------------------------------------------------------

Building the hardware
=====================

It is always good to build your hardware on a
[breadboard](https://learn.sparkfun.com/tutorials/how-to-use-a-breadboard)
before soldering the components together. That is so we can make sure everything
is working as expected and we save ourselves the hassle of having to de-solder
components.

These are the connections required for the demo to work:

- [Pin 5]-[Resistor]-[LED]-[Pin GND] //The short 'leg' of the LED connects to GND
- [Pin RST]-[Pin GND]

These are the connections to power the device from a battery:

- [Huzzah pin BAT]-[MicroLipo pin BAT]
- [Huzzah pin GND]-[MicroLipo pin GND]
- [Battery]-[MicroLipo]

In order to read the information being sent by the ESP, we wanna use the FTDI board.
We only care about 4 of the 6 pins in that header:

- [Huzzah TX]-[FTDI RX]
- [Huzzah RX]-[FTDI TX]
- [Huzzah GND]-[FTDI GND]
- [Huzzah 5V]-[FTDI 5V]

### Prototyping on the breadboard

This build will help you make sure your code is working properly before bothering
to power it from a battery.

![First iteration](https://i.imgur.com/bW1YpBa.jpg)

If the demo is working fine like that (see: "Running the demo") we can ditch the
breadboard and get those components soldered. If it's still working, solder the
MicroLipo and battery and feel free to remove the FTDI board and the connection
to your computer.

--------------------------------------------------------------------------------

Running the demo
================

### Uploading the code to the ESP8266

Assuming you already set up the Arduino IDE and its dependencies, you only need to:

1. Connect your device to your computer via USB (USB->FTDI->Huzzah)
2. Press the button labeled "GPIO0" in the Huzzah, and without letting go of it
   press the one labeled "Reset". Then let lo of both of them.
3. Click the button called "Upload" in the Arduino IDE.

### Reading the ESP's serial output

--------------------------------------------------------------------------------

Understanding the code
======================

### Workflow

![workflow diagram](https://i.imgur.com/o1GcZ1Z.png)
