# Serial Keyboard Bridge

This is the practical fallback for keyboards that do not advertise as usable BLE HID peripherals to the CYD.

## Firmware target

Build and flash:

```bash
.pio-venv/bin/pio run -e cheap-yellow-display-serial-keyboard-min -t upload
```

This target enables raw serial key input and disables the packet-based serial interface so the keyboard bridge owns the port.

## Host bridge

Install dependencies:

```bash
cd keyboard-server
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

Run the bridge:

```bash
python serial_keyboard.py --port /dev/ttyUSB0
```

If `--port` is omitted, the script will prompt you to choose one.

## Notes

- Left shift maps to Spectrum shift.
- Right shift maps to Spectrum symbol shift.
- Arrow keys are mapped to cursor-style Spectrum movement keys.
- `Esc` maps to key code `0` in the current bridge logic.