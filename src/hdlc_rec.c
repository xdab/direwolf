//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015  John Langner, WB2OSZ
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

/********************************************************************************
 *
 * File:	hdlc_rec.c
 *
 * Purpose:	Extract HDLC frames from a stream of bits.
 *
 *******************************************************************************/

#include "direwolf.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h> // uint64_t

// #include "tune.h"
#include "demod.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "fcs_calc.h"
#include "ax25_pad.h"
#include "rrbb.h"
#include "multi_modem.h"
#include "ptt.h"
#include "fx25.h"

// #define TEST 1				/* Define for unit testing. */

// #define DEBUG3 1				/* monitor the data detect signal. */

/*
 * Minimum & maximum sizes of an AX.25 frame including the 2 octet FCS.
 */

#define MIN_FRAME_LEN ((AX25_MIN_PACKET_LEN) + 2)

#define MAX_FRAME_LEN ((AX25_MAX_PACKET_LEN) + 2)

/*
 * This is the current state of the HDLC decoder.
 *
 * It is possible to run multiple decoders concurrently by
 * having a separate set of state variables for each.
 *
 * Should have a reset function instead of initializations here.
 */

struct hdlc_state_s
{

	int prev_raw; /* Keep track of previous bit so */
	/* we can look for transitions. */
	/* Should be only 0 or 1. */

	int lfsr; /* Descrambler shift register for 9600 baud. */

	int prev_descram; /* Previous descrambled for 9600 baud. */

	unsigned char pat_det; /* 8 bit pattern detector shift register. */
	/* See below for more details. */

	unsigned int flag4_det; /* Last 32 raw bits to look for 4 */
	/* flag patterns in a row. */

	unsigned char oacc; /* Accumulator for building up an octet. */

	int olen; /* Number of bits in oacc. */
	/* When this reaches 8, oacc is copied */
	/* to the frame buffer and olen is zeroed. */
	/* The value of -1 is a special case meaning */
	/* bits should not be accumulated. */

	unsigned char frame_buf[MAX_FRAME_LEN];
	/* One frame is kept here. */

	int frame_len; /* Number of octets in frame_buf. */
	/* Should be in range of 0 .. MAX_FRAME_LEN. */

	rrbb_t rrbb; /* Handle for bit array for raw received bits. */

	uint64_t eas_acc; /* Accumulate most recent 64 bits received for EAS. */

	int eas_gathering; /* Decoding in progress. */

	int eas_plus_found; /* "+" seen, indicating end of geographical area list. */

	int eas_fields_after_plus; /* Number of "-" characters after the "+". */
};

static struct hdlc_state_s hdlc_state[MAX_CHANS][MAX_SUBCHANS][MAX_SLICERS];

static int num_subchan[MAX_CHANS]; // TODO1.2 use ptr rather than copy.

static int composite_dcd[MAX_CHANS][MAX_SUBCHANS + 1];

/***********************************************************************************
 *
 * Name:	hdlc_rec_init
 *
 * Purpose:	Call once at the beginning to initialize.
 *
 * Inputs:	None.
 *
 ***********************************************************************************/

static int was_init = 0;

static struct audio_s *g_audio_p;

void hdlc_rec_init(struct audio_s *pa)
{
	int ch, sub, slice;
	struct hdlc_state_s *H;

	//
	// printf ("hdlc_rec_init (%p) \n", pa);

	assert(pa != NULL);
	g_audio_p = pa;

	memset(composite_dcd, 0, sizeof(composite_dcd));

	for (ch = 0; ch < MAX_CHANS; ch++)
	{

		if (pa->chan_medium[ch] == MEDIUM_RADIO)
		{

			num_subchan[ch] = pa->achan[ch].num_subchan;

			assert(num_subchan[ch] >= 1 && num_subchan[ch] <= MAX_SUBCHANS);

			for (sub = 0; sub < num_subchan[ch]; sub++)
			{
				for (slice = 0; slice < MAX_SLICERS; slice++)
				{

					H = &hdlc_state[ch][sub][slice];

					H->olen = -1;

					// TODO: FIX13 wasteful if not needed.
					// Should loop on number of slicers, not max.

					H->rrbb = rrbb_new(ch, sub, slice, 0, H->lfsr, H->prev_descram);
				}
			}
		}
	}
	hdlc_rec2_init(pa);
	was_init = 1;
}

