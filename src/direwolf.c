//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2019, 2020, 2021, 2023  John Langner, WB2OSZ
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
 * Module:      direwolf.c
 *
 * Purpose:   	Main program for "Dire Wolf" which includes:
 *
 *			Various DSP modems using the "sound card."
 *			AX.25 encoder/decoder.
 *			APRS data encoder / decoder.
 *			APRS digipeater.
 *			KISS TNC emulator.
 *			APRStt (touch tone input) gateway
 *			Internet Gateway (IGate)
 *			Ham Radio of Things - IoT with Ham Radio
 *			FX.25 Forward Error Correction.
 *			IL2P Forward Error Correction.
 *			Emergency Alert System (EAS) Specific Area Message Encoding (SAME) receiver.
 *			AIS receiver for tracking ships.
 *
 *---------------------------------------------------------------*/

#define DIREWOLF_C 1

#include "direwolf.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <ctype.h>

#if __ARM__
// #include <asm/hwcap.h>
// #include <sys/auxv.h>		// Doesn't seem to be there.
//  We have libc 2.13.  Looks like we might need 2.17 & gcc 4.8
#endif

#if __WIN32__
#include <stdio.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#if USE_SNDIO || __APPLE__
// no need to include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#if USE_HAMLIB
#include <hamlib/rig.h>
#endif

#include "version.h"
#include "audio.h"
#include "config.h"
#include "multi_modem.h"
#include "demod.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "ax25_pad.h"
#include "kiss.h"
#include "kissnet.h"
#include "kiss_frame.h"
#include "gen_tone.h"
#include "tq.h"
#include "xmit.h"
#include "ptt.h"
#include "recv.h"
#include "fx25.h"
#include "dwsock.h"
#include "dlq.h" // for fec_type_t definition.

// static int idx_decoded = 0;

#if __WIN32__
static BOOL cleanup_win(int);
#else
static void cleanup_linux(int);
#endif

static void usage();

#if defined(__SSE__) && !defined(__APPLE__)

static void __cpuid(int cpuinfo[4], int infotype)
{
	__asm__ __volatile__(
		"cpuid" : "=a"(cpuinfo[0]),
				  "=b"(cpuinfo[1]),
				  "=c"(cpuinfo[2]),
				  "=d"(cpuinfo[3]) : "a"(infotype));
}

#endif

/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Main program for packet radio virtual TNC.
 *
 * Inputs:	Command line arguments.
 *		See usage message for details.
 *
 * Outputs:	Decoded information is written to stdout.
 *
 *		A socket and pseudo terminal are created for
 *		for communication with other applications.
 *
 *--------------------------------------------------------------------*/

static struct audio_s audio_config;
static struct misc_config_s misc_config;

static const int audio_amplitude = 100; /* % of audio sample range. */
										/* This translates to +-32k for 16 bit samples. */
										/* Currently no option to change this. */

static int d_u_opt = 0; /* "-d u" command line option to print UTF-8 also in hexadecimal. */
static int d_p_opt = 0; /* "-d p" option for dumping packets over radio. */

static int q_h_opt = 0; /* "-q h" Quiet, suppress the "heard" line with audio level. */
static int q_d_opt = 0; /* "-q d" Quiet, suppress the printing of decoded of APRS packets. */

static int A_opt_ais_to_obj = 0; /* "-A" Convert received AIS to APRS "Object Report." */

