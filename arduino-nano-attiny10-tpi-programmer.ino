/**************************************************
 * Automatic TPI programmer for ATtiny10
 * Based on the original Arduino ATtiny10 TPI programmer.
 *
 * Behaviour:
 *  - Arduino starts
 *  - enters TPI programming mode
 *  - detects ATtiny10 by signature
 *  - reads/dumps relevant memory once over UART
 *  - waits for Intel HEX input beginning with ':'
 *  - receives HEX, checks checksums
 *  - chip erase
 *  - erase configuration section to safe state:
 *      RSTDISBL = 1, WDTON = 1, CKOUT = 1  (all unprogrammed)
 *      Reset remains Reset, Watchdog not forced always on, Clock output off
 *  - writes program flash
 *  - verifies flash
 *  - after successful verify: releases all TPI pins, pulses ATtiny10 RESET once,
 *    then leaves Arduino pins high-impedance so the target can run freely
 *
 * Wiring, through a few kOhm series resistors:
 *  Arduino D10 / SS   -> ATtiny10 RESET/PB3
 *  Arduino D11 / MOSI -> ATtiny10 TPIDATA/PB0
 *  Arduino D12 / MISO -> ATtiny10 TPIDATA/PB0
 *  Arduino D13 / SCK  -> ATtiny10 TPICLK/PB1
 *  GND common, VCC 5 V for programming, 100 nF at ATtiny10 VCC/GND.
 **************************************************/

#include <SPI.h>
#include "pins_arduino.h"

#define SLD    0x20
#define SLDp   0x24
#define SST    0x60
#define SSTp   0x64
#define SSTPRH 0x69
#define SSTPRL 0x68
#define SKEY   0xE0

#define NVM_PROGRAM_ENABLE 0x1289AB45CDD888FFULL

#define NVMCMD 0x33
#define NVMCSR 0x32
#define NVM_NOP           0x00
#define NVM_CHIP_ERASE    0x10
#define NVM_SECTION_ERASE 0x14
#define NVM_WORD_WRITE    0x1D

#define FLASH_BASE  0x4000
#define CONFIG_BASE 0x3F40
#define SIG_BASE    0x3FC0
#define CAL_BASE    0x3F80

#define PROG_MAX 1024
#define SERIAL_BAUD 115200
#define SERIAL_TIMEOUT_MS 20000UL
#define TPI_TIMEOUT_MS 1000UL
#define NVM_TIMEOUT_MS 2000UL

// Safe configuration byte for ATtiny10: all config bits unprogrammed = 1.
// bit 0 RSTDISBL = 1: external reset enabled
// bit 1 WDTON    = 1: watchdog not forced always-on; firmware may still enable it
// bit 2 CKOUT    = 1: clock output disabled
#define SAFE_CONFIG 0xFF

uint16_t adrs = 0x0000;
uint8_t data[PROG_MAX];
uint16_t progSize = 0;
bool chipReady = false;
bool dumpedOnce = false;
bool tpiError = false;
bool targetRunning = false;

uint8_t b, b1, b2, b3;

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) { ; }

  Serial.println();
  Serial.println(F("ATtiny10 Auto-TPI-Programmer"));
  Serial.println(F("Warte auf ATtiny10..."));
  targetRunning = false;

  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);

  SPI.begin();
  SPI.setBitOrder(LSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.setClockDivider(SPI_CLOCK_DIV128);   // slower = more robust
}

