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

#define EOB_SILENCE 1
#define EOT_SILENCE 3
#define THRESHOLD SHRT_MAX / 6

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

	short int currentByte = 0;
	short int bitPos = 7;
	short int silenceCount = 0;
	long int samplesTotal = 0;
	short int samplesCount = 0;
	short int eot = 1;

	while (!*(opts.done)) {
		short int sample;

		if (!audio_in(opts.dspdev, &sample)) {
			return NULL;
		}

		// currentlyReceivingAvg = (currentlyReceivingAvg + sample) / 2;

		if (sample > -THRESHOLD && sample < THRESHOLD) {
			// fprintf(stderr, "-");
			silenceCount++;
			// Prevent overflow
			if (silenceCount == SHRT_MAX) {
				silenceCount--;
			}
		} else {
			// fprintf(stderr, "+%i", silenceCount);
			// fprintf(stderr, "%i", sample > THRESHOLD ? 1 : 0);
			samplesTotal += sample;
			samplesCount += 1;
			if (eot) {
				eot = 0;
				silenceCount = 0;
				// fprintf(stderr, "START\n");
			} else if (silenceCount > 0) {
				silenceCount--;
			}
			// silenceCount /= 2;
		}

		// Short silence means next bit is coming
		// There may me multiple silent samples, but bit position should only be moved once
		if (silenceCount >= (opts.bitlength * 0.75) && samplesCount >= opts.bitlength * EOB_SILENCE) {
			silenceCount = 0;
			float sampleAvg = (float) samplesTotal / (float) samplesCount;
			// fprintf(stderr, ">%.1f", sampleAvg);
			if (sampleAvg > THRESHOLD) {
				currentByte |= 1 << bitPos;
				// fprintf(stderr, "-1<\n");
			} else if (sampleAvg < -THRESHOLD) {
				// fprintf(stderr, "-0<\n");
			} else {
				// fprintf(stderr, "-?<\n");
			}
			samplesTotal = 0;
			samplesCount = 0;

			bitPos--;
			if (bitPos < 0) {
				dataBuf[dataBufPos++] = currentByte;
				currentByte = 0;
				bitPos = 7;
				// fprintf(stderr, ">#%i\n", dataBuf[dataBufPos-1]);
			}
			continue;
		}

		// Long silence => EOT
		// If written at least one byte + checksum
		if (silenceCount > (opts.bitlength * EOT_SILENCE)) {
			silenceCount = 0;
			if (dataBufPos < 3) {
				continue;
			}
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
			bitPos = 7;
			samplesTotal = 0;
			samplesCount = 0;
			eot = 1;
		}

		if (dataBufPos >= DATA_BUF_SIZE - 2) {
			// fputs("received data past buffer size, reset", stderr);
			perror("prevent buffer overflow");
			return NULL;
		}
	}

	return NULL;
}

