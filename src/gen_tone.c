//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2014, 2015, 2016, 2019, 2023  John Langner, WB2OSZ
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

/*------------------------------------------------------------------
 *
 * Module:      gen_tone.c
 *
 * Purpose:     Convert bits to AFSK for writing to .WAV sound file
 *		or a sound device.
 *
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "audio.h"
#include "gen_tone.h"

#include "fsk_demod_state.h" /* for MAX_FILTER_SIZE which might be overly generous for here. */
/* but safe if we use same size as for receive. */
#include "dsp.h"

// Properties of the digitized sound stream & modem.

static struct audio_s *save_audio_config_p = NULL;

/*
 * 8 bit samples are unsigned bytes in range of 0 .. 255.
 *
 * 16 bit samples are signed short in range of -32768 .. +32767.
 */

/* Constants after initialization. */

#define TICKS_PER_CYCLE (256.0 * 256.0 * 256.0 * 256.0)

static int ticks_per_sample[MAX_CHANS]; /* Same for both channels of same soundcard */
										/* because they have same sample rate */
										/* but less confusing to have for each channel. */

static int ticks_per_bit[MAX_CHANS];
static int f1_change_per_sample[MAX_CHANS];
static int f2_change_per_sample[MAX_CHANS];
static float samples_per_symbol[MAX_CHANS];

static short sine_table[256];

/* Accumulators. */

static unsigned int tone_phase[MAX_CHANS]; // Phase accumulator for tone generation.
										   // Upper bits are used as index into sine table.

#define PHASE_SHIFT_180 (128u << 24)
#define PHASE_SHIFT_90 (64u << 24)
#define PHASE_SHIFT_45 (32u << 24)

static int bit_len_acc[MAX_CHANS]; // To accumulate fractional samples per bit.

static int lfsr[MAX_CHANS]; // Shift register for scrambler.

static int prev_dat[MAX_CHANS]; // Previous data bit.  Used for G3RUH style.

/*------------------------------------------------------------------
 *
 * Name:        gen_tone_init
 *
 * Purpose:     Initialize for AFSK tone generation which might
 *		be used for RTTY or amateur packet radio.
 *
 * Inputs:      audio_config_p		- Pointer to modem parameter structure, modem_s.
 *
 *				The fields we care about are:
 *
 *					samples_per_sec
 *					baud
 *					mark_freq
 *					space_freq
 *					samples_per_sec
 *
 *		amp		- Signal amplitude on scale of 0 .. 100.
 *
 *				  100% uses the full 16 bit sample range of +-32k.
 *
 *		gen_packets	- True if being called from "gen_packets" utility
 *				  rather than the "direwolf" application.
 *
 * Returns:     0 for success.
 *              -1 for failure.
 *
 * Description:	 Calculate various constants for use by the direct digital synthesis
 * 		audio tone generation.
 *
 *----------------------------------------------------------------*/

static int amp16bit; /* for 9600 baud */

