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
        *   **Intertechno V1 Decoder:** Verwendet ein 4-Puls-Schiebefenster, um das charakteristische IT-Timing-Muster (1xT, 3xT) zu erkennen und Trits zu dekodieren. Die Timing-Toleranzen wurden erweitert, um auch Sender mit leichten Abweichungen zu erfassen.
*   **Frequenzerkennung:** Automatische Erkennung der Modulfrequenz (433/868 MHz) über einen GPIO-Pin (`GPIO_433MARKER`) mit internem Pull-Up.
*   **Signal-Erfassung (RX):** Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben, um ein rohes, demoduliertes ASK/OOK-Signal am `GDO0_PIN` bereitzustellen. Dieser Pin wird als Interrupt-Quelle genutzt, um die Timestamps der Signalflanken per vergrößerter Queue (`slowrf_queue`, 512 Elemente) an den `slowrf_task` zu übergeben.
*   **Signal-Aussendung (TX):**
    *   Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging mit präzisen Microsekunden-Delays (`ets_delay_us`) zu erzeugen.
    *   Für eine korrekte ASK-Modulation wird die `PATABLE` mit zwei Werten (`{0x00, ...}`) und das `FREND0`-Register auf `0x11` gesetzt. Für Tests im Nahbereich wurde die Sendeleistung durch `0x50` (~0 dBm) statt `0xC0` (+10 dBm) reduziert, um Signalverzerrungen zu vermeiden.
    *   **FS20-Frequenz:** Für FS20 wird die Frequenz präzise auf **868.3 MHz** eingestellt (`FREQ` Register: `0x21656A`), um die Kompatibilität zu maximieren.
    *   **FS20-Paketstruktur:** Die Präambel wurde auf 16 '0'-Bits verlängert und Pakete werden zur Erhöhung der Übertragungssicherheit 6-fach wiederholt.
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
*   **[DONE]** SlowRF-Senden (TX) implementiert, inklusive Paket-Wiederholung und korrekter ASK-Modulation.
*   **[DONE]** FS20-Paritätsprüfung (Odd Parity) im TX-Encoder und RX-Decoder korrigiert.
*   **[DONE]** Erweiterter SlowRF-Empfang (RX) mit Zustandsmaschine, der erfolgreich FS20- und Intertechno-V1-Pakete dekodiert.
*   **[DONE]** Implementierung des Sende-Befehls (`is...`) für Intertechno V1.
*   **[DONE]** Erhöhung der Robustheit des FS20-Decoders gegen Timing-Schwankungen und "missed edges".
*   **[DONE]** Test-Funktion (`Tr`) zum Senden von zufälligen, validen FS20-Paketen implementiert.
*   **[DONE]** Build-System um eine automatische Build-Nummer und einen culfw-kompatiblen Versions-String erweitert.
*   **[DONE]** RX-Debugging-Modus (`X99`) implementiert, der rohe Pulsdauern ausgibt und umschaltbar ist.
*   **[DONE]** FS20-Sendefrequenz präzise auf 868.3 MHz kalibriert und Präambel verlängert.
*   **[PARTIAL]** End-to-End Test: Intertechno V1 (433MHz) RX/TX ist mit Referenz-CUL validiert. FS20 (868MHz) wird empfangen, der eigene Sender wird nach Anpassungen von Frequenz, Sendeleistung und Präambel nun gegen den Referenz-CUL getestet.

## 4. Neue Erkenntnisse / Probleme

*   **[INFO]** Eine korrekte ASK/OOK-Modulation auf dem CC1101 erfordert die Initialisierung der `PATABLE` mit zwei Werten sowie die Konfiguration von `FREND0`, um die Sendeleistung für logisch '0' und '1' festzulegen.
*   **[INFO]** Das FS20-Protokoll verwendet **ungerade Parität (Odd Parity)**.
*   **[INFO]** Die Dekodierung von Protokollen mit variablen Timings (wie Intertechno V1) erfordert einen anderen Ansatz (z.B. Puls-Pattern-Matching) als die feste Taktung von FS20.
*   **[INFO]** Für eine zuverlässige FS20-Kommunikation ist eine exakte Frequenz von **868.3 MHz** entscheidend, nicht nur die generelle 868-MHz-Bandeinstellung.
*   **[INFO]** Bei Tests mit geringem Abstand können hohe Sendeleistungen (OOK/ASK) zu Signalverzerrungen führen. Eine Reduktion der Leistung (z.B. `PATABLE` auf `0x50` statt `0xC0`) verbessert die Signalqualität im Nahfeld erheblich.

## 5. Nächste Schritte

*   **Validierung FS20-TX:** Umfassende Tests, ob der Referenz-CUL die gesendeten FS20-Pakete nach den Anpassungen (Frequenz, Leistung, Präambel) nun zuverlässig dekodiert.
*   **Rausch-Unterdrückung im RX-Pfad:** Implementierung eines RSSI-Schwellwerts oder einer Logik zur Mindest-Bit-Anzahl, um die Ausgabe von "Geister-Nachrichten" durch Rauschen zu unterdrücken.
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