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
    *   **Task-Management auf Core 0:** Alle Tasks (`slowrf_task`, `culfw_parser_task`, `thread_manager_task`, Management-Tasks) laufen auf dem einzigen verfügbaren **Core 0**.
    *   **Echtzeit-Absicherung durch Priorisierung:** Die `slowrf_task` besitzt eine hohe Priorität, um die Echtzeit-Verarbeitung von Funksignalen (RX/TX) vor weniger zeitkritischen Tasks (z.B. WiFi, Web-Server, Kommando-Parsing) zu schützen und Jitter zu minimieren.
    *   **Thread-Sicherheit:** Alle Zugriffe auf den CC1101-Treiber sind durch einen **rekursiven Mutex (Semaphore)** geschützt. Dies verhindert Race Conditions und Datenkorruption.
    *   **Stabilität:** Der Stack für den `culfw_parser_task` wurde auf 8192 Bytes erhöht, um Stack Overflows bei der Verarbeitung sehr langer serieller Kommandos (z.B. via `mi`-Kommando) zu verhindern.
*   **On-Board Intelligence & Matter-Gateway:**
    *   **Dateisystem:** Integration eines **SPIFFS-Dateisystems** zur Speicherung einer flexiblen, **verschlüsselten** Protokoll-Datenbank (`protocols.enc`). Ein Fallback-Mechanismus lädt einen hartcodierten Default, falls die Datei fehlt.
    *   **Table-Driven Decoding Engine:** Anstelle von fest einkompilierten Decodern wird eine generische Engine (`generic_decoder.c`) die Pulsfolgen mit den in der Protokolldatenbank definierten Mustern abgleichen. Die JSON-Definition unterstützt Schlüsselfelder wie `"type"` (`"switch"`, `"sensor"`) zur korrekten Zuordnung im Gateway, `id_ignore_bits` zur Trennung von Geräte-ID und Statuswert sowie `scale`/`offset` zur Konvertierung von Sensor-Rohdaten in physikalische Einheiten. Die Datenbank kann zur Laufzeit über `GR` (Generic Reload) neu geladen werden.
    *   **Matter-Architektur (Bidirektional, Hybride Konnektivität):** Der Stick wird als **Matter Aggregator (Bridge)** implementiert und unterstützt sowohl **Matter-over-WiFi** als auch **Matter-over-Thread**.
        *   **RX (RF $\rightarrow$ Matter):** Erkannte SlowRF-Geräte werden als dynamische **Endpoints** (z.B. Temperatursensor, Schalter) im Matter-Netzwerk on-the-fly angelegt und über eine **"Dynamic Endpoint Registry"** (`matter_bridge.c`) verwaltet. Der Name des dekodierenden Protokolls (z.B. "Nexa") wird pro Endpunkt gespeichert. Die maximale Anzahl dynamischer Endpoints wurde auf 20 erhöht (`CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT`).
        *   **TX (Matter $\rightarrow$ RF):** Eingehende Matter-Befehle werden über einen **Callback-Mechanismus** (`matter_bridge_command_cb`) verarbeitet. Die Bridge-Logik identifiziert den Ziel-Endpunkt, liest den bei der Endpoint-Erstellung gespeicherten Protokoll-Namen (z.B. "Somfy") und die RF-ID aus und ruft den passenden Encoder auf, um den Befehl in ein SlowRF-Funkkommando zu übersetzen und über den CC1101 zu senden.
        *   Die Anwendungslogik ist gegen eine **API-Interface-Schicht** (`matter_interface.h`) entwickelt, um die Kompilierbarkeit ohne das vollständige SDK zu gewährleisten (Simulations-Modus).
        *   **Thread-Sicherheit:** Alle API-Aufrufe, die den Zustand des Matter-Datenmodells verändern (z.B. `attribute::update`), werden durch einen expliziten Lock (`lock::chip_stack_lock`) gegen den Matter-Haupt-Thread abgesichert.
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
    *   **Präzise Frequenzsteuerung:** Eine `cc1101_set_frequency_raw()` Funktion erlaubt die exakte Einstellung spezifischer Frequenzen (z.B. **433.92 MHz** für Intertechno), die von den Standardbändern abweichen. Die Registerwerte hierfür wurden präzisiert.