/* Own copy of random number generator so we can get */
/* same predictable results on different operating systems. */
/* TODO: Consolidate multiple copies somewhere. */

#define MY_RAND_MAX 0x7fffffff
static int seed = 1;

static int my_rand(void)
{
	// Perform the calculation as unsigned to avoid signed overflow error.
	seed = (int)(((unsigned)seed * 1103515245) + 12345) & MY_RAND_MAX;
	return (seed);
}

/***********************************************************************************
 *
 * Name:	eas_rec_bit
 *
 * Purpose:	Extract EAS trasmissions from a stream of bits.
 *
 * Inputs:	chan	- Channel number.
 *
 *		subchan	- This allows multiple demodulators per channel.
 *
 *		slice	- Allows multiple slicers per demodulator (subchannel).
 *
 *		raw 	- One bit from the demodulator.
 *			  should be 0 or 1.
 *
 *		future_use - Not implemented yet.  PSK already provides it.
 *
 *
 * Description:	This is called once for each received bit.
 *		For each valid transmission, process_rec_frame()
 *		is called for further processing.
 *
 ***********************************************************************************/

#define PREAMBLE 0xababababababababULL
#define PREAMBLE_ZCZC 0x435a435aababababULL
#define PREAMBLE_NNNN 0x4e4e4e4eababababULL

