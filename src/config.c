//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2021  John Langner, WB2OSZ
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

#define CONFIG_C 1 // influences behavior of aprs_tt.h

// #define DEBUG 1

/*------------------------------------------------------------------
 *
 * Module:      config.c
 *
 * Purpose:   	Read configuration information from a file.
 *
 * Description:	This started out as a simple little application with a few
 *		command line options.  Due to creeping featurism, it's now
 *		time to add a configuration file to specify options.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "ax25_pad.h"
#include "audio.h"
#include "config.h"
#include "xmit.h"

#if USE_CM108 // Current Linux or Windows only
#include "cm108.h"
#endif

#define D2R(d) ((d) * M_PI / 180.)
#define R2D(r) ((r) * 180. / M_PI)

/* Do we have a string of all digits? */

static int alldigits(char *p)
{
	if (p == NULL)
		return (0);
	if (strlen(p) == 0)
		return (0);
	while (*p != '\0')
	{
		if (!isdigit(*p))
			return (0);
		p++;
	}
	return (1);
}

/* Do we have a string of all letters or + or -  ? */

static int alllettersorpm(char *p)
{
	if (p == NULL)
		return (0);
	if (strlen(p) == 0)
		return (0);
	while (*p != '\0')
	{
		if (!isalpha(*p) && *p != '+' && *p != '-')
			return (0);
		p++;
	}
	return (1);
}

/*------------------------------------------------------------------
 *
 * Name:        parse_ll
 *
 * Purpose:     Parse latitude or longitude from configuration file.
 *
 * Inputs:      str	- String like [-]deg[^min][hemisphere]
 *
 *		which	- LAT or LON for error checking and message.
 *
 *		line	- Line number for use in error message.
 *
 * Returns:     Coordinate in signed degrees.
 *
 *----------------------------------------------------------------*/

/* Acceptable symbols to separate degrees & minutes. */
/* Degree symbol is not in ASCII so documentation says to use "^" instead. */
/* Some wise guy will try to use degree symbol. */
/* UTF-8 is more difficult because it is a two byte sequence, c2 b0. */

#define DEG1 '^'
#define DEG2 0xb0 /* ISO Latin1 */
#define DEG3 0xf8 /* Microsoft code page 437 */

/*------------------------------------------------------------------
 *
 * Name:        parse_utm_zone
 *
 * Purpose:     Parse UTM zone from configuration file.
 *
 * Inputs:      szone	- String like [-]number[letter]
 *
 * Outputs:	latband	- Latitude band if specified, otherwise space or -.
 *
 *		hemi	- Hemisphere, always one of 'N' or 'S'.
 *
 * Returns:	Zone as number.
 *		Type is long because Convert_UTM_To_Geodetic expects that.
 *
 * Errors:	Prints message and return 0.
 *
 * Description:
 *		It seems there are multiple conventions for specifying the UTM hemisphere.
 *
 *		  - MGRS latitude band.  North if missing or >= 'N'.
 *		  - Negative zone for south.
 *		  - Separate North or South.
 *
 *		I'm using the first alternative.
 *		GEOTRANS uses the third.
 *		We will also recognize the second one but I'm not sure if I want to document it.
 *
 *----------------------------------------------------------------*/

long parse_utm_zone(char *szone, char *latband, char *hemi)
{
	long lzone;
	char *zlet;

	*latband = ' ';
	*hemi = 'N'; /* default */

	lzone = strtol(szone, &zlet, 10);

	if (*zlet == '\0')
	{
		/* Number is not followed by letter something else.  */
		/* Allow negative number to mean south. */

		if (lzone < 0)
		{
			*latband = '-';
			*hemi = 'S';
			lzone = (-lzone);
		}
	}
	else
	{
		if (islower(*zlet))
		{
			*zlet = toupper(*zlet);
		}
		*latband = *zlet;
		if (strchr("CDEFGHJKLMNPQRSTUVWX", *zlet) != NULL)
		{
			if (*zlet < 'N')
			{
				*hemi = 'S';
			}
		}
		else
		{

			printf("Latitudinal band in \"%s\" must be one of CDEFGHJKLMNPQRSTUVWX.\n", szone);
			*hemi = '?';
		}
	}

	if (lzone < 1 || lzone > 60)
	{

		printf("UTM Zone number %ld must be in range of 1 to 60.\n", lzone);
	}

	return (lzone);

} /* end parse_utm_zone */

/*-------------------------------------------------------------------
 *
 * Name:        split
 *
 * Purpose:     Separate a line into command and parameters.
 *
 * Inputs:	string		- Complete command line to start process.
 *				  NULL for subsequent calls.
 *
 *		rest_of_line	- Caller wants remainder of line, not just
 *				  the next parameter.
 *
 * Returns:	Pointer to next part with any quoting removed.
 *
 * Description:	the configuration file started out very simple and strtok
 *		was used to split up the lines.  As more complicated options
 *		were added, there were several different situations where
 *		parameter values might contain spaces.  These were handled
 *		inconsistently in different places.  In version 1.3, we now
 *		treat them consistently in one place.
 *
 *
 *--------------------------------------------------------------------*/

#define MAXCMDLEN 1200

static char *split(char *string, int rest_of_line)
{
	static char cmd[MAXCMDLEN];
	static char token[MAXCMDLEN];
	static char shutup[] = " "; // Shut up static analysis which gets upset
								// over the case where this could be called with
								// string NULL and c was not yet initialized.
	static char *c = shutup; // Current position in command line.
	char *s, *t;
	int in_quotes;

	/*
	 * If string is provided, make a copy.
	 * Drop any CRLF at the end.
	 * Change any tabs to spaces so we don't have to check for it later.
	 */
	if (string != NULL)
	{

		// printf("split in: '%s'\n", string);

		c = cmd;
		for (s = string; *s != '\0'; s++)
		{
			if (*s == '\t')
			{
				*c++ = ' ';
			}
			else if (*s == '\r' || *s == '\n')
			{
				;
			}
			else
			{
				*c++ = *s;
			}
		}
		*c = '\0';
		c = cmd;
	}

	/*
	 * Get next part, separated by whitespace, keeping spaces within quotes.
	 * Quotation marks inside need to be doubled.
	 */

	while (*c == ' ')
	{
		c++;
	};

	t = token;
	in_quotes = 0;
	for (; *c != '\0'; c++)
	{

		if (*c == '"')
		{
			if (in_quotes)
			{
				if (c[1] == '"')
				{
					*t++ = *c++;
				}
				else
				{
					in_quotes = 0;
				}
			}
			else
			{
				in_quotes = 1;
			}
		}
		else if (*c == ' ')
		{
			if (in_quotes || rest_of_line)
			{
				*t++ = *c;
			}
			else
			{
				break;
			}
		}
		else
		{
			*t++ = *c;
		}
	}
	*t = '\0';

	// printf("split out: '%s'\n", token);

	t = token;
	if (*t == '\0')
	{
		return (NULL);
	}

	return (t);

} /* end split */

