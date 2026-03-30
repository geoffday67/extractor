Import("env")
import serial, time

def reset_to_bootloader(source, target, env):
    port = env.GetProjectOption("upload_port")
    try:
        # Opening at 1200 baud signals the native USB CDC firmware to reboot
        # into the ROM bootloader, then we wait for the port to re-enumerate.
        s = serial.Serial(port, 1200)
        s.close()
        time.sleep(2)
        print(f"Reset {port} to bootloader via 1200-baud touch")
    except Exception as e:
        print(f"reset_port.py: {e} (ignored)")

env.AddPreAction("upload", reset_to_bootloader)
