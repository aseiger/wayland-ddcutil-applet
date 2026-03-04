/*
 * lcdstats.h - Send brightness/volume updates to lcdstats daemon
 *
 * Maintains a persistent Unix domain socket connection to
 * /tmp/lcdstats.sock.  On every (re)connect the current
 * brightness and volume are sent automatically.
 */

#ifndef LCDSTATS_H
#define LCDSTATS_H

/*
 * Open the connection to lcdstats.  Sends current brightness
 * and volume on success.  Safe to call if already connected.
 * Returns 0 on success, -1 if the daemon is unavailable.
 */
int lcdstats_init(void);

/*
 * Close the persistent connection (called on shutdown).
 */
void lcdstats_close(void);

/*
 * Send a brightness update (0-100).
 * Reconnects automatically if the link is down.
 * Returns 0 on success, -1 on error.
 */
int lcdstats_send_brightness(int value);

/*
 * Send a volume update (0-100).
 * Reconnects automatically if the link is down.
 * Returns 0 on success, -1 on error.
 */
int lcdstats_send_volume(int value);

#endif /* LCDSTATS_H */
