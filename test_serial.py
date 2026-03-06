import serial
import sys
import time

port = sys.argv[1]
try:
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.5)
    ser.write(b'V\n')
    time.sleep(0.5)
    while ser.in_waiting:
        print(ser.readline().decode('utf-8', errors='ignore').strip())
    ser.close()
except Exception as e:
    print(f"Error: {e}")