*   **Versionierung:** Automatisierter Build-Nummer und detaillierter, culfw-kompatibler Versions-String, der nun Chip-ID, die **aktuelle IPv4- und IPv6-Adresse**, den **Betriebsmodus**, den **Matter-Status** sowie die **verbleibende Duty-Cycle-Sendezeit** enthält (`V`-Kommando).

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
*   **[DONE]** End-to-End Test (Generic): Validierung des vollständigen Pfades für tabellengesteuerte Protokolle (Generic Decoder -> Matter Bridge -> TX-Translation).
*   **[DONE]** Finale Matter SDK-Integration: Projektstruktur auf ESP-IDF-Standard (`main/`) umgestellt, Abhängigkeiten (`idf_component.yml`) deklariert.
*   **[DONE]** C/C++ Interoperabilität: C-Wrapper (`matter_interface.cpp`) für Matter SDK implementiert und mit `extern "C"` für die Einbindung in das restliche C-Projekt kompatibel gemacht.
*   **[DONE]** Strategische Migration des Build-Systems von PlatformIO zu nativem ESP-IDF (`idf.py`) zur Behebung von SDK-Kompatibilitätsproblemen vollzogen.
*   **[DONE]** Behebung von Build-Fehlern unter ESP-IDF: Korrektur von Include-Pfaden, Übernahme von Pin-Definitionen in `CMakeLists.txt` und Anpassung der Flash-Größe.
*   **[DONE]** Behebung von Linker-Fehlern durch Aktivierung notwendiger SDK-Komponenten (`CONFIG_MBEDTLS_HKDF_C=y`).
*   **[DONE]** Erfolgreiche Kompilierung des Projekts mit voll aktiviertem ESP-Matter SDK.
*   **[DONE]** Konfiguration der Hardware-Sicherheitsfeatures (Secure Boot V2, Flash Encryption) in `sdkconfig.defaults` zur Härtung des IP-Schutzes.
*   **[DONE]** Erfolgreiche Diagnose des Hardware-Sicherheitsstatus mittels `espefuse.py` (Bestätigung: 1x Board "gebrickt", 1x Board offen).
*   **[DONE]** Erfolgreicher Build und Flash einer **unverschlüsselten** Firmware auf offener Hardware.
*   **[DONE]** Erfolgreicher Boot der Firmware: WiFi-Verbindung und Matter-Dienste starten korrekt auf dem Zielgerät.
*   **[DONE]** Entfernung der seriellen `CUL-TICK` Heartbeat-Meldung zur Verbesserung der Lesbarkeit der Konsole.
*   **[DONE]** Fehlerbehebung Timing-Inkompatibilität: Toleranzen im Intertechno-Decoder an Legacy-CULs angepasst und Logikfehler (ITv1/ITv3 Sync-Konflikt) behoben.
*   **[DONE]** Test-Infrastruktur gehärtet (automatischer Factory-Reset des Sender-CULs).
*   **[DONE]** End-to-End-Test (Sensor-Pfad): Emulation eines Temperatursensors via Intertechno V1 Protokoll erfolgreich validiert.
*   **[DONE]** Decoder-Validierung (FS20): Erfolgreicher Test des FS20-Decoders und der Matter-Bridge-Anbindung mittels direkter Puls-Injektion (`mi`-Kommando).
*   **[DONE]** Fehlerbehebung Matter-Initialisierung: Korrektur der Initialisierungssequenz in `matter_interface.cpp` zur expliziten Erzeugung des Root-Nodes und des Aggregator-Endpoints.
*   **[DONE]** Docker-Umgebung für Home Assistant und Matter-Server aufgesetzt und validiert.
*   **[DONE]** Erfolgreiche Verifikation der mDNS-Sichtbarkeit des Matter-Gerätes (`_matterc._udp`).
*   **[DONE]** Automatisierte Test-Skripte für Factory-Reset (`factory_reset.py`) und Commissioning-Versuche (`commission_script.py`) entwickelt.
*   **[DONE]** Entwicklung einer Prozedur zum simultanen Reset von Gerät (Factory Reset) und Matter-Server (Docker-Neustart) zur Behebung von "Stale Session"-Fehlern.
*   **[DONE]** Tiefenanalyse des Matter-Commissioning-Fehlers via serieller Logs und Netzwerk-Diagnose.
*   **[DONE]** **Build-Umgebung gehärtet:** Probleme mit der ESP-IDF-Toolchain (fehlendes `cmake` im PATH) behoben und einen stabilen Build-Prozess mit `idf.py` sichergestellt.
*   **[DONE]** **Release-Management:** Firmware-Version für den Pairing-Fix auf **v1.1.0-NG (Build 8)** aktualisiert.
*   **[DONE]** **Fehlerbehebung Matter-Pairing:** Thread-Funktionalität in allen Konfigurationsdateien konsistent deaktiviert, Pairing-Parameter (Passcode/Discriminator) fixiert und Konsolen-Logs auf USB-JTAG umgeleitet, um deterministisches Verhalten sicherzustellen.
*   **[DONE]** Implementierung zur **dynamischen Generierung des Pairing-Codes** durch das Matter-SDK in `matter_interface.cpp` zur Sicherstellung der Korrektheit.
*   **[DONE]** Build-Prozess gehärtet: Zahlreiche C/C++ Interoperabilitäts- und SDK-Konfigurationsfehler in `matter_interface.cpp` und `sdkconfig` behoben.
*   **[DONE]** Erfolgreicher Build und Flash der Firmware (**v1.1.0-NG**) mit funktionierender dynamischer Code-Generierung.
*   **[DONE]** **Erfolgreiches Matter-Commissioning:** Das Gerät wurde erfolgreich mit dem dynamisch generierten Code in Home Assistant eingebunden. Die grundlegende Kommunikation über IPv6 ist verifiziert.
*   **[DONE]** **IPv6-Konnektivität implementiert:** WiFi-Manager erweitert, um IPv6 Link-Local-Adressen zu aktivieren und zu verwalten.
*   **[DONE]** **Version-String (`V`) erweitert:** Ausgabe um die aktive IPv6-Adresse ergänzt.
*   **[DONE]** **Build-Skript optimiert:** Inkrementelles Kompilieren als Standard in `build_idf.sh` festgelegt, um Entwicklungszyklen zu beschleunigen.
*   **[DONE]** **Thread-Sicherheit für Matter-Bridge gehärtet:** API-Aufrufe mit `chip_stack_lock` synchronisiert, um Race Conditions zu verhindern.
*   **[DONE]** **Fehlerbehebung Web-Interface:** URL-Parsing für Befehle mit Leerzeichen (z.B. 'MT ...') korrigiert.
*   **[DONE]** **Test-Infrastruktur erweitert:** OpenThread Border Router (OTBR) als Docker-Container (`openthread/otbr`) aufgesetzt und für Thread-Kommunikation konfiguriert.
*   **[DONE]** **Fehlerbehebung Matter-Bridge-Absturz:** Behoben durch explizite Initialisierung der Endpoint-Konfigurationen (`config_t`), um kritische 'Cluster cannot be NULL'-Fehler im SDK zu vermeiden und die Stabilität bei der dynamischen Endpoint-Erstellung zu gewährleisten.
*   **[DONE]** **API-Anpassung (Matter SDK):** Endpoint-Erstellungslogik an geänderte SDK-API angepasst (Verwendung von `set_parent_endpoint` anstelle veralteter Funktionen), um Kompilierung zu ermöglichen und Build-Fehler zu beheben.
*   **[DONE]** **Sichtbarkeit dynamischer Endpoints:** Implementierung von `esp_matter::endpoint::enable()` nach der Erstellung, um die Endpoints im Matter-Netzwerk sichtbar zu machen.
*   **[DONE]** **End-to-End-Validierung (RX-Pfad):** Erfolgreiche Erstellung, Aktivierung und Sichtbarmachung dynamischer Endpoints (Sensor/Schalter) im Matter-Netzwerk (Home Assistant) und korrekte Übermittlung der Attribut-Werte verifiziert.
*   **[DONE]** **Thread-Infrastruktur (OTBR):** Die Docker-Konfiguration des OpenThread Border Router Containers wurde stabilisiert und gehärtet. Der Container startet nun zuverlässig.
*   **[DONE]** **Fehlerbehebung OTBR-Hardware:** Das OpenThread RCP-Modul (ESP32-C6) wurde erfolgreich mit einer stabilen, vorkompilierten Firmware (`generic-esp32c6.bin`) neu geflasht. Der Bootloop wurde damit behoben und der `otbr-agent` kann nun stabil mit dem Modul kommunizieren.
*   **[DONE]** **Release-Management:** Meilenstein **v1.1.0-NG** (stabile Matter-over-WiFi Implementierung) erstellt und auf GitHub veröffentlicht.
*   **[DONE]** **Architektur für Thread-Integration:** `thread_manager.c/h` Modul zur Kapselung des OpenThread-Stacks entworfen und implementiert.
*   **[DONE]** **Integration Thread-Stack:** `thread_manager_init()` in die Haupt-Applikationslogik (`app_main`) integriert.
*   **[DONE]** **Build-Konfiguration für Thread:** `sdkconfig.defaults` um `CONFIG_OPENTHREAD_ENABLED=y` und `CONFIG_ESP_MATTER_ENABLE_OPENTHREAD=y` erweitert.
*   **[DONE]** **Firmware-Speicher erweitert:** Partitionsschema (`partitions.csv`) angepasst, um die durch den Thread-Stack vergrößerte Firmware aufzunehmen (Factory-Partition auf 2.5MB vergrößert).

