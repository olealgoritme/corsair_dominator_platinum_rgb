# Corsair Dominator Platinum

- Needed something to turn off (or set RGB color) of the LEDs on the RAM sticks...
- Scans i2c bus for the specific RAM sticks and sets the RGB color of the LEDs.

## Dependencies

```bash
sudo apt install libi2c-dev
```

## Usage

```bash
gcc -o corsair_dominator_platinum_rgb corsair_dominator_platinum_rgb.c -li2c
sudo ./corsair_dominator_platinum_rgb 0xFFFFFF
sudo ./corsair_dominator_platinum_rgb 0x000000
```
