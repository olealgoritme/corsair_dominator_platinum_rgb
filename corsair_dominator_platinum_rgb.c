/*
* Set the RGB color of Corsair Dominator Platinum RAM sticks over SMBus.
*
* Compile with:
* make
* sudo ./corsair_dominator_platinum_rgb 0xFF0000
*
* Exit codes:
*   0  color applied to every detected device (or detection succeeded with -n)
*   1  invalid usage
*   2  no usable SMBus bus, or no devices found
*   3  one or more devices failed to accept the color
*
* Author: Ole Algoritme, 2024
*/

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <i2c/smbus.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CORSAIR_DOMINATOR_PLATINUM_NAME "Corsair Dominator Platinum"
#define I2C_MIN_ADDR 0x03
#define I2C_MAX_ADDR 0x77
#define LED_COUNT 12
#define PACKET_SIZE (LED_COUNT * 3 + 2) // +1 for the initial 0xC, +1 for the CRC
#define BLOCK_MAX 32                   // SMBus block write limit
#define WRITE_ATTEMPTS 3
#define MAX_BUSES 64

// Addresses the RGB controllers live at: 0x18-0x1F on DDR5, 0x58-0x5F on DDR4
static const uint8_t known_addr_ranges[][2] = {
    {0x18, 0x1F},
    {0x58, 0x5F},
};

static int verbose = 0;

// Parse "0xRRGGBB", "#RRGGBB" or "RRGGBB" into RGB values
static int parse_color(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (hex[0] == '#') {
        hex += 1;
    } else if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
    }

    if (strlen(hex) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        if (!isxdigit((unsigned char)hex[i])) {
            return -1;
        }
    }

    unsigned long color = strtoul(hex, NULL, 16);
    *r = (color >> 16) & 0xFF;
    *g = (color >> 8) & 0xFF;
    *b = color & 0xFF;

    return 0;
}

// Function to compute CRC-8
static uint8_t crc8(uint8_t init, uint8_t poly, const uint8_t *data, size_t len) {
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

static void build_packet(uint8_t *packet, uint8_t red, uint8_t green, uint8_t blue) {
    packet[0] = 0xC;
    for (unsigned int led = 0; led < LED_COUNT; led++) {
        unsigned int offset = (led * 3) + 1;
        packet[offset] = red;
        packet[offset + 1] = green;
        packet[offset + 2] = blue;
    }
    packet[PACKET_SIZE - 1] = crc8(0x0, 0x7, packet, PACKET_SIZE - 1);
}

// Send the packet to the device currently selected on fd, retrying on bus errors.
// Returns 0 on success, negative errno on failure.
static int write_packet(int fd, const uint8_t *packet) {
    int res = 0;

    for (int attempt = 1; attempt <= WRITE_ATTEMPTS; attempt++) {
        res = i2c_smbus_write_block_data(fd, 0x31, BLOCK_MAX, packet);
        if (res >= 0) {
            usleep(800); // 800 microseconds delay
            res = i2c_smbus_write_block_data(fd, 0x32, PACKET_SIZE - BLOCK_MAX, packet + BLOCK_MAX);
            if (res >= 0) {
                usleep(200); // 200 microseconds delay
                return 0;
            }
        }
        if (verbose) {
            fprintf(stderr, "  write attempt %d failed: %s\n", attempt, strerror(-res));
        }
        usleep(attempt * 5000);
    }

    return res < 0 ? res : -EIO;
}

static int test_for_corsair_dominator_platinum_controller(int fd, uint8_t address) {
    // Fails with EBUSY when a kernel driver owns the address; that is never our device
    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        return 0;
    }

    int res = i2c_smbus_read_byte_data(fd, 0x43);
    if (!(res == 0x1A || res == 0x1B)) {
        return 0;
    }

    res = i2c_smbus_read_byte_data(fd, 0x44);
    if (res != 0x04) {
        return 0;
    }

    return 1;
}

static int is_candidate_address(uint8_t address, int full_scan) {
    if (full_scan) {
        return 1;
    }
    for (size_t i = 0; i < sizeof(known_addr_ranges) / sizeof(known_addr_ranges[0]); i++) {
        if (address >= known_addr_ranges[i][0] && address <= known_addr_ranges[i][1]) {
            return 1;
        }
    }
    return 0;
}

static int compare_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

// Collect the numbers of all SMBus adapters (the bus the RAM sits on), sorted
static int find_smbus_buses(int *buses, int max) {
    DIR *dir = opendir("/sys/class/i2c-dev");
    if (!dir) {
        return 0;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max) {
        int bus;
        if (sscanf(entry->d_name, "i2c-%d", &bus) != 1) {
            continue;
        }

        char path[512];
        char name[128] = "";
        snprintf(path, sizeof(path), "/sys/class/i2c-dev/%s/name", entry->d_name);
        FILE *f = fopen(path, "r");
        if (!f) {
            continue;
        }
        if (!fgets(name, sizeof(name), f)) {
            name[0] = '\0';
        }
        fclose(f);

        if (strncmp(name, "SMBus", 5) == 0) {
            buses[count++] = bus;
        }
    }
    closedir(dir);

    qsort(buses, count, sizeof(int), compare_int);
    return count;
}

