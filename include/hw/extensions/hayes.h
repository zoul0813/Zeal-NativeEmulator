#pragma once

#ifdef ESP_PLATFORM
typedef esp_err_t hayes_err_t;
#else
typedef int hayes_err_t;
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_err.h"
#define HAYES_LOGI(t, f, ...) ESP_LOGI(t, f, ##__VA_ARGS__)
#define HAYES_LOGW(t, f, ...) ESP_LOGW(t, f, ##__VA_ARGS__)
#define HAYES_LOGE(t, f, ...) ESP_LOGE(t, f, ##__VA_ARGS__)
#define gai_strerror(rv)    esp_err_to_name(rv)
#else
#include "utils/log.h"
#define HAYES_LOGI(t, f, ...) log_printf("[%s] "f, t, ##__VA_ARGS__)
#define HAYES_LOGW(t, f, ...) log_err_printf("[%s] "f, t, ##__VA_ARGS__)
#define HAYES_LOGE(t, f, ...) log_perror("[%s] "f, t, ##__VA_ARGS__)
#endif

#include "utils/fifo.h"

#define HAYES_PORT_CTRL 0x0
#define HAYES_PORT_DATA 0x1
#define HAYES_PORT_CMD  0x2

/* --- Configuration --- */
#define RX_BUF_SIZE     1024
#define TX_BUF_SIZE     1024
#define CMD_BUF_SIZE    256

/* --- Status flags --- */
#define ST_RX_AVAIL   (1 << 0) // 0x01
#define ST_TX_READY   (1 << 1) // 0x02
#define ST_CARRIER    (1 << 2) // 0x04
#define ST_RING       (1 << 3) // 0x08
#define ST_ECHO       (1 << 4) // 0x20?

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

    fifo_t data_fifo;
    fifo_t cmd_fifo;
    char   cmd_buf[CMD_BUF_SIZE];
    size_t cmd_len;
} hayes_t;

int hayes_init(hayes_t *hayes);
int hayes_tick(hayes_t *hayes);
void hayes_write_data(hayes_t *hayes, uint8_t addr, uint8_t value);
int hayes_read_data(hayes_t *hayes, uint8_t addr);
void process_at_command(hayes_t *hayes);
