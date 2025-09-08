#include "hw/device.h"
#include "hw/extensions/hayes.h"

#define MODEM_IO_SIZE         16

#define MODEM_IO_BASE       0x40
#define MODEM_IO_CTRL       0x00
#define MODEM_IO_READ       0x01
#define MODEM_IO_WRITE      0x02

typedef struct {
    device_t         parent;
    hayes_t          hayes;
} modem_t;

int modem_init(modem_t* modem);
// void modem_tick(void);