int gen_tone_init(struct audio_s *audio_config_p, int amp)
{
	int j;
	int chan = 0;

#if DEBUG

	printf("gen_tone_init ( audio_config_p=%p, amp=%d )\n",
		   audio_config_p, amp);
#endif

	/*
	 * Save away modem parameters for later use.
	 */

	save_audio_config_p = audio_config_p;

	amp16bit = (int)((32767 * amp) / 100);

	for (chan = 0; chan < MAX_CHANS; chan++)
	{

		if (audio_config_p->chan_medium[chan] == MEDIUM_RADIO)
		{

			int a = ACHAN2ADEV(chan);

#if DEBUG

			printf("gen_tone_init: chan=%d, modem_type=%d, bps=%d, samples_per_sec=%d\n",
				   chan,
				   save_audio_config_p->achan[chan].modem_type,
				   audio_config_p->achan[chan].baud,
				   audio_config_p->adev[a].samples_per_sec);
#endif

			tone_phase[chan] = 0;
			bit_len_acc[chan] = 0;
			lfsr[chan] = 0;
			ticks_per_sample[chan] = (int)((TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec) + 0.5);
			ticks_per_bit[chan] = (int)((TICKS_PER_CYCLE / (double)audio_config_p->achan[chan].baud) + 0.5);
			samples_per_symbol[chan] = (float)audio_config_p->adev[a].samples_per_sec / (float)audio_config_p->achan[chan].baud;
			f1_change_per_sample[chan] = (int)(((double)audio_config_p->achan[chan].mark_freq * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec) + 0.5);
			f2_change_per_sample[chan] = (int)(((double)audio_config_p->achan[chan].space_freq * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec) + 0.5);
		}
	}

	for (j = 0; j < 256; j++)
	{
		double a;
		int s;

		a = ((double)(j) / 256.0) * (2 * M_PI);
		s = (int)(sin(a) * 32767 * amp / 100.0);

		/* 16 bit sound sample must fit in range of -32768 .. +32767. */

		if (s < -32768)
		{

			printf("gen_tone_init: Excessive amplitude is being clipped.\n");
			s = -32768;
		}
		else if (s > 32767)
		{

			printf("gen_tone_init: Excessive amplitude is being clipped.\n");
			s = 32767;
		}
		sine_table[j] = s;
	}

	return (0);

} /* end gen_tone_init */

/*-------------------------------------------------------------------
 *
 * Name:        tone_gen_put_bit
 *
 * Purpose:     Generate tone of proper duration for one data bit.
 *
 * Inputs:      chan	- Audio channel, 0 = first.
 *
 *		dat	- 0 for f1, 1 for f2.
 *
 * 			  	-1 inserts half bit to test data
 *				recovery PLL.
 *
 * Assumption:  fp is open to a file for write.
 *
 * Version 1.4:	Attempt to implement 2400 and 4800 bps PSK modes.
 *
 * Version 1.6: For G3RUH, rather than generating square wave and low
 *		pass filtering, generate the waveform directly.
 *		This avoids overshoot, ringing, and adding more jitter.
 *		Alternating bits come out has sine wave of baud/2 Hz.
 *
 * Version 1.6:	MFJ-2400 compatibility for V.26.
 *
 *--------------------------------------------------------------------*/

// Interpolate between two values.
// My original approximation simply jumped between phases, producing a discontinuity,
// and increasing bandwidth.
// According to multiple sources, we should transition more gently.
// Below see see a rough approximation of:
//  * A step function, immediately going to new value.
//  * Linear interpoation.
//  * Raised cosine.  Square root of cosine is also mentioned.
//
//	new	      -		    /		   --
//		      |		   /		  /
//		      |		  /		  |
//		      |		 /		  /
//	old	-------		/		--
//		step		linear		raised cosine
//
// Inputs are the old (previous value), new value, and a blending control
// 0 -> take old value
// 1 -> take new value.
// inbetween some sort of weighted average.

static inline float interpol8(float oldv, float newv, float bc)
{
	// Step function.
	// return (newv);				// 78 on 11/7

	assert(bc >= 0);
	assert(bc <= 1.1);

	if (bc < 0)
		return (oldv);
	if (bc > 1)
		return (newv);

	// Linear interpolation, just for comparison.
	// return (bc * newv + (1.0f - bc) * oldv);	// 39 on 11/7

	float rc = 0.5f * (cosf(bc * M_PI - M_PI) + 1.0f);
	float rrc = bc >= 0.5f
					? 0.5f * (sqrtf(fabsf(cosf(bc * M_PI - M_PI))) + 1.0f)
					: 0.5f * (-sqrtf(fabsf(cosf(bc * M_PI - M_PI))) + 1.0f);

	(void)rrc;
	return (rc * newv + (1.0f - bc) * oldv); // 49 on 11/7
											 // return (rrc * newv + (1.0f - bc) * oldv);	// 55 on 11/7
}

