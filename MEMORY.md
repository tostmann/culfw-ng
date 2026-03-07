# CULFW-NG Projekt-Gedächtnis

## 1. Projektziel

Entwicklung einer **intelligenten, hybriden Firmware** für ESP32-C6 basierte CUL-Sticks. Die Firmware soll klassische SlowRF-Protokolle (wie FS20, Intertechno) über ein CC1101-Modul senden und empfangen. Langfristiges strategisches Ziel ist die Schaffung eines autonomen **SlowRF-to-Matter/Thread Gateways** mit On-Board-Dekodierung.

### 1.1 Unterstützte Protokolle

| Protokoll | Empfang (RX) | Senden (TX) | Kommando | Frequenz (Standard) |
| :--- | :---: | :---: | :--- | :--- |
| **FS20** | Ja | Ja | `F...` | 868.30 MHz |
| **Intertechno (V1)** | Ja | Ja | `is...` | 433.92 MHz |
| **Intertechno (V3)** | Ja | Ja | `is...` (32bit) | 433.92 MHz |
| **HMS / EM1000** | Ja | Ja | `H...` | 868.30 MHz |
| **S300TH / ESA** | Ja | Nein | `K...` | 868.30 MHz |
| **FHT80b** | Ja | Ja | `T...` | 868.30 MHz |
| **Oregon Scientific** | Ja | Ja | `To...` (Test) | 433.92 MHz |
| **Generische Sensoren** | Ja | Nein | `r...` | 433/868 MHz |

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über PlatformIO.
*   **Board:** `esp32-c6-devkitc-1`
*   **Kommunikation:** Nativer USB-JTAG/CDC Treiber (`usb_serial_jtag`) für eine nicht-blockierende serielle Schnittstelle.
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **SPI-Kommunikation:** Die SPI-Geschwindigkeit wurde zur Erhöhung der Stabilität auf 500 kHz festgelegt. Für das Auslesen der Statusregister wird der `READ_BURST`-Modus (`0xC0`) verwendet.
*   **Persistenz:** Wichtige Einstellungen (z.B. der Reporting-Modus `X21`, die RF-Frequenz) werden im **Non-Volatile Storage (NVS)** des ESP32 gespeichert und bei Neustart automatisch wiederhergestellt.
*   **Software-Architektur: Gehärtetes Multi-Core FreeRTOS**
    *   **Strikte Task-Trennung mit Core-Affinität:**
        *   `slowrf_task` (hohe Priorität, **Core 0**): Exklusive Verarbeitung der Echtzeit-Funk-Signale (RX/TX). Das Pinning auf Core 0 minimiert Jitter und schützt die Signalverarbeitung vor Interferenzen durch WiFi/Matter-Stacks.
        *   `culfw_parser_task` (niedrigere Priorität, **Core 1**): Verarbeitet serielle Befehle, System-Management und ist für zukünftige Applikationslogik (z.B. Matter-Bridge) vorgesehen.
    *   **Thread-Sicherheit:** Alle Zugriffe auf den CC1101-Treiber sind durch einen **rekursiven Mutex (Semaphore)** geschützt. Dies verhindert Race Conditions und Datenkorruption, wenn mehrere Tasks (z.B. RF-Task und Parser-Task) auf den SPI-Bus zugreifen und erlaubt atomare, verschachtelte Operationen ohne Deadlocks.
    *   **Multi-Protokoll-Gateway:** Die Firmware agiert als "Staubsauger" für alle unterstützten OOK-Protokolle auf der aktiven Frequenz. Alle Decoder laufen parallel.
*   **Zukünftige Architektur: On-Board Intelligence**
    *   **Dateisystem:** Integration eines **SPIFFS-Dateisystems** zur Speicherung einer flexiblen Protokoll-Datenbank (`protocols.json`).
    *   **Table-Driven Decoding Engine:** Anstelle von fest einkompilierten Decodern wird eine generische Engine die Pulsfolgen mit den in `protocols.json` definierten Mustern abgleichen. Dies ermöglicht das Hinzufügen neuer Sensoren ohne Firmware-Update und ist die **Voraussetzung für die Matter-Integration**.