void loop() {
  if (targetRunning) {
    // Target is running freely; Arduino pins stay high impedance until Arduino reset.
    return;
  }

  if (!chipReady) {
    chipReady = enterProgrammingModeAndCheckID();
    if (!chipReady) {
      finish();
      delay(1000);
      return;
    }
  }

  if (!dumpedOnce) {
    dumpMemory();
    printConfigDecoded();
    Serial.println();
    Serial.println(F("Bereit. Intel-HEX senden; Startzeichen ':' startet Loeschen/Schreiben/Verify."));
    Serial.println(F("Optional: 'D' = erneut auslesen, 'F' = TPI beenden."));
    dumpedOnce = true;
  }

  if (Serial.available() > 0) {
    int c = Serial.read();
    if (c == ':' ) {
      if (receiveProgram(true)) {
        Serial.print(F("HEX empfangen, Bytes: "));
        Serial.println(progSize);
        programAndVerify();
      }
    } else if (c == 'D' || c == 'd') {
      dumpMemory();
      printConfigDecoded();
    } else if (c == 'F' || c == 'f') {
      releaseTargetAndRun();
      targetRunning = true;
      chipReady = false;
      dumpedOnce = false;
      Serial.println(F("TPI beendet, Pins hochohmig. Zum erneuten Programmieren Arduino resetten."));
    } else if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
      // ignore whitespace
    } else {
      Serial.println(F("Unbekannte Eingabe. HEX muss mit ':' beginnen."));
    }
  }
}

bool enterProgrammingModeAndCheckID() {
  tpiError = false;

  digitalWrite(SS, HIGH);
  delay(20);
  digitalWrite(SS, LOW);      // assert RESET on ATtiny10
  delay(20);

  // Activate TPI by clocking while TPIDATA is high.
  SPI.transfer(0xFF);
  SPI.transfer(0xFF);
  SPI.transfer(0xFF);

  // More conservative than the original 8-bit guard time.
  writeCSS(0x02, 0x07);

  send_skey(NVM_PROGRAM_ENABLE);

  unsigned long t0 = millis();
  while ((readCSS(0x00) & 0x02) == 0) {
    if (millis() - t0 > TPI_TIMEOUT_MS || tpiError) {
      Serial.println(F("FEHLER: NVM konnte nicht aktiviert werden."));
      return false;
    }
  }
  Serial.println(F("NVM enabled"));

  uint8_t id1, id2, id3;
  setPointer(SIG_BASE);
  tpi_send_byte(SLDp); id1 = tpi_receive_byte();
  tpi_send_byte(SLDp); id2 = tpi_receive_byte();
  tpi_send_byte(SLDp); id3 = tpi_receive_byte();

  Serial.print(F("Signature: "));
  outHex2(id1); Serial.print(' '); outHex2(id2); Serial.print(' '); outHex2(id3); Serial.println();

  if (id1 == 0x1E && id2 == 0x90 && id3 == 0x03) {
    Serial.println(F("ATtiny10 erkannt."));
    return true;
  }

  Serial.println(F("FEHLER: Kein ATtiny10 oder keine stabile TPI-Verbindung."));
  return false;
}

void programAndVerify() {
  if (progSize < 1) {
    Serial.println(F("FEHLER: Programmgroesse ist 0."));
    return;
  }

  if (!eraseChip()) return;
  if (!eraseConfigToSafe()) return;
  if (!writeProgramFlash()) return;

  if (verifyProgram()) {
    Serial.println(F("OK: geschrieben und verifiziert."));
    releaseTargetAndRun();
    Serial.println(F("Target reset ausgefuehrt. Arduino-Pins sind hochohmig; ATtiny10 laeuft jetzt frei."));
    targetRunning = true;
    chipReady = false;
    dumpedOnce = false;
    Serial.println(F("Zum erneuten Programmieren Arduino-Reset ausfuehren."));
  } else {
    Serial.println(F("FEHLER: Verify fehlgeschlagen."));
  }
}