/*-------------------------------------------------------------------
 *
 * Name:        config_init
 *
 * Purpose:     Read configuration file when application starts up.
 *
 * Inputs:	fname		- Name of configuration file.
 *
 * Outputs:	p_audio_config		- Radio channel parameters stored here.
 *
 *		p_misc_config	- Everything else.  This wasn't thought out well.
 *
 * Description:	Apply default values for various parameters then read the
 *		the configuration file which can override those values.
 *
 * Errors:	For invalid input, display line number and message on stdout (not stderr).
 *		In many cases this will result in keeping the default rather than aborting.
 *
 * Bugs:	Very simple-minded parsing.
 *		Not much error checking.  (e.g. atoi() will return 0 for invalid string.)
 *		Not very forgiving about sloppy input.
 *
 *--------------------------------------------------------------------*/

static void rtfm()
{

	printf("See online documentation:\n");
	printf("    stable release:    https://github.com/wb2osz/direwolf/tree/master/doc\n");
	printf("    development version:    https://github.com/wb2osz/direwolf/tree/dev/doc\n");
	printf("    additional topics:    https://github.com/wb2osz/direwolf-doc\n");
}

void config_init(char *fname, struct audio_s *p_audio_config,
				 struct misc_config_s *p_misc_config)
{
	FILE *fp;
	char filepath[128];
	char stuff[MAXCMDLEN];
	int line;
	int channel;
	int adevice;

#if DEBUG

	printf("config_init ( %s )\n", fname);
#endif

	/*
	 * First apply defaults.
	 */

	memset(p_audio_config, 0, sizeof(struct audio_s));

	/* First audio device is always available with defaults. */
	/* Others must be explicitly defined before use. */

	for (adevice = 0; adevice < MAX_ADEVS; adevice++)
	{

		strncpy(p_audio_config->adev[adevice].adevice_in, DEFAULT_ADEVICE, sizeof(p_audio_config->adev[adevice].adevice_in));
		strncpy(p_audio_config->adev[adevice].adevice_out, DEFAULT_ADEVICE, sizeof(p_audio_config->adev[adevice].adevice_out));

		p_audio_config->adev[adevice].defined = 0;
		p_audio_config->adev[adevice].num_channels = DEFAULT_NUM_CHANNELS;		 /* -2 stereo */
		p_audio_config->adev[adevice].samples_per_sec = DEFAULT_SAMPLES_PER_SEC; /* -r option */
		p_audio_config->adev[adevice].bits_per_sample = DEFAULT_BITS_PER_SAMPLE; /* -8 option for 8 instead of 16 bits */
	}

	p_audio_config->adev[0].defined = 1;

	for (channel = 0; channel < MAX_CHANS; channel++)
	{
		int ot, it;

		p_audio_config->chan_medium[channel] = MEDIUM_NONE; /* One or both channels will be */
															/* set to radio when corresponding */
															/* audio device is defined. */
		p_audio_config->achan[channel].modem_type = MODEM_AFSK;
		p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;	/* -m option */
		p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ; /* -s option */
		p_audio_config->achan[channel].baud = DEFAULT_BAUD;				/* -b option */

		/* None.  Will set default later based on other factors. */
		strncpy(p_audio_config->achan[channel].profiles, "", sizeof(p_audio_config->achan[channel].profiles));

		p_audio_config->achan[channel].num_freq = 1;
		p_audio_config->achan[channel].offset = 0;

		p_audio_config->achan[channel].layer2_xmit = LAYER2_AX25;

		p_audio_config->achan[channel].fix_bits = DEFAULT_FIX_BITS;
		p_audio_config->achan[channel].sanity_test = SANITY_APRS;
		p_audio_config->achan[channel].passall = 0;

		for (ot = 0; ot < NUM_OCTYPES; ot++)
		{
			p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_NONE;
			strncpy(p_audio_config->achan[channel].octrl[ot].ptt_device, "", sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));
			p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_NONE;
			p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_NONE;
			p_audio_config->achan[channel].octrl[ot].out_gpio_num = 0;
			p_audio_config->achan[channel].octrl[ot].ptt_lpt_bit = 0;
			p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
			p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 0;
		}

		for (it = 0; it < NUM_ICTYPES; it++)
		{
			p_audio_config->achan[channel].ictrl[it].method = PTT_METHOD_NONE;
			p_audio_config->achan[channel].ictrl[it].in_gpio_num = 0;
			p_audio_config->achan[channel].ictrl[it].invert = 0;
		}

		p_audio_config->achan[channel].dwait = DEFAULT_DWAIT;
		p_audio_config->achan[channel].slottime = DEFAULT_SLOTTIME;
		p_audio_config->achan[channel].persist = DEFAULT_PERSIST;
		p_audio_config->achan[channel].txdelay = DEFAULT_TXDELAY;
		p_audio_config->achan[channel].txtail = DEFAULT_TXTAIL;
		p_audio_config->achan[channel].fulldup = DEFAULT_FULLDUP;
	}

	/* First channel should always be valid. */
	/* If there is no ADEVICE, it uses default device in mono. */

	p_audio_config->chan_medium[0] = MEDIUM_RADIO;

	memset(p_misc_config, 0, sizeof(struct misc_config_s));

	for (int i = 0; i < MAX_KISS_TCP_PORTS; i++)
	{
		p_misc_config->kiss_port[i] = 0; // entry not used.
		p_misc_config->kiss_chan[i] = -1;
	}
	p_misc_config->kiss_port[0] = DEFAULT_KISS_PORT;
	p_misc_config->kiss_chan[0] = -1; // all channels.

	p_misc_config->enable_kiss_pt = 0; /* -p option */
	p_misc_config->kiss_copy = 0;

	strncpy(p_misc_config->kiss_serial_port, "", sizeof(p_misc_config->kiss_serial_port));
	p_misc_config->kiss_serial_speed = 0;
	p_misc_config->kiss_serial_poll = 0;

	/*
	 * Try to extract options from a file.
	 *
	 * Windows:  File must be in current working directory.
	 *
	 * Linux: Search current directory then home directory.
	 *
	 * Future possibility - Could also search home directory
	 * for Windows by combinting two variables:
	 *	HOMEDRIVE=C:
	 *	HOMEPATH=\Users\John
	 *
	 * It's not clear if this always points to same location:
	 *	USERPROFILE=C:\Users\John
	 */

	channel = 0;
	adevice = 0;

	// TODO: Would be better to have a search list and loop thru it.

	strncpy(filepath, fname, sizeof(filepath));

	fp = fopen(filepath, "r");

#ifndef __WIN32__
	if (fp == NULL && strcmp(fname, "direwolf.conf") == 0)
	{
		/* Failed to open the default location.  Try home dir. */
		char *p;

		strncpy(filepath, "", sizeof(filepath));

		p = getenv("HOME");
		if (p != NULL)
		{
			strncpy(filepath, p, sizeof(filepath));
			strlcat(filepath, "/direwolf.conf", sizeof(filepath));
			fp = fopen(filepath, "r");
		}
	}
