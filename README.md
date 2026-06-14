# arduino-nano-ATtiny10-TPI-programmer
Arduino Nano based TPI programmer for ATtiny10 with Intel HEX upload, flash verify, config reset and UART memory dump.

Arduino Nano based TPI programmer for ATtiny10 microcontrollers.

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

| Arduino Nano | ATtiny10 |
|---|---|
| D10 / SS   | RESISTOR 330 | RESET / PB3 |
| D11 / MOSI | RESISTOR 330 |  TPIDATA / PB0 |
| D12 / MISO | RESISTOR 330 |  TPIDATA / PB0 |

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

## License

MIT License
