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
*   **Kommunikation:** Nativer USB-JTAG/CDC Treiber (`usb_serial_jtag`) für eine nicht-blockierende serielle Schnittstelle. Der RX-Puffer wurde auf 2048 Bytes erhöht, um lange Diagnose-Kommandos zu unterstützen.
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **SPI-Kommunikation:** Die SPI-Geschwindigkeit wurde zur Erhöhung der Stabilität auf 500 kHz festgelegt. Für das Auslesen der Statusregister wird der `READ_BURST`-Modus (`0xC0`) verwendet.
*   **Persistenz:** Wichtige Einstellungen (z.B. der Betriebsmodus `X21`/`X25`, die RF-Frequenz) werden im **Non-Volatile Storage (NVS)** des ESP32 gespeichert und bei Neustart automatisch wiederhergestellt.
*   **Software-Architektur: Angepasste Single-Core FreeRTOS Architektur (RISC-V)**
    *   **Task-Management auf Core 0:** Alle Tasks (`slowrf_task`, `culfw_parser_task`, Management-Tasks) laufen auf dem einzigen verfügbaren **Core 0**.
    *   **Echtzeit-Absicherung durch Priorisierung:** Die `slowrf_task` besitzt eine hohe Priorität, um die Echtzeit-Verarbeitung von Funksignalen (RX/TX) vor weniger zeitkritischen Tasks (z.B. WiFi, Web-Server, Kommando-Parsing) zu schützen und Jitter zu minimieren.
    *   **Thread-Sicherheit:** Alle Zugriffe auf den CC1101-Treiber sind durch einen **rekursiven Mutex (Semaphore)** geschützt. Dies verhindert Race Conditions und Datenkorruption.
    *   **Stabilität:** Der Stack für den `culfw_parser_task` wurde auf 8192 Bytes erhöht, um Stack Overflows bei der Verarbeitung sehr langer serieller Kommandos (z.B. via `mi`-Kommando) zu verhindern.
    *   **Multi-Protokoll-Gateway:** Die Firmware agiert als "Staubsauger" für alle unterstützten OOK-Protokolle auf der aktiven Frequenz. Alle Decoder laufen parallel.
*   **Zukünftige Architektur: On-Board Intelligence & Matter**
    *   **Dateisystem:** Integration eines **SPIFFS-Dateisystems** zur Speicherung einer flexiblen Protokoll-Datenbank (`protocols.json`). Ein Fallback-Mechanismus lädt einen hartcodierten Default, falls die Datei fehlt.
    *   **Table-Driven Decoding Engine:** Anstelle von fest einkompilierten Decodern wird eine generische Engine die Pulsfolgen mit den in `protocols.json` definierten Mustern abgleichen. Das JSON-Format nutzt ein `timing`-Objekt mit Multiplikatoren für eine kompakte und flexible Definition sowie ein **`"type"`-Feld** (`"switch"`, `"sensor"`) zur korrekten Zuordnung im Matter-Gateway. Die Protokolldatenbank kann zur Laufzeit über das `GR`-Kommando (Generic Reload) neu geladen werden.
    *   **Matter-Architektur (Bidirektional):** Der Stick wird als **Matter Aggregator (Bridge)** implementiert. Die Bridge arbeitet in beide Richtungen:
        *   **RX (RF $\rightarrow$ Matter):** Erkannte SlowRF-Geräte werden als dynamische **Endpoints** (z.B. Temperatursensor, Schalter) im Matter-Netzwerk on-the-fly angelegt und über eine **"Dynamic Endpoint Registry"** (`matter_bridge.c`) verwaltet.
        *   **TX (Matter $\rightarrow$ RF):** Eingehende Matter-Befehle werden über einen **Callback-Mechanismus** (`matter_bridge_command_cb`) verarbeitet. Die Bridge-Logik übersetzt diese Befehle in die entsprechenden SlowRF-Funkkommandos und sendet sie über das CC1101-Modul aus.
        *   Die Anwendungslogik ist gegen eine **API-Interface-Schicht** (`matter_interface.h`) entwickelt, um die Kompilierbarkeit ohne das vollständige SDK zu gewährleisten (Simulations-Modus).
    *   **Matter Endpoint ID-Masking:** Um die Proliferation von Endpoints zu verhindern (z.B. ON/OFF-Befehle derselben Fernbedienung), wird eine ID-Maskierung verwendet. Statusbits im RF-Code werden bei der Generierung der Endpoint-ID durch ein 'X' ersetzt, sodass ein physisches Gerät immer demselben Matter-Endpoint zugeordnet wird. Für generische Protokolle wird die ID aus dem Protokollnamen und den oberen Bits des RF-Codes gebildet (z.B. `Nexa_FFFFFF`).
