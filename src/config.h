
/*----------------------------------------------------------------------------
 *
 * Name:	config.h
 *
 * Purpose:
 *
 * Description:
 *
 *-----------------------------------------------------------------------------*/

#ifndef CONFIG_H
#define CONFIG_H 1

#include "audio.h" /* for struct audio_s */

/*
 * All the leftovers.
 * This wasn't thought out.  It just happened.
 */

enum sendto_type_e
{
	SENDTO_XMIT,
	SENDTO_RECV
};

#define MAX_BEACONS 30
#define MAX_KISS_TCP_PORTS (MAX_CHANS + 1)

struct misc_config_s
{
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

	int kiss_port[MAX_KISS_TCP_PORTS]; /* TCP Port number for the "TCP KISS" protocol. */
	int kiss_chan[MAX_KISS_TCP_PORTS]; /* Radio Channel number for this port or -1 for all.  */

	int kiss_copy;		/* Data from network KISS client is copied to all others. */
	int enable_kiss_pt; /* Enable pseudo terminal for KISS. */
						/* Want this to be off by default because it hangs */
						/* after a while if nothing is reading from other end. */

	char kiss_serial_port[20];
	/* Serial port name for our end of the */
	/* virtual null modem for native Windows apps. */
	/* Version 1.5 add same capability for Linux. */

	int kiss_serial_speed; /* Speed, in bps, for the KISS serial port. */
						   /* If 0, just leave what was already there. */

	int kiss_serial_poll; /* When using Bluetooth KISS, the /dev/rfcomm0 device */
						  /* will appear and disappear as the remote application */
						  /* opens and closes the virtual COM port. */
						  /* When this is non-zero, we will check periodically to */
						  /* see if the device has appeared and we will open it. */

	int log_daily_names; /* True to generate new log file each day. */

	char log_path[80]; /* Either directory or full file name depending on above. */
};

#define MIN_IP_PORT_NUMBER 1024
#define MAX_IP_PORT_NUMBER 49151

#define DEFAULT_KISS_PORT 8001 /* Above plus 1. */

#define DEFAULT_NULLMODEM "COM3" /* should be equiv. to /dev/ttyS2 on Cygwin */

extern void config_init(char *fname, struct audio_s *p_modem,
						struct misc_config_s *misc_config);

#endif /* CONFIG_H */

/* end config.h */
