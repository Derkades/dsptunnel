#ifndef PARITY_H
#define PARITY_H

extern size_t add_parity(unsigned char *buf, size_t len);
extern size_t parity_check(unsigned char *buf, size_t len, size_t *fixed);

#endif