int main(int argc, char *argv[])
{
	int err;
	char config_file[100];
	int enable_pseudo_terminal = 0;
	int r_opt = 0, n_opt = 0, b_opt = 0, B_opt = 0, D_opt = 0, U_opt = 0; /* Command line options. */
	char P_opt[16];
	char l_opt_logdir[80];
	char L_opt_logfile[80];
	char input_file[80];

	int a_opt = 0; /* "-a n" interval, in seconds, for audio statistics report.  0 for none. */

	int d_n_opt = 0; /* "-d n" option for Network KISS.  Can be repeated for more detail. */
	int d_o_opt = 0; /* "-d o" option for output control such as PTT and DCD. */
#if USE_HAMLIB
	int d_h_opt = 0; /* "-d h" option for hamlib debugging.  Repeat for more detail */
#endif
	int d_x_opt = 1; /* "-d x" option for FX.25.  Default minimal. Repeat for more detail.  -qx to silence. */

	int E_tx_opt = 0; /* "-E n" Error rate % for clobbering transmit frames. */
	int E_rx_opt = 0; /* "-E Rn" Error rate % for clobbering receive frames. */

	float e_recv_ber = 0.0;		/* Receive Bit Error Rate (BER). */
	int X_fx25_xmit_enable = 0; /* FX.25 transmit enable. */

	char x_opt_mode = ' '; /* "-x N" option for transmitting calibration tones. */
	int x_opt_chan = 0;	   /* Split into 2 parts.  Mode e.g.  m, a, and optional channel. */

	strlcpy(l_opt_logdir, "", sizeof(l_opt_logdir));
	strlcpy(L_opt_logfile, "", sizeof(L_opt_logfile));
	strlcpy(P_opt, "", sizeof(P_opt));

#if __WIN32__

	// Select UTF-8 code page for console output.
	// http://msdn.microsoft.com/en-us/library/windows/desktop/ms686036(v=vs.85).aspx
	// This is the default I see for windows terminal:
	// >chcp
	// Active code page: 437

	// Restore on exit? oldcp = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);

#else

	/*
	 * Default on Raspian & Ubuntu Linux is fine.  Don't know about others.
	 *
	 * Should we look at LANG environment variable and issue a warning
	 * if it doesn't look something like  en_US.UTF-8 ?
	 */

#endif

	// TODO: control development/beta/release by version.h instead of changing here.
	// Print platform.  This will provide more information when people send a copy the information displayed.

	// Might want to print OS version here.   For Windows, see:
	// https://msdn.microsoft.com/en-us/library/ms724451(v=VS.85).aspx

	// printf ("Dire Wolf version %d.%d (%s) BETA TEST 7\n", MAJOR_VERSION, MINOR_VERSION, __DATE__);
	// printf ("Dire Wolf DEVELOPMENT version %d.%d %s (%s)\n", MAJOR_VERSION, MINOR_VERSION, "G", __DATE__);
	printf("Dire Wolf version %d.%d\n", MAJOR_VERSION, MINOR_VERSION);

#if defined(USE_HAMLIB) || defined(USE_CM108)
	printf("Includes optional support for: ");
#if defined(USE_HAMLIB)
	printf(" hamlib");
#endif
#if defined(USE_CM108)
	printf(" cm108-ptt");
#endif
	printf("\n");
#endif

#if __WIN32__
	// setlinebuf (stdout);   setvbuf???
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)cleanup_win, TRUE);
#else
	setlinebuf(stdout);
	signal(SIGINT, cleanup_linux);
#endif

	/*
	 * Starting with version 0.9, the prebuilt Windows version
	 * requires a minimum of a Pentium 3 or equivalent so we can
	 * use the SSE instructions.
	 * Try to warn anyone using a CPU from the previous
	 * century rather than just dying for no apparent reason.
	 *
	 * Apple computers with Intel processors started with P6. Since the
	 * cpu test code was giving Clang compiler grief it has been excluded.
	 *
	 * Version 1.6: Newer compiler with i686, rather than i386 target.
	 * This is running about 10% faster for the same hardware so it would
	 * appear the compiler is using newer, more efficient, instructions.
	 *
	 * According to https://en.wikipedia.org/wiki/P6_(microarchitecture)
	 * and https://en.wikipedia.org/wiki/Streaming_SIMD_Extensions
	 * the Pentium III still seems to be the minimum required because
	 * it has the P6 microarchitecture and SSE instructions.
	 *
	 * I've never heard any complaints about anyone getting the message below.
	 */

#if defined(__SSE__) && !defined(__APPLE__)
	int cpuinfo[4]; // EAX, EBX, ECX, EDX
	__cpuid(cpuinfo, 0);
	if (cpuinfo[0] >= 1)
	{
		__cpuid(cpuinfo, 1);
		// printf ("debug: cpuinfo = %x, %x, %x, %x\n", cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);
		//  https://en.wikipedia.org/wiki/CPUID
		if (!(cpuinfo[3] & (1 << 25)))
		{

			printf("------------------------------------------------------------------\n");
			printf("This version requires a minimum of a Pentium 3 or equivalent.\n");
			printf("If you are seeing this message, you are probably using a computer\n");
			printf("from the previous Century.  See instructions in User Guide for\n");
			printf("information on how you can compile it for use with your antique.\n");
			printf("------------------------------------------------------------------\n");
		}
	}

#endif

	// I've seen many references to people running this as root.
	// There is no reason to do that.
	// Ordinary users can access audio, gpio, etc. if they are in the correct groups.
	// Giving an applications permission to do things it does not need to do
	// is a huge security risk.

#ifndef __WIN32__
	if (getuid() == 0 || geteuid() == 0)
	{

		for (int n = 0; n < 15; n++)
		{
			printf("\n");
			printf("Dire Wolf requires only privileges available to ordinary users.\n");
			printf("Running this as root is an unnecessary security risk.\n");
			// SLEEP_SEC(1);
		}
	}
