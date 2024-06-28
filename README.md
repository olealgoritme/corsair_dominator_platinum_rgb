# Corsair Dominator Platinum

- Needed something to turn off the LEDs on the RAM sticks...

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
