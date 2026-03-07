# CULFW-NG Befehlsreferenz (ESP32-C6)

Diese Firmware ist culfw-kompatibel und erweitert um ESP32-spezifische Funktionen.

## Standard-Kommandos

| Befehl | Beschreibung | Beispiel |
| :--- | :--- | :--- |
| `V` | Ausgabe der Version, Hardware, ID und Frequenz | `V 1.0.1 culfw-NG Build: 123 (...) CUL32-C6 ID:58E6C5... (F-Band: 433MHz)` |
| `C` | CC1101 Diagnose (Part, Version, Marcstate) | `C35 = 0D, Part = 00, Ver = 14` |
| `X21` | Aktiviert Reporting (FS20, IT, HMS, etc.) | `X21` |
| `X25` | SIGNALduino-Emulationsmodus (MU;/MS;) | `X25` |
| `X00` | Deaktiviert Reporting | `X00` |
| `X99` | Debug-Modus: Ausgabe roher Puls-Timings (µs) | `X99` |
| `f433` | Schaltet auf 433.92 MHz um (Permanent im NVS) | `f433` |
| `f868` | Schaltet auf 868.30 MHz um (Permanent im NVS) | `f868` |
| `R<reg>` | Liest ein CC1101 Register (Hex) | `R0F` -> `0F = 3B` |
| `W<reg><val>` | Schreibt ein CC1101 Register (Hex) | `W0F3B` |
| `TX1` | Schaltet den Sendeträger permanent EIN (Test) | `TX1` |
| `TX0` | Schaltet den Sendeträger AUS | `TX0` |

## Protokoll-Kommandos (Senden)

| Befehl | Protokoll | Format |
| :--- | :--- | :--- |
| `F...` | **FS20** | `F<Housecode><Addr><Cmd>` (z.B. `F12345601`) |
| `is...` | **Intertechno** | `is<0/1/f Trits>` (V1) oder `is<32bit Hex>` (V3) |
| `H...` | **HMS** | `H<Housecode><Addr><Type><Data>` (z.B. `H1234010101`) |
| `T...` | **FHT** | `T<Code><Addr><Cmd><Val>` |
| `m...` | **Raw Pulses** | `m<HEX...>` (Dauer = Hex * 10µs, abwechselnd High/Low) |
| `To...` | **Oregon (Test)** | `To<HEX...>` (Sendet Oregon Manchester-kodiert) |

## Empfangene Datenformate (Reporting)

Wenn `X21` aktiv ist, werden empfangene Pakete wie folgt ausgegeben:

*   **FS20:** `F<HEX><RSSI>` (z.B. `F1234567840`)
*   **Intertechno:** `i<HEX><RSSI>`
*   **HMS:** `H<HEX><RSSI>`
*   **S300TH:** `K<HEX><RSSI>`
*   **Oregon:** `O<HEX><RSSI>`
*   **FHT:** `T<HEX><RSSI>`
*   **Generic:** `r<HEX><RSSI>` (Für unbekannte PWM/OOK Sensoren)

## Web-Interface & WiFi

Der Stick verbindet sich automatisch mit dem WLAN (**SSID: PalmBeach WiFi**).
Über die zugewiesene IP-Adresse kann ein einfaches Web-Interface aufgerufen werden:

*   **URL:** `http://<ip-adresse>/`
*   **Inhalt:** Aktueller Status, Frequenz, Modus und die letzten 10 empfangenen Funk-Events.

## Matter & Thread Integration (Vorschau)

| Befehl | Beschreibung | Beispiel |
| :--- | :--- | :--- |
| `MT` | Simuliert Matter-Events für Tests | `MT F123401` (Switch), `MT H1234T215` (Temp) |
| `ML` | Listet registrierte Matter-Endpunkte auf | `ML` |

Hinweis: Die echte Matter-Funktionalität erfordert das `esp-matter` SDK. Derzeit läuft die Firmware im **"Matter-Ready Stub Mode"**, der die Logik simuliert, bis das SDK in der Build-Umgebung verfügbar ist.

## Hardware-Pins (ESP32-C6)

*   **GDO0:** GPIO 2 (Interrupt/Data)
*   **GDO2:** GPIO 3 (Carrier Sense)
*   **LED:** GPIO 8 (Active Low, blinkt bei TX)
*   **Marker:** GPIO 4 (Low = 433MHz Hardware)