#endif
	if (fp == NULL)
	{
		// TODO: not exactly right for all situations.

		printf("ERROR - Could not open config file %s\n", filepath);
		printf("Try using -c command line option for alternate location.\n");
		rtfm();
		exit(EXIT_FAILURE);
	}

	printf("\nReading config file %s\n", filepath);

	line = 0;
	while (fgets(stuff, sizeof(stuff), fp) != NULL)
	{
		char *t;

		line++;

		t = split(stuff, 0);

		if (t == NULL)
		{
			continue;
		}

		if (*t == '#' || *t == '*')
		{
			continue;
		}

		/*
		 * ==================== Audio device parameters ====================
		 */

		/*
		 * ADEVICE[n] 		- Name of input sound device, and optionally output, if different.
		 *
		 *			ADEVICE    plughw:1,0			-- same for in and out.
		 *			ADEVICE	   plughw:2,0  plughw:3,0	-- different in/out for a channel or channel pair.
		 *			ADEVICE1   udp:7355  default		-- from Software defined radio (SDR) via UDP.
		 *
		 */

		/* Note that ALSA name can contain comma such as hw:1,0 */

		if (strncasecmp(t, "ADEVICE", 7) == 0)
		{
			/* "ADEVICE" is equivalent to "ADEVICE0". */
			adevice = 0;
			if (strlen(t) >= 8)
			{
				adevice = atoi(t + 7);
			}

			if (adevice < 0 || adevice >= MAX_ADEVS)
			{

				printf("Config file: Device number %d out of range for ADEVICE command on line %d.\n", adevice, line);
				printf("If you really need more than %d audio devices, increase MAX_ADEVS and recompile.\n", MAX_ADEVS);
				adevice = 0;
				continue;
			}

			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Config file: Missing name of audio device for ADEVICE command on line %d.\n", line);
				rtfm();
				exit(EXIT_FAILURE);
			}

			p_audio_config->adev[adevice].defined = 1;

			/* First channel of device is valid. */
			p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;

			strncpy(p_audio_config->adev[adevice].adevice_in, t, sizeof(p_audio_config->adev[adevice].adevice_in));
			strncpy(p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out));

			t = split(NULL, 0);
			if (t != NULL)
			{
				strncpy(p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out));
			}
		}

		/*
		 * PAIDEVICE[n]  input-device
		 * PAODEVICE[n]  output-device
		 *
		 *			This was submitted by KK5VD for the Mac OS X version.  (__APPLE__)
		 *
		 *			It looks like device names can contain spaces making it a little
		 *			more difficult to put two names on the same line unless we come up with
		 *			some other delimiter between them or a quoting scheme to handle
		 *			embedded spaces in a name.
		 *
		 *			It concerns me that we could have one defined without the other
		 *			if we don't put in more error checking later.
		 *
		 *	version 1.3 dev snapshot C:
		 *
		 *		We now have a general quoting scheme so the original ADEVICE can handle this.
		 *		These options will probably be removed before general 1.3 release.
		 */

		else if (strcasecmp(t, "PAIDEVICE") == 0)
		{
			adevice = 0;
			if (isdigit(t[9]))
			{
				adevice = t[9] - '0';
			}

			if (adevice < 0 || adevice >= MAX_ADEVS)
			{

				printf("Config file: Device number %d out of range for PADEVICE command on line %d.\n", adevice, line);
				adevice = 0;
				continue;
			}

			t = split(NULL, 1);
			if (t == NULL)
			{

				printf("Config file: Missing name of audio device for PADEVICE command on line %d.\n", line);
				continue;
			}

			p_audio_config->adev[adevice].defined = 1;

			/* First channel of device is valid. */
			p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;

			strncpy(p_audio_config->adev[adevice].adevice_in, t, sizeof(p_audio_config->adev[adevice].adevice_in));
		}
		else if (strcasecmp(t, "PAODEVICE") == 0)
		{
			adevice = 0;
			if (isdigit(t[9]))
			{
				adevice = t[9] - '0';
			}

			if (adevice < 0 || adevice >= MAX_ADEVS)
			{

				printf("Config file: Device number %d out of range for PADEVICE command on line %d.\n", adevice, line);
				adevice = 0;
				continue;
			}

			t = split(NULL, 1);
			if (t == NULL)
			{

				printf("Config file: Missing name of audio device for PADEVICE command on line %d.\n", line);
				continue;
			}

			p_audio_config->adev[adevice].defined = 1;

			/* First channel of device is valid. */
			p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;

			strncpy(p_audio_config->adev[adevice].adevice_out, t, sizeof(p_audio_config->adev[adevice].adevice_out));
		}

		/*
		 * ARATE 		- Audio samples per second, 11025, 22050, 44100, etc.
		 */

		else if (strcasecmp(t, "ARATE") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing audio sample rate for ARATE command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= MIN_SAMPLES_PER_SEC && n <= MAX_SAMPLES_PER_SEC)
			{
				p_audio_config->adev[adevice].samples_per_sec = n;
			}
			else
			{

				printf("Line %d: Use a more reasonable audio sample rate in range of %d - %d.\n",
					   line, MIN_SAMPLES_PER_SEC, MAX_SAMPLES_PER_SEC);
			}
		}

		/*
		 * ACHANNELS 		- Number of audio channels for current device: 1 or 2
		 */

		else if (strcasecmp(t, "ACHANNELS") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing number of audio channels for ACHANNELS command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n == 1 || n == 2)
			{
				p_audio_config->adev[adevice].num_channels = n;

				/* Set valid channels depending on mono or stereo. */

				p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice)] = MEDIUM_RADIO;
				if (n == 2)
				{
					p_audio_config->chan_medium[ADEVFIRSTCHAN(adevice) + 1] = MEDIUM_RADIO;
				}
			}
			else
			{

				printf("Line %d: Number of audio channels must be 1 or 2.\n", line);
			}
		}

		/*
		 * ==================== Radio channel parameters ====================
		 */

		/*
		 * CHANNEL n		- Set channel for channel-specific commands.
		 */

		else if (strcasecmp(t, "CHANNEL") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing channel number for CHANNEL command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= 0 && n < MAX_CHANS)
			{

				channel = n;

				if (p_audio_config->chan_medium[n] != MEDIUM_RADIO)
				{

					if (!p_audio_config->adev[ACHAN2ADEV(n)].defined)
					{

						printf("Line %d: Channel number %d is not valid because audio device %d is not defined.\n",
							   line, n, ACHAN2ADEV(n));
					}
					else
					{

						printf("Line %d: Channel number %d is not valid because audio device %d is not in stereo.\n",
							   line, n, ACHAN2ADEV(n));
					}
				}
			}
			else
			{

				printf("Line %d: Channel number must in range of 0 to %d.\n", line, MAX_CHANS - 1);
			}
		}

		/*
		 * MODEM	- Set modem properties for current channel.
		 *
		 *
		 * Old style:
		 * 	MODEM  baud [ mark  space  [A][B][C][+]  [  num-decoders spacing ] ]
		 *
		 * New style, version 1.2:
		 *	MODEM  speed [ option ] ...
		 *
		 * Options:
		 *	mark:space	- AFSK tones.  Defaults based on speed.
		 *	num@offset	- Multiple decoders on different frequencies.
		 *	/9		- Divide sample rate by specified number.
		 *	*9		- Upsample ratio for G3RUH.
		 *	[A-Z+-]+	- Letters, plus, minus for the demodulator "profile."
		 */

		else if (strcasecmp(t, "MODEM") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing data transmission speed for MODEM command.\n", line);
				continue;
			}

			n = atoi(t);
			if (n >= MIN_BAUD && n <= MAX_BAUD)
			{
				p_audio_config->achan[channel].baud = n;
				if (n != 300 && n != 1200 && n != 2400 && n != 4800 && n != 9600 && n != 19200 && n != MAX_BAUD - 1 && n != MAX_BAUD - 2)
				{

					printf("Line %d: Warning: Non-standard data rate of %d bits per second.  Are you sure?\n", line, n);
				}
			}
			else
			{
				p_audio_config->achan[channel].baud = DEFAULT_BAUD;

				printf("Line %d: Unreasonable data rate. Using %d bits per second.\n",
					   line, p_audio_config->achan[channel].baud);
			}

			/* Set defaults based on speed. */
			/* Should be same as -B command line option in direwolf.c. */

			/* We have similar logic in direwolf.c, config.c, gen_packets.c, and atest.c, */
			/* that need to be kept in sync.  Maybe it could be a common function someday. */

			if (p_audio_config->achan[channel].baud < 600)
			{
				p_audio_config->achan[channel].modem_type = MODEM_AFSK;
				p_audio_config->achan[channel].mark_freq = 1600;
				p_audio_config->achan[channel].space_freq = 1800;
			}
			else if (p_audio_config->achan[channel].baud < 1800)
			{
				p_audio_config->achan[channel].modem_type = MODEM_AFSK;
				p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;
				p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;
			}

			/* Get any options. */

			t = split(NULL, 0);
			if (t == NULL)
			{
				/* all done. */
				continue;
			}

			if (alldigits(t))
			{

				/* old style */

				printf("Line %d: Old style (pre version 1.2) format will no longer be supported in next version.\n", line);

				n = atoi(t);
				/* Originally the upper limit was 3000. */
				/* Version 1.0 increased to 5000 because someone */
				/* wanted to use 2400/4800 Hz AFSK. */
				/* Of course the MIC and SPKR connections won't */
				/* have enough bandwidth so radios must be modified. */
				if (n >= 300 && n <= 5000)
				{
					p_audio_config->achan[channel].mark_freq = n;
				}
				else
				{
					p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;

					printf("Line %d: Unreasonable mark tone frequency. Using %d.\n",
						   line, p_audio_config->achan[channel].mark_freq);
				}

				/* Get space frequency */

				t = split(NULL, 0);
				if (t == NULL)
				{

					printf("Line %d: Missing tone frequency for space.\n", line);
					continue;
				}
				n = atoi(t);
				if (n >= 300 && n <= 5000)
				{
					p_audio_config->achan[channel].space_freq = n;
				}
				else
				{
					p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;

					printf("Line %d: Unreasonable space tone frequency. Using %d.\n",
						   line, p_audio_config->achan[channel].space_freq);
				}

				/* Gently guide users toward new format. */

				if (p_audio_config->achan[channel].baud == 1200 &&
					p_audio_config->achan[channel].mark_freq == 1200 &&
					p_audio_config->achan[channel].space_freq == 2200)
				{

					printf("Line %d: The AFSK frequencies can be omitted when using the 1200 baud default 1200:2200.\n", line);
				}
				if (p_audio_config->achan[channel].baud == 300 &&
					p_audio_config->achan[channel].mark_freq == 1600 &&
					p_audio_config->achan[channel].space_freq == 1800)
				{

					printf("Line %d: The AFSK frequencies can be omitted when using the 300 baud default 1600:1800.\n", line);
				}

				/* New feature in 0.9 - Optional filter profile(s). */

				t = split(NULL, 0);
				if (t != NULL)
				{

					/* Look for some combination of letter(s) and + */

					if (isalpha(t[0]) || t[0] == '+')
					{
						char *pc;

						/* Here we only catch something other than letters and + mixed in. */
						/* Later, we check for valid letters and no more than one letter if + specified. */

						for (pc = t; *pc != '\0'; pc++)
						{
							if (!isalpha(*pc) && !(*pc == '+'))
							{

								printf("Line %d: Demodulator type can only contain letters and + character.\n", line);
							}
						}

						strncpy(p_audio_config->achan[channel].profiles, t, sizeof(p_audio_config->achan[channel].profiles));
						t = split(NULL, 0);
						if (strlen(p_audio_config->achan[channel].profiles) > 1 && t != NULL)
						{

							printf("Line %d: Can't combine multiple demodulator types and multiple frequencies.\n", line);
							continue;
						}
					}
				}

				/* New feature in 0.9 - optional number of decoders and frequency offset between. */

				if (t != NULL)
				{
					n = atoi(t);
					if (n < 1 || n > MAX_SUBCHANS)
					{

						printf("Line %d: Number of demodulators is out of range. Using 3.\n", line);
						n = 3;
					}
					p_audio_config->achan[channel].num_freq = n;

					t = split(NULL, 0);
					if (t != NULL)
					{
						n = atoi(t);
						if (n < 5 || n > abs(p_audio_config->achan[channel].mark_freq - p_audio_config->achan[channel].space_freq) / 2)
						{

							printf("Line %d: Unreasonable value for offset between modems.  Using 50 Hz.\n", line);
							n = 50;
						}
						p_audio_config->achan[channel].offset = n;

						printf("Line %d: New style for multiple demodulators is %d@%d\n", line,
							   p_audio_config->achan[channel].num_freq, p_audio_config->achan[channel].offset);
					}
					else
					{

						printf("Line %d: Missing frequency offset between modems.  Using 50 Hz.\n", line);
						p_audio_config->achan[channel].offset = 50;
					}
				}
			}
			else
			{

				/* New style in version 1.2. */

				while (t != NULL)
				{
					char *s;

					if ((s = strchr(t, ':')) != NULL)
					{ /* mark:space */

						p_audio_config->achan[channel].mark_freq = atoi(t);
						p_audio_config->achan[channel].space_freq = atoi(s + 1);

						p_audio_config->achan[channel].modem_type = MODEM_AFSK;

						if (p_audio_config->achan[channel].mark_freq < 300 || p_audio_config->achan[channel].mark_freq > 5000)
						{
							p_audio_config->achan[channel].mark_freq = DEFAULT_MARK_FREQ;

							printf("Line %d: Unreasonable mark tone frequency. Using %d instead.\n",
								   line, p_audio_config->achan[channel].mark_freq);
						}
						if (p_audio_config->achan[channel].space_freq < 300 || p_audio_config->achan[channel].space_freq > 5000)
						{
							p_audio_config->achan[channel].space_freq = DEFAULT_SPACE_FREQ;

							printf("Line %d: Unreasonable space tone frequency. Using %d instead.\n",
								   line, p_audio_config->achan[channel].space_freq);
						}

						else if ((s = strchr(t, '@')) != NULL)
						{ /* num@offset */

							p_audio_config->achan[channel].num_freq = atoi(t);
							p_audio_config->achan[channel].offset = atoi(s + 1);

							if (p_audio_config->achan[channel].num_freq < 1 || p_audio_config->achan[channel].num_freq > MAX_SUBCHANS)
							{

								printf("Line %d: Number of demodulators is out of range. Using 3.\n", line);
								p_audio_config->achan[channel].num_freq = 3;
							}

							if (p_audio_config->achan[channel].offset < 5 ||
								p_audio_config->achan[channel].offset > abs(p_audio_config->achan[channel].mark_freq - p_audio_config->achan[channel].space_freq) / 2)
							{

								printf("Line %d: Offset between demodulators is unreasonable. Using 50 Hz.\n", line);
								p_audio_config->achan[channel].offset = 50;
							}
						}

						else if (alllettersorpm(t))
						{ /* profile of letter(s) + - */

							// Will be validated later.
							strncpy(p_audio_config->achan[channel].profiles, t, sizeof(p_audio_config->achan[channel].profiles));
						}

						else if (*t == '/')
						{ /* /div */
							int n = atoi(t + 1);

							if (n >= 1 && n <= 8)
							{
								p_audio_config->achan[channel].decimate = n;
							}
							else
							{

								printf("Line %d: Ignoring unreasonable sample rate division factor of %d.\n", line, n);
							}
						}

						else if (*t == '*')
						{ /* *upsample */
							int n = atoi(t + 1);

							if (n >= 1 && n <= 4)
							{
								p_audio_config->achan[channel].upsample = n;
							}
							else
							{

								printf("Line %d: Ignoring unreasonable upsample ratio of %d.\n", line, n);
							}
						}

						else
						{

							printf("Line %d: Unrecognized option for MODEM: %s\n", line, t);
						}

						t = split(NULL, 0);
					}

					/* A later place catches disallowed combination of + and @. */
					/* A later place sets /n for 300 baud if not specified by user. */

					// printf ("debug: div = %d\n", p_audio_config->achan[channel].decimate);
				}
			}
		}

		/*
		 * FIX_BITS  n  [ APRS | AX25 | NONE ] [ PASSALL ]
		 *
		 *	- Attempt to fix frames with bad FCS.
		 *	- n is maximum number of bits to attempt fixing.
		 *	- Optional sanity check & allow everything even with bad FCS.
		 */

		else if (strcasecmp(t, "FIX_BITS") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing value for FIX_BITS command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= RETRY_NONE && n < RETRY_MAX)
			{ // MAX is actually last valid +1
				p_audio_config->achan[channel].fix_bits = (retry_t)n;
			}
			else
			{
				p_audio_config->achan[channel].fix_bits = DEFAULT_FIX_BITS;

				printf("Line %d: Invalid value %d for FIX_BITS. Using default of %d.\n",
					   line, n, p_audio_config->achan[channel].fix_bits);
			}

			if (p_audio_config->achan[channel].fix_bits > DEFAULT_FIX_BITS)
			{

				printf("Line %d: Using a FIX_BITS value greater than %d is not recommended for normal operation.\n",
					   line, DEFAULT_FIX_BITS);
				printf("FIX_BITS > 1 was an interesting experiment but turned out to be a bad idea.\n");
				printf("Don't be surprised if it takes 100%% CPU, direwolf can't keep up with the audio stream,\n");
				printf("and you see messages like \"Audio input device 0 error code -32: Broken pipe\"\n");
			}

			t = split(NULL, 0);
			while (t != NULL)
			{

				// If more than one sanity test, we silently take the last one.

				if (strcasecmp(t, "APRS") == 0)
				{
					p_audio_config->achan[channel].sanity_test = SANITY_APRS;
				}
				else if (strcasecmp(t, "AX25") == 0 || strcasecmp(t, "AX.25") == 0)
				{
					p_audio_config->achan[channel].sanity_test = SANITY_AX25;
				}
				else if (strcasecmp(t, "NONE") == 0)
				{
					p_audio_config->achan[channel].sanity_test = SANITY_NONE;
				}
				else if (strcasecmp(t, "PASSALL") == 0)
				{
					p_audio_config->achan[channel].passall = 1;

					printf("Line %d: There is an old saying, \"Be careful what you ask for because you might get it.\"\n", line);
					printf("The PASSALL option means allow all frames even when they are invalid.\n");
					printf("You are asking to receive random trash and you WILL get your wish.\n");
					printf("Don't complain when you see all sorts of random garbage.  That's what you asked for.\n");
				}
				else
				{

					printf("Line %d: Invalid option '%s' for FIX_BITS.\n", line, t);
				}
				t = split(NULL, 0);
			}
		}

		/*
		 * PTT 		- Push To Talk signal line.
		 * DCD		- Data Carrier Detect indicator.
		 * CON		- Connected to another station indicator.
		 *
		 * xxx  serial-port [-]rts-or-dtr [ [-]rts-or-dtr ]
		 * xxx  GPIO  [-]gpio-num
		 * xxx  LPT  [-]bit-num
		 * PTT  RIG  model  port [ rate ]
		 * PTT  RIG  AUTO  port [ rate ]
		 * PTT  CM108 [ [-]bit-num ] [ hid-device ]
		 *
		 * 		When model is 2, port would host:port like 127.0.0.1:4532
		 *		Otherwise, port would be a serial port like /dev/ttyS0
		 *
		 *
		 * Applies to most recent CHANNEL command.
		 */

		else if (strcasecmp(t, "PTT") == 0 || strcasecmp(t, "DCD") == 0 || strcasecmp(t, "CON") == 0)
		{
			int ot;
			char otname[8];

			if (strcasecmp(t, "PTT") == 0)
			{
				ot = OCTYPE_PTT;
				strncpy(otname, "PTT", sizeof(otname));
			}
			else if (strcasecmp(t, "DCD") == 0)
			{
				ot = OCTYPE_DCD;
				strncpy(otname, "DCD", sizeof(otname));
			}
			else
			{
				ot = OCTYPE_CON;
				strncpy(otname, "CON", sizeof(otname));
			}

			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Config file line %d: Missing output control device for %s command.\n",
					   line, otname);
				continue;
			}

			if (strcasecmp(t, "GPIO") == 0)
			{

				/* GPIO case, Linux only. */

#if __WIN32__

				printf("Config file line %d: %s with GPIO is only available on Linux.\n", line, otname);
#else
				t = split(NULL, 0);
				if (t == NULL)
				{

					printf("Config file line %d: Missing GPIO number for %s.\n", line, otname);
					continue;
				}

				if (*t == '-')
				{
					p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t + 1);
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
				}
				else
				{
					p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t);
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
				}
				p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_GPIO;