// Scan one bus and apply the color. Returns -1 if the bus could not be used.
static int process_bus(int bus, int full_scan, int dry_run, const uint8_t *packet,
                       int *found, int *failed) {
    char dev[32];
    snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", dev, strerror(errno));
        if (errno == EACCES) {
            fprintf(stderr, "Run as root (sudo) or add your user to the 'i2c' group.\n");
        }
        return -1;
    }

    // Keep two instances of this tool from interleaving writes on the same bus
    if (flock(fd, LOCK_EX) < 0) {
        fprintf(stderr, "Failed to lock %s: %s\n", dev, strerror(errno));
        close(fd);
        return -1;
    }

    unsigned long funcs = 0;
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0 ||
        !(funcs & I2C_FUNC_SMBUS_READ_BYTE_DATA) ||
        !(funcs & I2C_FUNC_SMBUS_WRITE_BLOCK_DATA)) {
        fprintf(stderr, "%s does not support the required SMBus transfers, skipping\n", dev);
        close(fd);
        return -1;
    }

    if (verbose) {
        printf("Scanning %s\n", dev);
    }

    for (uint8_t address = I2C_MIN_ADDR; address <= I2C_MAX_ADDR; address++) {
        if (!is_candidate_address(address, full_scan) ||
            !test_for_corsair_dominator_platinum_controller(fd, address)) {
            continue;
        }

        (*found)++;
        printf("Found '%s' on %s at address 0x%02X\n", CORSAIR_DOMINATOR_PLATINUM_NAME, dev, address);
        if (dry_run) {
            continue;
        }

        int res = write_packet(fd, packet);
        if (res < 0) {
            (*failed)++;
            fprintf(stderr, "Failed to set color on %s at 0x%02X: %s\n", dev, address, strerror(-res));
        }
    }

    close(fd);
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options] COLOR\n"
            "\n"
            "COLOR is 0xRRGGBB, #RRGGBB or RRGGBB (0x000000 turns the LEDs off).\n"
            "\n"
            "Options:\n"
            "  -b BUS   only use /dev/i2c-BUS (default: all SMBus adapters)\n"
            "  -a       scan all addresses 0x%02X-0x%02X instead of only the known ones\n"
            "  -n       dry run: only detect devices, do not change colors\n"
            "  -v       verbose output\n"
            "  -h       show this help\n",
            prog, I2C_MIN_ADDR, I2C_MAX_ADDR);
}

int main(int argc, char *argv[]) {
    int bus_override = -1;
    int full_scan = 0;
    int dry_run = 0;
    int opt;

    while ((opt = getopt(argc, argv, "b:anvh")) != -1) {
        switch (opt) {
        case 'b': {
            char *end;
            errno = 0;
            long bus = strtol(optarg, &end, 10);
            if (errno || *end != '\0' || bus < 0 || bus > 1023) {
                fprintf(stderr, "Invalid bus number '%s'.\n", optarg);
                return 1;
            }
            bus_override = (int)bus;
            break;
        }
        case 'a':
            full_scan = 1;
            break;
        case 'n':
            dry_run = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (optind != argc - 1 && !(dry_run && optind == argc)) {
        usage(argv[0]);
        return 1;
    }

    uint8_t red = 0, green = 0, blue = 0;
    if (optind < argc && parse_color(argv[optind], &red, &green, &blue) != 0) {
        fprintf(stderr, "Invalid color '%s'. Use 0xRRGGBB.\n", argv[optind]);
        return 1;
    }

    uint8_t packet[PACKET_SIZE];
    build_packet(packet, red, green, blue);

    int buses[MAX_BUSES];
    int bus_count;
    if (bus_override >= 0) {
        buses[0] = bus_override;
        bus_count = 1;
    } else {
        bus_count = find_smbus_buses(buses, MAX_BUSES);
        if (bus_count == 0) {
            fprintf(stderr, "No SMBus adapters found. Is the i2c-dev module loaded? (sudo modprobe i2c-dev)\n"
                            "On AMD boards the kernel may also need acpi_enforce_resources=lax.\n");
            return 2;
        }
    }

    int found = 0;
    int failed = 0;
    int usable = 0;
    for (int i = 0; i < bus_count; i++) {
        if (process_bus(buses[i], full_scan, dry_run, packet, &found, &failed) == 0) {
            usable++;
        }
    }

    if (usable == 0) {
        fprintf(stderr, "No usable SMBus bus.\n");
        return 2;
    }

    if (found == 0) {
        fprintf(stderr, "No '%s' devices found.%s\n", CORSAIR_DOMINATOR_PLATINUM_NAME,
                full_scan ? "" : " Try -a to scan all addresses.");
        return 2;
    }

    if (dry_run) {
        printf("Dry run: found %d device(s), no colors changed.\n", found);
        return 0;
    }

    if (failed > 0) {
        fprintf(stderr, "Color set on %d of %d device(s).\n", found - failed, found);
        return 3;
    }

    printf("Color set to 0x%02X%02X%02X on %d device(s).\n", red, green, blue, found);
    return 0;
}
