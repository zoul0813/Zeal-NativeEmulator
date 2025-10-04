#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "hw/extensions/hayes.h"

static const char *TAG = "HAYES";
static const char *VERSION = "0.0.0-alpha";


#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/* push an ASCII string into RX (modem -> guest), helper for result codes */
static void push_response(hayes_t *hayes, const char *s) {
    HAYES_LOGI(TAG, "push_response %s\n", s);
    while (*s) {
        fifo_push(&hayes->cmd_fifo, (uint8_t)*s++);
    }
    fifo_push(&hayes->cmd_fifo, (uint8_t) '\r'); // result codes usually end with CR (often CR LF; we use CR)
}

/* lowercase helper */
static void to_upper_ascii(char *s) {
    while (*s) {
        *s = (char) toupper(*s);
        s++;
    }
}

void hangup(hayes_t *hayes) {
    hayes->socket = -1;
    hayes->carrier = 0;
    hayes->bytes = 0;
    hayes->host[0] = '\0';
    hayes->port[0] = '\0';
}

int socket_close(hayes_t *hayes) {
    if(hayes->socket != -1) {
        int r = close(hayes->socket);
        hangup(hayes);
        return r;
    }
    return 1;
}

int socket_connect(hayes_t *hayes, const char *host, const char *port) {
    struct addrinfo hints, *res, *p;
    hayes->socket = -1;
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port, &hints, &res)) != 0) {
        HAYES_LOGW(TAG, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        hayes->socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (hayes->socket == -1) continue;

        if (connect(hayes->socket, p->ai_addr, p->ai_addrlen) == 0) break;

        close(hayes->socket);
        hayes->socket = -1;
    }

    freeaddrinfo(res);

    if (hayes->socket == -1) {
        HAYES_LOGW(TAG, "Failed to connect to %s:13\n", host);
        return -1;
    }

    fcntl(hayes->socket, F_SETFL, O_NONBLOCK);

    strncpy(hayes->host, host, sizeof(hayes->host));
    strncpy(hayes->port, port, sizeof(hayes->port));
    fifo_reset(&hayes->data_fifo);
    hayes->carrier = 1;

    return 0;
}

int socket_read(hayes_t *hayes) {
    if(hayes->socket < 0) return 1;

    // read the response line
    uint8_t buf[128];
    ssize_t n = recv(hayes->socket, buf, sizeof(buf), 0);
    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else {
            socket_close(hayes);
            HAYES_LOGE(TAG, "socket_read");
            return errno;
        }
    }

    hayes->bytes += n;
    for (ssize_t i = 0; i < n; i++) {
        fifo_push(&hayes->data_fifo, buf[i]);
    }
    return 0;
}

int socket_write(hayes_t *hayes, uint8_t value) {
    if(hayes->socket < 0) return 0;

    ssize_t n = send(hayes->socket, &value, 1, 0);
    if(n < 0) {
        socket_close(hayes);
        HAYES_LOGE(TAG, "socket_write");
        return -1;
    }
    return (int)n;
}

int socket_poll(hayes_t *hayes) {
    if(hayes->socket < 0) {
        return 0;
    }

    fd_set rfds;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

    FD_ZERO(&rfds);
    FD_SET(hayes->socket, &rfds);

    int ret = select(hayes->socket+1, &rfds, NULL, NULL, &tv);
    if(ret < 0) {
        hangup(hayes);
        HAYES_LOGE(TAG, "socket_poll");
        return -1;
    }

    if (ret == 0) {
        return 0;
    }

    if (ret > 0) {
        char tmp;
        ssize_t n = recv(hayes->socket, &tmp, 1, MSG_PEEK);
        if(n == 0) {
            hangup(hayes);
            HAYES_LOGI(TAG, "carrier hung up");
            return -3;
        } else if(n < 0) {
            if(errno != EWOULDBLOCK && errno != EAGAIN) {
                HAYES_LOGE(TAG, "socket_poll: connection lost");
                hangup(hayes);
                return -4;
            }
        }

        return 1;
    }

    return 0;
}

