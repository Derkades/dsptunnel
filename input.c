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
#include <limits.h>

#include "dsptunnel.h"
#include "fletcher.h"

#include "input.h"

#define THRESHOLD SHRT_MAX / 4

#define DATA_BUF_SIZE 2048
static unsigned char dataBuf[DATA_BUF_SIZE];
static int dataBufPos = 0;

#define AUDIO_BUF_SIZE 1024
static short int audioBuf[AUDIO_BUF_SIZE];
static int audioBufPos = AUDIO_BUF_SIZE;

static int audio_in(int dsp, short int *sample) {
	if (audioBufPos >= AUDIO_BUF_SIZE) {
		ssize_t read_bytes = read(dsp, audioBuf, sizeof(short int) * AUDIO_BUF_SIZE);
		if (read_bytes != sizeof(short int) * AUDIO_BUF_SIZE) {
			perror("audio_in: read");
			return 0;
		}
		audioBufPos = 0;
	}

	*sample = audioBuf[audioBufPos++];

	return 1;
}

void *input_loop(void *inopts) {
	struct threadopts opts = *(struct threadopts*) inopts;

	while (!*(opts.done)) {
		short int currentByte = 0;
		short int bitPos = 0;
		short int silenceCount = 0;

		short int sample;

		if (!audio_in(opts.dspdev, &sample)) {
			return NULL;
		}

		if (sample < -THRESHOLD) { // zero bit
			// Byte is already initialized as all zeroes
			// Don't need to change anything for a bit to be zero
		} else if (sample > THRESHOLD) { // one bit
			currentByte |= 1 << bitPos;
		} else { // silence
			silenceCount++;

			// Short silence means next bit is coming
			// There may me multiple silent samples, but bit position should only be moved once
			if (silenceCount == 1) {
				bitPos++;
				if (bitPos >= 8) {
					dataBuf[dataBufPos++] = currentByte;
					currentByte = 0;
					bitPos = 0;
				}
				continue;
			}

			// Long silence => EOT
			// If written at least one byte + checksum
			if (silenceCount > 2*opts.bitlength && dataBufPos >= 3) {
				unsigned short length = dataBufPos - 2;
				unsigned short expectedChecksum = fletcher16(dataBuf, length);
				unsigned short receivedChecksum = (dataBuf[length] << 8) | dataBuf[length + 1];

				fprintf(stderr, "> %i bytes, checksum: 0x%04hX (0x%04hX)\n", length, receivedChecksum, expectedChecksum);

				if (expectedChecksum != receivedChecksum) {
					fputs("input_loop: incorrect checksum\n", stderr);
				} else if (write(opts.tundev, dataBuf, length) != length) {
					perror("input_loop: write");
					return NULL;
				}
				dataBufPos = 0;
				currentByte = 0;
				bitPos = 0;
			}

			continue;
		}

		silenceCount = 0;

		if (dataBufPos >= DATA_BUF_SIZE - 2) {
			fputs("received data past buffer size, reset", stderr);
			dataBufPos = 0;
			currentByte = 0;
			bitPos = 0;
		}
	}

	return NULL;
}

