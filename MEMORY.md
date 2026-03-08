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
| **Somfy RTS** | Nein | Ja | `Ys...` / Matter | 433.42 MHz |
| **Generische Sensoren** | Ja | Nein | `r...` | 433/868 MHz |

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über PlatformIO.
*   **Board:** `esp32-c6-devkitc-1`
*   **Kommunikation:** Nativer USB-JTAG/CDC Treiber (`usb_serial_jtag`) für eine nicht-blockierende serielle Schnittstelle. Der RX-Puffer wurde auf 2048 Bytes erhöht, um lange Diagnose-Kommandos zu unterstützen.
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **SPI-Kommunikation:** Die SPI-Geschwindigkeit wurde zur Erhöhung der Stabilität auf 500 kHz festgelegt. Für das Auslesen der Statusregister wird der `READ_BURST`-Modus (`0xC0`) verwendet.
*   **Persistenz:** Wichtige Einstellungen (z.B. der Betriebsmodus `X21`/`X25`, die RF-Frequenz, Reporting An/Aus, **Rolling Codes**) werden im **Non-Volatile Storage (NVS)** des ESP32 gespeichert und bei Neustart automatisch wiederhergestellt.
*   **Software-Architektur: Angepasste Single-Core FreeRTOS Architektur (RISC-V)**
    *   **Task-Management auf Core 0:** Alle Tasks (`slowrf_task`, `culfw_parser_task`, Management-Tasks) laufen auf dem einzigen verfügbaren **Core 0**.
    *   **Echtzeit-Absicherung durch Priorisierung:** Die `slowrf_task` besitzt eine hohe Priorität, um die Echtzeit-Verarbeitung von Funksignalen (RX/TX) vor weniger zeitkritischen Tasks (z.B. WiFi, Web-Server, Kommando-Parsing) zu schützen und Jitter zu minimieren.
    *   **Thread-Sicherheit:** Alle Zugriffe auf den CC1101-Treiber sind durch einen **rekursiven Mutex (Semaphore)** geschützt. Dies verhindert Race Conditions und Datenkorruption.
    *   **Stabilität:** Der Stack für den `culfw_parser_task` wurde auf 8192 Bytes erhöht, um Stack Overflows bei der Verarbeitung sehr langer serieller Kommandos (z.B. via `mi`-Kommando) zu verhindern.
*   **On-Board Intelligence & Matter-Gateway:**
    *   **Dateisystem:** Integration eines **SPIFFS-Dateisystems** zur Speicherung einer flexiblen, **verschlüsselten** Protokoll-Datenbank (`protocols.enc`). Ein Fallback-Mechanismus lädt einen hartcodierten Default, falls die Datei fehlt.
    *   **Table-Driven Decoding Engine:** Anstelle von fest einkompilierten Decodern wird eine generische Engine (`generic_decoder.c`) die Pulsfolgen mit den in der Protokolldatenbank definierten Mustern abgleichen. Die JSON-Definition unterstützt Schlüsselfelder wie `"type"` (`"switch"`, `"sensor"`) zur korrekten Zuordnung im Gateway, `id_ignore_bits` zur Trennung von Geräte-ID und Statuswert sowie `scale`/`offset` zur Konvertierung von Sensor-Rohdaten in physikalische Einheiten. Die Datenbank kann zur Laufzeit über `GR` (Generic Reload) neu geladen werden.
    *   **Matter-Architektur (Bidirektional):** Der Stick wird als **Matter Aggregator (Bridge)** implementiert. Die Bridge arbeitet in beide Richtungen:
        *   **RX (RF $\rightarrow$ Matter):** Erkannte SlowRF-Geräte werden als dynamische **Endpoints** (z.B. Temperatursensor, Schalter) im Matter-Netzwerk on-the-fly angelegt und über eine **"Dynamic Endpoint Registry"** (`matter_bridge.c`) verwaltet. Der Name des dekodierenden Protokolls (z.B. "Nexa") wird pro Endpunkt gespeichert.
        *   **TX (Matter $\rightarrow$ RF):** Eingehende Matter-Befehle werden über einen **Callback-Mechanismus** (`matter_bridge_command_cb`) verarbeitet. Die Bridge-Logik identifiziert den Ziel-Endpunkt, liest den bei der Endpoint-Erstellung gespeicherten Protokoll-Namen (z.B. "Somfy") und die RF-ID aus und ruft den passenden Encoder auf, um den Befehl in ein SlowRF-Funkkommando zu übersetzen und über den CC1101 zu senden.
        *   Die Anwendungslogik ist gegen eine **API-Interface-Schicht** (`matter_interface.h`) entwickelt, um die Kompilierbarkeit ohne das vollständige SDK zu gewährleisten (Simulations-Modus).
    *   **Matter Endpoint ID-Masking:** Um die Proliferation von Endpoints zu verhindern (z.B. ON/OFF-Befehle derselben Fernbedienung), wird eine ID-Maskierung via `id_ignore_bits` in der Protokolldefinition verwendet. Dies stellt sicher, dass ein physisches Gerät immer demselben Matter-Endpoint zugeordnet wird.
