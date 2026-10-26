/**
 *  Copyright (C) 2011-2012  Juho Vähä-Herttua
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#ifndef NETUTILS_H
#define NETUTILS_H

int netutils_init();
void netutils_cleanup();

int netutils_init_socket(unsigned short *port, int use_ipv6, int use_udp);
/* As above, with the IPv4 address to bind to (network byte order; INADDR_ANY = all) */
int netutils_init_socket_at(unsigned short *port, int use_ipv6, int use_udp, unsigned long address);
/* Dotted IPv4 to network byte order; -1 when src is not one */
int netutils_parse_ipv4(const char *src, unsigned long *address);
unsigned char *netutils_get_address(void *sockaddr, int *length);
int netutils_parse_address(int family, const char *src, void *dst, int dstlen);

#endif
