
/* hdlc_send.h */

// In version 1.7 an extra layer of abstraction was added here.
// Rather than calling hdlc_send_frame, we now use another function
// which sends AX.25 or FX.25 depending on mode.

#include "ax25_pad.h"
#include "audio.h"

int layer2_send_frame(int chan, packet_t pp, int bad_fcs, struct audio_s *audio_config_p);

int layer2_preamble_postamble(int chan, int flags, int finish, struct audio_s *audio_config_p);

/* end hdlc_send.h */