*   **Bivalenter Betriebsmodus (Hybride Intelligenz):** Die Firmware ist umschaltbar gestaltet, um die Stärken von CUL und SIGNALduino zu vereinen.
    *   **CUL-Modus (`X21`):** 100%ige Kompatibilität zum etablierten CUL-Protokoll. Ausgabe generischer Protokolle mit neuem `G`-Präfix (`G<Name><Daten>...`).
    *   **SIGNALduino-Modus (`X25`):** Vollständige Emulation eines SIGNALduino mit Rohdaten-Ausgabe (`MU;...` für unbekannte, `MS;...` für bekannte Protokolle). Die Ausgabe erfolgt über eine zentrale `slowrf_output_packet`-Funktion. Die `MU`-Ausgabe wird unterdrückt, wenn ein Decoder (fest oder generisch) das Signal erfolgreich verarbeitet hat.
*   **WiFi & Web-Dashboard:** Ein integrierter HTTP-Server (Port 80) bietet ein **Diagnose-Dashboard** mit Auto-Refresh. Es zeigt:
    *   **System-Stammdaten:** Frequenz, Betriebsmodus.
    *   **Geladene Protokolle:** Liste der Generic Protocols aus `protocols.json` inkl. der Anzahl dekodierter Pakete.
    *   **Matter Bridge Status:** Liste aller dynamisch erstellten Endpunkte mit ihren RF-IDs.
    *   **Live-Log:** Die letzten empfangenen Funk-Events (Reverse-Chronological Order).
*   **Intellectual Property (IP) / Kopierschutz (3-Säulen-Strategie):**
    *   **1. Kommerzieller Schutz ("Matter-Schild"):** Die Bindung an den Matter-Standard erfordert ein offizielles **Device Attestation Certificate (DAC)**. Clones ohne dieses Zertifikat werden von Systemen wie Apple/Google Home als "nicht verifiziert" markiert.
    *   **2. Technischer Schutz (Hardware-Verschlüsselung):** Nutzung der ESP32-C6 Hardware-Features wie **Flash Encryption** (bindet die Firmware an den individuellen Chip) und **Secure Boot V2** (erlaubt nur vom Hersteller signierte Firmware-Updates).
    *   **3. Software-Bindung (IP-Schutz):** Kritische Logik, wie die zukünftige `protocols.json`-Decoding-Engine, kann an die **Chip-Unique-ID (MAC-Adresse)** gebunden werden. Diese ID wird bereits über das erweiterte `V`-Kommando ausgegeben und ist somit programmatisch verfügbar.
*   **Frequenzerkennung und -management:**
    *   **Hardware-Default:** Die Modulfrequenz (433/868 MHz) wird initial über einen GPIO-Pin (`GPIO_433MARKER`) erkannt.
    *   **Software-Override:** Ein Benutzer kann die Frequenz zur Laufzeit per Kommando (`f433` oder `f868`) umschalten. Diese Einstellung wird **permanent im NVS gespeichert**.
*   **Signal-Erfassung (RX):**
    *   Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben.
    *   **Rausch-Unterdrückung:** `GDO2` ist als **Carrier Sense** konfiguriert (`IOCFG2=0x0E`).
