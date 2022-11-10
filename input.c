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
#include "parity.h"

#include "input.h"

// #define DEBUG
// #define DEBUG_VERBOSE
#define PRINT_BUFFER

#define EOT_SILENCE 5
#define THRESHOLD 1500
#define CONSIDER_FIRST_SAMPLE

#define DATA_BUF_SIZE 2048
static unsigned char dataBuf[DATA_BUF_SIZE];
static int dataBufPos = 0;

#define AUDIO_BUF_SIZE 2048
static short int audioBuf[AUDIO_BUF_SIZE];
static int audioBufPos = AUDIO_BUF_SIZE;

struct threadopts opts;

short signed int sample = 0;
short unsigned int silenceCount = 0;
char secondPartSamples = 0;
signed int previousCumSum = 0;
signed int sampleCumSum = 0;
signed char bitPos = 7;
unsigned char currentByte = 0;

double levelSum = 0;
short unsigned int levelCount = 0;

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
}

void save_bit() {
	// NEG-POS signal means 1 bit
	if (previousCumSum < 0 && sampleCumSum > 0) {
		currentByte |= 1 << bitPos;
	}

	#ifdef DEBUG_VERBOSE
	if (previousCumSum < 0 && sampleCumSum > 0) {
		fprintf(stderr, " NP-1\n");
	} else if (previousCumSum > 0 && sampleCumSum < 0) {
		fprintf(stderr, " PN-0\n");
	} else {
		fprintf(stderr, " ????\n");
	}
	#else
		#ifdef DEBUG
		fprintf(stderr, " ");
		#endif
	#endif
}

short int receive_silence() {
	silenceCount++;
	// Prevent overflow
	if (silenceCount == SHRT_MAX) {
		silenceCount--;
	}

	if (silenceCount > (opts.bitlength * EOT_SILENCE)) {
		if (dataBufPos > 4) {
			if (bitPos == 0) {
				// Last bit of final byte was missing
				save_bit();
				save_byte();
			}

			float levelMean = levelSum / levelCount;
			levelSum = 0;
			levelCount = 0;

			size_t fixed = (size_t) -1;

			if (parity_check(dataBuf, dataBufPos, &fixed)) {
				if (fixed == (size_t) -1) {
					fprintf(stderr, "> %i bytes, valid, level %.0f\n", dataBufPos, levelMean);
				} else {
					fprintf(stderr, "> %i bytes, parity repair %lu, level %.0f\n", dataBufPos, fixed, levelMean);
				}
				#ifdef PRINT_BUFFER
				dataBuf[dataBufPos - 3] = '\0';
				fputs(dataBuf, stderr);
				#else
				if (write(opts.tundev, dataBuf, dataBufPos - 3) != dataBufPos - 3) {
					perror("input_loop: write");
					return 0;
				}
				#endif
			} else {
				unsigned short int checksum = fletcher16(dataBuf, dataBufPos - 3);
				fprintf(stderr, "> %i bytes, invalid checksum 0x%04hX, level %0.f\n", dataBufPos, checksum, levelMean);
			}
		}
		dataBufPos = 0;
	}

	return 1;
}

void receive_bit() {
	levelSum += abs(sample);
	levelCount++;

	if (silenceCount > opts.bitlength) {
		// Was silent for a while, this is the start of a new transmission
		// First sample is ignored
		#ifdef DEBUG
		fprintf(stderr, "\n|S%u|\n", silenceCount);
		#endif
		bitPos = 7;
		currentByte = 0;
		silenceCount = 0;

		#ifdef CONSIDER_FIRST_SAMPLE
			#ifdef DEBUG
			fprintf(stderr, "%i", sample > THRESHOLD ? 1 : 0);
			#endif
			sampleCumSum = sample;
		#else
			sampleCount = 0;
			sampleCumSum = 0;
		#endif

		if (dataBufPos > 0) {
			dataBufPos = 0;
			fprintf(stderr, "reset previous failed transmission\n");
		}
	} else {
		#ifdef DEBUG
		fprintf(stderr, "%i", sample > THRESHOLD ? 1 : 0);
		#endif
		if (secondPartSamples > 0) {
			sampleCumSum += sample;
			secondPartSamples++;
			// In second part. Count number of samples before finalizing bit and moving to the next
			if (secondPartSamples >= opts.bitlength) {
				save_bit();
				if (bitPos > 0) {
					bitPos--;
				} else {
					save_byte();
				}
				// Transition to first part state
				secondPartSamples = 0;
				sampleCumSum = 0;
			}
		} else {
			// In first part. Detect second part by polarity switch
			if ((sample > 0 && sampleCumSum < 0) || (sample < 0 && sampleCumSum > 0)) {
				secondPartSamples = 1; // Transition to second part state
				previousCumSum = sampleCumSum;
				sampleCumSum = sample;
			} else {
				sampleCumSum += sample;
			}
		}

		if (silenceCount > 0) {
			silenceCount--;
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

