
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2017  John Langner, WB2OSZ
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
 * Module:      dwsock.c
 *
 * Purpose:   	Functions for TCP sockets.
 *
 * Description:	These are used for connecting between different applications,
 *		possibly on different hosts.
 *
 * New in version 1.5:
 *		Duplicate code already exists in multiple places and I was about
 *		to add another one.  Instead, we will gather the common code here
 *		instead of having yet another copy.
 *
 *---------------------------------------------------------------*/

#if __WIN32__

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h> // _WIN32_WINNT must be set to 0x0501 before including this

#else

#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>

#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "dwsock.h"

/*-------------------------------------------------------------------
 *
 * Name:        dwsock_init
 *
 * Purpose:     Preparation before using socket interface.
 *
 * Inputs:	none
 *
 * Returns:	0 for success, -1 for error.
 *
 * Errors:	Message is printed.  I've never seen it fail.
 *
 * Description:	Doesn't do anything for Linux.
 *
 *--------------------------------------------------------------------*/

int dwsock_init(void)
{
#if __WIN32__
	WSADATA wsadata;
	int err;

	err = WSAStartup(MAKEWORD(2, 2), &wsadata);
	if (err != 0)
	{

		printf("WSAStartup failed, error: %d\n", err);
		return (-1);
	}

	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2)
	{

		printf("Could not find a usable version of Winsock.dll\n");
		WSACleanup();
		return (-1);
	}
#endif
	return (0);

} /* end dwsock_init */


/*-------------------------------------------------------------------
 *
 * Name:        dwsock_bind
 *
 * Purpose:     We also have a bunch of duplicate code for the server side.
 *
 * Inputs:
 *
 * TODO:	Use this instead of own copy in audio.c
 * TODO:	Use this instead of own copy in audio_portaudio.c
 * TODO:	Use this instead of own copy in kissnet.c
 *
 *--------------------------------------------------------------------*/

// Not implemented yet.

/*-------------------------------------------------------------------
 *
 * Name:        dwsock_ia_to_text
 *
 * Purpose:     Convert binary IP Address to text form.
 *
 * Inputs:	Family		- AF_INET or AF_INET6.
 *
 *		pAddr		- Pointer to the IP Address storage location.
 *
 *		StringBufSize	- Number of bytes in pStringBuf.
 *
 * Outputs:	pStringBuf	- Text result is placed here.
 *
 * Returns:	pStringBuf
 *
 * Description:	Can't use InetNtop because it is supported only on Windows Vista and later.
 * 		At one time Dire Wolf worked on Win XP.  Haven't tried it for years.
 * 		Maybe some other dependency on a newer OS version has crept in.
 *
 *--------------------------------------------------------------------*/

char *dwsock_ia_to_text(int Family, void *pAddr, char *pStringBuf, size_t StringBufSize)
{
	struct sockaddr_in *sa4;
	struct sockaddr_in6 *sa6;

	switch (Family)
	{

	case AF_INET:
		sa4 = (struct sockaddr_in *)pAddr;
#if __WIN32__
		snprintf(pStringBuf, StringBufSize, "%d.%d.%d.%d", sa4->sin_addr.S_un.S_un_b.s_b1,
				 sa4->sin_addr.S_un.S_un_b.s_b2,
				 sa4->sin_addr.S_un.S_un_b.s_b3,
				 sa4->sin_addr.S_un.S_un_b.s_b4);
#else
		inet_ntop(AF_INET, &(sa4->sin_addr), pStringBuf, StringBufSize);
#endif
		break;

	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)pAddr;
#if __WIN32__
		snprintf(pStringBuf, StringBufSize, "%x:%x:%x:%x:%x:%x:%x:%x",
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[0]),
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[1]),
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[2]),
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[3]),
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[4]),
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[5]),
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[6]),
				 ntohs(((unsigned short *)(&(sa6->sin6_addr)))[7]));
#else
		inet_ntop(AF_INET6, &(sa6->sin6_addr), pStringBuf, StringBufSize);
#endif
		break;

	default:
		snprintf(pStringBuf, StringBufSize, "Invalid address family!");
	}
	return pStringBuf;

} /* end dwsock_ia_to_text */

void dwsock_close(int fd)
{
#if __WIN32__
	closesocket(fd);
#else
	close(fd);
#endif
}

/* end dwsock.c */
