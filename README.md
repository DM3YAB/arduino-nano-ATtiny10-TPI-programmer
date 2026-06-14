# arduino-nano-ATtiny10-TPI-programmer  

Arduino Nano based TPI programmer for ATtiny10 microcontrollers
... with HEX upload, flash verify, config reset and UART memory dump.

## Features

- Enters TPI programming mode
- Detects ATtiny10 by signature
- Dumps relevant memory over UART
- Receives Intel HEX data over serial
- Checks HEX checksums
- Erases chip
- Resets configuration section to a safe state
- Writes program flash
- Verifies flash
- Releases TPI pins after successful programming

## Hardware

- Arduino Nano
- ATtiny10
- Series resistors of a few kOhm

## Wiring

| Arduino Nano | Series Resistor | ATtiny10 |
|---|---|---|
| D10 / SS   | 330 Ω | RESET / PB3 |
| D11 / MOSI | 330 Ω |  TPIDATA / PB0 |
| D12 / MISO | 330 Ω |  TPIDATA / PB0 |
| D13 / SCK  | 330 Ω | TPICLK/PB1 |

The TPI interface uses a single bidirectional data line (TPIDATA), therefore both MOSI and MISO are connected to PB0 through separate 330 Ω resistors.

### Power Supply

- Connect VCC and GND.
- 100 nF ceramic decoupling capacitor directly at the ATtiny10.
- 1 µF capacitor between VCC and GND for additional supply filtering.

## Usage

1. Upload this sketch to the Arduino Nano.
2. Open the serial monitor or a terminal.
3. Send an Intel HEX file over serial.
4. The programmer writes and verifies the flash.
5. After success, the ATtiny10 is released and starts running.

## Development Environment

Tested with:

- Arduino IDE 2.x
- ATtiny10Core 2.1.0
- Arduino Nano V3
- ATtiny10

## Example

A simple ATtiny10 LED blink example is provided.

Source code:
- `examples/blink/blink.cpp`

Precompiled HEX file:
- `examples/blink/blink.hex`

The HEX file can be uploaded directly using the programmer.

## License

MIT License