void dumpMemory() {
  uint8_t i;
  setPointer(0x0000);

  Serial.println();
  Serial.println(F("Current memory state:"));

  while (adrs < 0x4400) {
    tpi_send_byte(SLDp);
    b = tpi_receive_byte();
    if (tpiError) {
      Serial.println(F("FEHLER: TPI read timeout beim Dump."));
      return;
    }

    if ((0x0000 <= adrs && adrs <= 0x005F) ||
        (0x3F00 <= adrs && adrs <= 0x3F01) ||
        (0x3F40 <= adrs && adrs <= 0x3F41) ||
        (0x3F80 <= adrs && adrs <= 0x3F81) ||
        (0x3FC0 <= adrs && adrs <= 0x3FC3) ||
        (0x4000 <= adrs && adrs <= 0x43FF)) {

      if ((0x0000 == adrs) || (0x3F00 == adrs) || (0x3F40 == adrs) ||
          (0x3F80 == adrs) || (0x3FC0 == adrs) || (0x4000 == adrs)) {
        outNewline();
        if (adrs == 0x0000) Serial.print(F("registers, SRAM"));
        if (adrs == 0x3F00) Serial.print(F("NVM lock"));
        if (adrs == 0x3F40) Serial.print(F("configuration"));
        if (adrs == 0x3F80) Serial.print(F("calibration"));
        if (adrs == 0x3FC0) Serial.print(F("device ID"));
        if (adrs == 0x4000) Serial.print(F("program"));
        outNewline();
        for (i = 0; i < 5; i++) outChar(' ');
        for (i = 0; i < 16; i++) { outChar(' '); outChar('+'); outHex1(i); }
      }

      if ((0x000F & adrs) == 0) {
        outNewline();
        outHex4(adrs);
        outChar(':');
      }
      outChar(' ');
      outHex2(b);
    }

    adrs++;
    if (adrs == 0x0060) setPointer(0x3F00);
  }
  Serial.println();
}

void printConfigDecoded() {
  uint8_t cfg = readConfigByte();
  Serial.println();
  Serial.print(F("Config Byte 0: 0x")); outHex2(cfg); Serial.println();
  Serial.print(F("RSTDISBL: ")); Serial.println((cfg & 0x01) ? F("1 = Reset aktiv") : F("0 = RESET DEAKTIVIERT / gefaehrlich"));
  Serial.print(F("WDTON   : ")); Serial.println((cfg & 0x02) ? F("1 = Watchdog nicht erzwungen") : F("0 = Watchdog immer an"));
  Serial.print(F("CKOUT   : ")); Serial.println((cfg & 0x04) ? F("1 = Clock output aus") : F("0 = Clock output an"));
}

bool receiveProgram(bool colonAlreadyRead) {
  for (uint16_t i = 0; i < PROG_MAX; i++) data[i] = 0xFF;
  progSize = 0;

  bool first = colonAlreadyRead;

  while (true) {
    if (!first) {
      if (!waitForColon()) return false;
    }
    first = false;

    int len = readHexByteTimed();
    int ah  = readHexByteTimed();
    int al  = readHexByteTimed();
    int typ = readHexByteTimed();
    if (len < 0 || ah < 0 || al < 0 || typ < 0) return hexError(F("Timeout/Formatfehler im HEX-Kopf"));

    uint16_t addr = ((uint16_t)ah << 8) | (uint16_t)al;
    uint8_t sum = (uint8_t)len + (uint8_t)ah + (uint8_t)al + (uint8_t)typ;

    if (typ == 0x00) {  // data record
      for (uint8_t k = 0; k < (uint8_t)len; k++) {
        int v = readHexByteTimed();
        if (v < 0) return hexError(F("Timeout/Formatfehler in HEX-Daten"));
        sum += (uint8_t)v;

        uint16_t p = addr + k;
        if (p >= PROG_MAX) return hexError(F("Programm groesser als 1024 Bytes"));
        data[p] = (uint8_t)v;
        if (p + 1 > progSize) progSize = p + 1;
      }
    } else {
      // consume data bytes of unsupported/EOF records
      for (uint8_t k = 0; k < (uint8_t)len; k++) {
        int v = readHexByteTimed();
        if (v < 0) return hexError(F("Timeout/Formatfehler in HEX-Record"));
        sum += (uint8_t)v;
      }
    }

    int chk = readHexByteTimed();
    if (chk < 0) return hexError(F("Timeout/Formatfehler bei HEX-Checksumme"));
    sum += (uint8_t)chk;
    if (sum != 0) return hexError(F("HEX-Checksumme falsch"));

    if (typ == 0x01) {  // EOF
      if (progSize & 1) {
        if (progSize >= PROG_MAX) return hexError(F("Ungerade Programmgroesse am Speicherende"));
        data[progSize++] = 0xFF;
      }
      return true;
    }

    if (typ != 0x00) {
      Serial.print(F("Hinweis: HEX-Recordtyp ignoriert: 0x"));
      outHex2((uint8_t)typ); Serial.println();
    }
  }
}

