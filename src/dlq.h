
/*------------------------------------------------------------------
 *
 * Module:      dlq.h
 *
 *---------------------------------------------------------------*/

#ifndef DLQ_H
#define DLQ_H 1

#include "ax25_pad.h"
#include "audio.h"


/* A transmit or receive data block for connected mode. */

typedef struct cdata_s {
	int magic;			/* For integrity checking. */

#define TXDATA_MAGIC 0x09110911

	struct cdata_s *next;		/* Pointer to next when part of a list. */

	int pid;			/* Protocol id. */

	int size;			/* Number of bytes allocated. */

	int len;			/* Number of bytes actually used. */

	char data[];			/* Variable length data. */

} cdata_t;



typedef enum fec_type_e {fec_type_none=0, fec_type_fx25=1} fec_type_t;


/* A queue item. */

// TODO: call this event rather than item.
// TODO: should add fences.

typedef struct dlq_item_s {

	struct dlq_item_s *nextp;	/* Next item in queue. */

	int chan;			/* Radio channel of origin. */

// I'm not worried about amount of memory used but this might be a
// little clearer if a union was used for the different event types.

// Used for received frame.

	int subchan;			/* Winning "subchannel" when using multiple */
					/* decoders on one channel.  */
					/* Special case, -1 means DTMF decoder. */
					/* Maybe we should have a different type in this case? */

	int slice;			/* Winning slicer. */

	packet_t pp;			/* Pointer to frame structure. */

	alevel_t alevel;		/* Audio level. */

	fec_type_t fec_type;		// Type of FEC for received signal: none, FX.25, or IL2P.

	retry_t retries;		/* Effort expended to get a valid CRC. */
					/* Bits changed for regular AX.25. */
					/* Number of bytes fixed for FX.25. */

	char spectrum[MAX_SUBCHANS*MAX_SLICERS+1];	/* "Spectrum" display for multi-decoders. */

// Used by requests from a client application, connect, etc.

	char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];

	int num_addr;			/* Range 2 .. 10. */

	int client;


// Used only by client request to transmit connected data.

	cdata_t *txdata;

// Used for channel activity change.
// It is useful to know when the channel is busy either for carrier detect
// or when we are transmitting.

	int activity;			/* OCTYPE_PTT for my transmission start/end. */
					/* OCTYPE_DCD if we hear someone else. */

	int status;			/* 1 for active or 0 for quiet. */

} dlq_item_t;



void dlq_init (void);

void dlq_rec_frame (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, fec_type_t fec_type, retry_t retries, char *spectrum);

int dlq_wait_while_empty (double timeout_val);

struct dlq_item_s *dlq_remove (void);

void dlq_delete (struct dlq_item_s *pitem);


cdata_t *cdata_new (int pid, char *data, int len);

void cdata_delete (cdata_t *txdata);

void cdata_check_leak (void);


#endif

/* end dlq.h */
