#pragma once

/* --- Configuration --- */
#define RX_BUF_SIZE 1024
#define CMD_BUF_SIZE 256
#define TX_BUF_SIZE 1024

/* --- Status flags --- */
#define ST_RX_AVAIL   0x01
#define ST_TX_READY   0x02
#define ST_CARRIER    0x04
#define ST_RING       0x08
#define ST_IN_CMDMODE 0x10


typedef struct {
    int carrier;
    int ringing;
    int command_mode;
    int echo;
} hayes_t;

int hayes_init(hayes_t *hayes);
void hayes_write_data(hayes_t *hayes, uint8_t addr, uint8_t value);
int hayes_read_data(hayes_t *hayes, uint8_t addr);
void process_at_command(hayes_t *hayes);