#endif
			}
			else if (strcasecmp(t, "LPT") == 0)
			{

				/* Parallel printer case, x86 Linux only. */

#if (defined(__i386__) || defined(__x86_64__)) && (defined(__linux__) || defined(__unix__))

				t = split(NULL, 0);
				if (t == NULL)
				{

					printf("Config file line %d: Missing LPT bit number for %s.\n", line, otname);
					continue;
				}

				if (*t == '-')
				{
					p_audio_config->achan[channel].octrl[ot].ptt_lpt_bit = atoi(t + 1);
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
				}
				else
				{
					p_audio_config->achan[channel].octrl[ot].ptt_lpt_bit = atoi(t);
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
				}
				p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_LPT;
#else

				printf("Config file line %d: %s with LPT is only available on x86 Linux.\n", line, otname);
#endif
			}
			else if (strcasecmp(t, "RIG") == 0)
			{
#ifdef USE_HAMLIB

				t = split(NULL, 0);
				if (t == NULL)
				{

					printf("Config file line %d: Missing model number for hamlib.\n", line);
					continue;
				}
				if (strcasecmp(t, "AUTO") == 0)
				{
					p_audio_config->achan[channel].octrl[ot].ptt_model = -1;
				}
				else
				{
					if (!alldigits(t))
					{

						printf("Config file line %d: A rig number, not a name, is required here.\n", line);
						printf("For example, if you have a Yaesu FT-847, specify 101.\n");
						printf("See https://github.com/Hamlib/Hamlib/wiki/Supported-Radios for more details.\n");
						continue;
					}
					int n = atoi(t);
					if (n < 1 || n > 9999)
					{

						printf("Config file line %d: Unreasonable model number %d for hamlib.\n", line, n);
						continue;
					}
					p_audio_config->achan[channel].octrl[ot].ptt_model = n;
				}

				t = split(NULL, 0);
				if (t == NULL)
				{

					printf("Config file line %d: Missing port for hamlib.\n", line);
					continue;
				}
				strncpy(p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));

				// Optional serial port rate for CAT control PTT.

				t = split(NULL, 0);
				if (t != NULL)
				{
					if (!alldigits(t))
					{

						printf("Config file line %d: An optional number is required here for CAT serial port speed: %s\n", line, t);
						continue;
					}
					int n = atoi(t);
					p_audio_config->achan[channel].octrl[ot].ptt_rate = n;
				}

				t = split(NULL, 0);
				if (t != NULL)
				{

					printf("Config file line %d: %s was not expected after model & port for hamlib.\n", line, t);
				}

				p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_HAMLIB;