#endif

	/*
	 * Default location of configuration file is current directory.
	 * Can be overridden by -c command line option.
	 * TODO:  Automatically search other places.
	 */

	strlcpy(config_file, "direwolf.conf", sizeof(config_file));

	/*
	 * Look at command line options.
	 * So far, the only one is the configuration file location.
	 */

	strlcpy(input_file, "", sizeof(input_file));
	while (1)
	{
		// int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		int c;
		char *p;
		static struct option long_options[] = {
			{"future1", 1, 0, 0},
			{"future2", 0, 0, 0},
			{"future3", 1, 0, 'c'},
			{0, 0, 0, 0}};

		/* ':' following option character means arg is required. */

		c = getopt_long(argc, argv, "hP:B:gjJD:U:c:px:r:b:n:d:q:t:ul:L:Sa:E:T:e:X:AI:i:",
						long_options, &option_index);
		if (c == -1)
			break;

		switch (c)
		{

		case 0: /* possible future use */

			printf("option %s", long_options[option_index].name);
			if (optarg)
			{
				printf(" with arg %s", optarg);
			}
			printf("\n");
			break;

		case 'a': /* -a for audio statistics interval */

			a_opt = atoi(optarg);
			if (a_opt < 0)
				a_opt = 0;
			if (a_opt < 10)
			{

				printf("Setting such a small audio statistics interval will produce inaccurate sample rate display.\n");
			}
			break;

		case 'c': /* -c for configuration file name */

			strlcpy(config_file, optarg, sizeof(config_file));
			break;

#if __WIN32__
#else
		case 'p': /* -p enable pseudo terminal */

			/* We want this to be off by default because it hangs */
			/* eventually when nothing is reading from other side. */

			enable_pseudo_terminal = 1;
			break;
#endif

		case 'B': /* -B baud rate and modem properties. */
				  /* Also implies modem type based on speed. */
				  /* Special case "AIS" rather than number. */
			if (strcasecmp(optarg, "AIS") == 0)
			{
				B_opt = 12345; // See special case below.
			}
			else if (strcasecmp(optarg, "EAS") == 0)
			{
				B_opt = 23456; // See special case below.
			}
			else
			{
				B_opt = atoi(optarg);
			}
			if (B_opt < MIN_BAUD || B_opt > MAX_BAUD)
			{

				printf("Use a more reasonable data baud rate in range of %d - %d.\n", MIN_BAUD, MAX_BAUD);
				exit(EXIT_FAILURE);
			}
			break;

		case 'P': /* -P for modem profile. */

			// debug: printf ("Demodulator profile set to \"%s\"\n", optarg);
			strlcpy(P_opt, optarg, sizeof(P_opt));
			break;

		case 'D': /* -D divide AFSK demodulator sample rate */

			D_opt = atoi(optarg);
			if (D_opt < 1 || D_opt > 8)
			{

				printf("Crazy value for -D. \n");
				exit(EXIT_FAILURE);
			}
			break;

		case 'U': /* -U multiply G3RUH demodulator sample rate (upsample) */

			U_opt = atoi(optarg);
			if (U_opt < 1 || U_opt > 4)
			{

				printf("Crazy value for -U. \n");
				exit(EXIT_FAILURE);
			}
			break;

		case 'x': /* -x N for transmit calibration tones. */
				  /* N is composed of a channel number and/or one letter */
				  /* for the mode: mark, space, alternate, ptt-only. */

			for (char *p = optarg; *p != '\0'; p++)
			{
				switch (*p)
				{
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					x_opt_chan = x_opt_chan * 10 + *p - '0';
					if (x_opt_mode == ' ')
						x_opt_mode = 'a';
					break;
				case 'a':
					x_opt_mode = *p;
					break; // Alternating tones
				case 'm':
					x_opt_mode = *p;
					break; // Mark tone
				case 's':
					x_opt_mode = *p;
					break; // Space tone
				case 'p':
					x_opt_mode = *p;
					break; // Set PTT only
				default:

					printf("Invalid option '%c' for -x. Must be a, m, s, or p.\n", *p);

					exit(EXIT_FAILURE);
					break;
				}
			}
			if (x_opt_chan < 0 || x_opt_chan >= MAX_CHANS)
			{

				printf("Invalid channel %d for -x. \n", x_opt_chan);

				exit(EXIT_FAILURE);
			}
			break;

		case 'r': /* -r audio samples/sec.  e.g. 44100 */

			r_opt = atoi(optarg);
			if (r_opt < MIN_SAMPLES_PER_SEC || r_opt > MAX_SAMPLES_PER_SEC)
			{

				printf("-r option, audio samples/sec, is out of range.\n");
				r_opt = 0;
			}
			break;

		case 'n': /* -n number of audio channels for first audio device.  1 or 2. */

			n_opt = atoi(optarg);
			if (n_opt < 1 || n_opt > 2)
			{

				printf("-n option, number of audio channels, is out of range.\n");
				n_opt = 0;
			}
			break;

		case 'b': /* -b bits per sample.  8 or 16. */

			b_opt = atoi(optarg);
			if (b_opt != 8 && b_opt != 16)
			{

				printf("-b option, bits per sample, must be 8 or 16.\n");
				b_opt = 0;
			}
			break;

		case 'h': // -h for help
		case '?':

			/* For '?' unknown option message was already printed. */
			usage();
			break;

		case 'd': /* Set debug option. */

			/* New in 1.1.  Can combine multiple such as "-d pkk" */

			for (p = optarg; *p != '\0'; p++)
			{
				switch (*p)
				{
				case 'n':
					d_n_opt++;
					kiss_net_set_debug(d_n_opt);
					break;

				case 'u':
					d_u_opt = 1;
					break;

				case 'p':
					d_p_opt = 1;
					break; // TODO: packet dump for xmit side.
				case 'o':
					d_o_opt++;
					ptt_set_debug(d_o_opt);
					break;
#if AX25MEMDEBUG
				case 'l':
					ax25memdebug_set();
					break; // Track down memory Leak.  Not documented.
#endif					   // Previously 'm' but that is now used for mheard.
#if USE_HAMLIB
				case 'h':
					d_h_opt++;
					break; // Hamlib verbose level.
#endif
				case 'x':
					d_x_opt++;
					break; // FX.25
				default:
					break;
				}
			}
			break;

		case 'q': /* Set quiet option. */

			/* New in 1.2.  Quiet option to suppress some types of printing. */
			/* Can combine multiple such as "-q hd" */

			for (p = optarg; *p != '\0'; p++)
			{
				switch (*p)
				{
				case 'h':
					q_h_opt = 1;
					break;
				case 'd':
					q_d_opt = 1;
					break;
				case 'x':
					d_x_opt = 0;
					break; // Defaults to minimal info.  This silences.
				default:
					break;
				}
			}
			break;

		case 't': /* Was handled earlier. */
			break;

		case 'u': /* Print UTF-8 test and exit. */

			printf("\n  UTF-8 test string: ma%c%cana %c%c F%c%c%c%ce\n\n",
				   0xc3, 0xb1,
				   0xc2, 0xb0,
				   0xc3, 0xbc, 0xc3, 0x9f);

			exit(0);
			break;

		case 'l': /* -l for log directory with daily files */

			strlcpy(l_opt_logdir, optarg, sizeof(l_opt_logdir));
			break;

		case 'L': /* -L for log file name with full path */

			strlcpy(L_opt_logfile, optarg, sizeof(L_opt_logfile));
			break;

		case 'E': /* -E Error rate (%) for corrupting frames. */
				  /* Just a number is transmit.  Precede by R for receive. */

			if (*optarg == 'r' || *optarg == 'R')
			{
				E_rx_opt = atoi(optarg + 1);
				if (E_rx_opt < 1 || E_rx_opt > 99)
				{

					printf("-ER must be in range of 1 to 99.\n");
					E_rx_opt = 10;
				}
			}
			else
			{
				E_tx_opt = atoi(optarg);
				if (E_tx_opt < 1 || E_tx_opt > 99)
				{

					printf("-E must be in range of 1 to 99.\n");
					E_tx_opt = 10;
				}
			}
			break;

		case 'e': /* -e Receive Bit Error Rate (BER). */

			e_recv_ber = atof(optarg);
			break;

		case 'X':

			X_fx25_xmit_enable = atoi(optarg);
			break;

		case 'A': // -A 	convert AIS to APRS object

			A_opt_ais_to_obj = 1;
			break;

		default:

			/* Should not be here. */

			printf("?? getopt returned character code 0%o ??\n", c);
			usage();
		}
	} /* end while(1) for options */

	if (optind < argc)
	{

		if (optind < argc - 1)
		{

			printf("Warning: File(s) beyond the first are ignored.\n");
		}

		strlcpy(input_file, argv[optind], sizeof(input_file));
	}

	/*
	 * Get all types of configuration settings from configuration file.
	 *
	 * Possibly override some by command line options.
	 */

