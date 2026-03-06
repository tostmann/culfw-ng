# CULFW-NG Projekt-Gedächtnis

## 1. Projektziel

Entwicklung einer culfw-kompatiblen Firmware für ESP32-C6 basierte CUL-Sticks zur Emulation von SlowRF-Protokollen (wie FS20, Intertechno, etc.). Die Firmware soll die RF-Protokolle über ein CC1101-Modul senden und empfangen.

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über PlatformIO.
*   **Board:** `esp32-c6-devkitc-1`
*   **Kommunikation:** Nativer USB-JTAG/CDC Treiber (`usb_serial_jtag`) für eine nicht-blockierende serielle Schnittstelle (culfw-Kommandos). Standard-`printf`/`getchar` verursachten Watchdog-Timeouts und wurden ersetzt.
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **Software-Architektur:** FreeRTOS-Task-basiert.
    *   `culfw_parser_task`: Verarbeitet eingehende serielle Befehle (z.B. `V`, `X`, `F`).
    *   `slowrf_task`: Verarbeitet die vom CC1101-Modul empfangenen Pulsfolgen und gibt sie formatiert aus.
*   **Frequenzerkennung:** Automatische Erkennung der Modulfrequenz (433/868 MHz) über einen GPIO-Pin (`GPIO_433MARKER`), der auf GND gezogen wird.
*   **Signal-Erfassung (RX):** Ein GPIO-Pin (`GDO0_PIN`) wird als Interrupt-Quelle genutzt, um die Flanken der empfangenen Signale zu erfassen. Die Timestamps werden per Queue (`slowrf_queue`) an den `slowrf_task` übergeben.
*   **Signal-Aussendung (TX):** Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging mit präzisen Microsekunden-Delays (`ets_delay_us`) zu erzeugen.

## 3. Implementierungsstatus

*   **[DONE]** Projektinitialisierung mit PlatformIO.
*   **[DONE]** Basis-Implementierung des CC1101 SPI-Treibers (`cc1101.c`), erweitert um TX/RX-Modusumschaltung.
*   **[DONE]** Grundgerüst für den culfw-Kommando-Parser (`culfw_parser.c`) mit Erkennung von `V`, `X`, `C` und `F`.
*   **[DONE]** Grundgerüst für die SlowRF-Signalverarbeitung (`slowrf.c`) mit ISR und RTOS-Queue.
*   **[DONE]** Frequenz-Auto-Detektion implementiert.
*   **[DONE]** Firmware kompiliert erfolgreich und wurde auf Ziel-Hardware geflasht.
*   **[DONE]** **[FIXED]** Task Watchdog Timeout durch Umstellung auf nativen USB-JTAG-Treiber behoben. Die serielle Kommunikation ist jetzt stabil.
*   **[DONE]** Basis-Implementierung für SlowRF-Empfang (RX) mit Ausgabe im `F<HEX>`-Format.
*   **[DONE]** Basis-Implementierung für SlowRF-Senden (TX) von FS20-kompatiblen Frames über das `F`-Kommando.

## 4. Nächste Schritte

*   **Protokoll-Verfeinerung (RX):** Detaillierte Implementierung der Protokoll-Decoder im `slowrf_task`, inklusive Sync-Wort-Erkennung, Paritätsprüfung und Plausibilitäts-Checks für FS20.
*   **Protokoll-Erweiterung:** Unterstützung für weitere SlowRF-Protokolle hinzufügen (z.B. Intertechno V1/V3) mit entsprechenden Timings für Senden und Empfangen.
*   **Konfigurations-Management:** Speichern von culfw-Einstellungen (z.B. Reporting-Modus `X21`) im Non-Volatile Storage (NVS) des ESP32, um sie nach einem Neustart zu erhalten.

## 5. Hardware-Konfiguration (Pinout)

*   **SPI (für CC1101):**
    *   `MOSI`: GPIO 7
    *   `MISO`: GPIO 20
    *   `SCLK`: GPIO 6
    *   `CS`: GPIO 10
*   **CC1101 Signale:**
    *   `GDO0`: GPIO 2 (Input/Interrupt für RX, Output für TX)
*   **Frequenzerkennung:**
    *   `GPIO_433MARKER`: GPIO 4 (Input mit Pull-Up)