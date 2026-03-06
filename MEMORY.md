# CULFW-NG Projekt-Gedächtnis

## 1. Projektziel

Entwicklung einer culfw-kompatiblen Firmware für ESP32-C6 basierte CUL-Sticks zur Emulation von SlowRF-Protokollen (wie FS20, Intertechno, etc.). Die Firmware soll die RF-Protokolle über ein CC1101-Modul senden und empfangen.

## 2. Architektur & Design-Entscheidungen

*   **Plattform:** ESP32-C6 mit ESP-IDF Framework, verwaltet über PlatformIO.
*   **Board:** `esp32-c6-devkitc-1`
*   **Kommunikation:** Nativer USB-JTAG/CDC Treiber (`usb_serial_jtag`) für eine nicht-blockierende serielle Schnittstelle.
*   **RF-Modul:** CC1101 angebunden via SPI.
*   **SPI-Kommunikation:** Die SPI-Geschwindigkeit wurde zur Erhöhung der Stabilität auf 500 kHz festgelegt. Für das Auslesen der Statusregister (z.B. `PARTNUM`, `VERSION`) wird der `READ_BURST`-Modus (`0xC0`) verwendet, da der Chip Adressen unter `0x30` sonst als Command-Strobes interpretiert.
*   **Persistenz:** Wichtige Einstellungen (z.B. der Reporting-Modus `X21`) werden im **Non-Volatile Storage (NVS)** des ESP32 gespeichert und bei Neustart automatisch wiederhergestellt.
*   **Software-Architektur:** FreeRTOS-Task-basiert.
    *   `culfw_parser_task`: Verarbeitet eingehende serielle Befehle. Unterscheidet nun zwischen strukturierten Kommandos (z.B. `F12345678`) und Raw-Befehlen. Erkennt Protokollvarianten anhand der Datenlänge (z.B. `is` für Intertechno V1 vs. V3).
    *   `slowrf_task`: Implementiert parallele Zustandsmaschinen/Decoder, um aus den vom CC1101 empfangenen Pulsfolgen gültige Datenpakete zu dekodieren.
        *   **FS20-Decoder:** Dedizierte Sync-Bit-Erkennung -> 9-Bit-Akkumulation (8 Daten + 1 Parität) -> Prüfung auf **gerade Parität (Even Parity)**. Der Decoder ist robust gegen Timing-Schwankungen und kann "missed edges" (zwei kurze Pulse als ein langer Puls) verarbeiten.
        *   **Intertechno V1 Decoder:** Verwendet ein 4-Puls-Schiebefenster, um das charakteristische IT-Timing-Muster (1xT, 3xT) zu erkennen und Trits zu dekodieren. Die Timing-Toleranzen wurden erweitert, um auch Sender mit leichten Abweichungen zu erfassen.
        *   **Intertechno V3 Decoder:** Erkennt das lange Sync-Signal (~9.3ms) und dekodiert die nachfolgenden 32 PWM-kodierten Bits.
        *   **HMS/S300TH Sensor-Decoder:** Eine generische PWM-Decoder-Logik verarbeitet Sensor-Protokolle. Sie unterscheidet '0'- und '1'-Bits anhand der Länge des High-Pulses bei konstanter Low-Puls-Länge und akkumuliert die Daten in Nibbles.
*   **Frequenzerkennung:** Automatische Erkennung der Modulfrequenz (433/868 MHz) über einen GPIO-Pin (`GPIO_433MARKER`) mit internem Pull-Up.
*   **Signal-Erfassung (RX):**
    *   Der CC1101 wird im **asynchronen seriellen Modus** (`PKTCTRL0 = 0x32`) betrieben, um ein rohes, demoduliertes ASK/OOK-Signal am `GDO0_PIN` bereitzustellen. Dieser Pin wird als Interrupt-Quelle genutzt, um die Timestamps der Signalflanken an den `slowrf_task` zu übergeben.
    *   **Rausch-Unterdrückung:** `GDO2` ist als **Carrier Sense** konfiguriert (`IOCFG2=0x0E`). Der GPIO-Interrupt wird nur verarbeitet, wenn der Carrier-Sense-Pin aktiv ist, wodurch Rauschen bei inaktivem Kanal effektiv gefiltert wird.
    *   **Cross-Band-Unterdrückung:** Die `slowrf_task` prüft vor der Ausgabe eines dekodierten Pakets das per Hardware-Pin (`GPIO_433MARKER`) erkannte Frequenzband. 868MHz-Sticks geben nur FS20, HMS und S300TH aus, 433MHz-Sticks nur Intertechno.