*   **Bivalenter Betriebsmodus (Hybride Intelligenz):** Die Firmware wird umschaltbar gestaltet, um die Stärken von CUL und SIGNALduino zu vereinen.
    *   **CUL-Modus (`X21`):** 100%ige Kompatibilität zum etablierten CUL-Protokoll für maximale Stabilität mit bestehenden Systemen (z.B. FHEM `00_CUL.pm`).
    *   **SIGNALduino-Modus (`X25`):** Vollständige Emulation eines SIGNALduino. In diesem Modus gibt die Firmware Rohdaten (`MU;...`, `MS;...`) aus, um die riesige Sensor-Datenbank des FHEM-SIGNALduino-Projekts freizuschalten.
*   **Frequenzerkennung und -management:**
    *   **Hardware-Default:** Die Modulfrequenz (433/868 MHz) wird initial über einen GPIO-Pin (`GPIO_433MARKER`) erkannt.
    *   **Software-Override:** Ein Benutzer kann die Frequenz zur Laufzeit per Kommando (`f433` oder `f868`) umschalten. Diese Einstellung wird **permanent im NVS gespeichert** und überschreibt die Hardware-Erkennung.
*   **Signal-Erfassung (RX):**
    *   Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben, um ein rohes, demoduliertes ASK/OOK-Signal am `GDO0_PIN` bereitzustellen.
    *   **Rausch-Unterdrückung:** `GDO2` ist als **Carrier Sense** konfiguriert (`IOCFG2=0x0E`). Interrupts werden nur bei ausreichendem Signalpegel verarbeitet.
*   **Signal-Aussendung (TX):**
    *   Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging mit präzisen Microsekunden-Delays (`ets_delay_us`) zu erzeugen.
*   **Versionierung:** Automatisierte Build-Nummer und detaillierter, culfw-kompatibler Versions-String.

## 3. Implementierungsstatus

*   **[DONE]** Projektinitialisierung mit PlatformIO.
*   **[DONE]** Basis-Implementierung des CC1101 SPI-Treibers.
*   **[DONE]** Grundgerüst für den culfw-Kommando-Parser und die SlowRF-Signalverarbeitung.
*   **[DONE]** Frequenz-Auto-Detektion und NVS-Speicherung.
*   **[DONE]** SPI-Kommunikation stabilisiert.
*   **[DONE]** CC1101 für RX in asynchronen seriellen Modus konfiguriert.
*   **[DONE]** SlowRF-Senden (TX) implementiert, inklusive Paket-Wiederholung.
*   **[DONE]** Multi-Protokoll-Empfang (RX) mit parallelen Decodern implementiert.
*   **[DONE]** Implementierung und Validierung von Decodern/Encodern für **FS20, Intertechno V1/V3, HMS, S300TH, FHT, Oregon Scientific**.
*   **[DONE]** Implementierung einer Rauschunterdrückung via Hardware Carrier Sense (GDO2).
*   **[DONE]** Konfigurations-Management im Non-Volatile Storage (NVS).
*   **[DONE]** RSSI-Reporting an alle empfangenen Pakete angehängt.
*   **[DONE]** Remote-Diagnose-Befehle (`R`, `W`, `X99`, `m`) implementiert.
*   **[DONE]** Periodischer "CUL-TICK" als Heartbeat implementiert.
*   **[DONE]** Laufzeit-Frequenzumschaltung (`f433`/`f868`) implementiert.
*   **[DONE]** RTOS-Architektur gehärtet (Core-Pinning, Task-Priorisierung, SPI-Mutex).
*   **[DONE]** End-to-End Validierung aller implementierten Protokolle.
*   **[DONE]** Benutzer-Dokumentation (`COMMANDS.md`) erstellt.
*   **[DONE]** Release Management: Finaler Code-Stand als **Release v1.0.1** auf GitHub getaggt.
*   **[DONE]** Partitionsschema für Dateisystem (SPIFFS) erweitert.
*   **[DONE]** Build-System um Upload einer Filesystem-Partition (`data/`) erweitert.
*   **[IN PROGRESS]** Entwicklung einer generischen, tabellengesteuerten Decoding-Engine.
*   **[TODO]** Implementierung des SPIFFS-Treibers und des JSON-Parsers in der Firmware.
*   **[TODO]** Implementierung des bivalenten Betriebsmodus (CUL vs. SIGNALduino).

## 4. Neue Erkenntnisse / Probleme

