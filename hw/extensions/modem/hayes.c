#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "utils/log.h"
#include "hw/extensions/modem.h"
#include "hw/extensions/hayes.h"


/* command parsing */
static char cmd_buf[CMD_BUF_SIZE];
static size_t cmd_len = 0;

static int sockfd;

static uint8_t rx_buf[RX_BUF_SIZE];
static size_t rx_head = 0; // pop from head
static size_t rx_tail = 0; // push to tail

/* helpers for ring buffer */
static size_t rx_space(void) {
    if (rx_tail >= rx_head) return RX_BUF_SIZE - (rx_tail - rx_head) - 1;
    return rx_head - rx_tail - 1;
}
static size_t rx_pending(void) {
    if (rx_tail >= rx_head) return rx_tail - rx_head;
    return RX_BUF_SIZE - (rx_head - rx_tail);
}
static void rx_push(uint8_t b) {
    if (rx_space() == 0) return; // drop if full
    rx_buf[rx_tail++] = b;
    if (rx_tail >= RX_BUF_SIZE) rx_tail = 0;
}
static int rx_pop(uint8_t *out) {
    if (rx_pending() == 0) return 0;
    *out = rx_buf[rx_head++];
    if (rx_head >= RX_BUF_SIZE) rx_head = 0;
    return 1;
}

/* push an ASCII string into RX (modem -> guest), helper for result codes */
static void push_response(const char *s) {
    while (*s) {
        rx_push((uint8_t)*s++);
    }
    rx_push('\r'); // result codes usually end with CR (often CR LF; we use CR)
}

/* lowercase helper */
static void to_upper_ascii(char *s) {
    while (*s) { *s = (char)toupper((unsigned char)*s); s++; }
}


int socket_connect(const char *host, const char *port) {
    struct addrinfo hints, *res, *p;
    sockfd = -1;
    char buf[128];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port, &hints, &res)) != 0) {
        log_err_printf("getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) break;

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);

    if (sockfd == -1) {
        log_err_printf("Failed to connect to %s:13\n", host);
        return -1;
    }

    // read the response line
    ssize_t n;
    int skip = 0;
    while((n = read(sockfd, buf, sizeof(buf))) > 0) {
        for(ssize_t i = 0; i < n; i++) {
            unsigned char c = buf[i];

            if(skip) {
                skip = 0;
                continue;
            }

            if(c == 0xff) {
                skip = 1;
                continue;
            }

            rx_push(c);
        }
    }
    rx_push('\r');

    close(sockfd);
    return 0;
}


/* process an AT command line in cmd_buf (length = cmd_len), command_mode assumed */
void process_at_command(hayes_t *hayes) {
    // ensure a zero-terminated upper-case copy
    char tmp[CMD_BUF_SIZE];
    size_t n = cmd_len < (CMD_BUF_SIZE - 1) ? cmd_len : (CMD_BUF_SIZE - 1);
    memcpy(tmp, cmd_buf, n);
    tmp[n] = '\0';
    // trim trailing CR/LF/space
    while (n && (tmp[n-1] == '\r' || tmp[n-1] == '\n' || tmp[n-1] == ' ')) { tmp[--n] = '\0'; }
    // basic canonicalization
    to_upper_ascii(tmp);

    // If the command is just "AT" => OK
    if (strcmp(tmp, "AT") == 0) {
        if (hayes->echo) {
            push_response("AT");
        }
        push_response("OK");
        return;
    }

    // simple echo of typed AT cmds if echo enabled
    if (hayes->echo) { push_response(tmp); }

    // handle basic commands
    if (strcmp(tmp, "ATO") == 0) {
        // go to data mode if we had a previous connection
        if (hayes->carrier) {
            hayes->command_mode = 0;
            push_response("CONNECT");
        } else {
            push_response("ERROR");
        }
        return;
    }

    if (strcmp(tmp, "ATA") == 0) {
        // answer: connect (simulate immediate connect)
        hayes->carrier = 1;
        hayes->command_mode = 0;
        push_response("CONNECT");
        return;
    }

    if (strcmp(tmp, "ATH") == 0) {
        // hangup
        hayes->carrier = 0;
        hayes->command_mode = 1;
        push_response("OK");
        return;
    }

    // Echo control
    if (strcmp(tmp, "ATE0") == 0) { hayes->echo = 0; push_response("OK"); return; }
    if (strcmp(tmp, "ATE1") == 0) { hayes->echo = 1; push_response("OK"); return; }

    // Dial: ATD<number> -> simulate dialing then CONNECT or NO CARRIER
    if (n >= 3 && strncmp(tmp, "ATD", 3) == 0) {
        // // extract number (we won't really use it)
        const char *uri = tmp + 3;
        char buf[256];
        strncpy(buf, uri, sizeof(buf)-1);

        char *host;
        char *port;
        char *sep = strchr(buf, ':');
        if(sep) {
            *sep = '\0';
            host = buf;
            port = sep + 1;
        } else {
            host = buf;
            port = "23";
        }

        // simulate dialing delay by scheduling connect next tick
        // For simplicity: immediate connect
        hayes->carrier = 1;
        hayes->command_mode = 0;

        int sock = socket_connect(host, port);
        if(sock < 0) {
            push_response("ERROR");
            return;
        }
        push_response("CONNECT");

        return;
    }

    if(strcmp(tmp, "ATTIME") == 0) {
        push_response("Checking time...");
        socket_connect("time.nist.gov", "13");

        return;
    }

    // Unknown
    push_response("ERROR");
}

