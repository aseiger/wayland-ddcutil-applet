/*
 * lcdstats.c - Send brightness/volume updates to lcdstats daemon
 *
 * Maintains a persistent Unix domain socket connection to
 * /tmp/lcdstats.sock and sends newline-delimited JSON messages:
 *
 *   {"type": "brightness", "value": 75}\n
 *   {"type": "volume",     "value": 50}\n
 *
 * On every (re)connect, the current brightness and volume are
 * read from the hardware and sent so the LCD display is in sync.
 */

#include "lcdstats.h"
#include "brightness.h"
#include "volume.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define LCDSTATS_SOCK_PATH "/tmp/lcdstats.sock"

/* Persistent socket fd (-1 = not connected) */
static int sock_fd = -1;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static void drop_connection(void)
{
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
}

/*
 * Low-level send on the persistent socket.
 * Returns 0 on success, -1 on error (caller should drop & retry).
 */
static int raw_send(const char *type, int value)
{
    if (sock_fd < 0)
        return -1;

    char buf[128];
    int len = snprintf(buf, sizeof(buf),
                       "{\"type\": \"%s\", \"value\": %d}\n", type, value);

    /* MSG_NOSIGNAL avoids SIGPIPE if the peer closed */
    ssize_t n = send(sock_fd, buf, (size_t)len, MSG_NOSIGNAL);
    if (n != len)
        return -1;

    return 0;
}

/*
 * Send current brightness and volume (called right after connect).
 */
static void send_current_values(void)
{
    int br = brightness_get();
    if (br >= 0)
        raw_send("brightness", br);

    int vol = volume_get();
    if (vol >= 0)
        raw_send("volume", vol);
}

/*
 * Open the socket and connect.  On success, sends current values.
 * Returns 0 on success, -1 on failure.
 */
static int do_connect(void)
{
    drop_connection();

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("lcdstats: socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, LCDSTATS_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        /* Daemon not running — silently ignore */
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    /*
     * Server sends a status JSON on connect — we do not need it,
     * but it will sit harmlessly in the kernel recv buffer.
     */

    send_current_values();
    return 0;
}

/*
 * Ensure the socket is connected.  Returns 0 if ready, -1 if not.
 */
static int ensure_connected(void)
{
    if (sock_fd >= 0)
        return 0;
    return do_connect();
}

/*
 * Send a typed message, reconnecting once on failure.
 */
static int lcdstats_send(const char *type, int value)
{
    if (ensure_connected() < 0)
        return -1;

    if (raw_send(type, value) == 0)
        return 0;

    /* Write failed — connection stale.  Reconnect and retry. */
    if (do_connect() < 0)
        return -1;

    return raw_send(type, value);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int lcdstats_init(void)
{
    return do_connect();
}

void lcdstats_close(void)
{
    drop_connection();
}

int lcdstats_send_brightness(int value)
{
    return lcdstats_send("brightness", value);
}

int lcdstats_send_volume(int value)
{
    return lcdstats_send("volume", value);
}
