# CULFW-NG (Next Generation)

culfw-NG is a modern port of the classic `culfw` for the ESP32-C6 (CUL32-C6) using ESP-IDF / PlatformIO. It provides a compatible command interface for host systems like FHEM while leveraging the power of an RTOS-based environment.

## Supported Protocols

*   **FS20** (868.3 MHz) - Full RX/TX support.
*   **HMS** (868.3 MHz) - RX/TX support.
*   **S300TH** (868.3 MHz) - RX support.
*   **Intertechno V1** (433.92 MHz) - RX/TX support.
*   **Intertechno V3** (433.92 MHz) - RX/TX support.

## Hardware Support (CUL32-C6)

Designed for ESP32-C6 + CC1101 module.
*   **GDO0**: GPIO 2
*   **GDO2**: GPIO 3
*   **SPI**: MOSI (21), MISO (20), SCK (19), SS (18)
*   **Frequency Auto-Detection**: GPIO 4 (Low = 433 MHz, High = 868 MHz)
*   **Status LED**: GPIO 8 (low active)

## Development

Built using PlatformIO with the `espressif32` platform and `espidf` framework.

### Build & Upload

```bash
pio run -t upload
```

### Serial Interface

Communication via USB-JTAG/CDC at 115200 Baud. Compatible with `culfw` commands (`V`, `X`, `F`, `is`, etc.).
