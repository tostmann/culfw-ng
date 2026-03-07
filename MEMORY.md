# CULFW-NG Projekt-Gedächtnis

## 1. Projektziel

Entwicklung einer culfw-kompatiblen Firmware für ESP32-C6 basierte CUL-Sticks zur Emulation von SlowRF-Protokollen (wie FS20, Intertechno, etc.). Die Firmware soll die RF-Protokolle über ein CC1101-Modul senden und empfangen.

### 1.1 Unterstützte Protokolle

| Protokoll | Empfang (RX) | Senden (TX) | Kommando | Frequenz (Standard) |
| :--- | :---: | :---: | :--- | :--- |
| **FS20** | Ja | Ja | `F...` | 868.30 MHz |
| **Intertechno (V1)** | Ja | Ja | `is...` | 433.92 MHz |
| **Intertechno (V3)** | Ja | Ja | `is...` (32bit) | 433.92 MHz |
| **HMS / EM1000** | Ja | Ja | `H...` | 868.30 MHz |
| **S300TH / ESA** | Ja | Nein | `S...` | 868.30 MHz |
| **FHT80b** | Ja | Ja | `T...` | 868.30 MHz |
| **Oregon Scientific** | Ja | Ja | `To...` (Test) | 433.92 MHz |
| **Generische Sensoren** | Ja | Nein | `r...` | 433/868 MHz |

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über PlatformIO.
*   **Board:** `esp32-c6-devkitc-1`
*   **Kommunikation:** Nativer USB-JTAG/CDC Treiber (`usb_serial_jtag`) für eine nicht-blockierende serielle Schnittstelle.
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **SPI-Kommunikation:** Die SPI-Geschwindigkeit wurde zur Erhöhung der Stabilität auf 500 kHz festgelegt. Für das Auslesen der Statusregister (z.B. `PARTNUM`, `VERSION`) wird der `READ_BURST`-Modus (`0xC0`) verwendet, da der Chip Adressen unter `0x30` sonst als Command-Strobes interpretiert.
*   **Persistenz:** Wichtige Einstellungen (z.B. der Reporting-Modus `X21`, die RF-Frequenz) werden im **Non-Volatile Storage (NVS)** des ESP32 gespeichert und bei Neustart automatisch wiederhergestellt.
*   **Software-Architektur:** FreeRTOS-Task-basiert.
    *   `culfw_parser_task`: Verarbeitet eingehende serielle Befehle. Unterscheidet nun zwischen strukturierten Kommandos (z.B. `F12345678`) und Raw-Befehlen. Erkennt Protokollvarianten anhand der Datenlänge (z.B. `is` für Intertechno V1 vs. V3). Verarbeitet das `H<HEX>` Kommando zum Senden von HMS-Daten, das `T<HEX>` Kommando für FHT-Daten und die Frequenzumschaltung (`f433`/`f868`).
    *   `slowrf_task`: Implementiert **parallele** Zustandsmaschinen/Decoder, um aus den vom CC1101 empfangenen Pulsfolgen gültige Datenpakete zu dekodieren. Jede eingehende Pulsfolge wird an alle Decoder gleichzeitig weitergeleitet. Der Stick agiert somit als **Multi-Protokoll-Gateway** für die jeweils aktive Frequenz.
        *   **FS20-Decoder:** Dedizierte Sync-Bit-Erkennung -> 9-Bit-Akkumulation (8 Daten + 1 Parität) -> Prüfung auf **gerade Parität (Even Parity)**. Der Decoder ist robust gegen Timing-Schwankungen und kann "missed edges" (zwei kurze Pulse als ein langer Puls) verarbeiten.
        *   **Intertechno V1 Decoder:** Verwendet ein 4-Puls-Schiebefenster, um das charakteristische IT-Timing-Muster (1xT, 3xT) zu erkennen und Trits zu dekodieren. Die Timing-Toleranzen wurden erweitert, um auch Sender mit leichten Abweichungen zu erfassen.
        *   **Intertechno V3 Decoder:** Erkennt das lange Sync-Signal (~9.3ms) und dekodiert die nachfolgenden 32 PWM-kodierten Bits. Die Sync-Erkennung verhindert eine Fehldetektion von V1-Signalen.
        *   **HMS/S300TH Sensor-Decoder:** Eine generische PWM-Decoder-Logik verarbeitet Sensor-Protokolle. Sie unterscheidet '0'- und '1'-Bits anhand der Länge des High-Pulses bei konstanter Low-Puls-Länge und akkumuliert die Daten in Nibbles.
        *   **Oregon Scientific Decoder (V2/V3):** Nach Erkennung der charakteristischen OS-Präambel (eine Folge kurzer Pulse) werden die nachfolgenden, Manchester-kodierten Daten dekodiert. Der Decoder extrahiert die Daten-Nibbles und validiert das Paket anhand des Sync-Nibbles (`0xA`).
        *   **FHT-Decoder:** Erkennt das FHT-spezifische Sync-Muster und dekodiert die nachfolgenden 9-Bit-Datenpakete (8 Datenbits + 1 Steuerbit).
        *   **Generischer PWM-Sensor-Decoder (rtl_433-ähnlich):** Eine flexible Zustandsmaschine dekodiert diverse OOK/PWM-basierte Sensoren, die nicht von den spezifischen Decodern abgedeckt werden. Sie analysiert das Puls-Timing und gibt die rohen Daten als Hex-String mit dem Präfix `r` aus.