*   **Signal-Aussendung (TX):**
    *   Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging zu erzeugen.
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
*   **[DONE]** Implementierung des SPIFFS-Treibers (`config_loader.c`) zum Laden von `protocols.json`.
*   **[DONE]** Integration des Generic Decoders in den `slowrf_task` (Parallelbetrieb mit festen Decodern).
*   **[DONE]** Implementierung der vollständigen Bit-Matching-Logik in `generic_decoder.c` (Sync- und Bit-Reading State Machine).
*   **[DONE]** Implementierung des bivalenten Betriebsmodus (CUL vs. SIGNALduino). Umschaltung via `X21`/`X25`, NVS-Persistenz und adaptive Output-Formate (`MS;`, `MU;`) sind aktiv.
*   **[DONE]** Erweiterung der Matter-Bridge um automatische Registrierung und Reporting für alle unterstützten Protokolle (FS20, IT, HMS, S300TH, Oregon, Generic).
*   **[DONE]** SIGNALduino `MU;` Logik verfeinert: Rohdaten-Ausgabe wird unterdrückt, wenn ein Decoder (fest oder generisch) gematcht hat.
*   **[DONE]** WiFi-Konnektivität implementiert (Verbindung zu "PalmBeach WiFi").
*   **[DONE]** Erweitertes Web-Interface: Ausbau der Weboberfläche zu einem vollwertigen Diagnose-Dashboard mit Auto-Refresh, System-Status, Protokoll-Liste und Matter-Endpunkt-Anzeige.
*   **[DONE]** Integration der RF-Decoder mit dem Web-Log für Live-Event-Anzeige.
*   **[DONE]** Chip-Unique-ID (MAC) Auslesung als Basis für Kopierschutz implementiert.
*   **[DONE]** Behebung des Flash-Partition-Konflikts (Overlap-Fehler bei 0x10000).
*   **[DONE]** Anpassung der RTOS-Architektur an Single-Core-Betrieb (ESP32-C6).
*   **[DONE]** Device-Identität: Erweiterung des V (Version) Kommandos um die eindeutige Chip-ID (MAC) und die aktuelle IP-Adresse.
*   **[DONE]** Funktionstest: Erfolgreiche Verifizierung der IP-Ausgabe (V-Kommando) und des Web-Interfaces auf 433/868 MHz Hardware.
*   **[DONE]** Implementierung von Diagnose-Kommandos für Generic Decoder (`GL` zum Auflisten inkl. Zähler, `GR` zum Neuladen).
*   **[DONE]** Validierung des JSON-Parsers und der Protokoll-Lade-Logik aus SPIFFS.
*   **[DONE]** Anbindung des Generic Decoders an die SIGNALduino MU-Unterdrückungslogik.
*   **[DONE]** Validierung der *Sync-Detektion* in der Generic Decoder State Machine mit injizierten Test-Signalen (Nexa).
*   **[DONE]** Fehlerbehebung in der Generic Decoder Bit-Matching-Logik (`STATE_READ_BITS`).
*   **[DONE]** Implementierung und Validierung eines Test-Kommandos (`mi`) zur Injektion von Puls-Sequenzen.
*   **[DONE]** Anpassung des `mi`-Kommandos auf 16-Bit-Werte (`mi<HEX4>`) für lange Pulse (>2.55ms).
*   **[DONE]** Erfolgreiche Validierung des Generic Decoders mit injizierten Nexa- und Intertechno-V1-Signalen.
*   **[DONE]** Erhöhung der Puffergrößen im Kommando-Parser (2048 Bytes) und Task-Stack (8KB) zur Verarbeitung langer Test-Kommandos.
*   **[DONE]** Verfeinerung des SIGNALduino `MS;` Ausgabeformats für den Generic Decoder.
*   **[DONE]** Absicherung der seriellen Ausgabe (`usb_serial_jtag_write_bytes`) gegen Datenverlust durch `portMAX_DELAY`.
*   **[DONE]** Erweiterung der Matter-Bridge Logik um ID-Maskierung zur Gruppierung von physischen Geräten.
*   **[DONE]** Erweiterung von `protocols.json` um ein `"type"`-Feld (`switch`/`sensor`) zur korrekten Matter-Endpoint-Erstellung.
*   **[DONE]** Protokoll-Datenbank (`protocols.json`) um Bresser_Temp und X10_RF erweitert.
*   **[DONE]** Implementierung von Diagnose-Kommandos für Matter (`ML` zum Auflisten der Endpunkte, inkl. Uptime).
*   **[DONE]** Erweiterung des `V`-Kommandos um Modus-Anzeige und Matter-Status.
*   **[DONE]** Implementierung eines Matter-Bridge Heartbeat-Tasks zur Überwachung.
*   **[DONE]** Stabilitätstests: Langzeittests im hybriden Betrieb durchgeführt, Stack-Overflow-Probleme bei langen seriellen Kommandos identifiziert und behoben.
*   **[DONE]** SIGNALduino Kompatibilität: Endgültige Verifizierung der `MS;`-Ausgabeformate mit Generic Decoder.
*   **[DONE]** Validierung des Sensor-Pfads: End-to-End-Test der Matter-Bridge-Sensor-Kette mit injiziertem Bresser_Temp-Signal erfolgreich.
*   **[DONE]** Code-Bereinigung: Überflüssige Debug-Logs aus dem `generic_decoder` entfernt.
*   **[DONE]** Verfeinerung der Generic Decoder Logik zur Erkennung von Paket-Enden bei Protokollen mit variabler Bit-Länge.
*   **[DONE]** API-Erweiterung: Anpassung von `matter_interface.h` um einen Command-Callback für den TX-Pfad.
*   **[DONE]** Bidirektionale Matter-Bridge (TX-Pfad): Implementierung der Callback-Logik zur Weiterleitung von Matter-Befehlen an die RF-Sendefunktionen.
*   **[DONE]** Puffer-Vergrößerung: Erhöhung der RF-ID-Puffer in der Matter-Bridge für lange Identifikatoren.