/*

EAS has no error detection.
Maybe that doesn't matter because we would normally be dealing with a reasonable
VHF FM or TV signal.
Let's see what happens when we intentionally introduce errors.
When some match and others don't, the multislice voting should give preference
to those matching others.

	$ src/atest -P+ -B EAS -e 3e-3 ../../ref-doc/EAS/same.wav
	Demodulator profile set to "+"
	96000 samples per second.  16 bits per sample.  1 audio channels.
	2079360 audio bytes in file.  Duration = 10.8 seconds.
	Fix Bits level = 0
	Channel 0: 521 baud, AFSK 2083 & 1563 Hz, D+, 96000 sample rate / 3.

case 1:  Slice 6 is different than others (EQS vs. EAS) so we want one of the others that match.
	 Slice 3 has an unexpected character (in 0120u7) so it is a mismatch.
	 At this point we are not doing validity checking other than all printable characters.

	 We are left with 0 & 4 which don't match (012057 vs. 012077).
	 So I guess we don't have any two that match so it is a toss up.

	reject 7 invalid character = ZCZC-EAS-RWT-0120▒
	reject 5 invalid character = ZCZC-ECW-RWT-012057-012081-012101-012103-012115+003
	frame_buf 6 = ZCZC-EQS-RWT-012057-012081-012101-012103-012115+0030-2780415-WTSP/TV-
	frame_buf 4 = ZCZC-EAS-RWT-012077-012081-012101-012103-012115+0030-2780415-WTSP/TV-
	frame_buf 3 = ZCZC-EAS-RWT-0120u7-012281-012101-012103-092115+0038-2780415-VTSP/TV-
	frame_buf 0 = ZCZC-EAS-RWT-012057-412081-012101-012103-012115+0030-2780415-WTSP/TV-

	DECODED[1] 0:01.313 EAS audio level = 194(106/108)     |__||_|__
	[0.0] EAS>APDW16:{DEZCZC-EAS-RWT-012057-412081-012101-012103-012115+0030-2780415-WTSP/TV-

Case 2: We have two that match so pick either one.

	reject 5 invalid character = ZCZC-EAS-RW▒
	reject 7 invalid character = ZCZC-EAS-RWT-0
	reject 3 invalid character = ZCZC-EAS-RWT-012057-012080-012101-012103-01211
	reject 0 invalid character = ZCZC-EAS-RWT-012057-012081-012101-012103-012115+0030-2780415-W▒
	frame_buf 6 = ZCZC-EAS-RWT-012057-012081-012!01-012103-012115+0030-2780415-WTSP/TV-
	frame_buf 1 = ZCZC-EAS-RWT-012057-012081-012101-012103-012115+0030-2780415-WTSP/TV-

	DECODED[2] 0:03.617 EAS audio level = 194(106/108)     _|____|__
	[0.1] EAS>APDW16:{DEZCZC-EAS-RWT-012057-012081-012101-012103-012115+0030-2780415-WTSP/TV-

Case 3: Slice 6 is a mismatch (EAs vs. EAS).
	Slice 7 has RST rather than RWT.
	2 & 4 don't match either (012141 vs. 012101).
	We have another case where no two match so there is no clear winner.


	reject 5 invalid character = ZCZC-EAS-RWT-012057-012081-012101-012103-012115+▒
	frame_buf 7 = ZCZC-EAS-RST-012057-012081-012101-012103-012115+0030-2780415-WTSP/TV-
	frame_buf 6 = ZCZC-EAs-RWT-012057-012081-012101-012103-012115+0030-2780415-WTSP/TV-
	frame_buf 4 = ZCZC-EAS-RWT-112057-012081-012101-012103-012115+0030-2780415-WTSP/TV-
	frame_buf 2 = ZCZC-EAS-RWT-012057-012081-012141-012103-012115+0030-2780415-WTSP/TV-

	DECODED[3] 0:05.920 EAS audio level = 194(106/108)     __|_|_||_
	[0.2] EAS>APDW16:{DEZCZC-EAS-RWT-012057-012081-012141-012103-012115+0030-2780415-WTSP/TV-

Conclusions:

	(1) The existing algorithm gives a higher preference to those frames matching others.
	We didn't see any cases here where that would be to our advantage.

	(2) A partial solution would be more validity checking.  (i.e. non-digit where
	digit is expected.)  But wait... We might want to keep it for consideration:

	(3) If I got REALLY ambitious, some day, we could compare all of them one column
	at a time and take the most popular (and valid for that column) character and
	use all of the most popular characters. Better yet, at the bit level.

Of course this is probably all overkill because we would normally expect to have pretty
decent signals.  The designers didn't even bother to add any sort of checksum for error checking.

The random errors injected are also not realistic. Actual noise would probably wipe out the
same bit(s) for all of the slices.

The protocol specification suggests comparing all 3 transmissions and taking the best 2 out of 3.
I think that would best be left to an external application and we just concentrate on being
a good modem here and providing a result when it is received.

*/

/***********************************************************************************
 *
 * Name:	hdlc_rec_bit
 *
 * Purpose:	Extract HDLC frames from a stream of bits.
 *
 * Inputs:	chan	- Channel number.
 *
 *		subchan	- This allows multiple demodulators per channel.
 *
 *		slice	- Allows multiple slicers per demodulator (subchannel).
 *
 *		raw 	- One bit from the demodulator.
 *			  should be 0 or 1.
 *
 *		is_scrambled - Is the data scrambled?
 *
 *
 * Description:	This is called once for each received bit.
 *		For each valid frame, process_rec_frame()
 *		is called for further processing.
 *
 ***********************************************************************************/