#else
#if __WIN32__

				printf("Config file line %d: Windows version of direwolf does not support HAMLIB.\n", line);
				exit(EXIT_FAILURE);
#else

				printf("Config file line %d: %s with RIG is only available when hamlib support is enabled.\n", line, otname);
				printf("You must rebuild direwolf with hamlib support.\n");
				printf("See User Guide for details.\n");
#endif

#endif
			}
			else if (strcasecmp(t, "CM108") == 0)
			{

				/* CM108 - GPIO of USB sound card. case, Linux and Windows only. */

#if USE_CM108

				if (ot != OCTYPE_PTT)
				{
					// Future project:  Allow DCD and CON via the same device.
					// This gets more complicated because we can't selectively change a single GPIO bit.
					// We would need to keep track of what is currently there, change one bit, in our local
					// copy of the status and then write out the byte for all of the pins.
					// Let's keep it simple with just PTT for the first stab at this.

					printf("Config file line %d: PTT CM108 option is only valid for PTT, not %s.\n", line, otname);
					continue;
				}

				p_audio_config->achan[channel].octrl[ot].out_gpio_num = 3; // All known designs use GPIO 3.
																		   // User can override for special cases.
				p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;   // High for transmit.
				strcpy(p_audio_config->achan[channel].octrl[ot].ptt_device, "");

				// Try to find PTT device for audio output device.
				// Simplifiying assumption is that we have one radio per USB Audio Adapter.
				// Failure at this point is not an error.
				// See if config file sets it explicitly before complaining.

				cm108_find_ptt(p_audio_config->adev[ACHAN2ADEV(channel)].adevice_out,
							   p_audio_config->achan[channel].octrl[ot].ptt_device,
							   (int)sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));

				while ((t = split(NULL, 0)) != NULL)
				{
					if (*t == '-')
					{
						p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t + 1);
						p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
					}
					else if (isdigit(*t))
					{
						p_audio_config->achan[channel].octrl[ot].out_gpio_num = atoi(t);
						p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
					}
