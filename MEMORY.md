# CULFW-NG Projekt-Gedächtnis

## 1. Projektziel

Entwicklung einer culfw-kompatiblen Firmware für ESP32-C6 basierte CUL-Sticks zur Emulation von SlowRF-Protokollen (wie FS20, Intertechno, etc.). Die Firmware soll die RF-Protokolle über ein CC1101-Modul senden und empfangen.

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über PlatformIO.
*   **Board:** `esp32-c6-devkitc-1`
*   **Kommunikation:** Nativer USB-JTAG/CDC Treiber (`usb_serial_jtag`) für eine nicht-blockierende serielle Schnittstelle.
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **SPI-Kommunikation:** Die SPI-Geschwindigkeit wurde zur Erhöhung der Stabilität auf 500 kHz festgelegt. Für das Auslesen der Statusregister (z.B. `PARTNUM`, `VERSION`) wird der `READ_BURST`-Modus (`0xC0`) verwendet, da der Chip Adressen unter `0x30` sonst als Command-Strobes interpretiert.
*   **Software-Architektur:** FreeRTOS-Task-basiert.
    *   `culfw_parser_task`: Verarbeitet eingehende serielle Befehle (z.B. `V`, `X`, `F`).
    *   `slowrf_task`: Implementiert eine Zustandsmaschine (Sync-Erkennung -> Bit-Akkumulation -> Paritätsprüfung), um aus den vom CC1101 empfangenen Pulsfolgen gültige Datenpakete zu dekodieren.
*   **Frequenzerkennung:** Automatische Erkennung der Modulfrequenz (433/868 MHz) über einen GPIO-Pin (`GPIO_433MARKER`) mit internem Pull-Up.
*   **Signal-Erfassung (RX):** Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben, um ein rohes, demoduliertes ASK/OOK-Signal am `GDO0_PIN` bereitzustellen. Dieser Pin wird als Interrupt-Quelle genutzt, um die Timestamps der Signalflanken per Queue (`slowrf_queue`) an den `slowrf_task` zu übergeben.
*   **Signal-Aussendung (TX):** Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging mit präzisen Microsekunden-Delays (`ets_delay_us`) zu erzeugen. Pakete werden zur Erhöhung der Übertragungssicherheit mehrfach (3x) gesendet.
*   **Versionierung:** Automatisierte Build-Nummer und detaillierter, culfw-kompatibler Versions-String (`V`-Kommando), um die Identifikation durch Host-Systeme (z.B. FHEM) sicherzustellen.
*   **Test-Infrastruktur:** Ein `Tr`-Kommando generiert und sendet 5 zufällige FS20-Frames, um die TX/RX-Kette mit variierenden OOK-Pattern zu validieren.
*   **Diagnose:**
    *   Erweitertes `C`-Kommando zur Diagnose, das Part- und Versionsnummer sowie weitere Konfigurationsregister des CC1101-Chips ausliest.
    *   Ein `X99`-Kommando wurde implementiert, um die rohen Puls-Timings (in µs) der empfangenen Signale auszugeben, was die Kalibrierung des Decoders erheblich vereinfacht.

## 3. Implementierungsstatus

*   **[DONE]** Projektinitialisierung mit PlatformIO.
*   **[DONE]** Basis-Implementierung des CC1101 SPI-Treibers (`cc1101.c`).
*   **[DONE]** Grundgerüst für den culfw-Kommando-Parser (`culfw_parser.c`).
*   **[DONE]** Grundgerüst für die SlowRF-Signalverarbeitung (`slowrf.c`).
*   **[DONE]** Frequenz-Auto-Detektion implementiert und stabilisiert.
*   **[DONE]** Task Watchdog Timeout durch Umstellung auf nativen USB-JTAG-Treiber behoben.
*   **[DONE]** SPI-Kommunikation stabilisiert: Takt auf 500 kHz reduziert, MISO/MOSI Pin-Mapping korrigiert und Burst-Read-Modus für Statusregister implementiert. CC1101 wird nun auf beiden Frequenzbändern zuverlässig erkannt (`Part: 0x00, Vers: 0x14`).
*   **[DONE]** CC1101 für RX in asynchronen seriellen Modus konfiguriert.
*   **[DONE]** Erweiterter SlowRF-Empfang (RX) mit Zustandsmaschine.
*   **[DONE]** SlowRF-Senden (TX) implementiert, inklusive Paket-Wiederholung.
*   **[DONE]** Test-Funktion (`Tr`) zum Senden von zufälligen, validen FS20-Paketen implementiert.
*   **[DONE]** FS20-Paritätsprüfung im Decoder implementiert, um fehlerhafte Pakete zu verwerfen.
*   **[DONE]** Build-System um eine automatische Build-Nummer und einen culfw-kompatiblen Versions-String erweitert.
*   **[DONE]** RX-Debugging-Modus (`X99`) implementiert, der rohe Pulsdauern ausgibt.

## 4. Neue Erkenntnisse / Probleme

*   **[INFO]** Das Auslesen der CC1101-Statusregister (z.B. PARTNUM, VERSION) erfordert das Setzen des Burst-Read-Bits, auch wenn nur ein einzelnes Byte gelesen wird. Andernfalls werden die Adressen als Command-Strobes interpretiert, was zu fehlerhaften Werten (z.B. 0x00) führt.

## 5. Nächste Schritte

*   **Kalibrierung des RX-Decoders:** Feinjustierung der Timing-Toleranzen im `slowrf_task` für FS20 unter Verwendung der `X99`-Pulsdaten von einem Referenz-Sender.
*   **Validierung der TX-Kette:** Sicherstellen, dass von der Firmware gesendete Pakete auf einem Referenz-CUL erfolgreich empfangen werden.
*   **Protokoll-Erweiterung:** Unterstützung für weitere SlowRF-Protokolle hinzufügen (z.B. Intertechno V1/V3) mit entsprechenden Timings für Senden und Empfangen.
*   **Konfigurations-Management:** Speichern von culfw-Einstellungen (z.B. Reporting-Modus `X21`) im Non-Volatile Storage (NVS) des ESP32, um sie nach einem Neustart zu erhalten.

## 6. Hardware-Konfiguration (Pinout)

*   **SPI (für CC1101):**
    *   `MOSI`: GPIO 20
    *   `MISO`: GPIO 21
    *   `SCLK`: GPIO 19
    *   `CS`: GPIO 18
*   **CC1101 Signale:**
    *   `GDO0`: GPIO 2 (Input/Interrupt für RX, Output für TX)
*   **Frequenzerkennung:**
    *   `GPIO_433MARKER`: GPIO 4 (Input mit Pull-Up)