#if USE_HAMLIB
	rig_set_debug(d_h_opt);
#endif

	(void)dwsock_init();

	config_init(config_file, &audio_config, &misc_config);

	if (r_opt != 0)
	{
		audio_config.adev[0].samples_per_sec = r_opt;
	}

	if (n_opt != 0)
	{
		audio_config.adev[0].num_channels = n_opt;
		if (n_opt == 2)
		{
			audio_config.chan_medium[1] = MEDIUM_RADIO;
		}
	}

	if (b_opt != 0)
	{
		audio_config.adev[0].bits_per_sample = b_opt;
	}

	if (B_opt != 0)
	{
		audio_config.achan[0].baud = B_opt;

		/* We have similar logic in direwolf.c, config.c, gen_packets.c, and atest.c, */
		/* that need to be kept in sync.  Maybe it could be a common function someday. */

		if (audio_config.achan[0].baud < 600)
		{
			audio_config.achan[0].modem_type = MODEM_AFSK;
			audio_config.achan[0].mark_freq = 1600; // Typical for HF SSB.
			audio_config.achan[0].space_freq = 1800;
			audio_config.achan[0].decimate = 3; // Reduce CPU load.
		}
		else if (audio_config.achan[0].baud < 1800)
		{
			audio_config.achan[0].modem_type = MODEM_AFSK;
			audio_config.achan[0].mark_freq = DEFAULT_MARK_FREQ;
			audio_config.achan[0].space_freq = DEFAULT_SPACE_FREQ;
		}
	}

	audio_config.statistics_interval = a_opt;

	if (strlen(P_opt) > 0)
	{
		/* -P for modem profile. */
		strlcpy(audio_config.achan[0].profiles, P_opt, sizeof(audio_config.achan[0].profiles));
	}

	if (D_opt != 0)
	{
		// Reduce audio sampling rate to reduce CPU requirements.
		audio_config.achan[0].decimate = D_opt;
	}

	if (U_opt != 0)
	{
		// Increase G3RUH audio sampling rate to improve performance.
		// The value is normally determined automatically based on audio
		// sample rate and baud.  This allows override for experimentation.
		audio_config.achan[0].upsample = U_opt;
	}

	// temp - only xmit errors.

	audio_config.xmit_error_rate = E_tx_opt;
	audio_config.recv_error_rate = E_rx_opt;

	if (strlen(l_opt_logdir) > 0 && strlen(L_opt_logfile) > 0)
	{

		printf("Logging options -l and -L can't be used together.  Pick one or the other.\n");
		exit(1);
	}

	if (strlen(L_opt_logfile) > 0)
	{
		misc_config.log_daily_names = 0;
		strlcpy(misc_config.log_path, L_opt_logfile, sizeof(misc_config.log_path));
	}
	else if (strlen(l_opt_logdir) > 0)
	{
		misc_config.log_daily_names = 1;
		strlcpy(misc_config.log_path, l_opt_logdir, sizeof(misc_config.log_path));
	}

	misc_config.enable_kiss_pt = enable_pseudo_terminal;

	if (strlen(input_file) > 0)
	{

		strlcpy(audio_config.adev[0].adevice_in, input_file, sizeof(audio_config.adev[0].adevice_in));
	}

	audio_config.recv_ber = e_recv_ber;

	if (X_fx25_xmit_enable > 0)
	{
		audio_config.achan[0].fx25_strength = X_fx25_xmit_enable;
		audio_config.achan[0].layer2_xmit = LAYER2_FX25;
	}

	/*
	 * Open the audio source
	 *	- soundcard
	 *	- stdin
	 *	- UDP
	 * Files not supported at this time.
	 * Can always "cat" the file and pipe it into stdin.
	 */

	err = audio_open(&audio_config);
	if (err < 0)
	{

		printf("Pointless to continue without audio device.\n");
		usage();
		exit(1);
	}

	/*
	 * Initialize the demodulator(s) and layer 2 decoder (HDLC, IL2P).
	 */
	multi_modem_init(&audio_config);
	fx25_init(d_x_opt);

	gen_tone_init(&audio_config, audio_amplitude);

	assert(audio_config.adev[0].bits_per_sample == 8 || audio_config.adev[0].bits_per_sample == 16);
	assert(audio_config.adev[0].num_channels == 1 || audio_config.adev[0].num_channels == 2);
	assert(audio_config.adev[0].samples_per_sec >= MIN_SAMPLES_PER_SEC && audio_config.adev[0].samples_per_sec <= MAX_SAMPLES_PER_SEC);

	/*
	 * Initialize the transmit queue.
	 */

	xmit_init(&audio_config, d_p_opt);

	/*
	 * If -x N option specified, transmit calibration tones for transmitter
	 * audio level adjustment, up to 1 minute then quit.
	 * a: Alternating mark/space tones
	 * m: Mark tone (e.g. 1200Hz)
	 * s: Space tone (e.g. 2200Hz)
	 * p: Set PTT only.
	 * A leading or trailing number is the channel.
	 */

	if (x_opt_mode != ' ')
	{
		if (audio_config.chan_medium[x_opt_chan] == MEDIUM_RADIO)
		{
			if (audio_config.achan[x_opt_chan].mark_freq && audio_config.achan[x_opt_chan].space_freq)
			{
				int max_duration = 60;
				int n = audio_config.achan[x_opt_chan].baud * max_duration;

				ptt_set(OCTYPE_PTT, x_opt_chan, 1);

				switch (x_opt_mode)
				{
				default:
				case 'a': // Alternating tones: -x a
					printf("\nSending alternating mark/space calibration tones (%d/%dHz) on channel %d.\nPress control-C to terminate.\n",
						   audio_config.achan[x_opt_chan].mark_freq,
						   audio_config.achan[x_opt_chan].space_freq,
						   x_opt_chan);
					while (n-- > 0)
					{
						tone_gen_put_bit(x_opt_chan, n & 1);
					}
					break;
				case 'm': // "Mark" tone: -x m
					printf("\nSending mark calibration tone (%dHz) on channel %d.\nPress control-C to terminate.\n",
						   audio_config.achan[x_opt_chan].mark_freq,
						   x_opt_chan);
					while (n-- > 0)
					{
						tone_gen_put_bit(x_opt_chan, 1);
					}
					break;
				case 's': // "Space" tone: -x s
					printf("\nSending space calibration tone (%dHz) on channel %d.\nPress control-C to terminate.\n",
						   audio_config.achan[x_opt_chan].space_freq,
						   x_opt_chan);
					while (n-- > 0)
					{
						tone_gen_put_bit(x_opt_chan, 0);
					}
					break;
				case 'p': // Silence - set PTT only: -x p
					printf("\nSending silence (Set PTT only) on channel %d.\nPress control-C to terminate.\n", x_opt_chan);
					SLEEP_SEC(max_duration);
					break;
				}

				ptt_set(OCTYPE_PTT, x_opt_chan, 0);

				exit(EXIT_SUCCESS);
			}
			else
			{

				printf("\nMark/Space frequencies not defined for channel %d. Cannot calibrate using this modem type.\n", x_opt_chan);

				exit(EXIT_FAILURE);
			}
		}
		else
		{

			printf("\nChannel %d is not configured as a radio channel.\n", x_opt_chan);

			exit(EXIT_FAILURE);
		}
	}

	/*
	 * Provide KISS socket interface for use by a client application.
	 */
	kissnet_init(&misc_config);

	/*
	 * Create a pseudo terminal and KISS TNC emulator.
	 */
	kisspt_init(&misc_config);
	kiss_frame_init(&audio_config);

	/*
	 * Get sound samples and decode them.
	 * Use hot attribute for all functions called for every audio sample.
	 */
	recv_init(&audio_config);
	recv_process();

	exit(EXIT_SUCCESS);
}

