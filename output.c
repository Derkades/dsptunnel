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
#include <string.h>

#include "dsptunnel.h"
// #include "fletcher.h"
#include "parity.h"

#include "output.h"

// #define DEBUG_PRINT
#define DEBUG_BYTES 512
#define DEBUG_SLEEP 1

#define EOT_SILENCE 8

#define DATA_BUF_SIZE 2048
static unsigned char dataBuf[DATA_BUF_SIZE];

#define AUDIO_BUF_SIZE 4096
static short int audioBuf[AUDIO_BUF_SIZE];
static int audioBufPos = 0;

static short int write_sample(struct threadopts opts, short int sample) {
	#ifdef DEBUG_PRINT
	if (sample > 0) {
		fputs("1", stderr);
	} else if (sample < 0) {
		fputs("0", stderr);
	} else {
		fputs("-", stderr);
	}
	#endif

	if (audioBufPos >= AUDIO_BUF_SIZE) {
		ssize_t written = write(opts.dspdev, audioBuf, sizeof(short int) * AUDIO_BUF_SIZE);
		if (written != (sizeof(short int) * AUDIO_BUF_SIZE)) {
			perror("audio_out: length mismatch");
			return 0;
		}
		audioBufPos = 0;
	}

	audioBuf[audioBufPos++] = sample;

	return 1;
}

static short int write_sample_bl(struct threadopts opts, short int sample) {
	#ifdef DEBUG_PRINT
	fputs(" ", stderr);
	#endif
	for (int k = 0; k < opts.bitlength; k++) {
		if (!write_sample(opts, sample)) {
			return 0;
		}
	}
	return 1;
}

static short int write_sample_bl_repeat(struct threadopts opts, short int bit, size_t times) {
	for (size_t i = 0; i < times; i++) {
		if (!write_sample_bl(opts, bit)) {
			return 0;
		}
	}
	return 1;
}

static short int write_byte(struct threadopts opts, unsigned char byte) {
	for (int j = 7; j >= 0; j--) {
		// Output POS-NEG if bit is 0, output NEG-POS if bit is 1
		unsigned char bit = (byte >> j) & 1;
		if (!write_sample_bl(opts, bit ? SHRT_MIN : SHRT_MAX)) {
			return 0;
		}
		if (!write_sample_bl(opts, bit ? SHRT_MAX : SHRT_MIN)) {
			return 0;
		}
	}
	return 1;
}

void *output_loop(void *inopts) {
	struct threadopts opts = *(struct threadopts *) inopts;

	struct pollfd pollfd;
	pollfd.fd = opts.tundev;
	pollfd.events = POLLIN;

	while (!*(opts.done) ) {
		#ifdef DEBUG_BYTES
		sleep(DEBUG_SLEEP);
		ssize_t size = DEBUG_BYTES;
		dataBuf[0] = 'a';
		for (int i = 1; i < DEBUG_BYTES; i++) {
			dataBuf[i] = dataBuf[i - 1] + 1;
			if (dataBuf[i] > 'z') {
				dataBuf[i] = 'a';
			}
		}
		#else
		if (!poll(&pollfd, 1, 0 )) {
			// No network data available, write silence
			for (int i = 0; i < EOT_SILENCE*opts.bitlength; i++) {
				if (!write_sample_bl_repeat(opts, 0, EOT_SILENCE)) {
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
		#endif

		size = add_parity(dataBuf, size);

		unsigned short int checksum = (dataBuf[size-3] << 8) | dataBuf[size-2];
		fprintf(stderr, "< %li bytes, checksum: 0x%04hX\n", size, checksum);

		// For all bytes in the output buffer
		for (int i = 0; i < size; i++) {
			unsigned char byte = dataBuf[i];

			#ifdef DEBUG_PRINT
			fprintf(stderr, "\n%i '%c' - ", byte, byte);
			#endif

			if (!write_byte(opts, byte)) {
				return NULL;
			}
		}

		// Write long silence so the receiving end can detect EOT
		if (!write_sample_bl_repeat(opts, 0, EOT_SILENCE)) {
			return NULL;
		}

		#ifdef DEBUG_BYTES
		// Flush audio buffer
		while (audioBufPos != 1) {
			write_sample(opts, 0);
		}
		#endif
	}

	return NULL;
}