*   **Signal-Aussendung (TX):**
    *   Der `GDO0_PIN` wird dynamisch als Output konfiguriert, um Sendesequenzen per Bit-Banging mit präzisen Microsekunden-Delays (`ets_delay_us`) zu erzeugen.
    *   Zur Vermeidung von Echos (Selbst-Empfang) wird der `GDO0`-Interrupt während des Sendevorgangs temporär deaktiviert.
    *   Zur Sicherstellung der Sende-Bereitschaft wird vor dem Senden der **MARCSTATE** (`0x35`) geprüft und der `STX`-Strobe bei Bedarf wiederholt, bis der Chip im `TX`-State (`0x13`) ist.
    *   Für eine korrekte ASK-Modulation und maximale Reichweite wird die `PATABLE` mit `{0x00, 0xC0, ...}` (+10 dBm) initialisiert und das `FREND0`-Register auf `0x11` gesetzt.
    *   **FS20-Paketstruktur:** Die Präambel wurde auf 24 '0'-Bits verlängert und Pakete werden zur Erhöhung der Übertragungssicherheit **10-fach** wiederholt.
*   **RF-Konfiguration:** Die Frequenzregister des CC1101 wurden präzise auf die Standard-Mittenfrequenzen kalibriert (868.30 MHz für FS20/HMS/S300TH, 433.92 MHz für Intertechno).
*   **Versionierung:** Automatisierte Build-Nummer und detaillierter, culfw-kompatibler Versions-String (`V`-Kommando), um die Identifikation durch Host-Systeme (z.B. FHEM) sicherzustellen.
*   **Diagnose & Feedback:**
    *   Erweitertes `C`-Kommando zur Diagnose, das Part-, Versionsnummer und den **MARCSTATE** (`0x35`) ausliest.
    *   Ein `X99`-Kommando gibt die rohen Puls-Timings (in µs) der empfangenen Signale aus.
    *   **Remote-Registerzugriff:** `Rxx`-Kommando zum Lesen und `WxxYY`-Kommando zum Schreiben von CC1101-Registern zur Laufzeit.
    *   **RSSI-Reporting:** An alle empfangenen Datenpakete wird der RSSI-Wert (Signalstärke) als Hex-Wert angehängt (z.B. `F12345678C1`).
    *   Diagnose-Befehle `TX1`/`TX0` zum manuellen Schalten des Sendeträgers für Frequenzmessungen.
    *   Eine LED (`GPIO_8`) signalisiert aktive Sendevorgänge.
    *   Ein periodischer "CUL-TICK" wird über die serielle Schnittstelle gesendet, um die Verbindung zum Host zu signalisieren.
    *   `H<HEX>`-Kommando zum Senden von HMS-Protokolldaten.

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
*   **[DONE]** Erweiterter SlowRF-Empfang (RX) mit Zustandsmaschine, der erfolgreich FS20- und Intertechno-V1-Pakete dekodiert.
*   **[DONE]** Implementierung des Sende-Befehls (`is...`) für Intertechno V1.
*   **[DONE]** Erhöhung der Robustheit des FS20-Decoders gegen Timing-Schwankungen und "missed edges".
*   **[DONE]** TX-Echo durch Deaktivieren der RX-Interrupts während des Sendens behoben.
*   **[DONE]** Test-Funktion (`Tr`) zum Senden von zufälligen, validen FS20-Paketen implementiert.
*   **[DONE]** Build-System um eine automatische Build-Nummer und einen culfw-kompatiblen Versions-String erweitert.
*   **[DONE]** RX-Debugging-Modus (`X99`) implementiert, der rohe Pulsdauern ausgibt und umschaltbar ist.
*   **[DONE]** FS20-Sendefrequenz präzise auf 868.3 MHz kalibriert und Präambel verlängert.
*   **[DONE]** Implementierung und Validierung einer Rauschunterdrückung via Hardware Carrier Sense (GDO2).
*   **[DONE]** Hinzufügen von visuellem Feedback (LED) für Sendevorgänge.
*   **[DONE]** Protokoll-Erweiterung: Sende-Encoder für Intertechno V3 implementiert.
*   **[DONE]** Konfigurations-Management: Speichern von culfw-Einstellungen (z.B. `X21`-Modus) im Non-Volatile Storage (NVS).
*   **[DONE]** Code-Refactoring: Protokollspezifische Sende-Funktionen (`cc1101_send_fs20`, etc.) ausgelagert.
*   **[DONE]** Stabilisierung des TX-Modus durch Warten auf korrekten `MARCSTATE`.
*   **[DONE]** Protokoll-Erweiterung: Implementierung des Empfangs-Decoders für Intertechno V3.
*   **[DONE]** RSSI-Reporting: Empfangene Datenpakete werden um den RSSI-Wert ergänzt.
*   **[DONE]** Remote-Diagnose: Implementierung von `R`/`W`-Befehlen zum Lesen/Schreiben von CC1101-Registern.
*   **[DONE]** Host-System-Interface: Periodischer "CUL-TICK" als Heartbeat implementiert.
*   **[DONE]** Implementierung einer frequenz-selektiven Protokoll-Dekodierung zur Unterdrückung von Cross-Band-Empfang.
*   **[DONE]** Protokoll-Erweiterung: Empfangs-Decoder für **HMS** und **S300TH** (868MHz) implementiert.
*   **[DONE]** Protokoll-Erweiterung: Sende-Encoder für HMS (`H...`-Kommando) implementiert.
*   **[DONE]** End-to-End Test: HMS und S300TH Empfang gegen Referenz-CUL validiert.
*   **[DONE]** End-to-End Test: Intertechno V1 (433MHz) und FS20 (868MHz) RX/TX sind gegen einen Referenz-CUL validiert, inklusive strikter Kanaltrennung.