## 4. Neue Erkenntnisse / Probleme

*   **Architektur-Korrektur (Single-Core):** Der ESP32-C6 ist ein **Single-Core Prozessor (RISC-V)**. Die RTOS-Architektur wurde auf ein Single-Core-Modell mit **Task-Priorisierung** als primäres Steuerungsinstrument umgestellt, um die Echtzeitfähigkeit zu gewährleisten.
*   **Architektur-Verfeinerung (Initialisierung):** Die Initialisierungs-Sequenz wurde angepasst. `config_loader_load_protocols()` wird nun in `app_main` ausgeführt, *bevor* die `slowrf_task` startet, um eine Race Condition zu vermeiden.
*   **Erkenntnis (Test-Infrastruktur):** Die Validierung des Generic Decoders erforderte ein neues Diagnosekommando (`mi<HEX>`), um exakte Pulsfolgen in die RX-Pipeline einzuspeisen. Dieses Kommando musste auf 16-Bit-Werte erweitert werden.
*   **Erkenntnis (System-Stabilität):** Die Verarbeitung sehr langer serieller Kommandos (>1000 Bytes) für die Signal-Injektion führte zu Stack-Overflow-Crashes. Dies erforderte eine signifikante Erhöhung des Stacks für den `culfw_parser_task` (auf 8KB) und der Größe des seriellen RX-Puffers.
*   **Erkenntnis (Decoder-Robustheit):** Die Generic Decoder Logik muss in der Lage sein, ein gültiges Paket auch dann abzuschließen, wenn die Bit-Anzahl im `min`/`max`-Bereich liegt und der nächste Puls *nicht* mehr zu einem validen Bit passt. Diese "early exit"-Logik ist entscheidend für die Unterstützung von Protokollen mit variabler Länge.
*   **Erkenntnis (Diagnose-Fähigkeit):** Ein vollumfängliches Web-Dashboard, das Protokolle, Matter-Endpunkte und Live-Logs kombiniert, ist für die Validierung des komplexen Brückensystems unerlässlich und beschleunigt die Fehlersuche erheblich.

## 5. Nächste Schritte

*   **Validierung des TX-Pfads:** Implementierung eines seriellen Test-Kommandos (z.B. `MC <EP_ID> <Value>`), um den Matter-Command-Callback und den gesamten Matter-to-RF-Sendeprozess im Simulationsmodus zu testen.
*   **Matter SDK-Integration:** Wechsel vom Simulations-Modus (`matter_interface.c`) zur echten ESP-Matter SDK-Implementierung, um die Bridge-Funktionalität in realen Matter-Ökosystemen zu testen.
*   **Reichweiten- & Störfestigkeitstests:** Durchführung von Tests mit realen Sendern/Sensoren über größere Distanzen und in Umgebungen mit potentiellen Störquellen (z.B. WLAN, andere Funkprotokolle).
*   **IP-Schutz Implementierung:** Aktivierung und Konfiguration von **Secure Boot V2** und **Flash Encryption** in der finalen Hardware.
*   **Dokumentation & Release:** Finalisierung der `COMMANDS.md` und Vorbereitung eines stabilen Release-Kandidaten (v1.1.0).

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