/* process an AT command line in cmd_buf (length = cmd_len) */
void process_at_command(hayes_t *hayes) {
    fifo_reset(&hayes->cmd_fifo);

    // ensure a zero-terminated upper-case copy
    char tmp[CMD_BUF_SIZE];
    size_t n = MIN(hayes->cmd_len, (CMD_BUF_SIZE - 1));
    memcpy(tmp, hayes->cmd_buf, n);
    tmp[n] = '\0';
    // trim trailing CR/LF/space
    while (n && (tmp[n-1] == '\r' || tmp[n-1] == '\n' || tmp[n-1] == ' ')) { tmp[--n] = '\0'; }
    // basic canonicalization
    to_upper_ascii(tmp);

    // If the command is just "AT" => OK
    if (strcmp(tmp, "AT") == 0) {
        if (hayes->echo) {
            push_response(hayes, "AT");
        }
        push_response(hayes, "OK");
        return;
    }

    // simple echo of typed AT cmds if echo enabled
    if (hayes->echo) {
        push_response(hayes, tmp);
    }

    // `ATO` — Return to Online Data Mode
    if (strcmp(tmp, "ATO") == 0) {
        // go to data mode if we had a previous connection
        if (hayes->carrier) {
            push_response(hayes, "CONNECT");
        } else {
            push_response(hayes, "ERROR");
        }
        return;
    }

    // `ATH` - Hang up (on-hook)
    // `ATH0` = Hang up
    // `AT&D2` - Drop DTR = hang up
    if (strcmp(tmp, "ATH") == 0 || strcmp(tmp, "ATH0") == 0 || strcmp(tmp, "AT&D2") == 0) {
        // hangup
        hayes->carrier = 0;
        socket_close(hayes);
        push_response(hayes, "OK");
        fifo_reset(&hayes->data_fifo);
        return;
    }

    // `ATE0` - Disable local echo
    if (strcmp(tmp, "ATE0") == 0) {
        hayes->echo = 0;
        push_response(hayes, "OK");
        return;
    }

    // `ATE1` - Enable local echo
    if (strcmp(tmp, "ATE1") == 0) {
        hayes->echo = 1;
        push_response(hayes, "OK");
        return;
    }

    // `ATD<number>` - Dial a number
    if (n >= 3 && strncmp(tmp, "ATD", 3) == 0) {
        if(hayes->carrier) {
            push_response(hayes, "BUSY");
            return;
        }

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

        int sock = socket_connect(hayes, host, port);
        if(sock < 0) {
            push_response(hayes, "ERROR");
            return;
        }

        push_response(hayes, "CONNECTING");
        return;
    }

    if(strcmp(tmp, "ATI") == 0) {
        push_response(hayes, "Zeal 8-bit ESP32 Hayes Modem");
        return;
    }

    if(strncmp(tmp, "AT+", 3) == 0) {
        char buf[1024];
        if(strlen(tmp) <= 3) {
            push_response(hayes, "ERROR");
            return;
        }
        const char *sub = &tmp[3];
        if(strcmp(sub, "V") == 0) {
            push_response(hayes, VERSION);
            return;
        }
        if(strcmp(sub, "CS") == 0) {
            sprintf(buf, "CARRIER: %s", hayes->carrier ? "YES" : "NO");
            push_response(hayes, buf);
            return;
        }
        if(strcmp(sub, "CLCC") == 0) {
            if(hayes->carrier) {
                sprintf(buf, "HOST: %s:%s", hayes->host, hayes->port);
            } else {
                sprintf(buf, "HOST: Disconnected");
            }
            push_response(hayes, buf);
            return;
        }
    }

    /**
     * Unsupported AT commands
     */
    // // `ATA` - Answer an incoming call
    // if (strcmp(tmp, "ATA") == 0) {
    //     // answer: connect (simulate immediate connect)
    //     hayes->carrier = 1;
    //     hayes->command_mode = 0;
    //     push_response(hayes, "CONNECT");
    //     return;
    // }

    // Unknown
    push_response(hayes, "ERROR");
}

void hayes_write_data(hayes_t *hayes, uint8_t addr, uint8_t value)
{
    switch (addr) {
        case HAYES_PORT_DATA:  // DATA
            HAYES_LOGI(TAG, "send %02x\n", value);
            socket_write(hayes, value);
            break;
        case HAYES_PORT_CMD:
            // In command mode, accumulate bytes until CR is seen
            if (value == '\r' || value == '\n') {
                if (hayes->cmd_len > 0) {
                    hayes->cmd_buf[hayes->cmd_len] = '\0';
                    // process command line
                    HAYES_LOGI(TAG, "AT Command: %s\n", hayes->cmd_buf);
                    process_at_command(hayes);
                    hayes->cmd_len = 0;
                } else {
                    // blank line -> OK per some modems
                    push_response(hayes, "OK");
                }
            } else {
                if (hayes->cmd_len < (CMD_BUF_SIZE - 1)) {
                    hayes->cmd_buf[hayes->cmd_len++] = (char)value;
                }
            }
            // echo if enabled
            if (hayes->echo) {
                fifo_push(&hayes->cmd_fifo, value);
            }
            break;
        case HAYES_PORT_CTRL:
            hayes->echo = value & ST_ECHO ? 1 : 0;
            if(hayes->carrier && !(value & ST_CARRIER)) {
                socket_close(hayes);
            }
            break;
        default:
            // ignore
            HAYES_LOGI(TAG, "io_write: Unmapped port %02x %02x\n", addr, value);
            break;
    }
}

int hayes_read_data(hayes_t *hayes, uint8_t addr)
{
    switch (addr) {
        case HAYES_PORT_DATA: { // DATA port
            /* b will not be modified if no data was available */
            uint8_t b = 0x00;
            fifo_pop(&hayes->data_fifo, &b);
            return b;
        }
        case HAYES_PORT_CMD: { // CMD port
            /* b will not be modified if no data was available */
            uint8_t b = 0x00;
            fifo_pop(&hayes->cmd_fifo, &b);
            return b;
        }
        case HAYES_PORT_CTRL: { // STATUS
            uint8_t s = 0;
            if (fifo_size(&hayes->data_fifo)) s |= ST_RX_AVAIL;
            s |= ST_TX_READY; // always ready
            if (hayes->carrier) s |= ST_CARRIER;
            if (hayes->ringing) s |= ST_RING;
            if (hayes->echo) s |= ST_ECHO;
            return s;
        }
        default:
            // unmapped ports read as 0xFF
            HAYES_LOGI(TAG, "io_read: Unmapped port %02x\n", addr);
            return 0xFF;
    }
}

int hayes_init(hayes_t *hayes) {
    hangup(hayes);

    hayes->echo = 1; // ATE1 default
    hayes->cmd_len = 0;

    if (!fifo_init(&hayes->data_fifo, RX_BUF_SIZE)) {
        HAYES_LOGW(TAG, "Could not allocate data fifo\n");
        return -1;
    }

    if (!fifo_init(&hayes->cmd_fifo, CMD_BUF_SIZE)) {
        HAYES_LOGW(TAG, "Could not allocate command fifo\n");
        return -1;
    }

    return 0;
}

int hayes_tick(hayes_t *hayes) {
    int ret = socket_poll(hayes);

    if(ret > 0 && !fifo_size(&hayes->data_fifo)) {
        socket_read(hayes);
    }

    return ret;
}
