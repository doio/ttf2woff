/*
 *	Copyright (C) 2013-2017 Jan Bobrowski <jb@wizard.ae.krakow.pl>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include "ttf2woff.h"

static struct buf get_or_inflate(u8 *p, size_t len, size_t orig_len)
{
	struct buf buf;
	uLongf blen;
	char *m;
	int v;

	if(len == orig_len) {
		buf.ptr = p;
		buf.len = len;
		return buf;
	}

	buf.len = orig_len;
	buf.ptr = my_alloc(orig_len);

	blen = buf.len;
	v = uncompress(buf.ptr, &blen, p, len);
	switch(v) {
	case Z_OK:
		if(blen==buf.len)
			return buf;
	case Z_MEM_ERROR: m = "Bad uncompressed length"; break;
	case Z_DATA_ERROR: m = "Data corrupted"; ; break;
	default: m = "Error";
	}

	errx(3, "zlib: %s", m);
}

void read_woff(struct ttf *ttf, u8 *data, size_t length)
{
	u8 *p;
	int i;

	if(length <= 44+20) ERR_TRUNCATED;

	ttf->flavor = g32(data+4);

	ttf->ntables = g16(data+12);
	if(!ttf->ntables) ERR_FONT;

	if(length < g32(data+8)) ERR_TRUNCATED;
	length = g32(data+8);

	{
		u32 len=g32(data+28), off;
		ttf->woff_meta.len = 0;
		if(len) {
			off = g32(data+24);
			if(OVERFLOWS(off,len) || off+len > length)
				ERR_FONT;
			ttf->woff_meta = get_or_inflate(data+off, len, g32(data+32));
		}
	}

	ttf->woff_priv.len = g32(data+40);
	ttf->woff_priv.ptr = ttf->woff_priv.len ? data+g32(data+36) : 0;

	alloc_tables(ttf);

	p = data+44;
	for(i=0; i<ttf->ntables; i++) {
		struct table *t = &ttf->tables[i];
		u32 off=g32(p+4), len=g32(p+8);
		if(OVERFLOWS(off,len) || off+len > length)
			ERR_FONT;
		t->tag = g32(p);
		t->csum = g32(p+16);
		t->pos = off;
		t->free_buf = 1;
		t->buf = get_or_inflate(data+off, len, g32(p+12));
		name_table(t);
		p += 20;
	}
}
