*ZIEL*
Portierung und Testung von Funktionalität der SlowRF von culfw auf CUL32-C6 in modernem RTOS-Style

nutze Quellen von culfw oder SignalDunio und erzeuge eine Implementierung als RTOS Task für ESP32-C6. Das Kommandointerface via USB-JTAG/CDC soll culfw kompatibel sein.
Bedenke die spätere erweiterbarkeit auf andere culfw Protokolle.
Bedenke die spätere nutzung als Thread oder/und Matter Gateway für diese Protokolle. 

Wir wollen Matter entweder über Wifi ODER Thread - nicht gleichzeitig. Es soll je eine sdkconfig dafür existieren. Im Grundsatz später auch komplett eine Firmware ohne Matter, nur serielle culfw/Singaliduno emulation.

*PLATFORM*

nutze das native ESP-IDF Buildsystem insbesondere für offizielles Matter SDK und Thread
# source /root/.platformio/packages/framework-espidf/export.sh
# ./build_idf.sh

*AVOID RUNNIG POTENTIAL BLOCKING COMMANDS!!!*

BEFOLGE STRICT: dass Du keine blockierenden Kommandos senden darfst um unseren Dialog flüssig fortzusetzen, füge im Zweifel immer "timeout" voran zur Prüfung:
timeout 3s pio monitor
timeout 3s cat /dev/ttyACM0
startet auch DOCKER container NICHT blockierend! Nutze docker log!
timeout 10s docker exec ...

*ESP-TOOLS*

is in $HOME/.platformio/penv/bin

*HARDWARE SETUP*

CC1101 gekoppelt an ESP32-C6-NINI

Target: cul32-c6
GDO0 = GPIO2
GDO2 = GPIO3
SS = GPIO18
SCK = GPIO19
MISO = GPIO20
MOSI = GPIO21
LED = GPIO8 (low active)
Switch = GPIO9 (low active)
433MHz Marker == GPIO4 (Low == 433MHz)

all binaries go into binaries/ - maintain the manifest inside this directory

Du läufst auf einem Raspberry Pi 5 mit vollem root Zugriff

# ls -l /dev/serial/by-id/

usb-busware.de_CUL868-if00 - Referenz CUL 868MHz auf ATMEGA32u4 Basis mit culfw - CRLF Zeilenterminierung
usb-busware.de_CUL433-if00 - Referenz CUL 433MHz auf ATMEGA32u4 Basis mit culfw - CRLF Zeilenterminierung
usb-Espressif_USB_JTAG_serial_debug_unit_58:E6:C5:E6:90:B0-if00 - 433MHz Zielhardware ESP32-C6
usb-Espressif_USB_JTAG_serial_debug_unit_54:32:04:03:59:68-if00 - OpenThread RCP


*HINT*

ignoriere normalen Funktraffic während der Test wie: F666666xxx

*GITHUB*

ssh git@github.com:tostmann/culfw-ng.git

*WIFI*

SSID: "PalmBeach WiFi"
Password: "12345678"

* IP Sicherheit / Verschlüsselung *
bereite verschlüsselung vor. Implementiere aber derzeit alles noch UNVERSCHLÜSSELT
