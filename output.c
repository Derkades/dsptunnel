/*
*    This file is part of dsptunnel.
*
*    dsptunnel is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    dsptunnel is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with dsptunnel.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <limits.h>

#include "dsptunnel.h"
#include "fletcher.h"

#include "output.h"

#define SILENCE_LENGTH 4

#define DATA_BUF_SIZE 2048
static unsigned char dataBuf[DATA_BUF_SIZE];

#define AUDIO_BUF_SIZE 1024
static short int audioBuf[AUDIO_BUF_SIZE];
static int audioBufPos = 0;

static int audio_out(int dsp_fd, short int value) {
	if (audioBufPos >= AUDIO_BUF_SIZE) {
		ssize_t written = write(dsp_fd, audioBuf, sizeof(short int) * AUDIO_BUF_SIZE);
		if (written != (sizeof(short int) * AUDIO_BUF_SIZE)) {
			perror("audio_out: length mismatch");
			return 0;
		}
		audioBufPos = 0;
	}

	audioBuf[audioBufPos++] = value;

	return 1;
}

void *output_loop(void *inopts) {
	struct threadopts opts = *(struct threadopts *) inopts;

	struct pollfd pollfd;
	pollfd.fd = opts.tundev;
	pollfd.events = POLLIN;

	while (!*(opts.done) ) {
		if (!poll(&pollfd, 1, 0 )) {
			// No network data available, write silence
			for (int i = 0; i < SILENCE_LENGTH*opts.bitlength; i++) {
				if (!audio_out(opts.dspdev, 0)) {
					return NULL;
				}
			}
			continue;
		}

		// Read from tun device into data buffer

		// Last 2 bits are used to store a checksum later
		ssize_t size = read(opts.tundev, dataBuf, DATA_BUF_SIZE - 2);
		if (size == -1) {
			perror("output_loop: read" );
			return NULL;
		}

		// Calculate checksum, in 2 bytes at end of buffer
		unsigned short int checksum = fletcher16(dataBuf, size);

		dataBuf[size++] = (checksum>>8)&0xff;
		dataBuf[size++] = checksum&0xff;

		fprintf(stderr, "< %li bytes, checksum: 0x%04hX\n", size, checksum );

		// For all bytes in the output buffer
		for (int i = 0; i < size; i++) {
			unsigned char data = dataBuf[i];

			// For all bits in this byte
			for (int j = 0; j < 8; j++) {
				// Output positive sample if bit is 1, negative if bit is 0
				short int sample = data >> j & 1 ? SHRT_MAX : SHRT_MIN;
				// Multiple times
				for (int k = 0; k < opts.bitlength; k++) {
					if (!audio_out(opts.dspdev, sample)) {
						return NULL;
					}
				}
			}
		}

		// Write silence so the receiving end can detect EOT
		for (int i = 0; i < SILENCE_LENGTH*opts.bitlength; i++) {
			if (!audio_out(opts.dspdev, 0)) {
				return NULL;
			}
		}
	}

	return NULL;
}