## 4. Erkenntnisse & Gelöste Probleme

*   **Neues Problem: Build-Fehler nach Aktivierung von OpenThread.**
    *   **Analyse:** Der Build-Prozess bricht mit dem Fehler `fatal error: esp_openthread.h: No such file or directory` ab. Dies deutet darauf hin, dass die neuen Konfigurationseinstellungen aus `sdkconfig.defaults` noch nicht in der aktiven Build-Konfiguration (`sdkconfig`) übernommen wurden und das Build-System die Abhängigkeiten zur OpenThread-Komponente nicht korrekt auflöst.
    *   **Lösungsstrategie:** Ausführen von `idf.py reconfigure`, um die Projektkonfiguration zu aktualisieren und die Inkludierung der OpenThread-Header zu erzwingen.
*   **Gelöstes Problem: OpenThread-Funkmodul (ESP32-C6) war in permanenter Boot-Schleife.**
    *   **Analyse:** Der OpenThread Border Router (`otbr-agent`) konnte keine Verbindung zum Funkmodul (Radio Co-Processor, RCP) auf `/dev/ttyACM3` herstellen. Eine direkte Analyse der seriellen Schnittstelle des Moduls zeigte, dass dessen Firmware nicht startete, sondern sich der ESP32-C6 in einer permanenten Neustart-Schleife befand (`ESP-ROM:esp32c6...`).
    *   **Lösung:** Die fehlerhafte Firmware des RCP-Moduls wurde mittels `esptool.py` vollständig überschrieben. Eine stabile, vorkompilierte RCP-Firmware (`generic-esp32c6.bin`) wurde auf den ESP32-C6 geflasht. Unmittelbar danach startete das Modul korrekt und der `otbr-agent` im Docker-Container konnte die Verbindung erfolgreich herstellen. **Der Blocker für die Matter-over-Thread-Validierung ist damit beseitigt.**