*   **Strategische Neuausrichtung:** Die reine CUL-Emulation ist nicht ausreichend. Die Überlegenheit der ESP32-C6-Plattform liegt in der Fähigkeit zur **On-Board-Dekodierung** und der Integration in moderne IoT-Ökosysteme (Matter/Thread). Der Stick muss zum autonomen Gateway werden.
*   **Hybride Intelligenz als Erfolgsfaktor:** Die Firmware wird bivalent ausgelegt. Sie vereint die Stabilität des etablierten CUL-Protokolls (`X21`-Modus) mit der Flexibilität des SIGNALduino-Rohdatenformats (`X25`-Modus). Dies ermöglicht dem Benutzer die Wahl des optimalen Modus für seine Anwendung.
*   **Voraussetzung für Matter:** Die **On-Board-Dekodierung** (gesteuert durch eine JSON-Datenbank im Dateisystem) ist die zwingende Voraussetzung für eine spätere Matter-Bridge-Funktionalität. Nur wenn der Stick die Semantik der Daten versteht (z.B. "Temperatur: 21.5°C"), kann er diese als standardisierten Matter-Endpunkt bereitstellen.
*   **Multi-Protokoll-Gateway-Architektur bestätigt:** Die parallele Ausführung aller Protokoll-Decoder ermöglicht den simultanen Empfang verschiedener Protokolle auf demselben Frequenzband.
*   **RTOS-Härtung:** Die initiale Implementierung mit einem einfachen Mutex barg die Gefahr von Deadlocks bei verschachtelten Funktionsaufrufen im CC1101-Treiber. Die Umstellung auf einen **rekursiven Mutex** hat dieses Problem behoben und die Stabilität der Treiber-Interaktionen unter Last erhöht.
*   **Test-Methodik bestätigt: Sensor-Emulation:** Die implementierte TX-Funktionalität erlaubt es, einen zweiten CULFW-NG Stick als vollwertigen Sensor/Aktor-Emulator zu verwenden. Dies ermöglicht umfassende End-to-End-Tests der gesamten Empfangs- und Dekodierungs-Pipeline ohne die Notwendigkeit physischer Test-Hardware.
*   **Matter Test-Strategie definiert:** Die Validierung der zukünftigen Matter-Integration wird nicht primär über komplexe Systeme wie HASS erfolgen, sondern über das offizielle CLI-Entwicklerwerkzeug **`chip-tool`**. Dies ermöglicht schlanke, skriptbare Tests für das Commissioning und die Attribut-Interaktion direkt auf dem Entwicklungssystem (z.B. Raspberry Pi via Docker), ohne GUI-Overhead.

## 5. Nächste Schritte

*   **Hybride CUL/SIGNALduino-Firmware (Strategische Priorität):** Entwicklung einer **bivalenten Firmware**, die per Kommando zwischen zwei Betriebsmodi umschalten kann, um maximale Kompatibilität und Flexibilität zu bieten.
    *   **CUL-Modus (`X21`, Standard):** 100%ige Kompatibilität mit dem FHEM-Modul `00_CUL.pm` für maximale Stabilität.
    *   **SIGNALduino-Modus (`X25`):** Vollständige Emulation eines SIGNALduino. In diesem Modus gibt die Firmware alle empfangenen OOK-Signale im **SIGNALduino-Raw-Format (`MU;...`, `MS;...`)** aus. Dies schaltet die Kompatibilität zur riesigen Sensor-Datenbank des FHEM-SIGNALduino-Projekts frei.
*   **On-Board Decoding Engine:** Implementierung der tabellengesteuerten Dekodierungslogik, die Protokolldefinitionen aus einer `protocols.json` im SPIFFS-Dateisystem liest. Dies macht die Firmware zukunftssicher und vom Host-System unabhängig.
*   **FHEM-Integration & Validierung:** Umfassende Tests der Firmware in beiden Modi (`CUL` und `SIGNALduino`) mit einem FHEM-Host-System zur Sicherstellung der Langzeitstabilität und Kompatibilität. Dies wird durch die Sensor-Emulations-Fähigkeit erleichtert.
*   **Roadmap-Planung: Matter/Thread-Bridge:** Nach erfolgreicher Implementierung des On-Board-Decoders, Beginn der Integration des ESP-Matter-SDK. Ziel ist es, dekodierte Sensordaten (z.B. Temperatur, Luftfeuchtigkeit, Kontaktstatus) direkt als standardisierte Matter-Endpunkte im Netzwerk bereitzustellen. Die initialen Tests und die Validierung der Implementierung erfolgen über das CLI-Tool `chip-tool`.

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