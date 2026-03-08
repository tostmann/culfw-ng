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
| **Generische Sensoren** | Ja | Ja (via Matter) | `r...` / Matter | 433/868 MHz |

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über **natives ESP-IDF (`idf.py`)**.
*   **Board:** `ESP32-C6-NINI`
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
*   **Zustandsbehaftete Protokolle (Rolling Codes):** Für Protokolle wie **Somfy RTS** wurde ein dedizierter **Rolling-Code-Manager** (`rolling_code.c`) implementiert. Dieser speichert die Zählerstände für jedes Gerät persistent im NVS, um eine De-Synchronisation mit Original-Fernbedienungen zu verhindern. Die Sende-Routine wurde gehärtet und sendet einen präzisen Wake-Up-Puls gefolgt von der Manchester-kodierten Frame-Sequenz.
*   **Bivalenter Betriebsmodus (Hybride Intelligenz):** Die Firmware ist umschaltbar gestaltet, um die Stärken von CUL und SIGNALduino zu vereinen. Der gewählte Modus wird im NVS persistent gespeichert.
    *   **CUL-Modus (`X21`):** 100%ige Kompatibilität zum etablierten CUL-Protokoll. Ausgabe generischer Protokolle mit neuem `G`-Präfix (`G<Name><Daten>...`).
    *   **SIGNALduino-Modus (`X25`):** Vollständige Emulation eines SIGNALduino mit Rohdaten-Ausgabe (`MU;...` für unbekannte, `MS;...` für bekannte Protokolle). Die Ausgabe erfolgt über eine zentrale `slowrf_output_packet`-Funktion. Die `MU`-Ausgabe wird unterdrückt, wenn ein Decoder (fest oder generisch) das Signal erfolgreich verarbeitet hat, um doppelte Meldungen zu vermeiden.
*   **WiFi & Web-Dashboard:** Ein integrierter HTTP-Server (Port 80) bietet ein **vollwertiges, interaktives Diagnose-Dashboard** mit modernem CSS-Styling und 10-Sekunden-Auto-Refresh. Es zeigt Systemdaten (Chip-ID, IP-Adresse, RSSI), Protokolle, den Matter-Status, den **Duty-Cycle-Status** und eine **vollständige Live-Ansicht aller CC1101-Register** an. Zusätzlich ermöglicht es die **direkte Steuerung** von Frequenz und Modus, die **Simulation von Matter-TX-Befehlen** sowie die **Injektion von RF-Testsequenzen**.
*   **Regulatorische Konformität (Duty Cycle):** Eine implementierte, **frequenzdynamische Duty-Cycle-Überwachung** (`culfw_duty_cycle.c`) unterscheidet zwischen dem 868-MHz-Band (1%-Limit, 36.000 ms/h) und dem 433-MHz-Band (10%-Limit, 360.000 ms/h). Sie akkumuliert die Sendezeit über eine gleitende Stunde und blockiert Sendeanfragen bei Überschreitung, um die Funkzulassung nicht zu gefährden.
*   **Systemwartung & Reset:** Implementierung eines Factory-Resets, der alle persistenten Einstellungen im NVS löscht. Dieser kann via Kommando (`e`), über einen **Button im Web-Dashboard** oder durch Halten des Tasters an `GPIO 9` während des Boot-Vorgangs ausgelöst werden.
*   **Intellectual Property (IP) / Kopierschutz (3-Säulen-Strategie):**
    *   **1. Kommerzieller Schutz ("Matter-Schild"):** Die Bindung an den Matter-Standard erfordert ein offizielles **Device Attestation Certificate (DAC)**. Clones ohne dieses Zertifikat werden von Systemen wie Apple/Google Home als "nicht verifiziert" markiert.
    *   **2. Technischer Schutz (Hardware-Verschlüsselung):** Nutzung der ESP32-C6 Hardware-Features wie **Flash Encryption** (bindet die Firmware an den individuellen Chip) und **Secure Boot V2** (erlaubt nur vom Hersteller signierte Firmware-Updates).
    *   **3. Software-Bindung (IP-Schutz):** Die Protokoll-Datenbank (`protocols.json`) wird als verschlüsselte Datei (`protocols.enc`) auf dem SPIFFS abgelegt. Die Entschlüsselung im `config_loader` ist direkt an die **eindeutige Chip-ID (MAC-Adresse)** des ESP32 gebunden, was die Extraktion und Portierung der IP auf nicht autorisierte Hardware wirksam verhindert. Ein Python-Tool (`encrypt_protocols.py`) dient zur Erstellung der verschlüsselten Datei für eine spezifische Hardware.