*   **Gelöstes Problem: Docker-basierte OTBR-Instabilität durch Bug im Start-Skript und falsche API-Konfiguration.**
    *   **Analyse:** Der `openthread/otbr` Docker-Container stürzte in einer Neustart-Schleife ab oder war für Home Assistant nicht erreichbar. Die Ursache war zweigeteilt:
        1.  **Interner Start-Daemon Bug:** Das Standard-Startskript (`/app/script/server`) im Container verwendet einen fehlerhaften `start-stop-daemon`-Aufruf, der bei wiederholten Starts zu einem instabilen Zustand führt.
        2.  **Falsche Listen-Adresse:** Der `otbr-agent` lauscht standardmäßig nur auf `127.0.0.1` (localhost), was den Zugriff von anderen Containern (z.B. Home Assistant Matter Server) verhindert.
    *   **Lösung:** Eine grundlegende Anpassung der `docker-compose.yml`. Das fehlerhafte Start-Skript des Containers wird komplett umgangen, indem der `entrypoint` des Containers überschrieben wird. Die Dienste (`rsyslog`, `dbus`, `otbr-agent`, `otbr-web`) werden nun direkt und in der korrekten Reihenfolge gestartet. Dem `otbr-agent` wird dabei der kritische Parameter `--rest-listen-address 0.0.0.0` übergeben. **Der OTBR-Container startet nun stabil und ist im Netzwerk voll funktionsfähig.**
*   **Erkenntnis: Dynamische Endpoints müssen explizit aktiviert werden (Validiert).**
    *   **Analyse:** Dynamisch über `...::create()` erstellte Endpoints wurden vom SDK zwar intern verwaltet, aber nicht aktiv im Matter-Netzwerk publiziert.
    *   **Konsequenz:** In `matter_interface.cpp` wird nun direkt nach der Erstellung eines Endpoints die Funktion `esp_matter::endpoint::enable(endpoint)` aufgerufen. **Das Problem der Unsichtbarkeit ist damit nachweislich gelöst.**
*   **Erkenntnis: SDK-Abstürze durch uninitialisierte Konfigurationen (Validiert).**
    *   **Analyse:** Die Firmware stürzte beim Erstellen dynamischer Matter-Endpoints mit einer `Cluster cannot be NULL`-Fehlermeldung ab.
    *   **Konsequenz:** Die `matter_interface.cpp` wurde gehärtet, indem für jeden Endpoint-Typ eine explizite, standard-initialisierte `config_t`-Struktur erzeugt wird. **Der Absturz konnte mit dieser Methode nachweislich behoben werden.**