*   **Frequenzerkennung und -management:**
    *   **Hardware-Default:** Die Modulfrequenz (433/868 MHz) wird initial über einen GPIO-Pin (`GPIO_433MARKER`) mit internem Pull-Up erkannt.
    *   **Software-Override:** Ein Benutzer kann die Frequenz zur Laufzeit per Kommando (`f433` oder `f868`) umschalten. Diese Einstellung wird **permanent im NVS gespeichert** und überschreibt beim nächsten Start die Hardware-Erkennung. Dies ermöglicht den korrekten Betrieb von fehlbestückten Modulen.
*   **Signal-Erfassung (RX):**
    *   Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben, um ein rohes, demoduliertes ASK/OOK-Signal am `GDO0_PIN` bereitzustellen. Dieser Pin wird als Interrupt-Quelle genutzt, um die Timestamps der Signalflanken an den `slowrf_task` zu übergeben.
    *   **Rausch-Unterdrückung:** `GDO2` ist als **Carrier Sense** konfiguriert (`IOCFG2=0x0E`). Der GPIO-Interrupt wird nur verarbeitet, wenn der Carrier-Sense-Pin aktiv ist, wodurch Rauschen bei inaktivem Kanal effektiv gefiltert wird. Die AGC-Einstellungen wurden für maximale Empfindlichkeit optimiert (`AGCCTRL2=0x07`).
*   **Signal-Aussendung (TX):**
    *   Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging mit präzisen Microsekunden-Delays (`ets_delay_us`) zu erzeugen.
    *   Zur Vermeidung von Echos (Selbst-Empfang) wird der `GDO0`-Interrupt während des Sendevorgangs temporär deaktiviert.
    *   Zur Sicherstellung der Sende-Bereitschaft wird vor dem Senden der **MARCSTATE** (`0x35`) geprüft und der `STX`-Strobe bei Bedarf wiederholt, bis der Chip im `TX`-State (`0x13`) ist.
    *   Für eine korrekte ASK-Modulation und maximale Reichweite wird die `PATABLE` mit `{0x00, 0xC0, ...}` (+10 dBm) initialisiert und das `FREND0`-Register auf `0x11` gesetzt.
    *   **FS20-Paketstruktur:** Die Präambel wurde auf 24 '0'-Bits verlängert und Pakete werden zur Erhöhung der Übertragungssicherheit **10-fach** wiederholt.
    *   **FHT-Paketstruktur:** Für das Senden von FHT-Daten (`T`-Kommando) wird die spezifische Präambel gefolgt von den 10-Bit-Datenframes (Start, 8 Daten, Stop) per Bit-Banging erzeugt.