// #define PSKIQ 1  // not ready for prime time yet.
#if PSKIQ
static int xmit_octant[MAX_CHANS];		// absolute phase in 45 degree units.
static int xmit_prev_octant[MAX_CHANS]; // from previous symbol.

// For PSK, we generate the final signal by combining fixed frequency cosine and
// sine by the following weights.
static const float ci[8] = {1, .7071, 0, -.7071, -1, -.7071, 0, .7071};
static const float sq[8] = {0, .7071, 1, .7071, 0, -.7071, -1, -.7071};
#endif

void tone_gen_put_bit(int chan, int dat)
{
	int a = ACHAN2ADEV(chan); /* device for channel. */

	assert(save_audio_config_p != NULL);

	if (save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO)
	{

		printf("Invalid channel %d for tone generation.\n", chan);
		return;
	}

	if (dat < 0)
	{
		/* Hack to test receive PLL recovery. */
		bit_len_acc[chan] -= ticks_per_bit[chan];
		dat = 0;
	}

#if PSKIQ
	int blend = 1;
#endif
	do
	{ /* until enough audio samples for this symbol. */

		int sam;

		switch (save_audio_config_p->achan[chan].modem_type)
		{

		case MODEM_AFSK:

#if DEBUG

			printf("tone_gen_put_bit %d AFSK\n", __LINE__);
#endif

			// v1.7 reversed.
			// Previously a data '1' selected the second (usually higher) tone.
			// It never really mattered before because we were using NRZI.
			// With the addition of IL2P, we need to be more careful.
			// A data '1' should be the mark tone.

			tone_phase[chan] += dat ? f1_change_per_sample[chan] : f2_change_per_sample[chan];
			sam = sine_table[(tone_phase[chan] >> 24) & 0xff];
			gen_tone_put_sample(chan, a, sam);
			break;

		default:

			printf("INTERNAL ERROR: %s %d achan[%d].modem_type = %d\n",
				   __FILE__, __LINE__, chan, save_audio_config_p->achan[chan].modem_type);
			exit(EXIT_FAILURE);
		}

		/* Enough for the bit time? */

		bit_len_acc[chan] += ticks_per_sample[chan];

	} while (bit_len_acc[chan] < ticks_per_bit[chan]);

	bit_len_acc[chan] -= ticks_per_bit[chan];

	prev_dat[chan] = dat; // Only needed for G3RUH baseband/scrambled.

} /* end tone_gen_put_bit */

void gen_tone_put_sample(int chan, int a, int sam)
{

	/* Ship out an audio sample. */
	/* 16 bit is signed, little endian, range -32768 .. +32767 */
	/* 8 bit is unsigned, range 0 .. 255 */

	assert(save_audio_config_p != NULL);

	assert(save_audio_config_p->adev[a].num_channels == 1 || save_audio_config_p->adev[a].num_channels == 2);

	assert(save_audio_config_p->adev[a].bits_per_sample == 16 || save_audio_config_p->adev[a].bits_per_sample == 8);

	// Bad news if we are clipping and distorting the signal.
	// We are using the full range.
	// Too late to change now because everyone would need to recalibrate their
	// transmit audio level.

	if (sam < -32767)
	{

		printf("Warning: Audio sample %d clipped to -32767.\n", sam);
		sam = -32767;
	}
	else if (sam > 32767)
	{

		printf("Warning: Audio sample %d clipped to +32767.\n", sam);
		sam = 32767;
	}

	if (save_audio_config_p->adev[a].num_channels == 1)
	{

		/* Mono */

		if (save_audio_config_p->adev[a].bits_per_sample == 8)
		{
			audio_put(a, ((sam + 32768) >> 8) & 0xff);
		}
		else
		{
			audio_put(a, sam & 0xff);
			audio_put(a, (sam >> 8) & 0xff);
		}
	}
	else
	{

		if (chan == ADEVFIRSTCHAN(a))
		{

			/* Stereo, left channel. */

			if (save_audio_config_p->adev[a].bits_per_sample == 8)
			{
				audio_put(a, ((sam + 32768) >> 8) & 0xff);
				audio_put(a, 0);
			}
			else
			{
				audio_put(a, sam & 0xff);
				audio_put(a, (sam >> 8) & 0xff);

				audio_put(a, 0);
				audio_put(a, 0);
			}
		}
		else
		{

			/* Stereo, right channel. */

			if (save_audio_config_p->adev[a].bits_per_sample == 8)
			{
				audio_put(a, 0);
				audio_put(a, ((sam + 32768) >> 8) & 0xff);
			}
			else
			{
				audio_put(a, 0);
				audio_put(a, 0);

				audio_put(a, sam & 0xff);
				audio_put(a, (sam >> 8) & 0xff);
			}
		}
	}
}