*   **Erkenntnis: ESP-Matter SDK API-Evolution.**
    *   **Analyse:** Ein Build-Fehler (`'get_root_node_endpoint' is not a member of ...`) zeigte, dass sich die API zur Erstellung und Verknüpfung von Endpoints in der verwendeten SDK-Version (`>=1.2.0`) geändert hat.
    *   **Konsequenz:** Die Logik in `matter_interface.cpp` wurde umgestellt. Endpoints werden nun zuerst am Haupt-`node` erstellt und anschließend dem `aggregator`-Endpoint mittels `set_parent_endpoint` explizit untergeordnet.
*   **Erkenntnis: Irreversibilität von eFuses – Praxistest mit Konsequenzen.**
    *   **Analyse:** Das 868MHz-Board hat **permanent aktive `SECURE_BOOT_EN` eFuses** mit einem nicht mehr verfügbaren Schlüssel.
    *   **Konsequenz:** Dieses Board ist für die weitere Entwicklung effektiv unbrauchbar ("gebrickt"). Die Entwicklung wird **ausschließlich auf ungesicherter Hardware** (433MHz-Board) fortgesetzt.

## 5. Nächste Schritte

*   **Behebung des Build-Fehlers (Höchste Priorität):** Die Projektkonfiguration mittels `idf.py reconfigure` erneuern, um die OpenThread-Abhängigkeiten korrekt aufzulösen und einen erfolgreichen Build der Firmware mit aktiviertem Thread-Stack zu ermöglichen.
*   **Matter-over-Thread Validierung:** Nach erfolgreichem Build: Durchführung eines End-to-End-Tests der Kommunikation (RX und TX) über das nun auf dem Gerät aktive Thread-Netzwerk, um die hybride (WiFi & Thread) Funktionalität zu verifizieren.
*   **End-to-End-Validierung der Matter-Bridge (TX-Pfad):** Senden von Befehlen aus Home Assistant (z.B. Schalten eines via RF erstellten FS20- oder Somfy-Endpoints) und Verifikation des am CC1101 korrekt generierten und gesendeten Funksignals über WiFi und Thread.
*   **System-Validierung (Langzeit-Stabilität):** Durchführung von Langzeit-Stabilitätstests sowie Reichweiten- und Störfestigkeitstests in realen Einsatzszenarien.
*   **Deployment-Prozess für gesicherte Hardware (Zurückgestellt):** Das Erarbeiten einer zuverlässigen Methode zum Flashen der signierten Firmware auf Geräte mit bereits aktivierten eFuses ist für die Produktion kritisch, wird aber aufgrund der Komplexität und der "gebrickten" Hardware vorerst zurückgestellt.

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
*   **Build-System:** Umgestellt auf natives **ESP-IDF (`idf.py`)**.
    *   **Anforderung:** Benötigt eine korrekt konfigurierte ESP-IDF-Toolchain, inklusive `cmake` und der `export.sh` Umgebungsvariablen.
    *   **Optimierung:** Das Build-Skript (`build_idf.sh`) wurde für schnellere, inkrementelle Builds optimiert.
*   **Test-Umgebung (Systemebene):**
    *   **Technologie:** Docker und Docker Compose auf Raspberry Pi 5.
    *   **Services:**
        *   `homeassistant`: `ghcr.io/home-assistant/home-assistant:stable`
        *   `matter-server`: `ghcr.io/home-assistant-libs/python-matter-server:stable`
        *   `otbr`: `openthread/otbr:latest` (OpenThread Border Router)
    *   **Konfiguration:** Alle Container laufen im `host`-Netzwerkmodus. Die Stabilität des `otbr`-Containers wurde durch eine **grundlegend modifizierte `docker-compose.yml`** sichergestellt. Diese umgeht das fehlerhafte interne Start-Skript (`start-stop-daemon`-Bug) und startet die `otbr-agent`- und `otbr-web`-Dienste direkt. Kritisch ist hierbei, dass der `otbr-agent` explizit mit dem Parameter `--rest-listen-address 0.0.0.0` gestartet wird, um die REST-API für Home Assistant im Netzwerk verfügbar zu machen. Das Web-Interface ist auf Port **8081** erreichbar.
    *   **Test-Skripte:** Python-Skripte für automatisierte Tests, inkl. Factory-Reset (`force_reset.py`) und Log-Erfassung (`capture_boot.py`).