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

void read_ttf(struct ttf *ttf, u8 *data, size_t length, unsigned start)
{
	int i;
	u8 *p;

	if(length-start < 12+16)
		ERR_TRUNCATED;

	ttf->flavor = g32(data+start);
	ttf->ntables = g16(data+start+4);

	if(!ttf->ntables)
		ERR_FONT;
	if(length - start <= 12+16*ttf->ntables)
		ERR_TRUNCATED;

	alloc_tables(ttf);

	p = data+start+12;
	for(i=0; i<ttf->ntables; i++) {
		struct table *t = &ttf->tables[i];
		u32 off=g32(p+8), len=g32(p+12);
		if(OVERFLOWS(off,len) || off+len > length)
			ERR_FONT;
		t->tag = g32(p);
		t->csum = g32(p+4);
		t->pos = off;
		t->buf.ptr = data + off;
		t->buf.len = len;
		name_table(t);

		p += 16;
	}
}
