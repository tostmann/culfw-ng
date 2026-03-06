# CULFW-NG Projekt-Gedächtnis

## 1. Projektziel

Entwicklung einer culfw-kompatiblen Firmware für ESP32-C6 basierte CUL-Sticks zur Emulation von SlowRF-Protokollen (wie FS20, Intertechno, etc.). Die Firmware soll die RF-Protokolle über ein CC1101-Modul senden und empfangen.

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über PlatformIO.
*   **Board:** `esp32-c6-devkitc-1`
*   **Kommunikation:** USB-JTAG/CDC für die serielle Schnittstelle (culfw-Kommandos).
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **Software-Architektur:** FreeRTOS-Task-basiert.
    *   `culfw_parser_task`: Verarbeitet eingehende serielle Befehle (z.B. `V`, `X`).
    *   `slowrf_task`: Verarbeitet die vom CC1101-Modul empfangenen Pulsfolgen.
*   **Frequenzerkennung:** Automatische Erkennung der Modulfrequenz (433/868 MHz) über einen GPIO-Pin (`GPIO_433MARKER`), der auf GND gezogen wird.
*   **Signal-Erfassung:** Ein GPIO-Pin (`GDO0_PIN`) wird als Interrupt-Quelle genutzt, um die Flanken der empfangenen Signale zu erfassen. Die Timestamps werden per Queue (`slowrf_queue`) an den `slowrf_task` übergeben.

## 3. Implementierungsstatus

*   **[DONE]** Projektinitialisierung mit PlatformIO.
*   **[DONE]** Basis-Implementierung des CC1101 SPI-Treibers (`cc1101.c`).
*   **[DONE]** Grundgerüst für den culfw-Kommando-Parser (`culfw_parser.c`) mit Erkennung von `V`, `X`, `C`.
*   **[DONE]** Grundgerüst für die SlowRF-Signalverarbeitung (`slowrf.c`) mit ISR und RTOS-Queue.
*   **[DONE]** Frequenz-Auto-Detektion implementiert.
*   **[DONE]** Firmware kompiliert erfolgreich und wurde auf Ziel-Hardware geflasht.

## 4. Aktuelle Probleme & Blocker

*   **[CRITICAL] Task Watchdog Timeout:** Die Firmware stürzt kurz nach dem Start ab und startet neu. Der Log deutet auf einen Watchdog-Timeout hin, der durch blockierende Operationen in den RTOS-Tasks (`culfw_parser_task` oder `slowrf_task`) verursacht wird.
    *   **Hypothese:** Die `getchar()`-Funktion im `culfw_parser_task` blockiert den Task und verhindert, dass der Watchdog zurückgesetzt wird. Die Kommunikation über die ESP32-C6 USB-JTAG/CDC-Schnittstelle muss nicht-blockierend oder mit Timeouts implementiert werden.
    *   **Maßnahme:** Die `while(1)`-Schleifen in den Tasks wurden mit `vTaskDelay(pdMS_TO_TICKS(10))` versehen, um dem Scheduler Zeit zu geben. Das Problem besteht weiterhin.

## 5. Hardware-Konfiguration (Pinout)

*   **SPI (für CC1101):**
    *   `MOSI`: GPIO 7
    *   `MISO`: GPIO 20
    *   `SCLK`: GPIO 6
    *   `CS`: GPIO 10
*   **CC1101 Signale:**
    *   `GDO0`: GPIO 2 (Input, Interrupt-Quelle)
*   **Frequenzerkennung:**
    *   `GPIO_433MARKER`: GPIO 4 (Input mit Pull-Up)