void gen_tone_put_quiet_ms(int chan, int time_ms)
{

	int a = ACHAN2ADEV(chan); /* device for channel. */
	int sam = 0;

	int nsamples = (int)((time_ms * (float)save_audio_config_p->adev[a].samples_per_sec / 1000.) + 0.5);

	for (int j = 0; j < nsamples; j++)
	{
		gen_tone_put_sample(chan, a, sam);
	};

	// Avoid abrupt change when it starts up again.
	tone_phase[chan] = 0;
}

/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Quick test program for above.
 *
 * Description: Compile like this for unit test:
 *
 *		gcc -Wall -DMAIN -o gen_tone_test gen_tone.c audio.c textcolor.c
 *
 *		gcc -Wall -DMAIN -o gen_tone_test.exe gen_tone.c audio_win.c textcolor.c -lwinmm
 *
 *--------------------------------------------------------------------*/

#if MAIN

int main()
{
	int n;
	int chan1 = 0;
	int chan2 = 1;
	int r;
	struct audio_s my_audio_config;

	/* to sound card */
	/* one channel.  2 times:  one second of each tone. */

	memset(&my_audio_config, 0, sizeof(my_audio_config));
	strlcpy(my_audio_config.adev[0].adevice_in, DEFAULT_ADEVICE, sizeof(my_audio_config.adev[0].adevice_in));
	strlcpy(my_audio_config.adev[0].adevice_out, DEFAULT_ADEVICE, sizeof(my_audio_config.adev[0].adevice_out));

	audio_open(&my_audio_config);
	gen_tone_init(&my_audio_config, 100);

	for (r = 0; r < 2; r++)
	{

		for (n = 0; n < my_audio_config.baud[0] * 2; n++)
		{
			tone_gen_put_bit(chan1, 1);
		}

		for (n = 0; n < my_audio_config.baud[0] * 2; n++)
		{
			tone_gen_put_bit(chan1, 0);
		}
	}

	audio_close();

	/* Now try stereo. */

	memset(&my_audio_config, 0, sizeof(my_audio_config));
	strlcpy(my_audio_config.adev[0].adevice_in, DEFAULT_ADEVICE, sizeof(my_audio_config.adev[0].adevice_in));
	strlcpy(my_audio_config.adev[0].adevice_out, DEFAULT_ADEVICE, , sizeof(my_audio_config.adev[0].adevice_out));
	my_audio_config.adev[0].num_channels = 2;

	audio_open(&my_audio_config);
	gen_tone_init(&my_audio_config, 100);

	for (r = 0; r < 4; r++)
	{

		for (n = 0; n < my_audio_config.baud[0] * 2; n++)
		{
			tone_gen_put_bit(chan1, 1);
		}

		for (n = 0; n < my_audio_config.baud[0] * 2; n++)
		{
			tone_gen_put_bit(chan1, 0);
		}

		for (n = 0; n < my_audio_config.baud[0] * 2; n++)
		{
			tone_gen_put_bit(chan2, 1);
		}

		for (n = 0; n < my_audio_config.baud[0] * 2; n++)
		{
			tone_gen_put_bit(chan2, 0);
		}
	}

	audio_close();

	return (0);
}

#endif

/* end gen_tone.c */