## 4. Neue Erkenntnisse / Probleme

*   **[INFO]** Die culfw-Implementierung des FS20-Protokolls verwendet **gerade Parität (Even Parity)**, abweichend von manchen Spezifikationen. Dies ist für die Kompatibilität entscheidend.
*   **[INFO]** Eine längere Präambel (z.B. 24 '0'-Bits statt 12) und eine hohe Wiederholrate (z.B. 10x) verbessern die FS20-Übertragungssicherheit signifikant.
*   **[INFO]** Hardwareseitiges Carrier Sense (RSSI-Schwellwert) über den GDO2-Pin ist eine sehr effektive Methode, um den RX-Prozessor von der Verarbeitung von reinem Rauschen zu entlasten.
*   **[INFO]** Das Speichern von Konfigurationen im NVS ist essentiell für eine nahtlose Integration in Host-Systeme wie FHEM, da diese erwarten, dass der CUL seinen Zustand nach einem Neustart beibehält.
*   **[INFO]** Die Remote-Register-Befehle (`R`/`W`) sind ein mächtiges Werkzeug zur Feinabstimmung der RF-Parameter (z.B. AGC-Verhalten, Frequenz-Offsets), ohne dass eine Neukompilierung erforderlich ist.
*   **[INFO]** Eine softwareseitige Prüfung des Frequenzbandes vor dem Melden eines dekodierten Pakets ist eine effektive und notwendige Methode, um Cross-Band-Störungen zu eliminieren, selbst wenn der RF-Chip physikalisch Signale außerhalb seines Zielbandes empfängt.
*   **[INFO]** Die Frequenzregister (FREQ2, FREQ1, FREQ0) des CC1101 müssen präzise auf die Ziel-Mittenfrequenz (z.B. 433.92 MHz, 868.30 MHz) kalibriert werden, um die Empfängerempfindlichkeit zu maximieren und die Kanaltrennung zu verbessern.
*   **[INFO]** Aktueller Protokoll-Umfang: FS20, Intertechno V1, Intertechno V3, HMS, S300TH. Wichtige Sensor-Protokolle wie Oregon Scientific (433MHz) sind noch nicht implementiert.

## 5. Nächste Schritte

*   **Protokoll-Erweiterung:** Implementierung des Dekoders für **Oregon Scientific** (V2/V3) für das 433MHz-Band.
*   **FHEM-Integration:** Validierung der Firmware mit einem Host-System (FHEM) zur Sicherstellung der Kompatibilität und Langzeitstabilität.
*   **Dokumentation:** Erstellen einer kurzen Anleitung für die neuen Diagnose-Befehle (`R`, `W`, `X99`, `TX1`/`TX0`, `H`).
*   **Langzeittests:** Überwachung der Stabilität und des Speicherverbrauchs über mehrere Tage.

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