#if __WIN32__
					else if (*t == '\\')
					{
						strncpy(p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));
					}
					else
					{

						printf("Config file line %d: Found \"%s\" when expecting GPIO number or device name like \\\\?\\hid#vid_0d8c&... .\n", line, t);
						continue;
					}
#else
					else if (*t == '/')
					{
						strncpy(p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));
					}
					else
					{

						printf("Config file line %d: Found \"%s\" when expecting GPIO number or device name like /dev/hidraw1.\n", line, t);
						continue;
					}
#endif
				}
				if (p_audio_config->achan[channel].octrl[ot].out_gpio_num < 1 || p_audio_config->achan[channel].octrl[ot].out_gpio_num > 8)
				{

					printf("Config file line %d: CM108 GPIO number %d is not in range of 1 thru 8.\n", line,
						   p_audio_config->achan[channel].octrl[ot].out_gpio_num);
					continue;
				}
				if (strlen(p_audio_config->achan[channel].octrl[ot].ptt_device) == 0)
				{

					printf("Config file line %d: Could not determine USB Audio GPIO PTT device for audio output %s.\n", line,
						   p_audio_config->adev[ACHAN2ADEV(channel)].adevice_out);
#if __WIN32__
					printf("You must explicitly mention a HID path.\n");
#else
					printf("You must explicitly mention a device name such as /dev/hidraw1.\n");
#endif
					printf("Run \"cm108\" utility to get a list.\n");
					printf("See Interface Guide for details.\n");
					continue;
				}
				p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_CM108;

#else

				printf("Config file line %d: %s with CM108 is only available when USB Audio GPIO support is enabled.\n", line, otname);
				printf("You must rebuild direwolf with CM108 Audio Adapter GPIO PTT support.\n");
				printf("See Interface Guide for details.\n");
				rtfm();
				exit(EXIT_FAILURE);
