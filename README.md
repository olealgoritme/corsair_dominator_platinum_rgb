# Corsair Dominator Platinum

- Needed something to turn off (or set RGB color) of the LEDs on the RAM sticks...
- Scans the SMBus for the specific RAM sticks (DDR4 and DDR5) and sets the RGB color of the LEDs.

## Dependencies

```bash
sudo apt install libi2c-dev
sudo modprobe i2c-dev
```

On some AMD boards the SMBus is only accessible with the kernel parameter `acpi_enforce_resources=lax`.

## Usage

```bash
make
sudo ./corsair_dominator_platinum_rgb 0xFFFFFF   # white
sudo ./corsair_dominator_platinum_rgb 0x000000   # off
./corsair_dominator_platinum_rgb -n              # only detect, change nothing
sudo make install                                # installs to /usr/local/bin
```

Colors can be given as `0xRRGGBB`, `#RRGGBB` or `RRGGBB`.
Root is not needed if your user is in the `i2c` group.

| Option   | Description                                                  |
|----------|--------------------------------------------------------------|
| `-b BUS` | only use `/dev/i2c-BUS` (default: all SMBus adapters)        |
| `-a`     | scan all addresses 0x03-0x77 instead of only the known ones  |
| `-n`     | dry run: only detect devices                                 |
| `-v`     | verbose output                                               |

Exit codes: `0` success, `1` invalid usage, `2` no bus/devices found, `3` a device rejected the color.
