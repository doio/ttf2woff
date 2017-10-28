/*
 *	Copyright (C) 2013-2017 Jan Bobrowski <jb@wizard.ae.krakow.pl>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include "ttf2woff.h"

void read_ttc(struct ttf *ttf, u8 *data, size_t length, int fontn)
{
	unsigned n, o;

	if(length < 16+12+16) ERR_TRUNCATED;

	n = g32(data+8);
	if(n > 1<<26 || length < 16+(4+12+16)*n) ERR_TRUNCATED;

	if(fontn<0 || fontn>=n)
		errx(1, "No font #%d in collection",fontn);

	o = g32(data+12+4*fontn);
	if(o >= length) ERR_TRUNCATED;

	read_ttf(ttf, data, length, o);
}
