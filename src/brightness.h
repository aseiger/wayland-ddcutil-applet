/*
 * brightness.h - Display brightness control via DDC/CI (VCP 0x10)
 */

#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H

/*
 * Get the current brightness (0-100).
 * Returns -1 on error.
 */
int brightness_get(void);

/*
 * Get the maximum brightness value (always 100 for DDC).
 */
int brightness_get_max(void);

/*
 * Set the brightness to `value` (clamped to 0-100).
 * Returns 0 on success, -1 on error.
 */
int brightness_set(int value);

#endif /* BRIGHTNESS_H */