*   **Zustandsbehaftete Protokolle (Rolling Codes):** Für Protokolle wie **Somfy RTS** wurde ein dedizierter **Rolling-Code-Manager** (`rolling_code.c`) implementiert. Dieser speichert die Zählerstände für jedes Gerät persistent im NVS, um eine De-Synchronisation mit Original-Fernbedienungen zu verhindern.
*   **Bivalenter Betriebsmodus (Hybride Intelligenz):** Die Firmware ist umschaltbar gestaltet, um die Stärken von CUL und SIGNALduino zu vereinen. Der gewählte Modus wird im NVS persistent gespeichert.
    *   **CUL-Modus (`X21`):** 100%ige Kompatibilität zum etablierten CUL-Protokoll. Ausgabe generischer Protokolle mit neuem `G`-Präfix (`G<Name><Daten>...`).
    *   **SIGNALduino-Modus (`X25`):** Vollständige Emulation eines SIGNALduino mit Rohdaten-Ausgabe (`MU;...` für unbekannte, `MS;...` für bekannte Protokolle). Die Ausgabe erfolgt über eine zentrale `slowrf_output_packet`-Funktion. Die `MU`-Ausgabe wird unterdrückt, wenn ein Decoder (fest oder generisch) das Signal erfolgreich verarbeitet hat.
*   **WiFi & Web-Dashboard:** Ein integrierter HTTP-Server (Port 80) bietet ein **vollwertiges, interaktives Diagnose-Dashboard** mit modernem CSS-Styling und Auto-Refresh. Es zeigt nicht nur Systemdaten, Protokolle und den Matter-Status, sondern ermöglicht auch die **direkte Steuerung** von Frequenz und Betriebsmodus, die **Anzeige des Duty-Cycle-Status** sowie die **Simulation von Matter-TX-Befehlen** über ein Web-Formular.
*   **Regulatorische Konformität (Duty Cycle):** Eine implementierte **1%-Duty-Cycle-Überwachung** (`culfw_duty_cycle.c`) für das 868-MHz-Band akkumuliert die Sendezeit über eine gleitende Stunde und blockiert weitere Sendeanfragen, wenn das Limit von 36.000 ms überschritten wird, um die Funkzulassung nicht zu gefährden.
*   **Intellectual Property (IP) / Kopierschutz (3-Säulen-Strategie):**
    *   **1. Kommerzieller Schutz ("Matter-Schild"):** Die Bindung an den Matter-Standard erfordert ein offizielles **Device Attestation Certificate (DAC)**. Clones ohne dieses Zertifikat werden von Systemen wie Apple/Google Home als "nicht verifiziert" markiert.
    *   **2. Technischer Schutz (Hardware-Verschlüsselung):** Nutzung der ESP32-C6 Hardware-Features wie **Flash Encryption** (bindet die Firmware an den individuellen Chip) und **Secure Boot V2** (erlaubt nur vom Hersteller signierte Firmware-Updates).
    *   **3. Software-Bindung (IP-Schutz):** Die Protokoll-Datenbank (`protocols.json`) wird als verschlüsselte Datei (`protocols.enc`) auf dem SPIFFS abgelegt. Die Entschlüsselung im `config_loader` ist direkt an die **eindeutige Chip-ID (MAC-Adresse)** des ESP32 gebunden, was die Extraktion und Portierung der IP auf nicht autorisierte Hardware wirksam verhindert. Ein Python-Tool (`encrypt_protocols.py`) dient zur Erstellung der verschlüsselten Datei für eine spezifische Hardware.
*   **Frequenzerkennung und -management:**
    *   **Hardware-Default:** Die Modulfrequenz (433/868 MHz) wird initial über einen GPIO-Pin (`GPIO_433MARKER`) erkannt.
    *   **Software-Override:** Ein Benutzer kann die Frequenz zur Laufzeit per Kommando (`f433` oder `f868`) umschalten. Diese Einstellung wird **permanent im NVS gespeichert**.
    *   **Präzise Frequenzsteuerung:** Eine `cc1101_set_frequency_raw()` Funktion erlaubt die exakte Einstellung spezifischer Frequenzen (z.B. **433.42 MHz** für Somfy RTS), die von den Standardbändern abweichen.
