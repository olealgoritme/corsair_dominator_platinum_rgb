/*
* Compile with:
* gcc -o corsair_dominator_platinum_rgb corsair_dominator_platinum_rgb.c -li2c
* sudo ./corsair_dominator_platinum_rgb 0xFF0000
*
* Author: Ole Algoritme, 2024
*/

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <i2c/smbus.h>

#define I2C_BUS "/dev/i2c-1"
#define CORSAIR_DOMINATOR_PLATINUM_NAME "Corsair Dominator Platinum"
#define I2C_MIN_ADDR 0x03
#define I2C_MAX_ADDR 0x77

// Function to convert hex string to RGB values
int hext_to_rgb(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (strlen(hex) != 8 || strncmp(hex, "0x", 2) != 0) {
        return -1;
    }

    unsigned int color;
    if (sscanf(hex + 2, "%06x", &color) != 1) {
        return -1;
    }

    *r = (color >> 16) & 0xFF;
    *g = (color >> 8) & 0xFF;
    *b = color & 0xFF;

    return 0;
}

// Function to compute CRC-8
uint8_t crc8(uint8_t init, uint8_t poly, uint8_t *data, size_t len) {
    uint8_t crc = init;

    for (size_t i = 0; i < len; i++) {
        uint8_t val = data[i];
        for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
            uint8_t bit = (crc & 0x80) ? (val & mask) ? 0 : poly
                                       : (val & mask) ? poly
                                                      : 0;
            crc = (crc << 1) ^ bit;
        }
    }

    return crc;
}

void set_colors(uint8_t *led_data, unsigned int leds_count, uint8_t red, uint8_t green, uint8_t blue) {
    for (unsigned int led = 0; led < leds_count; led++) {
        unsigned int offset = (led * 3) + 1;
        led_data[offset] = red;
        led_data[offset + 1] = green;
        led_data[offset + 2] = blue;
    }
}

void apply_colors(int file, uint8_t *led_data, size_t data_size) {
    uint8_t data[data_size];
    memcpy(data, led_data, data_size);

    uint8_t crc = crc8(0x0, 0x7, data, data_size - 1);
    data[data_size - 1] = crc;

    i2c_smbus_write_block_data(file, 0x31, 32, data);
    usleep(800); // 800 microseconds delay
    i2c_smbus_write_block_data(file, 0x32, data_size - 32, data + 32);
    usleep(200); // 200 microseconds delay
}

int test_for_corsair_dominator_platinum_controller(int file, uint8_t address) {
    if (ioctl(file, I2C_SLAVE, address) < 0) {
        return 0;
    }

    int res = i2c_smbus_read_byte_data(file, 0x43);
    if (!(res == 0x1A || res == 0x1B)) {
        return 0;
    }

    res = i2c_smbus_read_byte_data(file, 0x44);
    if (res != 0x04) {
        return 0;
    }

    return 1;
}

void detect_and_apply_colors(uint8_t red, uint8_t green, uint8_t blue) {
    int file;
    if ((file = open(I2C_BUS, O_RDWR)) < 0) {
        perror("Failed to open the I2C bus");
        return;
    }

    uint8_t led_data[12 * 3 + 2]; // 12 LEDs, 3 bytes per LED, +1 for the initial 0xC, +1 for the CRC
    led_data[0] = 0xC;

    // Set all colors
    set_colors(led_data, 12, red, green, blue);

    for (uint8_t address = I2C_MIN_ADDR; address <= I2C_MAX_ADDR; address++) {
        if (test_for_corsair_dominator_platinum_controller(file, address)) {
            printf("Found '%s' at address 0x%02X\n", CORSAIR_DOMINATOR_PLATINUM_NAME, address);
            apply_colors(file, led_data, sizeof(led_data));
        }
    }

    close(file);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s 0xRRGGBB\n", argv[0]);
        return 1;
    }

    uint8_t red, green, blue;
    if (hext_to_rgb(argv[1], &red, &green, &blue) != 0) {
        fprintf(stderr, "Invalid color format. Use 0xRRGGBB.\n");
        return 1;
    }

    detect_and_apply_colors(red, green, blue);

    printf("Color set to %s on all detected devices.\n", argv[1]);

    return 0;
}