/*-------------------------------------------------------------------
 *
 * Name:        app_process_rec_frame
 *
 * Purpose:     This is called when we receive a frame with a valid
 *		FCS and acceptable size.
 *
 * Inputs:	chan	- Audio channel number, 0 or 1.
 *		subchan	- Which modem caught it.
 *			  Special case -1 for DTMF decoder.
 *		slice	- Slicer which caught it.
 *		pp	- Packet handle.
 *		alevel	- Audio level, range of 0 - 100.
 *				(Special case, use negative to skip
 *				 display of audio level line.
 *				 Use -2 to indicate DTMF message.)
 *		retries	- Level of bit correction used.
 *		spectrum - Display of how well multiple decoders did.
 *
 *
 * Description:	Print decoded packet.
 *		Optionally send to another application.
 *
 *--------------------------------------------------------------------*/

// TODO:  Use only one printf per line so output doesn't get jumbled up with stuff from other threads.

void app_process_rec_packet(int chan, int subchan, int slice, packet_t pp, alevel_t alevel, fec_type_t fec_type, retry_t retries, char *spectrum)
{

	char stemp[500];
	unsigned char *pinfo;
	int info_len;
	char heard[AX25_MAX_ADDR_LEN];
	// int j;
	int h;
	char display_retries[32]; // Extra stuff before slice indicators.
							  // Can indicate FX.25/IL2P or fix_bits.

	assert(chan >= 0 && chan < MAX_TOTAL_CHANS); // TOTAL for virtual channels
	assert(subchan >= -2 && subchan < MAX_SUBCHANS);
	assert(slice >= 0 && slice < MAX_SLICERS);
	assert(pp != NULL); // 1.1J+

	strlcpy(display_retries, "", sizeof(display_retries));

	switch (fec_type)
	{
	case fec_type_fx25:
		strlcpy(display_retries, " FX.25 ", sizeof(display_retries));
		break;
	case fec_type_none:
	default:
		// Possible fix_bits indication.
		if (audio_config.achan[chan].fix_bits != RETRY_NONE || audio_config.achan[chan].passall)
		{
			assert(retries >= RETRY_NONE && retries <= RETRY_MAX);
			snprintf(display_retries, sizeof(display_retries), " [%s] ", retry_text[(int)retries]);
		}
		break;
	}

	ax25_format_addrs(pp, stemp);

	info_len = ax25_get_info(pp, &pinfo);

	/* Print so we can see what is going on. */

	/* Display audio input level. */
	/* Who are we hearing?   Original station or digipeater. */

	if (ax25_get_num_addr(pp) == 0)
	{
		/* Not AX.25. No station to display below. */
		h = -1;
		strlcpy(heard, "", sizeof(heard));
	}
	else
	{
		h = ax25_get_heard(pp);
		ax25_get_addr_with_ssid(pp, h, heard);
	}

	printf("\n");

	// The HEARD line.

	if ((!q_h_opt) && alevel.rec >= 0)
	{	/* suppress if "-q h" option */
		// FIXME: rather than checking for ichannel, how about checking medium==radio
		if (h != -1 && h != AX25_SOURCE)
		{
			printf("Digipeater ");
		}

		char alevel_text[AX25_ALEVEL_TO_TEXT_SIZE];

		ax25_alevel_to_text(alevel, alevel_text);

		/* As suggested by KJ4ERJ, if we are receiving from */
		/* WIDEn-0, it is quite likely (but not guaranteed), that */
		/* we are actually hearing the preceding station in the path. */

		if (h >= AX25_REPEATER_2 &&
			strncmp(heard, "WIDE", 4) == 0 &&
			isdigit(heard[4]) &&
			heard[5] == '\0')
		{

			char probably_really[AX25_MAX_ADDR_LEN];

			ax25_get_addr_with_ssid(pp, h - 1, probably_really);

			printf("%s (probably %s) audio level = %s  %s  %s\n", heard, probably_really, alevel_text, display_retries, spectrum);
		}
		else if (strcmp(heard, "DTMF") == 0)
		{

			printf("%s audio level = %s  tt\n", heard, alevel_text);
		}
		else
		{

			printf("%s audio level = %s  %s  %s\n", heard, alevel_text, display_retries, spectrum);
		}
	}

	/* Version 1.2:   Cranking the input level way up produces 199. */
	/* Keeping it under 100 gives us plenty of headroom to avoid saturation. */

	// TODO:  suppress this message if not using soundcard input.
	// i.e. we have no control over the situation when using SDR.

	if (alevel.rec > 110)
	{

		printf("Audio input level is too high.  Reduce so most stations are around 50.\n");
	}
	else if (alevel.rec < 5)
	{

		printf("Audio input level is too low.  Increase so most stations are around 50.\n");
	}

	if (ax25_is_aprs(pp))
	{
	}
	else
	{
	}

	if (audio_config.achan[chan].num_subchan > 1 && audio_config.achan[chan].num_slicers == 1)
	{
		printf("[%d.%d] ", chan, subchan);
	}
	else if (audio_config.achan[chan].num_subchan == 1 && audio_config.achan[chan].num_slicers > 1)
	{
		printf("[%d.%d] ", chan, slice);
	}
	else if (audio_config.achan[chan].num_subchan > 1 && audio_config.achan[chan].num_slicers > 1)
	{
		printf("[%d.%d.%d] ", chan, subchan, slice);
	}
	else
	{
		printf("[%d] ", chan);
	}

	printf("%s", stemp); /* stations followed by : */
	ax25_safe_print((char *)pinfo, info_len, (!ax25_is_aprs(pp)) && (!d_u_opt));
	printf("\n");

	// Also display in pure ASCII if non-ASCII characters and "-d u" option specified.

	if (d_u_opt)
	{

		unsigned char *p;
		int n = 0;

		for (p = pinfo; *p != '\0'; p++)
		{
			if (*p >= 0x80)
				n++;
		}

		if (n > 0)
		{

			ax25_safe_print((char *)pinfo, info_len, 1);
			printf("\n");
		}
	}

	/* Optional hex dump of packet. */

	if (d_p_opt)
	{

		printf("------\n");
		ax25_hex_dump(pp);
		printf("------\n");
	}

	/* Send to another application if connected. */
	// TODO:  Put a wrapper around this so we only call one function to send by all methods.
	// We see the same sequence in tt_user.c.

	int flen;
	unsigned char fbuf[AX25_MAX_PACKET_LEN];

	flen = ax25_pack(pp, fbuf);

	kissnet_send_rec_packet(chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1); // KISS TCP
	kisspt_send_rec_packet(chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1);  // KISS pseudo terminal

} /* end app_process_rec_packet */