*   **Versionierung:** Automatisierter Build-Nummer und detaillierter, culfw-kompatibler Versions-String, der nun Chip-ID, die **aktuelle IP-Adresse**, den **Betriebsmodus** sowie den **Matter-Status** enthält (`V`-Kommando).

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
*   **[DONE]** RTOS-Architektur gehärtet (Task-Priorisierung, rekursiver SPI-Mutex).
*   **[DONE]** End-to-End Validierung aller implementierten Protokolle.
*   **[DONE]** Benutzer-Dokumentation (`COMMANDS.md`) erstellt und aktualisiert.
*   **[DONE]** Release Management: Finaler Code-Stand als **Release v1.0.1** auf GitHub getaggt.
*   **[DONE]** Partitionsschema für Dateisystem (SPIFFS) und Matter-Unterstützung erweitert (`partitions.csv` mit 3MB App-Partition).
*   **[DONE]** Architektur für Matter-Integration finalisiert (Portability-Layer via `matter_interface.h` mit Simulations-Modus).
*   **[DONE]** Basis-Struktur für Matter-Bridge-Modul (`matter_bridge.c/h`) erstellt (Dynamic Endpoint Registry).
*   **[DONE]** Diagnose-Kommando (`MT`) zur Simulation von Sensor-Events für Matter-Tests implementiert.
*   **[DONE]** Anbindung der RF-Decoder in `slowrf.c` an die `matter_bridge` zur automatischen Event-Weiterleitung.
*   **[DONE]** Entwicklung einer generischen, tabellengesteuerten Decoding-Engine (`generic_decoder.c` mit cJSON).
*   **[DONE]** Implementierung des SPIFFS-Treibers (`config_loader.c`) zum Laden der Protokolldatenbank.
*   **[DONE]** Integration des Generic Decoders in den `slowrf_task` (Parallelbetrieb mit festen Decodern).
*   **[DONE]** Implementierung der vollständigen Bit-Matching-Logik in `generic_decoder.c` (Sync- und Bit-Reading State Machine).
*   **[DONE]** Implementierung des bivalenten Betriebsmodus (CUL vs. SIGNALduino). Umschaltung via `X21`/`X25`, NVS-Persistenz und adaptive Output-Formate (`MS;`, `MU;`) sind aktiv.
*   **[DONE]** Erweiterung der Matter-Bridge um automatische Registrierung und Reporting für alle unterstützten Protokolle.
*   **[DONE]** SIGNALduino `MU;` Logik verfeinert: Rohdaten-Ausgabe wird unterdrückt, wenn ein Decoder (fest oder generisch) gematcht hat.
*   **[DONE]** WiFi-Konnektivität und Web-Server implementiert.
*   **[DONE]** Device-Identität: Erweiterung des V (Version) Kommandos um die eindeutige Chip-ID (MAC) und die aktuelle IP-Adresse.
*   **[DONE]** Implementierung von Diagnose-Kommandos für Generic Decoder (`GL` zum Auflisten inkl. Zähler, `GR` zum Neuladen).
*   **[DONE]** Implementierung und Validierung eines Test-Kommandos (`mi`) zur Injektion von Puls-Sequenzen (16-Bit-Werte).
*   **[DONE]** Erhöhung der Puffergrößen im Kommando-Parser (2048 Bytes) und Task-Stack (8KB) zur Verarbeitung langer Test-Kommandos.
*   **[DONE]** API-Erweiterung: Anpassung von `matter_interface.h` um einen Command-Callback für den TX-Pfad.
*   **[DONE]** Erweiterung von `protocols.json` um `"type"` und `id_ignore_bits` zur flexiblen Matter-Zuordnung.
*   **[DONE]** Generic Decoder: Erweiterung um `scale` und `offset` für die automatische Konvertierung von Sensorwerten.
*   **[DONE]** Implementierung der Protokoll-Verschlüsselung (XOR mit Chip-ID) und Erstellung des `encrypt_protocols.py`-Tools.
*   **[DONE]** Bidirektionale Matter-Bridge (TX-Pfad): Implementierung und Validierung der Logik zur Rekonstruktion von Sendebefehlen (Nexa/IT_V3/IT_V1/FS20) aus der Matter-ID.
*   **[DONE]** Persistenz des Betriebsmodus (`X21`/`X25`) und des Reporting-Status (`X00`/`X99`) im NVS implementiert.
*   **[DONE]** Erweiterung des Web-Interfaces zu einem vollwertigen Diagnose-Dashboard mit CSS-Styling, Auto-Refresh sowie interaktiver Steuerung von Frequenz/Modus und Matter-TX-Simulation.
*   **[DONE]** Implementierung einer Duty-Cycle-Überwachung (1%-Regel) zur Einhaltung regulatorischer Vorschriften.
*   **[DONE]** Implementierung des Sende-Protokolls (TX) für Somfy RTS inkl. Rolling-Code-Management und präziser Frequenzabstimmung.
*   **[DONE]** Integration von Somfy RTS in den bidirektionalen Matter-Bridge TX-Pfad.
*   **[DONE]** Erweiterung des Web-Dashboards um Anzeige des Duty-Cycle-Status und der eindeutigen Chip-ID.
*   **[DONE]** End-to-End-Test: Vollständiger RX->Matter->TX-Zyklus für Schalter- (Nexa) und Aktor-Protokolle (Somfy RTS) validiert.
*   **[DONE]** IP-Schutz: Tooling zur hardwaregebundenen Verschlüsselung der Protokolldatenbank (`encrypt_protocols.py`) erfolgreich eingesetzt.
*   **[DONE]** Test-Infrastruktur: Python-Script (`test_matter_bridge.py`) zur automatisierten Validierung der Bridge-Logik erstellt.
*   **[DONE]** Code-Bereinigung und Finalisierung der Dokumentation (`COMMANDS.md`).