*   **RF-Konfiguration:** Die Frequenzregister des CC1101 wurden präzise auf die Standard-Mittenfrequenzen kalibriert (868.30 MHz für FS20/HMS/S300TH, 433.92 MHz für Intertechno).
*   **Versionierung:** Automatisierte Build-Nummer und detaillierter, culfw-kompatibler Versions-String (`V`-Kommando), der zur besseren Diagnose auch das erkannte Frequenzband enthält.
*   **Diagnose & Feedback:**
    *   Erweitertes `C`-Kommando zur Diagnose, das Part-, Versionsnummer und den **MARCSTATE** (`0x35`) ausliest.
    *   Ein `X99`-Kommando gibt die rohen Puls-Timings (in µs) der empfangenen Signale aus.
    *   **Remote-Registerzugriff:** `Rxx`-Kommando zum Lesen und `WxxYY`-Kommando zum Schreiben von CC1101-Registern zur Laufzeit.
    *   **RSSI-Reporting:** An alle empfangenen Datenpakete wird der RSSI-Wert (Signalstärke) als Hex-Wert angehängt (z.B. `F12345678C1`).
    *   Diagnose-Befehle `TX1`/`TX0` zum manuellen Schalten des Sendeträgers für Frequenzmessungen.
    *   Eine LED (`GPIO_8`) signalisiert aktive Sendevorgänge.
    *   Ein periodischer "CUL-TICK" wird über die serielle Schnittstelle gesendet, um die Verbindung zum Host zu signalisieren.
    *   `H<HEX>`-Kommando zum Senden von HMS-Protokolldaten.
    *   `T<HEX>`-Kommando zum Senden von FHT-Protokolldaten.
    *   `f<freq>`-Kommando (`f433`/`f868`) zur Laufzeit-Umschaltung der Frequenz.
    *   `m<HEX>`-Kommando zum Senden von rohen Pulsfolgen (µs-genau) zur Emulation beliebiger OOK-Protokolle.

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
*   **[DONE]** FS20-Paritätsprüfung (Even Parity) im TX-Encoder und RX-Decoder korrigiert.
*   **[DONE]** Erweiterter SlowRF-Empfang (RX) mit Zustandsmaschine, der erfolgreich FS20 dekodiert.
*   **[DONE]** Erhöhung der Robustheit des FS20-Decoders gegen Timing-Schwankungen und "missed edges".
*   **[DONE]** TX-Echo durch Deaktivieren der RX-Interrupts während des Sendens behoben.
*   **[DONE]** Test-Funktion (`Tr`) zum Senden von zufälligen, validen FS20-Paketen implementiert.
*   **[DONE]** Build-System um eine automatische Build-Nummer und einen culfw-kompatiblen Versions-String erweitert.
*   **[DONE]** RX-Debugging-Modus (`X99`) implementiert, der rohe Pulsdauern ausgibt und umschaltbar ist.
*   **[DONE]** FS20-Sendefrequenz präzise auf 868.3 MHz kalibriert und Präambel verlängert.
*   **[DONE]** Implementierung und Validierung einer Rauschunterdrückung via Hardware Carrier Sense (GDO2).
*   **[DONE]** Hinzufügen von visuellem Feedback (LED) für Sendevorgänge.
*   **[DONE]** Konfigurations-Management: Speichern von culfw-Einstellungen (z.B. `X21`-Modus) im Non-Volatile Storage (NVS).
*   **[DONE]** Code-Refactoring: Protokollspezifische Sende-Funktionen (`cc1101_send_fs20`, etc.) ausgelagert.
*   **[DONE]** Stabilisierung des TX-Modus durch Warten auf korrekten `MARCSTATE`.
*   **[DONE]** RSSI-Reporting: Empfangene Datenpakete werden um den RSSI-Wert ergänzt.
*   **[DONE]** Remote-Diagnose: Implementierung von `R`/`W`-Befehlen zum Lesen/Schreiben von CC1101-Registern.
*   **[DONE]** Host-System-Interface: Periodischer "CUL-TICK" als Heartbeat implementiert.
*   **[DONE]** Protokoll-Erweiterung: Empfangs-Decoder für **HMS** und **S300TH** (868MHz) implementiert.
*   **[DONE]** Protokoll-Erweiterung: Sende-Encoder für HMS (`H...`-Kommando) implementiert.
*   **[DONE]** Protokoll-Erweiterung: Sende-Encoder und Empfangs-Decoder für **Intertechno V1 & V3** (433MHz) implementiert.
*   **[DONE]** Protokoll-Erweiterung: Empfangs-Decoder für **Oregon Scientific**, sowie Sende- und Empfangs-Logik für **FHT** implementiert.
*   **[DONE]** Protokoll-Erweiterung: Generischer PWM-Decoder für rtl_433-ähnliche Sensoren (Raw-Hex-Ausgabe `r...`) implementiert.
*   **[DONE]** Implementierung einer Laufzeit-Frequenzumschaltung (`f433`/`f868`) mit Speicherung im NVS.
*   **[DONE]** End-to-End Validierung: Alle implementierten Protokolle (FS20, IT, HMS, FHT, OS) wurden in einem Cross-Validation-Setup erfolgreich validiert.
*   **[DONE]** Benutzer-Dokumentation: Eine Übersicht der culfw-kompatiblen und erweiterten Befehle wurde erstellt (`COMMANDS.md`).
*   **[DONE]** Release Management: Finaler Code-Stand als **Release v1.0.1** auf GitHub getaggt und gepusht.

