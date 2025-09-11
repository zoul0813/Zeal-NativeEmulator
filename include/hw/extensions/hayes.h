#pragma once

#include "utils/fifo.h"

#define HAYES_PORT_DATA 0x0
#define HAYES_PORT_CTRL 0x1

/* --- Configuration --- */
#define RX_BUF_SIZE     1024
#define TX_BUF_SIZE     1024
#define CMD_BUF_SIZE    256

/* --- Status flags --- */
#define ST_RX_AVAIL   (1 << 0) // 0x01
#define ST_TX_READY   (1 << 1) // 0x02
#define ST_CARRIER    (1 << 2) // 0x04
#define ST_RING       (1 << 3) // 0x08
#define ST_CMDMODE    (1 << 4) // 0x10
#define ST_ECHO       (1 << 5) // 0x20?

#define SOCKET_STATE  -1


typedef struct {
    int carrier;
    int ringing;
    int command_mode;
    int echo;

    int socket;
    int bytes;
    char host[256];
    char port[16];

    fifo_t rx_fifo;
    char   cmd_buf[CMD_BUF_SIZE];
    size_t cmd_len;
} hayes_t;

int hayes_init(hayes_t *hayes);
int hayes_tick(hayes_t *hayes);
void hayes_write_data(hayes_t *hayes, uint8_t addr, uint8_t value);
int hayes_read_data(hayes_t *hayes, uint8_t addr);
void process_at_command(hayes_t *hayes);
