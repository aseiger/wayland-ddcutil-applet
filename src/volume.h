/*
 * volume.h - Display speaker volume control via DDC/CI (VCP 0x62)
 */

#ifndef VOLUME_H
#define VOLUME_H

/*
 * Get the current volume (0-100).
 * Returns -1 on error.
 */
int volume_get(void);

/*
 * Set the volume to `percent` (0-100).
 * Returns 0 on success, -1 on error.
 */
int volume_set(int percent);

#endif /* VOLUME_H */