## 4. Neue Erkenntnisse / Probleme

*   **Hardware-Fehlbestückung (Chargenproblem) bestätigt:** Umfangreiche Tests haben zweifelsfrei bestätigt, dass die verwendeten `E07-900MM10S` Module trotz ihres 868-MHz-Labels physikalisch für 433 MHz bestückt sind. Mit der Laufzeit-Frequenzumschaltung (`f433`-Kommando) und der persistenten Speicherung der Einstellung im NVS können diese Module nun dauerhaft und korrekt als 433-MHz-Sticks betrieben werden.
*   **Multi-Protokoll-Gateway-Architektur bestätigt:** Die parallele Ausführung aller Protokoll-Decoder in der `slowrf_task` ermöglicht den simultanen Empfang verschiedener Protokolle (z.B. Intertechno und Oregon Scientific) auf demselben Frequenzband, ohne dass eine Umschaltung erforderlich ist. Für den Empfang muss der Benutzer lediglich die korrekte Frequenz (`f433`/`f868`) wählen.
*   **Effektiver Testaufbau:** Ein Cross-Validation-Testaufbau mit zwei CUL32-C6 (einer als Sender/Emulator, einer als Empfänger) und einem Legacy-CUL als Referenz hat sich als sehr effektiv für das Debugging von Protokoll-Decodern erwiesen.
*   **Effiziente Rauschunterdrückung:** Hardwareseitiges Carrier Sense (RSSI-Schwellwert) über den GDO2-Pin ist eine sehr effektive Methode, um den RX-Prozessor von der Verarbeitung von reinem Rauschen zu entlasten.
*   **Voraussetzung für Host-Integration:** Das Speichern von Konfigurationen (Modus, Frequenz) im NVS ist essentiell für eine nahtlose Integration in Host-Systeme wie FHEM, da diese erwarten, dass der CUL seinen Zustand nach einem Neustart beibehält.
*   **Mächtige Emulations-Werkzeuge:** Das `m<HEX>`-Kommando zum Senden roher Pulsfolgen ist ein extrem mächtiges Werkzeug zur Emulation und zum Testen beliebiger OOK-Protokolle, ohne dass die Firmware neu kompiliert werden muss.
*   **Stabilität erreicht:** Die Firmware hat die finale Cross-Validation-Testphase bestanden und läuft stabil. Alle implementierten Protokolldecoder und -encoder funktionieren wie erwartet. Das Release v1.0.1 ist stabil.

## 5. Nächste Schritte

*   **FHEM-Integration:** Validierung der Firmware mit einem Host-System (FHEM) zur Sicherstellung der Kompatibilität und Langzeitstabilität.
*   **SIGNALduino-Kompatibilität:** Prüfung der Kompatibilität mit dem FHEM **SIGNALduino**-Modul. Dies würde die Unterstützung hunderter zusätzlicher Sensoren ermöglichen. Ggf. Implementierung eines kompatiblen Raw-Message-Formats (z.B. `MU;...`).
*   **RSSI-Kalibrierung:** Abgleich der ausgegebenen RSSI-Hex-Werte mit realen dBm-Werten für eine genauere Signalstärken-Anzeige.
*   **Langzeittests:** Überwachung der Stabilität und des Speicherverbrauchs über mehrere Tage im produktiven Einsatz.
*   **Roadmap-Planung:** Evaluierung der Integration in moderne IoT-Ökosysteme (z.B. als Matter/Thread-Bridge).

## 6. Hardware-Konfiguration (Pinout)

*   **SPI (für CC1101):**
    *   `MOSI`: GPIO 21
    *   `MISO`: GPIO 20
    *   `SCLK`: GPIO 19
    *   `CS`: GPIO 18
*   **CC1101 Signale:**
    *   `GDO0`: GPIO 2 (Input/Interrupt für RX, Output für TX)
    *   `GDO2`: GPIO 3 (Input für Carrier Sense)
*   **Frequenzerkennung:**
    *   `GPIO_433MARKER`: GPIO 4 (Input mit Pull-Up)
*   **Visuelles Feedback / Taster:**
    *   `LED`: GPIO 8
    *   `SW`: GPIO 9

## 7. Projekt-Infrastruktur

*   **Versionskontrolle:** Das Projekt wird auf GitHub verwaltet.
    *   **Repository:** `https://github.com/tostmann/culfw-ng`