#endif
			}
			else
			{

				/* serial port case. */

				strncpy(p_audio_config->achan[channel].octrl[ot].ptt_device, t, sizeof(p_audio_config->achan[channel].octrl[ot].ptt_device));

				t = split(NULL, 0);
				if (t == NULL)
				{

					printf("Config file line %d: Missing RTS or DTR after %s device name.\n",
						   line, otname);
					continue;
				}

				if (strcasecmp(t, "rts") == 0)
				{
					p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_RTS;
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
				}
				else if (strcasecmp(t, "dtr") == 0)
				{
					p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_DTR;
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 0;
				}
				else if (strcasecmp(t, "-rts") == 0)
				{
					p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_RTS;
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
				}
				else if (strcasecmp(t, "-dtr") == 0)
				{
					p_audio_config->achan[channel].octrl[ot].ptt_line = PTT_LINE_DTR;
					p_audio_config->achan[channel].octrl[ot].ptt_invert = 1;
				}
				else
				{

					printf("Config file line %d: Expected RTS or DTR after %s device name.\n",
						   line, otname);
					continue;
				}

				p_audio_config->achan[channel].octrl[ot].ptt_method = PTT_METHOD_SERIAL;

				/* In version 1.2, we allow a second one for same serial port. */
				/* Some interfaces want the two control lines driven with opposite polarity. */
				/* e.g.   PTT COM1 RTS -DTR  */

				t = split(NULL, 0);
				if (t != NULL)
				{

					if (strcasecmp(t, "rts") == 0)
					{
						p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_RTS;
						p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 0;
					}
					else if (strcasecmp(t, "dtr") == 0)
					{
						p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_DTR;
						p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 0;
					}
					else if (strcasecmp(t, "-rts") == 0)
					{
						p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_RTS;
						p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 1;
					}
					else if (strcasecmp(t, "-dtr") == 0)
					{
						p_audio_config->achan[channel].octrl[ot].ptt_line2 = PTT_LINE_DTR;
						p_audio_config->achan[channel].octrl[ot].ptt_invert2 = 1;
					}
					else
					{

						printf("Config file line %d: Expected RTS or DTR after first RTS or DTR.\n",
							   line);
						continue;
					}

					/* Would not make sense to specify the same one twice. */

					if (p_audio_config->achan[channel].octrl[ot].ptt_line == p_audio_config->achan[channel].octrl[ot].ptt_line2)
					{
						printf("Config file line %d: Doesn't make sense to specify the some control line twice.\n",
							   line);
					}

				} /* end of second serial port control line. */
			}	  /* end of serial port case. */

		} /* end of PTT, DCD, CON */

		/*
		 * INPUTS
		 *
		 * TXINH - TX holdoff input
		 *
		 * TXINH GPIO [-]gpio-num (only type supported so far)
		 */

		else if (strcasecmp(t, "TXINH") == 0)
		{
			char itname[8];

			strncpy(itname, "TXINH", sizeof(itname));

			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Config file line %d: Missing input type name for %s command.\n", line, itname);
				continue;
			}

			if (strcasecmp(t, "GPIO") == 0)
			{

#if __WIN32__

				printf("Config file line %d: %s with GPIO is only available on Linux.\n", line, itname);
#else
				t = split(NULL, 0);
				if (t == NULL)
				{

					printf("Config file line %d: Missing GPIO number for %s.\n", line, itname);
					continue;
				}

				if (*t == '-')
				{
					p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].in_gpio_num = atoi(t + 1);
					p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].invert = 1;
				}
				else
				{
					p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].in_gpio_num = atoi(t);
					p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].invert = 0;
				}
				p_audio_config->achan[channel].ictrl[ICTYPE_TXINH].method = PTT_METHOD_GPIO;
