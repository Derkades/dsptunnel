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

#define DEBUG
// #define DEBUG_VERBOSE

#define EOT_SILENCE 5
#define THRESHOLD 2000
#define CONSIDER_FIRST_SAMPLE

#define DATA_BUF_SIZE 2048
static unsigned char dataBuf[DATA_BUF_SIZE];
static int dataBufPos = 0;

#define AUDIO_BUF_SIZE 2048
static short int audioBuf[AUDIO_BUF_SIZE];
static int audioBufPos = AUDIO_BUF_SIZE;

struct threadopts opts;

short int sample = 0;
short unsigned int sampleCount = 0;
short unsigned int silenceCount = 0;
int sampleCumSum = 0;
short signed int bitPos = 7;
short unsigned int currentByte = 0;
// short unsigned int silenceExpected = 0;

static int audio_in() {
	if (audioBufPos >= AUDIO_BUF_SIZE) {
		ssize_t read_bytes = read(opts.dspdev, audioBuf, sizeof(short int) * AUDIO_BUF_SIZE);
		if (read_bytes != sizeof(short int) * AUDIO_BUF_SIZE) {
			perror("audio_in: read");
			return 0;
		}
		audioBufPos = 0;
	}

	sample = audioBuf[audioBufPos++];

	return 1;
}

void save_byte() {
	#ifdef DEBUG
	fprintf(stderr, "0x%x '%c'\n", currentByte, currentByte);
	#endif
	dataBuf[dataBufPos++] = currentByte;
	bitPos = 7;
	currentByte = 0;
	// silenceExpected = 1;
}

void save_bit() {
	int sampleMean = sampleCumSum / opts.bitlength;
	if (sampleMean > THRESHOLD) {
		currentByte |= 1 << bitPos;
	}
	#ifdef DEBUG_VERBOSE
	fprintf(stderr, " #%i", sampleCount);
	if (sampleMean > 0) {
		fprintf(stderr, " ~%i > 1\n", sampleMean);
	} else {
		fprintf(stderr, " ~%i > 0\n", sampleMean);
	}
	#else
	fprintf(stderr, " ");
	#endif
	sampleCumSum = 0;
	sampleCount = 0;
}

short int receive_silence() {
	silenceCount++;
	// Prevent overflow
	if (silenceCount == SHRT_MAX) {
		silenceCount--;
	}

	if (silenceCount > (opts.bitlength * EOT_SILENCE) && dataBufPos >= 3) {
		if (bitPos == 0) {
			// Last bit of final byte was missing
			save_bit();
			save_byte();
		}

		unsigned short length = dataBufPos - 2;
		unsigned short expectedChecksum = fletcher16(dataBuf, length);
		unsigned short receivedChecksum = (dataBuf[length] << 8) | dataBuf[length + 1];

		// fprintf(stderr, "> %i bytes, checksum: 0x%04hX (0x%04hX)\n", dataBufPos, receivedChecksum, expectedChecksum);

		if (expectedChecksum != receivedChecksum) {
			fprintf(stderr, "> %i bytes, incorrect checksum 0x%04hX / 0x%04hX", dataBufPos, receivedChecksum, expectedChecksum);
			// fputs("input_loop: incorrect checksum\n", stderr);
		} else if (write(opts.tundev, dataBuf, length) != length) {
			perror("input_loop: write");
			return 0;
		} else {
			fprintf(stderr, "> %i bytes, correct checksum 0x%04hX", dataBufPos, receivedChecksum);
		}
		dataBufPos = 0;
	}

	return 1;
}

void receive_bit() {
	if (silenceCount > opts.bitlength) {
		// Was silent for a while, this is the start of a new transmission
		// First sample is ignored
		#ifdef DEBUG
		fprintf(stderr, "\n|S%u|\n", silenceCount);
		#endif
		bitPos = 7;
		currentByte = 0;
		// sampleCount = 0;
		silenceCount = 0;
		// silenceExpected = 0;
		// sampleCumSum = 0;
		// sampleCumSum = sample;

		#ifdef CONSIDER_FIRST_SAMPLE
			#ifdef DEBUG
			fprintf(stderr, "%i", sample > THRESHOLD ? 1 : 0);
			#endif
			sampleCount = 1;
			sampleCumSum = sample;
		#else
			sampleCount = 0;
			sampleCumSum = 0;
		#endif

		if (dataBufPos > 0) {
			dataBufPos = 0;
			fprintf(stderr, "reset buffer, previous not correctly received\n");
		}
	// } else if (silenceExpected) {
	// 	#ifdef DEBUG
	// 	fprintf(stderr, " I%i", sample > THRESHOLD ? 1 : 0);
	// 	#endif
	} else {
		#ifdef DEBUG
		fprintf(stderr, "%i", sample > THRESHOLD ? 1 : 0);
		#endif
		sampleCumSum += sample;
		if (silenceCount > 0) {
			silenceCount--;
		}
		sampleCount++;
		if (sampleCount >= opts.bitlength) {
			save_bit();
			if (bitPos > 0) {
				bitPos--;
			} else {
				save_byte();
			}
		}
	}
}

void *input_loop(void *inopts) {
	opts = *(struct threadopts*) inopts;

	while (!*(opts.done)) {
		if (!audio_in(opts.dspdev, &sample)) {
			return NULL;
		}

		if (sample > -THRESHOLD && sample < THRESHOLD) {
			if (!receive_silence()) {
				return NULL;
			}
		} else {
			receive_bit();
		}
	}

	return NULL;
}