## 4. Neue Erkenntnisse / Probleme

*   **Architektur-Korrektur (Single-Core):** Der ESP32-C6 ist ein **Single-Core Prozessor (RISC-V)**. Die RTOS-Architektur wurde auf ein Single-Core-Modell mit **Task-Priorisierung** als primäres Steuerungsinstrument umgestellt, um die Echtzeitfähigkeit zu gewährleisten.
*   **Architektur-Verfeinerung (Initialisierung):** Die gesamte Initialisierungssequenz in `app_main` wurde optimiert, um eine stabile System-Startup-Logik sicherzustellen: NVS -> SPIFFS/Config -> WiFi -> Web Server -> Matter Bridge -> CC1101 Radio -> Start der RTOS-Tasks. Dies verhindert Race Conditions zwischen abhängigen Komponenten.
*   **Erkenntnis (Komplexität zustandsbehafteter Protokolle):** Die Implementierung von Protokollen wie **Somfy RTS** erfordert mehr als nur einen Encoder. Ein persistenter **Rolling-Code-Manager**, der die Zählerstände im NVS speichert, ist unerlässlich, um die Synchronisation mit dem Empfänger aufrechtzuerhalten und eine zuverlässige Funktion zu gewährleisten.
*   **Erkenntnis (Implementierung regulatorischer Notwendigkeit):** Die Implementierung einer serverseitigen **Duty-Cycle-Überwachung** ist für ein kommerziell tragfähiges Produkt im 868-MHz-Band unerlässlich, um die gesetzliche 1%-Regel einzuhalten. Die Realisierung (`culfw_duty_cycle.c`) sichert die Konformität ab.
*   **Erkenntnis (Diagnose-Fähigkeit):** Ein vollumfängliches, **interaktives** Web-Dashboard, das nicht nur Live-Daten anzeigt, sondern auch die direkte Steuerung (Frequenz, Modus) und die Simulation von Ereignissen (Matter-TX) ermöglicht, ist für die Validierung des komplexen Brückensystems unerlässlich und beschleunigt die Fehlersuche erheblich.
*   **Erkenntnis (Matter SDK Komplexität):** Die direkte Integration des ESP-Matter SDK in eine bestehende PlatformIO/ESP-IDF-Umgebung ist aufwändig. Die gewählte Architektur mit einer Abstraktionsschicht (`matter_interface.h`) und einem **Simulations-Modus** hat sich als entscheidend erwiesen, um die Gateway-Logik unabhängig vom schweren SDK entwickeln und testen zu können. Dies ermöglicht eine Parallelentwicklung und reduziert die Build-Komplexität.

## 5. Nächste Schritte

*   **Finale Matter SDK-Integration:** Einrichten der dedizierten Build-Umgebung (ESP-IDF 5.x mit esp-matter), um vom Simulations-Modus auf die echte Implementierung umzuschalten und Tests in realen Matter-Ökosystemen (Apple Home, Google Home) durchzuführen.
*   **IP-Schutz Härtung:** Aktivierung der ESP32-C6 Hardware-Sicherheitsfeatures **Secure Boot V2** und **Flash Encryption**, um die 3-Säulen-Strategie zu vervollständigen.
*   **Erweiterung der Protokoll-Unterstützung (TX):** Ausbau der Matter-TX-Logik für weitere Protokolle wie HMS und FHT80b.
*   **System-Validierung:** Durchführung von Langzeit-Stabilitätstests sowie Reichweiten- und Störfestigkeitstests in realen Einsatzszenarien.
*   **Release-Vorbereitung:** Erstellung eines Release-Kandidaten (v1.1.0) und Finalisierung der Endbenutzer-Dokumentation.

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