bool waitForColon() {
  unsigned long t0 = millis();
  while (true) {
    if (Serial.available() > 0) {
      int c = Serial.read();
      if (c == ':') return true;
      if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
      return hexError(F("HEX muss mit ':' beginnen"));
    }
    if (millis() - t0 > SERIAL_TIMEOUT_MS) return hexError(F("Timeout: kein ':' empfangen"));
  }
}

int readHexByteTimed() {
  int h = readHexNibbleTimed();
  int l = readHexNibbleTimed();
  if (h < 0 || l < 0) return -1;
  return (h << 4) | l;
}

int readHexNibbleTimed() {
  unsigned long t0 = millis();
  while (Serial.available() < 1) {
    if (millis() - t0 > SERIAL_TIMEOUT_MS) return -1;
  }
  int c = Serial.read();
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool hexError(const __FlashStringHelper *msg) {
  Serial.print(F("FEHLER HEX: "));
  Serial.println(msg);
  progSize = 0;
  return false;
}

bool eraseChip() {
  setPointer(FLASH_BASE + 1);  // high byte of word in code section
  writeIO(NVMCMD, NVM_CHIP_ERASE);
  tpi_send_byte(SST);
  tpi_send_byte(0xAA);

  if (!waitNVM(F("Chip erase"))) return false;
  writeIO(NVMCMD, NVM_NOP);
  Serial.println(F("Chip erased"));
  return true;
}

bool eraseConfigToSafe() {
  // Chip erase does not change configuration bits; erase config section to all 1/unprogrammed.
  setPointer(CONFIG_BASE + 1);  // high byte of configuration word 0
  writeIO(NVMCMD, NVM_SECTION_ERASE);
  tpi_send_byte(SST);
  tpi_send_byte(0xFF);

  if (!waitNVM(F("Config erase"))) return false;
  writeIO(NVMCMD, NVM_NOP);

  uint8_t cfg = readConfigByte();
  if ((cfg & 0x07) != 0x07) {
    Serial.print(F("WARNUNG: Config nicht sicher: 0x")); outHex2(cfg); Serial.println();
    return false;
  }

  Serial.println(F("Config safe: RESET aktiv, WDT nicht erzwungen, CKOUT aus"));
  return true;
}

bool writeProgramFlash() {
  if (progSize < 1) return false;

  setPointer(FLASH_BASE);
  writeIO(NVMCMD, NVM_WORD_WRITE);

  for (uint16_t k = 0; k < progSize; k += 2) {
    tpi_send_byte(SSTp);
    tpi_send_byte(data[k]);
    tpi_send_byte(SSTp);
    tpi_send_byte(data[k + 1]);
    if (!waitNVM(F("Flash write"))) {
      writeIO(NVMCMD, NVM_NOP);
      return false;
    }
  }

  writeIO(NVMCMD, NVM_NOP);
  SPI.transfer(0xFF);
  SPI.transfer(0xFF);

  Serial.print(F("Wrote program: "));
  Serial.print(progSize, DEC);
  Serial.println(F(" of 1024 bytes"));
  return true;
}

bool verifyProgram() {
  bool correct = true;
  setPointer(FLASH_BASE);

  for (uint16_t ind = 0; ind < progSize; ind++) {
    tpi_send_byte(SLDp);
    b = tpi_receive_byte();
    if (tpiError) {
      Serial.println(F("FEHLER: TPI timeout beim Verify."));
      return false;
    }

    if (b != data[ind]) {
      correct = false;
      Serial.print(F("Verify error byte 0x")); outHex4(ind);
      Serial.print(F(": expected 0x")); outHex2(data[ind]);
      Serial.print(F(" read 0x")); outHex2(b);
      Serial.println();
    }
  }
  return correct;
}

uint8_t readConfigByte() {
  setPointer(CONFIG_BASE);
  tpi_send_byte(SLD);
  return tpi_receive_byte();
}

bool waitNVM(const __FlashStringHelper *what) {
  unsigned long t0 = millis();
  while ((readIO(NVMCSR) & (1 << 7)) != 0) {
    if (millis() - t0 > NVM_TIMEOUT_MS || tpiError) {
      Serial.print(F("FEHLER: NVM timeout bei "));
      Serial.println(what);
      return false;
    }
  }
  return true;
}

void releaseTargetAndRun() {
  // End TPI access first. If this fails because the target already left TPI,
  // the following pin release still makes the programmer electrically passive.
  writeCSS(0x00, 0x00);
  SPI.transfer(0xFF);
  SPI.transfer(0xFF);

  // Stop Arduino SPI driving MOSI/SCK/SS.
  SPI.end();

  // Release TPI data and clock lines. No internal pullups: high impedance.
  digitalWrite(MOSI, LOW);
  pinMode(MOSI, INPUT);
  digitalWrite(MISO, LOW);
  pinMode(MISO, INPUT);
  digitalWrite(SCK, LOW);
  pinMode(SCK, INPUT);

  // Generate one clean target reset pulse, then release RESET as high impedance.
  // Requires the external RESET pullup at the ATtiny10 side.
  digitalWrite(SS, LOW);
  pinMode(SS, OUTPUT);
  delay(30);
  digitalWrite(SS, LOW);   // make sure pullup is disabled before INPUT
  pinMode(SS, INPUT);      // release RESET; external pullup brings it HIGH
  delay(30);
}

void finish() {
  releaseTargetAndRun();
}

void tpi_send_byte(uint8_t dataByte) {
  uint8_t par = dataByte;
  par ^= (par >> 4);
  par ^= (par >> 2);
  par ^= (par >> 1);

  SPI.transfer(0x03 | (dataByte << 3));
  SPI.transfer(0xF0 | ((par & 1) << 3) | (dataByte >> 5));
}

uint8_t tpi_receive_byte(void) {
  unsigned long t0 = millis();

  do {
    b1 = SPI.transfer(0xFF);
    if (millis() - t0 > TPI_TIMEOUT_MS) {
      tpiError = true;
      return 0xFF;
    }
  } while (0xFF == b1);

  b2 = SPI.transfer(0xFF);
  if (0x0F == (0x0F & b1)) b3 = SPI.transfer(0xFF);

  while (0x7F != b1) {
    b2 <<= 1;
    if (0x80 & b1) b2 |= 1;
    b1 <<= 1;
    b1 |= 0x01;
  }
  return b2;
}

void send_skey(uint64_t nvm_key) {
  tpi_send_byte(SKEY);
  while (nvm_key) {
    tpi_send_byte(nvm_key & 0xFF);
    nvm_key >>= 8;
  }
}

void setPointer(uint16_t address) {
  adrs = address;
  tpi_send_byte(SSTPRL);
  tpi_send_byte(address & 0xFF);
  tpi_send_byte(SSTPRH);
  tpi_send_byte((address >> 8) & 0xFF);
}

void writeIO(uint8_t address, uint8_t value) {
  tpi_send_byte(0x90 | (address & 0x0F) | ((address & 0x30) << 1));
  tpi_send_byte(value);
}

uint8_t readIO(uint8_t address) {
  tpi_send_byte(0x10 | (address & 0x0F) | ((address & 0x30) << 1));
  return tpi_receive_byte();
}

void writeCSS(uint8_t address, uint8_t value) {
  tpi_send_byte(0xC0 | address);
  tpi_send_byte(value);
}

uint8_t readCSS(uint8_t address) {
  tpi_send_byte(0x80 | address);
  return tpi_receive_byte();
}

void outChar(char c) { Serial.print(c); }
void outNewline(void) { Serial.println(); }
void outHex1(uint8_t n) { Serial.print(0x0F & n, HEX); }
void outHex2(uint8_t n) { outHex1(n >> 4); outHex1(n); }
void outHex4(uint16_t n) { outHex2(n >> 8); outHex2(n); }
