#include <unistd.h>
// #include <stdlib.h>
#include <string.h>

#define BYTES 4

unsigned char checksum(unsigned char *in) {
    unsigned char sum = 0;
    for (size_t i = 0; i < BYTES + 1; i++) {
        sum += in[i];
    }
    return sum;
}

unsigned char parity(unsigned char *in, size_t skip) {
    unsigned char parity = 0;
    for (size_t i = 0; i < BYTES + 1; i++) {
        if (i != skip) {
            parity ^= in[i];
        }
    }
    return parity;
}

void parity_encode(unsigned char *in, size_t in_size, unsigned char *out, size_t *out_size) {
    // TODO set out size
    for (size_t in_base = 0; in_base < in_size; in_base += BYTES) {
        size_t out_base = (in_base / 2) * 3;
        memcpy(out + out_base, in + in_base, BYTES);
        out[out_base + BYTES] = parity(in + in_base, BYTES);
        out[out_base + BYTES + 1] = checksum(in + in_base);
    }
}

unsigned short int parity_decode(unsigned char *in, size_t in_size, unsigned char* out, size_t *out_size) {
    for (size_t in_base = 0; in_base < in_size; in_base += BYTES + 2) {
        size_t out_base = in_base / 3 * 2;
        if (checksum(in + in_base) == out[out_base + BYTES + 1]) {
            goto copy;
        } else {
            // Corruption, restore each bit from parity until checksum matches
            for (size_t skip = 0; skip < BYTES; skip++) {
                unsigned char orig = in[in_base + skip];
                in[in_base + skip] = parity(in_base, skip);
                if (checksum(in + in_base) == out[out_base + BYTES + 1]) {
                    goto copy;
                }
            }
            return 0;
        }
        copy:
        memcpy(out + out_base, in + in_base, BYTES);
    }
    return 1;
}

void main() {
    const char* str = "testtesttest";

    char in[1024];
    char out[1024];

    size_t in_size = strlen(str);
    size_t out_size;
    memcpy(in, str, in_size);
    parity_encode(in, in_size, out, &out_size);
}