void hdlc_rec_bit(int chan, int subchan, int slice, int raw, int is_scrambled)
{

	int dbit; /* Data bit after undoing NRZI. */
	/* Should be only 0 or 1. */
	struct hdlc_state_s *H;

	assert(was_init == 1);

	assert(chan >= 0 && chan < MAX_CHANS);
	assert(subchan >= 0 && subchan < MAX_SUBCHANS);

	assert(slice >= 0 && slice < MAX_SLICERS);

	// -e option can be used to artificially introduce the desired
	// Bit Error Rate (BER) for testing.

	if (g_audio_p->recv_ber != 0)
	{
		double r = (double)my_rand() / (double)MY_RAND_MAX; // calculate as double to preserve all 31 bits.
		if (g_audio_p->recv_ber > r)
		{

			// FIXME
			//
			// printf ("hdlc_rec_bit randomly clobber bit, ber = %.6f\n", g_audio_p->recv_ber);

			raw = !raw;
		}
	}

	/*
	 * Different state information for each channel / subchannel / slice.
	 */
	H = &hdlc_state[chan][subchan][slice];

	/*
	 * Using NRZI encoding,
	 *   A '0' bit is represented by an inversion since previous bit.
	 *   A '1' bit is represented by no change.
	 */

	dbit = (raw == H->prev_raw);
	H->prev_raw = raw;

	// After BER insertion, NRZI, and any descrambling, feed into FX.25 decoder as well.
	fx25_rec_bit(chan, subchan, slice, dbit);

	/*
	 * Octets are sent LSB first.
	 * Shift the most recent 8 bits thru the pattern detector.
	 */
	H->pat_det >>= 1;
	if (dbit)
	{
		H->pat_det |= 0x80;
	}

	H->flag4_det >>= 1;
	if (dbit)
	{
		H->flag4_det |= 0x80000000;
	}

	rrbb_append_bit(H->rrbb, raw);

	if (H->pat_det == 0x7e)
	{

		rrbb_chop8(H->rrbb);

		/*
		 * The special pattern 01111110 indicates beginning and ending of a frame.
		 * If we have an adequate number of whole octets, it is a candidate for
		 * further processing.
		 *
		 * It might look odd that olen is being tested for 7 instead of 0.
		 * This is because oacc would already have 7 bits from the special
		 * "flag" pattern before it is detected here.
		 */

		if (rrbb_get_len(H->rrbb) >= MIN_FRAME_LEN * 8)
		{

			alevel_t alevel = demod_get_audio_level(chan, subchan);

			rrbb_set_audio_level(H->rrbb, alevel);
			hdlc_rec2_block(H->rrbb);
			/* Now owned by someone else who will free it. */

			H->rrbb = rrbb_new(chan, subchan, slice, is_scrambled, H->lfsr, H->prev_descram); /* Allocate a new one. */
		}
		else
		{
			rrbb_clear(H->rrbb, is_scrambled, H->lfsr, H->prev_descram);
		}

		H->olen = 0; /* Allow accumulation of octets. */
		H->frame_len = 0;

		rrbb_append_bit(H->rrbb, H->prev_raw); /* Last bit of flag.  Needed to get first data bit. */
											   /* Now that we are saving other initial state information, */
											   /* it would be sensible to do the same for this instead */
											   /* of lumping it in with the frame data bits. */
	}

	// #define EXPERIMENT12B 1

#if EXPERIMENT12B

	else if (H->pat_det == 0xff)
	{

		/*
		 * Valid data will never have seven 1 bits in a row.
		 *
		 *	11111110
		 *
		 * This indicates loss of signal.
		 * But we will let it slip thru because it might diminish
		 * our single bit fixup effort.   Instead give up on frame
		 * only when we see eight 1 bits in a row.
		 *
		 *	11111111
		 *
		 * What is the impact?  No difference.
		 *
		 *  Before:	atest -P E -F 1 ../02_Track_2.wav	= 1003
		 *  After:	atest -P E -F 1 ../02_Track_2.wav	= 1003
		 */

#else
	else if (H->pat_det == 0xfe)
	{

		/*
		 * Valid data will never have 7 one bits in a row.
		 *
		 *	11111110
		 *
		 * This indicates loss of signal.
		 */

#endif

		H->olen = -1;	  /* Stop accumulating octets. */
		H->frame_len = 0; /* Discard anything in progress. */

		rrbb_clear(H->rrbb, is_scrambled, H->lfsr, H->prev_descram);
	}
	else if ((H->pat_det & 0xfc) == 0x7c)
	{

		/*
		 * If we have five '1' bits in a row, followed by a '0' bit,
		 *
		 *	0111110xx
		 *
		 * the current '0' bit should be discarded because it was added for
		 * "bit stuffing."
		 */
		;
	}
	else
	{

		/*
		 * In all other cases, accumulate bits into octets, and complete octets
		 * into the frame buffer.
		 */
		if (H->olen >= 0)
		{

			H->oacc >>= 1;
			if (dbit)
			{
				H->oacc |= 0x80;
			}
			H->olen++;

			if (H->olen == 8)
			{
				H->olen = 0;

				if (H->frame_len < MAX_FRAME_LEN)
				{
					H->frame_buf[H->frame_len] = H->oacc;
					H->frame_len++;
				}
			}
		}
	}
}

// TODO:  Data Carrier Detect (DCD) is now based on DPLL lock
// rather than data patterns found here.
// It would make sense to move the next 2 functions to demod.c
// because this is done at the modem level, rather than HDLC decoder.

/*-------------------------------------------------------------------
 *
 * Name:        dcd_change
 *
 * Purpose:     Combine DCD states of all subchannels/ into an overall
 *		state for the channel.
 *
 * Inputs:	chan
 *
 *		subchan		0 to MAX_SUBCHANS-1 for HDLC.
 *				SPECIAL CASE --> MAX_SUBCHANS for DTMF decoder.
 *
 *		slice		slicer number, 0 .. MAX_SLICERS - 1.
 *
 *		state		1 for active, 0 for not.
 *
 * Returns:	None.  Use hdlc_rec_data_detect_any to retrieve result.
 *
 * Description:	DCD for the channel is active if ANY of the subchannels/slices
 *		are active.  Update the DCD indicator.
 *
 * version 1.3:	Add DTMF detection into the final result.
 *		This is now called from dtmf.c too.
 *
 *--------------------------------------------------------------------*/

void dcd_change(int chan, int subchan, int slice, int state)
{
	int old, new;

	assert(chan >= 0 && chan < MAX_CHANS);
	assert(subchan >= 0 && subchan <= MAX_SUBCHANS);
	assert(slice >= 0 && slice < MAX_SLICERS);
	assert(state == 0 || state == 1);

#if DEBUG

	printf("DCD %d.%d.%d = %d \n", chan, subchan, slice, state);
#endif

	old = hdlc_rec_data_detect_any(chan);

	if (state)
	{
		composite_dcd[chan][subchan] |= (1 << slice);
	}
	else
	{
		composite_dcd[chan][subchan] &= ~(1 << slice);
	}

	new = hdlc_rec_data_detect_any(chan);

	if (new != old)
	{
		ptt_set(OCTYPE_DCD, chan, new);
	}
}

/*-------------------------------------------------------------------
 *
 * Name:        hdlc_rec_data_detect_any
 *
 * Purpose:     Determine if the radio channel is currently busy
 *		with packet data.
 *		This version doesn't care about voice or other sounds.
 *		This is used by the transmit logic to transmit only
 *		when the channel is clear.
 *
 * Inputs:	chan	- Audio channel.
 *
 * Returns:	True if channel is busy (data detected) or
 *		false if OK to transmit.
 *
 *
 * Description:	We have two different versions here.
 *
 *		hdlc_rec_data_detect_any sees if ANY of the decoders
 *		for this channel are receiving a signal.   This is
 *		used to determine whether the channel is clear and
 *		we can transmit.  This would apply to the 300 baud
 *		HF SSB case where we have multiple decoders running
 *		at the same time.  The channel is busy if ANY of them
 *		thinks the channel is busy.
 *
 * Version 1.3: New option for input signal to inhibit transmit.
 *
 *--------------------------------------------------------------------*/

int hdlc_rec_data_detect_any(int chan)
{

	int sc;
	assert(chan >= 0 && chan < MAX_CHANS);

	for (sc = 0; sc < num_subchan[chan]; sc++)
	{
		if (composite_dcd[chan][sc] != 0)
			return (1);
	}

	if (get_input(ICTYPE_TXINH, chan) == 1)
		return (1);

	return (0);

} /* end hdlc_rec_data_detect_any */

/* end hdlc_rec.c */
