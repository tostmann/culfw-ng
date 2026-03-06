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
    *   `culfw_parser_task`: Verarbeitet eingehende serielle Befehle (z.B. `V`, `X`, `F`, `is`).
    *   `slowrf_task`: Implementiert parallele Zustandsmaschinen/Decoder, um aus den vom CC1101 empfangenen Pulsfolgen gültige Datenpakete zu dekodieren.
        *   **FS20-Decoder:** Sync-Erkennung -> 9-Bit-Akkumulation (8 Daten + 1 Parität) -> Prüfung auf **ungerade Parität (Odd Parity)**. Der Decoder wurde robust gegen Timing-Schwankungen gemacht und kann "missed edges" (zwei kurze Pulse als ein langer Puls) verarbeiten.
        *   **Intertechno V1 Decoder:** Verwendet ein 4-Puls-Schiebefenster, um das charakteristische IT-Timing-Muster (1xT, 3xT) zu erkennen und Trits zu dekodieren.
*   **Frequenzerkennung:** Automatische Erkennung der Modulfrequenz (433/868 MHz) über einen GPIO-Pin (`GPIO_433MARKER`) mit internem Pull-Up.
*   **Signal-Erfassung (RX):** Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben, um ein rohes, demoduliertes ASK/OOK-Signal am `GDO0_PIN` bereitzustellen. Dieser Pin wird als Interrupt-Quelle genutzt, um die Timestamps der Signalflanken per Queue (`slowrf_queue`) an den `slowrf_task` zu übergeben.
*   **Signal-Aussendung (TX):**
    *   Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging mit präzisen Microsekunden-Delays (`ets_delay_us`) zu erzeugen.
    *   Für eine korrekte ASK-Modulation wird die `PATABLE` mit zwei Werten (`{0x00, 0xC0}`) initialisiert und das `FREND0`-Register auf `0x11` gesetzt, um "0" (keine Leistung) und "1" (maximale Leistung) klar zu definieren.
    *   Pakete werden zur Erhöhung der Übertragungssicherheit mehrfach (üblicherweise 3x) gesendet.
*   **Versionierung:** Automatisierte Build-Nummer und detaillierter, culfw-kompatibler Versions-String (`V`-Kommando), um die Identifikation durch Host-Systeme (z.B. FHEM) sicherzustellen.
*   **Test-Infrastruktur:** Ein `Tr`-Kommando generiert und sendet 5 zufällige FS20-Frames, um die TX/RX-Kette mit variierenden OOK-Pattern zu validieren.
*   **Diagnose:**
    *   Erweitertes `C`-Kommando zur Diagnose, das Part-, Versionsnummer und den **MARCSTATE** (`0x35`) ausliest, um den internen Zustand des CC1101 (z.B. `0x0D` für RX) zu verifizieren.
    *   Ein `X99`-Kommando wurde implementiert, um die rohen Puls-Timings (in µs) der empfangenen Signale auszugeben. Dies ist über das `X`-Kommando dynamisch ein- und ausschaltbar.

## 3. Implementierungsstatus

*   **[DONE]** Projektinitialisierung mit PlatformIO.
*   **[DONE]** Basis-Implementierung des CC1101 SPI-Treibers (`cc1101.c`), inklusive Burst-Write/Read-Funktionen.
*   **[DONE]** Grundgerüst für den culfw-Kommando-Parser (`culfw_parser.c`).
*   **[DONE]** Grundgerüst für die SlowRF-Signalverarbeitung (`slowrf.c`).
*   **[DONE]** Frequenz-Auto-Detektion implementiert und stabilisiert.
*   **[DONE]** Task Watchdog Timeout durch Umstellung auf nativen USB-JTAG-Treiber behoben.
*   **[DONE]** SPI-Kommunikation stabilisiert: CC1101 wird auf beiden Frequenzbändern zuverlässig erkannt.
*   **[DONE]** CC1101 für RX in asynchronen seriellen Modus konfiguriert.
*   **[DONE]** SlowRF-Senden (TX) implementiert, inklusive Paket-Wiederholung und korrekter ASK-Modulation via `PATABLE` und `FREND0`.
*   **[DONE]** FS20-Paritätsprüfung (Odd Parity) im TX-Encoder und RX-Decoder korrigiert.
*   **[DONE]** Erweiterter SlowRF-Empfang (RX) mit Zustandsmaschine, der erfolgreich FS20- und Intertechno-V1-Pakete dekodiert.
*   **[DONE]** Implementierung des Sende-Befehls (`is...`) für Intertechno V1.
*   **[DONE]** Erhöhung der Robustheit des FS20-Decoders gegen Timing-Schwankungen und "missed edges".
*   **[DONE]** Test-Funktion (`Tr`) zum Senden von zufälligen, validen FS20-Paketen implementiert.
*   **[DONE]** Build-System um eine automatische Build-Nummer und einen culfw-kompatiblen Versions-String erweitert.
*   **[DONE]** RX-Debugging-Modus (`X99`) implementiert, der rohe Pulsdauern ausgibt und umschaltbar ist.
*   **[PARTIAL]** End-to-End Test: Intertechno V1 (433MHz) RX/TX ist mit Referenz-CUL validiert. FS20 (868MHz) wird empfangen, aber der eigene Sender noch nicht vom Referenz-CUL dekodiert.

## 4. Neue Erkenntnisse / Probleme

*   **[INFO]** Eine korrekte ASK/OOK-Modulation auf dem CC1101 erfordert die Initialisierung der `PATABLE` mit zwei Werten (`{0x00, 0xC0}`) sowie die Konfiguration von `FREND0`, um die Sendeleistung für logisch '0' und '1' festzulegen.
*   **[INFO]** Das FS20-Protokoll verwendet **ungerade Parität (Odd Parity)**. Eine Implementierung mit gerader Parität führt dazu, dass Pakete von Standard-Empfängern verworfen werden.
*   **[INFO]** Die Dekodierung von Protokollen mit variablen Timings (wie Intertechno V1) erfordert einen anderen Ansatz (z.B. Puls-Pattern-Matching) als die feste Taktung von FS20.
*   **[PROBLEM]** Der FS20-Sender (TX) wird vom Referenz-CUL noch nicht zuverlässig dekodiert. Ursachen könnten eine zu kurze Präambel oder leichte Timing-Abweichungen sein, die von der originalen Firmware nicht toleriert werden.

## 5. Nächste Schritte

*   **Feinabstimmung FS20-TX:** Anpassung der Präambel-Länge und der Pulsbreiten (`ets_delay_us`), um die Kompatibilität mit dem Referenz-CUL sicherzustellen.
*   **Protokoll-Erweiterung:** Unterstützung für weitere SlowRF-Protokolle hinzufügen (z.B. Intertechno V3).
*   **Konfigurations-Management:** Speichern von culfw-Einstellungen (z.B. Reporting-Modus `X21`) im Non-Volatile Storage (NVS) des ESP32, um sie nach einem Neustart zu erhalten.
*   **FHEM-Integration:** Validierung der Firmware mit einem Host-System (FHEM) zur Sicherstellung der Kompatibilität und Langzeitstabilität.
*   **Code-Refactoring:** Auslagern der protokollspezifischen Timing-Konstanten in eine saubere Struktur, um die Wartbarkeit und Erweiterbarkeit zu verbessern.

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