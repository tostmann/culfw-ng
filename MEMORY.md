# CULFW-NG Projekt-Gedächtnis

## 1. Projektziel

Entwicklung einer **intelligenten, hybriden Firmware** für ESP32-C6 basierte CUL-Sticks. Die Firmware soll klassische SlowRF-Protokolle (wie FS20, Intertechno) über ein CC1101-Modul senden und empfangen. Langfristiges strategisches Ziel ist die Schaffung eines autonomen **SlowRF-to-Matter/Thread Gateways** mit On-Board-Dekodierung, das als kommerziell tragfähiges und kopiergeschütztes Standalone-Produkt fungiert und nicht von Host-Systemen wie FHEM abhängig ist.

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
*   **Persistenz:** Wichtige Einstellungen (z.B. der Betriebsmodus `X21`/`X25`, die RF-Frequenz) werden im **Non-Volatile Storage (NVS)** des ESP32 gespeichert und bei Neustart automatisch wiederhergestellt.
*   **Software-Architektur: Gehärtetes Multi-Core FreeRTOS**
    *   **Strikte Task-Trennung mit Core-Affinität:**
        *   `slowrf_task` (hohe Priorität, **Core 0**): Exklusive Verarbeitung der Echtzeit-Funk-Signale (RX/TX). Das Pinning auf Core 0 minimiert Jitter und schützt die Signalverarbeitung vor Interferenzen durch WiFi/Matter-Stacks.
        *   `culfw_parser_task` (niedrigere Priorität, **Core 1**): Verarbeitet serielle Befehle, System-Management und ist für zukünftige Applikationslogik (z.B. Matter-Bridge) vorgesehen.
    *   **Thread-Sicherheit:** Alle Zugriffe auf den CC1101-Treiber sind durch einen **rekursiven Mutex (Semaphore)** geschützt. Dies verhindert Race Conditions und Datenkorruption.
    *   **Multi-Protokoll-Gateway:** Die Firmware agiert als "Staubsauger" für alle unterstützten OOK-Protokolle auf der aktiven Frequenz. Alle Decoder laufen parallel.
*   **Zukünftige Architektur: On-Board Intelligence & Matter**
    *   **Dateisystem:** Integration eines **SPIFFS-Dateisystems** zur Speicherung einer flexiblen Protokoll-Datenbank (`protocols.json`).
    *   **Table-Driven Decoding Engine:** Anstelle von fest einkompilierten Decodern wird eine generische Engine die Pulsfolgen mit den in `protocols.json` definierten Mustern abgleichen.
    *   **Matter-Architektur:** Der Stick wird als **Matter Aggregator (Bridge)** implementiert. Nur das Gateway wird einmalig gepaired. Erkannte SlowRF-Geräte werden als dynamische **Endpoints** (z.B. Temperatursensor, Schalter) im Matter-Netzwerk on-the-fly angelegt und über eine **"Dynamic Endpoint Registry"** (`matter_bridge.c`) verwaltet. Die Anwendungslogik ist gegen eine **API-Interface-Schicht** (`matter_interface.h`) entwickelt, um die Kompilierbarkeit ohne das vollständige SDK zu gewährleisten (Simulations-Modus).
*   **Bivalenter Betriebsmodus (Hybride Intelligenz):** Die Firmware ist umschaltbar gestaltet, um die Stärken von CUL und SIGNALduino zu vereinen.
    *   **CUL-Modus (`X21`):** 100%ige Kompatibilität zum etablierten CUL-Protokoll.
    *   **SIGNALduino-Modus (`X25`):** Vollständige Emulation eines SIGNALduino mit Rohdaten-Ausgabe (`MU;...` für unbekannte, `MS;...` für bekannte Protokolle). Die Ausgabe erfolgt über eine zentrale `slowrf_output_packet`-Funktion.
*   **Intellectual Property (IP) / Kopierschutz (3-Säulen-Strategie):**
    *   **1. Kommerzieller Schutz ("Matter-Schild"):** Die Bindung an den Matter-Standard erfordert ein offizielles **Device Attestation Certificate (DAC)**. Clones ohne dieses Zertifikat werden von Systemen wie Apple/Google Home als "nicht verifiziert" markiert.
    *   **2. Technischer Schutz (Hardware-Verschlüsselung):** Nutzung der ESP32-C6 Hardware-Features wie **Flash Encryption** (bindet die Firmware an den individuellen Chip) und **Secure Boot V2** (erlaubt nur vom Hersteller signierte Firmware-Updates).
    *   **3. Software-Bindung (IP-Schutz):** Kritische Logik, wie die zukünftige `protocols.json`-Decoding-Engine, kann an die **Chip-Unique-ID (MAC-Adresse)** gebunden werden, um eine einfache Software-Kopie zu verhindern.
*   **Frequenzerkennung und -management:**
    *   **Hardware-Default:** Die Modulfrequenz (433/868 MHz) wird initial über einen GPIO-Pin (`GPIO_433MARKER`) erkannt.
    *   **Software-Override:** Ein Benutzer kann die Frequenz zur Laufzeit per Kommando (`f433` oder `f868`) umschalten. Diese Einstellung wird **permanent im NVS gespeichert**.
*   **Signal-Erfassung (RX):**
    *   Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben.
    *   **Rausch-Unterdrückung:** `GDO2` ist als **Carrier Sense** konfiguriert (`IOCFG2=0x0E`).