*   **Frequenzerkennung und -management:**
    *   **Hardware-Default:** Die Modulfrequenz (433/868 MHz) wird initial über einen GPIO-Pin (`GPIO_433MARKER`) erkannt.
    *   **Software-Override:** Ein Benutzer kann die Frequenz zur Laufzeit per Kommando (`f433` oder `f868`) umschalten. Diese Einstellung wird **permanent im NVS gespeichert**.
    *   **Präzise Frequenzsteuerung:** Eine `cc1101_set_frequency_raw()` Funktion erlaubt die exakte Einstellung spezifischer Frequenzen (z.B. **433.42 MHz** für Somfy RTS), die von den Standardbändern abweichen.
*   **Versionierung:** Automatisierter Build-Nummer und detaillierter, culfw-kompatibler Versions-String, der nun Chip-ID, die **aktuelle IP-Adresse**, den **Betriebsmodus**, den **Matter-Status** sowie die **verbleibende Duty-Cycle-Sendezeit** enthält (`V`-Kommando).

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
*   **[DONE]** SIGNALduino `MU;` Logik verfeinert: Rohdaten-Ausgabe wird unterdrückt, wenn ein Decoder (fest oder generisch) gematcht hat.
*   **[DONE]** Erweiterung der Matter-Bridge um automatische Registrierung und Reporting für alle unterstützten Protokolle.
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
*   **[DONE]** Implementierung des Sende-Protokolls (TX) für Somfy RTS inkl. Rolling-Code-Management und präziser Frequenzabstimmung.
*   **[DONE]** Härtung des Somfy RTS Sende-Protokolls (Wake-up, Frame Repetition).
*   **[DONE]** Integration von Somfy RTS in den bidirektionalen Matter-Bridge TX-Pfad.
*   **[DONE]** Implementierung eines Factory-Resets (`e`-Kommando und Taster-Abfrage bei Boot).
*   **[DONE]** Implementierung der LED-Steuerung (`l`-Kommando).
*   **[DONE]** IP-Schutz: Tooling zur hardwaregebundenen Verschlüsselung der Protokolldatenbank (`encrypt_protocols.py`) erfolgreich eingesetzt und im Feld validiert (Hardware-MAC Bindung).
*   **[DONE]** Erstellung einer automatisierten Test-Infrastruktur (Python-Skripte) zur Validierung der Bridge- und Decoder-Logik.
*   **[DONE]** Code-Bereinigung und Aktualisierung der Dokumentation (`COMMANDS.md`).
*   **[DONE]** Erweiterung des TX-Pfads der Matter-Bridge um FHT- und HMS-Protokolle.
*   **[DONE]** Dynamische Duty-Cycle-Überwachung implementiert (10% für 433MHz / 1% für 868MHz).
*   **[DONE]** Erweiterung des Web-Dashboards um Echtzeit-RSSI, RF-Injection-Tool und Factory-Reset-Button.
*   **[DONE]** Implementierung der Diagnose-Kommandos `MA` (Matter Add) und `rssi`.
*   **[DONE]** Version-String (`V`) um verbleibende Duty-Cycle-Sendezeit (`DC_Rem`) erweitert.
*   **[DONE]** End-to-End-Test: Vollständiger RX->Matter->TX-Zyklus für Schalter- (Nexa), Sensor- (HMS) und Aktor-Protokolle (Somfy RTS, FHT) validiert.
*   **[DONE]** End-to-End-Test (Generic): Validierung des vollständigen Pfades für tabellengesteuerte Protokolle (Generic Decoder -> Matter Bridge -> TX-Translation).
*   **[DONE]** Release Management: Finaler Code-Stand als **Release v1.0.7 (Build 7)** und Binaries (`binaries/`) auf GitHub vorbereitet.
*   **[DONE]** Finale Matter SDK-Integration: Projektstruktur auf ESP-IDF-Standard (`main/`) umgestellt, Abhängigkeiten (`idf_component.yml`) deklariert.
*   **[DONE]** C/C++ Interoperabilität: C-Wrapper (`matter_interface.cpp`) für Matter SDK implementiert und mit `extern "C"` für die Einbindung in das restliche C-Projekt kompatibel gemacht.
*   **[DONE]** Strategische Migration des Build-Systems von PlatformIO zu nativem ESP-IDF (`idf.py`) zur Behebung von SDK-Kompatibilitätsproblemen vollzogen.
*   **[DONE]** Behebung von Build-Fehlern unter ESP-IDF: Korrektur von Include-Pfaden, Übernahme von Pin-Definitionen in `CMakeLists.txt` und Anpassung der Flash-Größe.
*   **[DONE]** Behebung von Linker-Fehlern durch Aktivierung notwendiger SDK-Komponenten (`CONFIG_MBEDTLS_HKDF_C=y`).
*   **[DONE]** Erfolgreiche Kompilierung des Projekts mit voll aktiviertem ESP-Matter SDK.
*   **[DONE]** Konfiguration der Hardware-Sicherheitsfeatures (Secure Boot V2, Flash Encryption) in `sdkconfig.defaults` zur Härtung des IP-Schutzes.