void hayes_write_data(hayes_t *hayes, uint8_t addr, uint8_t value)
{
    switch (addr) {
        case 0: { // DATA
            if (hayes->command_mode) {
                // In command mode, accumulate bytes until CR is seen
                if (value == '\r' || value == '\n') {
                    if (cmd_len > 0) {
                        // process command line
                        log_printf("[MODEM] io_write: process at command %02x %02x (%s)\n", addr, value, cmd_buf);
                        process_at_command(hayes);
                        cmd_len = 0;
                    } else {
                        // blank line -> OK per some modems
                        push_response("OK");
                    }
                } else {
                    if (cmd_len < (CMD_BUF_SIZE - 1)) {
                        cmd_buf[cmd_len++] = (char)value;
                    }
                }
                // echo if enabled
                if (hayes->echo) rx_push(value);
            } else {
                // DATA mode: this byte is sent out the "line"
                // if (modem_send_cb) modem_send_cb(value, modem_cb_user);
                log_printf("[MODEM] send %02x\n", value);
            }
            break;
        }
        case 1: {
            // STATUS is read-only
            log_printf("[MODEM] io_write: status is ready??? %02x %02x\n", addr, value);
            break;
        }
        case 2: {
            // CONTROL port (DTR/RTS)
            int new_dtr = (value & 0x01) ? 1 : 0;
            if (!new_dtr && hayes->carrier) {
                // dropping DTR typically causes hangup
                hayes->carrier = 0;
                hayes->command_mode = 1;
                push_response("NO CARRIER");
            }
            // store nothing else for now
            break;
        }
        default:
            // ignore
            log_printf("[MODEM] io_write: Unmapped port %02x %02x\n", addr, value);
            break;
    }
}

int hayes_read_data(hayes_t *hayes, uint8_t addr)
{
    switch (addr) {
        case 0: { // DATA port
            uint8_t b = 0x00;
            if (rx_pop(&b)) {
                return b;
            } else {
                // When nothing to read, return 0x00 or 0xFF per your emulator preference
                return 0x00;
            }
        }
        case 1: { // STATUS
            uint8_t s = 0;
            if (rx_pending()) s |= ST_RX_AVAIL;
            s |= ST_TX_READY; // always ready
            if (hayes->carrier) s |= ST_CARRIER;
            if (hayes->ringing) s |= ST_RING;
            if (hayes->command_mode) s |= ST_IN_CMDMODE;
            return s;
        }
        default:
            // unmapped ports read as 0xFF
            log_printf("[MODEM] io_read: Unmapped port %02x\n", addr);
            return 0xFF;
    }
}

int hayes_init(hayes_t *hayes) {
    hayes->carrier = 0;
    hayes->ringing = 0;
    hayes->command_mode = 1; // start in command mode (typical for modems)
    hayes->echo = 1; // ATE1 default

    rx_head = rx_tail = 0;

    return 0;
}