#endif
			}
		}

		/*
		 * DWAIT n		- Extra delay for receiver squelch. n = 10 mS units.
		 */

		else if (strcasecmp(t, "DWAIT") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing delay time for DWAIT command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= 0 && n <= 255)
			{
				p_audio_config->achan[channel].dwait = n;
			}
			else
			{
				p_audio_config->achan[channel].dwait = DEFAULT_DWAIT;

				printf("Line %d: Invalid delay time for DWAIT. Using %d.\n",
					   line, p_audio_config->achan[channel].dwait);
			}
		}

		/*
		 * SLOTTIME n		- For non-digipeat transmit delay timing. n = 10 mS units.
		 */

		else if (strcasecmp(t, "SLOTTIME") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing delay time for SLOTTIME command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= 0 && n <= 255)
			{
				p_audio_config->achan[channel].slottime = n;
			}
			else
			{
				p_audio_config->achan[channel].slottime = DEFAULT_SLOTTIME;

				printf("Line %d: Invalid delay time for persist algorithm. Using %d.\n",
					   line, p_audio_config->achan[channel].slottime);
			}
		}

		/*
		 * PERSIST 		- For non-digipeat transmit delay timing.
		 */

		else if (strcasecmp(t, "PERSIST") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing probability for PERSIST command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= 0 && n <= 255)
			{
				p_audio_config->achan[channel].persist = n;
			}
			else
			{
				p_audio_config->achan[channel].persist = DEFAULT_PERSIST;

				printf("Line %d: Invalid probability for persist algorithm. Using %d.\n",
					   line, p_audio_config->achan[channel].persist);
			}
		}

		/*
		 * TXDELAY n		- For transmit delay timing. n = 10 mS units.
		 */

		else if (strcasecmp(t, "TXDELAY") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing time for TXDELAY command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= 0 && n <= 255)
			{
				p_audio_config->achan[channel].txdelay = n;
			}
			else
			{
				p_audio_config->achan[channel].txdelay = DEFAULT_TXDELAY;

				printf("Line %d: Invalid time for transmit delay. Using %d.\n",
					   line, p_audio_config->achan[channel].txdelay);
			}
		}

		/*
		 * TXTAIL n		- For transmit timing. n = 10 mS units.
		 */

		else if (strcasecmp(t, "TXTAIL") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing time for TXTAIL command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= 0 && n <= 255)
			{
				p_audio_config->achan[channel].txtail = n;
			}
			else
			{
				p_audio_config->achan[channel].txtail = DEFAULT_TXTAIL;

				printf("Line %d: Invalid time for transmit timing. Using %d.\n",
					   line, p_audio_config->achan[channel].txtail);
			}
		}

		/*
		 * FULLDUP  {on|off} 		- Full Duplex
		 */
		else if (strcasecmp(t, "FULLDUP") == 0)
		{

			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing parameter for FULLDUP command.  Expecting ON or OFF.\n", line);
				continue;
			}
			if (strcasecmp(t, "ON") == 0)
			{
				p_audio_config->achan[channel].fulldup = 1;
			}
			else if (strcasecmp(t, "OFF") == 0)
			{
				p_audio_config->achan[channel].fulldup = 0;
			}
			else
			{
				p_audio_config->achan[channel].fulldup = 0;

				printf("Line %d: Expected ON or OFF for FULLDUP.\n", line);
			}
		}

		/*
		 * FX25TX n		- Enable FX.25 transmission.  Default off.
		 *				0 = off, 1 = auto mode, others are suggestions for testing
		 *				or special cases.  16, 32, 64 is number of parity bytes to add.
		 *				Also set by "-X n" command line option.
		 *				V1.7 changed from global to per-channel setting.
		 */

		else if (strcasecmp(t, "FX25TX") == 0)
		{
			int n;
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing FEC mode for FX25TX command.\n", line);
				continue;
			}
			n = atoi(t);
			if (n >= 0 && n < 200)
			{
				p_audio_config->achan[channel].fx25_strength = n;
				p_audio_config->achan[channel].layer2_xmit = LAYER2_FX25;
			}
			else
			{
				p_audio_config->achan[channel].fx25_strength = 1;
				p_audio_config->achan[channel].layer2_xmit = LAYER2_FX25;

				printf("Line %d: Unreasonable value for FX.25 transmission mode. Using %d.\n",
					   line, p_audio_config->achan[channel].fx25_strength);
			}
		}

		/*
		 * ==================== All the left overs ====================
		 */

		/*
		 * KISSPORT port [ chan ]		- Port number for KISS over IP.
		 */

		// Previously we allowed only a single TCP port for KISS.
		// An increasing number of people want to run multiple radios.
		// Unfortunately, most applications don't know how to deal with multi-radio TNCs.
		// They ignore the channel on receive and always transmit to channel 0.
		// Running multiple instances of direwolf is a work-around but this leads to
		// more complex configuration and we lose the cross-channel digipeating capability.
		// In release 1.7 we add a new feature to assign a single radio channel to a TCP port.
		// e.g.
		//	KISSPORT 8001		# default, all channels.  Radio channel = KISS channel.
		//
		//	KISSPORT 7000 0		# Only radio channel 0 for receive.
		//				# Transmit to radio channel 0, ignoring KISS channel.
		//
		//	KISSPORT 7001 1		# Only radio channel 1 for receive.  KISS channel set to 0.
		//				# Transmit to radio channel 1, ignoring KISS channel.

		// FIXME
		else if (strcasecmp(t, "KISSPORT") == 0)
		{
			int n;
			int tcp_port = 0;
			int chan = -1; // optional.  default to all if not specified.
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Line %d: Missing TCP port number for KISSPORT command.\n", line);
				continue;
			}
			n = atoi(t);
			if ((n >= MIN_IP_PORT_NUMBER && n <= MAX_IP_PORT_NUMBER) || n == 0)
			{
				tcp_port = n;
			}
			else
			{

				printf("Line %d: Invalid TCP port number for KISS TCPIP Socket Interface.\n", line);
				printf("Use something in the range of %d to %d.\n", MIN_IP_PORT_NUMBER, MAX_IP_PORT_NUMBER);
				continue;
			}

			t = split(NULL, 0);
			if (t != NULL)
			{
				chan = atoi(t);
				if (chan < 0 || chan >= MAX_CHANS)
				{

					printf("Line %d: Invalid channel %d for KISSPORT command.  Must be in range 0 thru %d.\n", line, chan, MAX_CHANS - 1);
					continue;
				}
			}

			// "KISSPORT 0" is used to remove the default entry.

			if (tcp_port == 0)
			{
				p_misc_config->kiss_port[0] = 0; // Should all be wiped out?
			}
			else
			{

				// Try to find an empty slot.
				// A duplicate TCP port number will overwrite the previous value.

				int slot = -1;
				for (int i = 0; i < MAX_KISS_TCP_PORTS && slot == -1; i++)
				{
					if (p_misc_config->kiss_port[i] == tcp_port)
					{
						slot = i;
						if (!(slot == 0 && tcp_port == DEFAULT_KISS_PORT))
						{

							printf("Line %d: Warning: Duplicate TCP port %d will overwrite previous value.\n", line, tcp_port);
						}
					}
					else if (p_misc_config->kiss_port[i] == 0)
					{
						slot = i;
					}
				}
				if (slot >= 0)
				{
					p_misc_config->kiss_port[slot] = tcp_port;
					p_misc_config->kiss_chan[slot] = chan;
				}
				else
				{

					printf("Line %d: Too many KISSPORT commands.\n", line);
				}
			}
		}

		/*
		 * NULLMODEM name [ speed ]	- Device name for serial port or our end of the virtual "null modem"
		 * SERIALKISS name  [ speed ]
		 *
		 * Version 1.5:  Added SERIALKISS which is equivalent to NULLMODEM.
		 * The original name sort of made sense when it was used only for one end of a virtual
		 * null modem cable on Windows only.  Now it is also available for Linux.
		 * TODO1.5: In retrospect, this doesn't seem like such a good name.
		 */

		else if (strcasecmp(t, "NULLMODEM") == 0 || strcasecmp(t, "SERIALKISS") == 0)
		{
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Config file: Missing serial port name on line %d.\n", line);
				continue;
			}
			else
			{
				if (strlen(p_misc_config->kiss_serial_port) > 0)
				{

					printf("Config file: Warning serial port name on line %d replaces earlier value.\n", line);
				}
				strncpy(p_misc_config->kiss_serial_port, t, sizeof(p_misc_config->kiss_serial_port));
				p_misc_config->kiss_serial_speed = 0;
				p_misc_config->kiss_serial_poll = 0;
			}

			t = split(NULL, 0);
			if (t != NULL)
			{
				p_misc_config->kiss_serial_speed = atoi(t);
			}
		}

		/*
		 * SERIALKISSPOLL name		- Poll for serial port name that might come and go.
		 *			  	  e.g. /dev/rfcomm0 for bluetooth.
		 */

		else if (strcasecmp(t, "SERIALKISSPOLL") == 0)
		{
			t = split(NULL, 0);
			if (t == NULL)
			{

				printf("Config file: Missing serial port name on line %d.\n", line);
				continue;
			}
			else
			{
				if (strlen(p_misc_config->kiss_serial_port) > 0)
				{

					printf("Config file: Warning serial port name on line %d replaces earlier value.\n", line);
				}
				strncpy(p_misc_config->kiss_serial_port, t, sizeof(p_misc_config->kiss_serial_port));
				p_misc_config->kiss_serial_speed = 0;
				p_misc_config->kiss_serial_poll = 1; // set polling.
			}
		}

		/*
		 * KISSCOPY 		- Data from network KISS client is copied to all others.
		 *			  This does not apply to pseudo terminal KISS.
		 */

		else if (strcasecmp(t, "KISSCOPY") == 0)
		{
			p_misc_config->kiss_copy = 1;
		}

		/*
		 * Invalid command.
		 */
		else
		{

			printf("Config file: Unrecognized command '%s' on line %d.\n", t, line);
		}
	}

	fclose(fp);
} /* end config_init */

/* end config.c */