## 4. Neue Erkenntnisse / Probleme

*   **Problem: Flashen von Hardware mit aktiven Sicherheitsmerkmalen:**
    *   **Problem:** Nach der erfolgreichen Kompilierung der Firmware mit aktivierten Sicherheitsfeatures (Secure Boot V2, Flash Encryption) schlägt der Standard-Flash-Befehl (`idf.py flash`) fehl.
    *   **Analyse:** Das Tool `esptool.py` bricht mit der Meldung `Active security features detected, erasing flash is disabled` ab. Dies ist ein Sicherheitsmechanismus, der verhindert, dass bereits gesicherte (d.h. mit "gebrannten" eFuses) Chips einfach überschrieben werden können. Das Zielgerät ist offenbar bereits für die Produktion vorkonfiguriert.
    *   **Konsequenz:** Der Deployment-Prozess für bereits gesicherte Hardware unterscheidet sich fundamental vom Deployment auf "jungfräulicher" Hardware. Ein einfaches Löschen und Flashen ist nicht möglich.
*   **Erkenntnis: Vergrößerung des Bootloaders durch Sicherheitsfeatures:**
    *   **Problem:** Die Aktivierung von Secure Boot V2 und Flash Encryption fügt dem Bootloader erhebliche Mengen an Code für Signaturverifizierung und Entschlüsselungsroutinen hinzu. Dies führte dazu, dass der Bootloader die im Standard-Partitionsschema vorgesehene Größe von 32 KB (`0x8000`) überschritt.
    *   **Konsequenz:** Der Build-Prozess schlug fehl, da der Bootloader nicht in die für ihn vorgesehene Partition passte.
    *   **Lösung:** Die `partitions.csv` musste angepasst werden, um den Start-Offset der App-Partition (auf `0x20000`) zu verschieben und somit dem Bootloader mehr Platz einzuräumen.
*   **Strategische Entscheidung: Migration von PlatformIO zu nativem ESP-IDF:**
    *   **Problem:** Das PlatformIO-Build-System erwies sich bei der Integration des hochkomplexen `esp-matter`-SDKs als unzuverlässig und fehleranfällig (z.B. bei der Einbettung von Binärdaten für Zertifikate).
    *   **Entscheidung:** Das Projekt wurde vollständig auf den nativen ESP-IDF-Toolchain (`idf.py`) umgestellt, um einen stabilen, vorhersagbaren und wartbaren Build-Prozess zu gewährleisten.
