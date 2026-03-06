import serial
import time
import sys

def test_serial(port_name, baud=115200):
    try:
        ser = serial.Serial(port_name, baud, timeout=2)
        print(f"Testing {port_name}...")
        ser.write(b"V\r\n")
        time.sleep(0.5)
        response = ser.read(100)
        print(f"Response: {response.decode('ascii', errors='ignore').strip()}")
        ser.close()
    except Exception as e:
        print(f"Error on {port_name}: {e}")

test_serial("/dev/ttyACM1", 38400)
test_serial("/dev/ttyACM3", 115200)