/* Process control C and window close events. */

#if __WIN32__

static BOOL cleanup_win(int ctrltype)
{
	if (ctrltype == CTRL_C_EVENT || ctrltype == CTRL_CLOSE_EVENT)
	{

		printf("\nQRT\n");
		ptt_term();
		ExitProcess(0);
	}
	return (TRUE);
}

#else

static void cleanup_linux()
{
	printf("\nQRT\n");
	ptt_term();
	exit(0);
}

#endif

static void usage()
{

	printf("\n");
	printf("Dire Wolf version %d.%d\n", MAJOR_VERSION, MINOR_VERSION);
	printf("\n");
	printf("Usage: direwolf [options] [ - | stdin | UDP:nnnn ]\n");
	printf("Options:\n");
	printf("    -c fname       Configuration file name.\n");
	printf("    -l logdir      Directory name for log files.  Use . for current.\n");
	printf("    -r n           Audio sample rate, per sec.\n");
	printf("    -n n           Number of audio channels, 1 or 2.\n");
	printf("    -b n           Bits per audio sample, 8 or 16.\n");
	printf("    -B n           Data rate in bits/sec for channel 0.  Standard values are 300 are 1200.\n");
	printf("                     300 bps defaults to AFSK tones of 1600 & 1800.\n");
	printf("                     1200 bps uses AFSK tones of 1200 & 2200.\n");
	printf("    -P xxx         Modem Profiles.\n");
	printf("    -D n           Divide audio sample rate by n for channel 0.\n");
	printf("    -X n           1 to enable FX.25 transmit.  16, 32, 64 for specific number of check bytes.\n");
	printf("    -d             Debug options:\n");
	printf("       k             k = KISS serial port or pseudo terminal client.\n");
	printf("       n             n = KISS network client.\n");
	printf("       u             u = Display non-ASCII text in hexadecimal.\n");
	printf("       p             p = dump Packets in hexadecimal.\n");
	printf("       o             o = output controls such as PTT and DCD.\n");
#if USE_HAMLIB
	printf("       h             h = hamlib increase verbose level.\n");
#endif
	printf("       x             x = FX.25 increase verbose level.\n");
	printf("    -q             Quiet (suppress output) options:\n");
	printf("       h             h = Heard line with the audio level.\n");
	printf("       d             d = Decoding of APRS packets.\n");
	printf("       x             x = Silence FX.25 information.\n");
	printf("    -t n           Text colors.  0=disabled. 1=default.  2,3,4,... alternatives.\n");
	printf("                     Use 9 to test compatibility with your terminal.\n");
	printf("    -a n           Audio statistics interval in seconds.  0 to disable.\n");
#if __WIN32__
#else
	printf("    -p             Enable pseudo terminal for KISS protocol.\n");
#endif
	printf("    -x             Send Xmit level calibration tones.\n");
	printf("       a             a = Alternating mark/space tones.\n");
	printf("       m             m = Steady mark tone (e.g. 1200Hz).\n");
	printf("       s             s = Steady space tone (e.g. 2200Hz).\n");
	printf("       p             p = Silence (Set PTT only).\n");
	printf("        chan          Optionally add a number to specify radio channel.\n");
	printf("    -u             Print UTF-8 test string and exit.\n");
	printf("    -S             Print symbol tables and exit.\n");
	printf("    -T fmt         Time stamp format for sent and received frames.\n");
	printf("    -e ber         Receive Bit Error Rate (BER), e.g. 1e-5\n");
	printf("\n");

	printf("After any options, there can be a single command line argument for the source of\n");
	printf("received audio.  This can override the audio input specified in the configuration file.\n");
	printf("\n");

#if __WIN32__
	printf("Documentation can be found in the 'doc' folder\n");
#else
	// TODO: Could vary by platform and build options.
	printf("Documentation can be found in /usr/local/share/doc/direwolf\n");
#endif
	printf("or online at https://github.com/wb2osz/direwolf/tree/master/doc\n");
	printf("additional topics: https://github.com/wb2osz/direwolf-doc\n");

	exit(EXIT_FAILURE);
}

/* end direwolf.c */