*   **Erkenntnis (Abhängigkeitsmanagement im ESP-IDF):** Die Migration zu nativem IDF zeigt, wie kritisch die `sdkconfig`-Konfiguration ist. Ein Linker-Fehler (`undefined reference to 'mbedtls_hkdf'`) innerhalb des Matter SDKs konnte nur durch die Aktivierung eines spezifischen Flags (`CONFIG_MBEDTLS_HKDF_C`) in der MbedTLS-Komponente behoben werden. Dies verdeutlicht, dass Build-Flags und Komponenteneinstellungen tiefgreifende Auswirkungen auf die gesamte Anwendung haben.
*   **Erkenntnis (C/C++ Interoperabilität bei SDKs):** Das ESP-Matter SDK ist in C++ implementiert. Die Integration in ein bestehendes C-Projekt erforderte die Umstellung der Schnittstellen-Module (z.B. `matter_interface.c`) auf C++ (`.cpp`) und die Sicherstellung der C-Linkage für den Rest des Projekts über `extern "C"` im Header, um Linker-Fehler zu vermeiden.
*   **Architektur-Korrektur (Single-Core):** Der ESP32-C6 ist ein **Single-Core Prozessor (RISC-V)**. Die RTOS-Architektur wurde auf ein Single-Core-Modell mit **Task-Priorisierung** als primäres Steuerungsinstrument umgestellt, um die Echtzeitfähigkeit zu gewährleisten.
*   **Erkenntnis (Komplexität zustandsbehafteter Protokolle):** Die Implementierung von Protokollen wie **Somfy RTS** erfordert mehr als nur einen Encoder. Ein persistenter **Rolling-Code-Manager**, der die Zählerstände im NVS speichert, ist unerlässlich, um die Synchronisation mit dem Empfänger aufrechtzuerhalten und eine zuverlässige Funktion zu gewährleisten.
*   **Erkenntnis (Interaktive Web-Diagnose als Entwicklungsbeschleuniger):** Die Erweiterung des Web-Dashboards zu einem interaktiven Diagnose- und Test-Werkzeug hat sich als fundamentaler Entwicklungsbeschleuniger erwiesen. Das Testen und Debuggen von Decodern ist nun ohne physische Sender möglich, was die Entwicklungszyklen drastisch verkürzt.

## 5. Nächste Schritte

*   **Analyse der eFuse-Konfiguration:** Mit `espefuse.py summary` den exakten Status der Sicherheits-eFuses auf der Zielhardware ermitteln, um den genauen Grund für den Flash-Fehler zu bestätigen.
*   **Entwicklung eines Deployment-Prozesses für gesicherte Hardware:** Erarbeiten und Testen einer zuverlässigen Methode zum Flashen der signierten Firmware auf Geräte mit bereits aktivierten eFuses (z.B. via `esptool.py --force`).
*   **Validierung des Matter-Builds:** Nach erfolgreichem Flash, Durchführung eines vollständigen Matter-Commissioning-Prozesses mit einem handelsüblichen Controller (z.B. Apple Home), um die korrekte Funktion der Zertifikate (DAC) zu verifizieren.
*   **System-Validierung (Langzeit-Stabilität):** Durchführung von Langzeit-Stabilitätstests sowie Reichweiten- und Störfestigkeitstests in realen Einsatzszenarien.
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
    *   `SW`: GPIO 9 (Input mit Pull-Up für Factory Reset bei Boot)

## 7. Projekt-Infrastruktur

*   **Versionskontrolle:** Das Projekt wird auf GitHub verwaltet.
    *   **Repository:** `https://github.com/tostmann/culfw-ng`
*   **Build-System:** Umgestellt auf natives **ESP-IDF (`idf.py`)** zur Lösung von Kompatibilitätsproblemen mit dem ESP-Matter SDK. Die `platformio.ini` wurde entfernt.