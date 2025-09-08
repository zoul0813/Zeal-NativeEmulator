#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "hw/device.h"
#include "utils/log.h"
#include "utils/helpers.h"

#include "hw/extensions/modem.h"
#include "hw/extensions/hayes.h"


/* IO read/write functions */
uint8_t modem_io_read(device_t* dev, uint32_t addr) {
    if (dev == NULL) {
        log_err_printf("[MODEM] null device\n");
        return 0;
    }

    modem_t *modem = (modem_t*)dev;
    // log_printf("[MODEM] io_read: %02x\n", addr);
    return hayes_read_data(&modem->hayes, (uint8_t)addr);
}

void modem_io_write(device_t* dev, uint32_t addr, uint8_t value) {
    if (dev == NULL) {
        log_err_printf("[MODEM] null device\n");
        return;
    }
    // log_printf("[MODEM] io_write: %02x %02x\n", addr, value);
    modem_t *modem = (modem_t*)dev;
    hayes_write_data(&modem->hayes, (uint8_t)addr, value);
}


int modem_init(modem_t* dev) {
    if (dev == NULL) {
        return 1;
    }

    if(!hayes_init(&dev->hayes)) {
        log_err_printf("[MODEM] Failed to initialize Hayes modem\n");
    }

    device_init_io(DEVICE(dev),  "modem_dev", modem_io_read, modem_io_write, MODEM_IO_SIZE);

    log_printf("[MODEM] Initialized\n");
    return 0;
}
