#include <stdlib.h>
#include <stdio.h>

#include "fletcher.h"

// Add parity and checksum to end of buffer
// Returns new length
size_t add_parity(unsigned char *buf, size_t len) {
    // Add two checksum bytes
    unsigned short int checksum = fletcher16(buf, len);
    buf[len++] = (checksum>>8) & 0xff;
    buf[len++] = checksum & 0xff;

    // Add parity byte, XOR of all previous bytes
    buf[len++] = 0;
    for (size_t i = 0; i < len-1; i++) {
        buf[len-1] ^= buf[i];
    }

    return len;
}

// len is length of data buffer including parity and checksum
// returns 1 if correct, otherwise 0
int parity_check(unsigned char *buf, size_t len, size_t *fixed) {
    // Parity byte is at [len-1], so checksum at [len-3] and [len-2]
    if (fletcher16(buf, len - 3) == ((buf[len-3] << 8) | buf[len-2])) {
        return 1;
    }

    for (size_t i = 0; i < len; i++) {
        // Save original value
        unsigned char original = buf[i];

        // Reconstruct byte using parity
        // XOR of all other bytes
        buf[i] = 0;
        for (size_t j = 0; j < len; j++) {
            if (i != j) {
                buf[i] ^= buf[j];
            }
        }

        // If checksum matches, this byte was corrupt and has been successfully repaired
        if (fletcher16(buf, len - 3) == ((buf[len-3] << 8) | buf[len-2])) {
            *fixed = i;
            return 1;
        }

        // Restore original value otherwise
        buf[i] = original;
    }

    return 0;
}