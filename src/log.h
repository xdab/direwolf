
/* log.h */

#include "hdlc_rec2.h" // for retry_t
#include "ax25_pad.h"

void log_init(int daily_names, char *path);
void log_write(int chan, packet_t pp, alevel_t alevel, retry_t retries);
void log_rr_bits(packet_t pp);
void log_term(void);