*   **Signal-Aussendung (TX):**
    *   Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging zu erzeugen.
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
*   **[DONE]** RTOS-Architektur gehärtet (Core-Pinning, Task-Priorisierung, rekursiver SPI-Mutex).
*   **[DONE]** End-to-End Validierung aller implementierten Protokolle.
*   **[DONE]** Benutzer-Dokumentation (`COMMANDS.md`) erstellt.
*   **[DONE]** Release Management: Finaler Code-Stand als **Release v1.0.1** auf GitHub getaggt.
*   **[DONE]** Chip-Unique-ID (MAC) Auslesung als Basis für Kopierschutz implementiert.
*   **[DONE]** Partitionsschema für Dateisystem (SPIFFS) und Matter-Unterstützung erweitert (`partitions.csv` mit 3MB App-Partition).
*   **[DONE]** Architektur für Matter-Integration finalisiert (Portability-Layer via `matter_interface.h` mit Simulations-Modus).
*   **[DONE]** Basis-Struktur für Matter-Bridge-Modul (`matter_bridge.c/h`) erstellt (Dynamic Endpoint Registry).
*   **[DONE]** Diagnose-Kommando (`MT`) zur Simulation von Sensor-Events für Matter-Tests implementiert.
*   **[DONE]** Anbindung der RF-Decoder in `slowrf.c` an die `matter_bridge` zur automatischen Event-Weiterleitung.
*   **[DONE]** Entwicklung einer generischen, tabellengesteuerten Decoding-Engine (`generic_decoder.c` mit cJSON).
*   **[DONE]** Implementierung des SPIFFS-Treibers (`config_loader.c`) zum Laden von `protocols.json`.
*   **[DONE]** Integration des Generic Decoders in den `slowrf_task` (Parallelbetrieb mit festen Decodern).
*   **[DONE]** Implementierung der vollständigen Bit-Matching-Logik in `generic_decoder.c` (Sync- und Bit-Reading State Machine).
*   **[DONE]** Implementierung des bivalenten Betriebsmodus (CUL vs. SIGNALduino). Umschaltung via `X21`/`X25`, NVS-Persistenz und adaptive Output-Formate (`MS;`, `MU;`) sind aktiv.
*   **[DONE]** Erweiterung der Matter-Bridge um automatische Registrierung und Reporting für alle unterstützten Protokolle (FS20, IT, HMS, S300TH, Oregon, Generic).
*   **[DONE]** Implementierung von Diagnose-Kommandos für Matter (`ML` zum Auflisten der Endpunkte).
*   **[DONE]** WiFi-Konnektivität implementiert (Verbindung zu "PalmBeach WiFi").
*   **[DONE]** SIGNALduino `MU;` Logik verfeinert: Rohdaten-Ausgabe wird unterdrückt, wenn ein Decoder (fest oder generisch) gematcht hat.

## 4. Neue Erkenntnisse / Probleme

*   **Build-System gehärtet:** Ein hartnäckiges Build-Problem mit der `esp_hid`-Komponente (Bluetooth-Abhängigkeit) wurde durch eine lokale Dummy-Komponente gelöst. Ein weiteres Problem durch **doppelte Header-Dateien** (`src/slowrf.h` vs. `include/slowrf.h`) wurde durch eine saubere Trennung und Refactoring der Includes behoben, was den Build-Prozess weiter stabilisiert.
*   **Decoder-Architektur optimiert:** Die interne Datenstruktur des `generic_decoder` wurde von "Pulse Pairs" auf eine **flache Liste von Puls-Definitionen** umgestellt. Dies hat die State-Machine-Logik erheblich vereinfacht.
*   **Robuste Konfigurations-Engine:** Die SPIFFS-Ladelogik in `config_loader.c` ist mit einem **Fallback-Mechanismus** ausgestattet. Falls `protocols.json` fehlt, wird ein hartcodierter Default-JSON-String geladen, was die Grundfunktion sicherstellt.
*   **JSON-Protokoll-Format verfeinert:** Das Format für `protocols.json` nutzt nun ein zentrales `timing`-Objekt, dessen Werte über **Multiplikatoren** in den `definitions` wiederverwendet werden. Dies ist flexibel, kompakt und reduziert Redundanz.
*   **Vollautonome Matter-Bridge:** Die Integration ist abgeschlossen. Alle Decoder (fest und generisch) melden erkannte Geräte und deren Zustände automatisch an das `matter_bridge`-Modul. Dieses registriert neue Geräte on-the-fly als Matter-Endpoints und aktualisiert deren Status, wodurch der Stick als autonomes Gateway fungiert.
*   **SIGNALduino-Emulation implementiert:** Ein **Puls-Akkumulator (`mu_buffer`)** sammelt Pulse zwischen langen Synchronisations-Pausen. Falls kein spezifischer Decoder ein Paket erkennt, werden die Rohdaten im `MU;...`-Format (Message Unknown) ausgegeben. Dekodierte Pakete werden im `MS;...`-Format gesendet.

## 5. Nächste Schritte

*   **Validierung:** Durchführung von Tests zur Verifizierung der `MU;`-Rohdaten-Timings im Vergleich zu einem originalen SIGNALduino, um die Kompatibilität mit Host-Systemen sicherzustellen.
*   **Datenbank-Erweiterung:** Ergänzung der `protocols.json` um weitere, komplexere OOK-Protokolle, um die Flexibilität der generischen Decoding-Engine zu demonstrieren.
*   **Stabilitätstests:** Durchführung von Langzeittests im hybriden Matter-Gateway-Betrieb, um die Zuverlässigkeit des RTOS, der Speicherverwaltung und der parallelen Signalverarbeitung unter Last zu prüfen.

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