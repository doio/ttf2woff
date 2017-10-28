/*
 *	Copyright (C) 2017 Jan Bobrowski <jb@wizard.ae.krakow.pl>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <assert.h>
#include <brotli/decode.h>
#include "ttf2woff.h"

#include <stdio.h>

static char woff2_known_tags[] = "cmapheadhheahmtxmaxpnameOS/2postcvt fpgmglyflocaprep"
 "CFF VORGEBDTEBLCgasphdmxkernLTSHPCLTVDMXvheavmtxBASEGDEFGPOSGSUBEBSCJSTFMATHC"
 "BDTCBLCCOLRCPALSVG sbixacntavarbdatblocbslncvarfdscfeatfmtxfvargvarhstyjustlc"
 "armortmorxopbdproptrakZapfSilfGlatGlocFeatSill";

static int brotli_decompress(struct buf *out, u8 *data, unsigned len)
{
	size_t inn = len;
	const uint8_t* inp = data;
	size_t outn = out->len;
	uint8_t* outp = out->ptr;
	BrotliDecoderResult r;

	r = BrotliDecoderDecompress(inn, inp, &outn, outp);
	if(r == BROTLI_DECODER_RESULT_SUCCESS) {
		return outn;
	}
	errx(1, "brotli error");
}

static void reconstruct_3_glyf(struct table *glyf, struct table *loca);
static void reconstruct_1_hmtx(struct table *hmtx);

static u8 *base128(u8 *p, u32 *vp)
{
	u32 v, b;
	b = *p++;
	if(b == 0x80) return 0;
	v = b&0x7F;
	if(b & 0x80) for(;;) {
		b = *p++;
		v = v<<7 | b&0x7F;
		if(!(b & 0x80))
			break;
		if(v & 0xFE000000)
			return 0;
	}
	*vp = v;
	return p;
}

void read_woff2(struct ttf *ttf, u8 *data, size_t length)
{
	u8 *p, *end=data+length;
	int i;
	u32 tsize;
	struct buf tbuf;
	int tr_allowed;
	struct table *glyf, *loca, *hmtx;

	if(length<=48+20) ERR_TRUNCATED;

	ttf->flavor = g32(data+4);
	if(ttf->flavor == g32("ttcf"))
		errx(1, "WOFF2 collections not supported");

	if(g32(data+8) > length) ERR_TRUNCATED;

	ttf->ntables = g16(data+12);
	if(!ttf->ntables) ERR_FONT;

	// ttf->woff_meta

	ttf->woff_priv.len = g32(data+44);
	ttf->woff_priv.ptr = ttf->woff_priv.len ? data+g32(data+40) : 0;

	alloc_tables(ttf);

	tsize = 0;
	glyf = loca = hmtx = 0;
	p = data + 48;
	for(i=0; i<ttf->ntables; i++) {
		struct table *t = &ttf->tables[i];
		u32 olen, tlen;
		int v, kt, tf;

		if(end - p < 5) ERR_TRUNCATED;

		v = *p++;
		kt = v&63;
		if(kt < 63)
			t->tag = g32(woff2_known_tags + 4*kt);
		else
			t->tag = g32(p), p += 4;

		v >>= 6;
		tf = 0;
		tr_allowed = 1;
		if(t->tag == g32("glyf")) {
			if(v!=3) glyf=t, tf=1;
			tr_allowed = 9;
		} else if(t->tag == g32("loca")) {
			if(v!=3) loca = t, tf=1;
			tr_allowed = 9;
		} else if(t->tag == g32("hmtx")) {
			if(v) hmtx = t, tf=1;
			tr_allowed = 3;
		}
		if(!(tr_allowed & (1<<v))) ERR_FONT;

		if(end - p < 10) ERR_TRUNCATED;
		p = base128(p, &olen);
		tlen = ~0;
		t->buf.len = olen;
		if(tf) {
			p = base128(p, &tlen);
			t->buf.len = tlen;
		}
		tsize += t->buf.len;

		t->modified = 1; // need checksum

		name_table(t);
	}

	if(!!glyf != !!loca) ERR_FONT;

	// tsize - transformed but not compressed

	tbuf.ptr = my_alloc(tbuf.len = tsize);
	brotli_decompress(&tbuf, p, end-p);
	ttf->aux_buf = tbuf;

	p = tbuf.ptr;
	for(i=0; i<ttf->ntables; i++) {
		struct table *t = &ttf->tables[i];
		t->buf.ptr = p;
		p += t->buf.len;
	}

	if(glyf)
		reconstruct_3_glyf(glyf, loca);
	if(hmtx)
		reconstruct_1_hmtx(hmtx);
}

/* hmtx */

static void reconstruct_1_hmtx(struct table *hmtx /*, struct table *hhea */)
{
	errx(1, "hmtx reconstruction not implemented");
}

/* glyf & loca */

struct range {
	u8 *p, *e;
#ifndef NDEBUG
	char *name;
#endif
};

static void underrun(struct range *s) {
#ifdef NDEBUG
	errx(1, "Decoding underrun");
#else
	errx(1, "Decoding underrun [%s]", s->name);
#endif
}

static u8 *bite(struct range *s, int n) {
	u8 *p = s->p;
	s->p += n;
	if(s->p > s->e) underrun(s);
	return p;
}

static int read16(struct range *s) {
	return g16(bite(s,2));
}

static int r255(struct range *s)
{
	int v;
	if(s->p == s->e) underrun(s);
	v = *s->p++;
	if(v >= 253) {
		if(s->p == s->e) underrun(s);
		switch(v) {
		case 253:
			v = *s->p++ << 8;
			if(s->p == s->e) underrun(s);
			break;
		case 254:
			v = 2*253;
			break;
		case 255:
			v = 253;
		}
		v += *s->p++;
	}
	return v;
}

static void decode_coords(int f, struct range *s, int d[2]);
static u8 *set_loca(u8 *loca_p, int loca_t, struct xbuf *xb);

static void reconstruct_3_glyf(struct table *glyf, struct table *loca)
{
	struct range cstr, pstr, fstr, gstr, zstr, bstr, istr;
	struct xbuf out = {0};
	struct xbuf flags = {0};
	struct xbuf xcoord = {0};
	struct xbuf ycoord = {0};
	int nglyphs, loca_t;
	u8 *p, *bbmp, *loca_p;
	int i;

	if(loca->buf.len) ERR_FONT;
	if(glyf->buf.len < 36) ERR_FONT;
	p = glyf->buf.ptr;

	{
		u32 o = 36;
		for(i=0;i<7;i++) {
			u32 l = g32(p+8+4*i);
			if(OVERFLOWS(o,l) || (o += l)>glyf->buf.len) ERR_FONT;
		}
#ifdef NDEBUG
#define POS(N,O) do {N.p=p+o, o+=g32(p+(O)); N.e=p+o;} while(0)
#else
#define POS(N,O) do {N.p=p+o, o+=g32(p+(O)); N.e=p+o; N.name=#N;} while(0)
#endif
		o = 36;
		POS(cstr,8);
		POS(pstr,12);
		POS(fstr,16);
		POS(gstr,20);
		POS(zstr,24);
		POS(bstr,28);
		POS(istr,32);
	}

	nglyphs = g16(p+4);
	loca_t = g16(p+6);
	if(loca_t > 1) ERR_FONT;

	{
		int v = nglyphs+31 >> 5 << 2;
		if(bstr.p + v > bstr.e) ERR_FONT;
		bbmp = bstr.p;
		bstr.p += v;
#define BBMP(N) (bbmp[(N)>>3] & 128>>((N)&7))
	}

	loca->buf.ptr = my_alloc(loca->buf.len = (loca_t?4:2)*(nglyphs + 1));
	loca->free_buf = 1;
	loca_p = loca->buf.ptr;

	for(i=0; i<nglyphs; i++) {
		int nc, ni;
		int bb_offs;

		loca_p = set_loca(loca_p, loca_t, &out);
		nc = read16(&cstr);
		if(!nc) {
			if(BBMP(i)) ERR_FONT;
			continue;
		}

		xbspace(&out, 2 + 4*2);
		xb16(&out, nc);
		bb_offs = XB_LENGTH(out);
		out.p += 4*2;

		if(nc < 0xFFFF) {
			int j, np;
			u8 *ends;
			struct flag_enc fe;
			int xMin, yMin, xMax, yMax;
			int x,y;

			xbspace(&out, 2*nc);
			ends = out.p;
			np = 0;
			for(j=0; j<nc; j++) {
				np += r255(&pstr);
				xb16(&out, np-1);
			}
			if(!np) ERR_FONT;

			XB_RESET(xcoord); XB_RESET(ycoord);
			XB_RESET(flags); xbspace(&flags, np);
			p = bite(&fstr, np);
			xMin = yMin = 32767; xMax = yMax = -32768;
			fe_init(&fe, flags.p);
			x = y = 0;
			for(j=0; j<np; j++) {
				int f, of;
				int d[2];

				f = p[j];
				decode_coords(f&127, &gstr, d);
				x += d[0], y += d[1];
				if(xMin > x) xMin = x;
				if(yMin > y) yMin = y;
				if(xMax < x) xMax = x;
				if(yMax < y) yMax = y;
				if(g16(ends)==j) {
					x = y = 0; // ?
					ends += 2;
				}

				of = f>>7 ^ 1;
				of |= ttf_encode_coord(&xcoord, d[0]);
				of |= ttf_encode_coord(&ycoord, d[1]) << 1;
				fe_flag(&fe, of);
			}
			flags.p = fe_end(&fe);

			if(!BBMP(i)) {
				u8 *p = out.buf.ptr + bb_offs;
				p16(p, xMin); p16(p+2, yMin); p16(p+4, xMax); p16(p+6, yMax);
			}

			ni = r255(&gstr);
			xbspace(&out, 2+ni + XB_LENGTH(flags) + XB_LENGTH(xcoord) + XB_LENGTH(ycoord));
			xb16(&out, ni);
			xbcopy(&out, bite(&istr, ni), ni);
			xbcopy(&out, flags.buf.ptr, XB_LENGTH(flags));
			xbcopy(&out, xcoord.buf.ptr, XB_LENGTH(xcoord));
			xbcopy(&out, ycoord.buf.ptr, XB_LENGTH(ycoord));
		} else {
			int ff = 0;
			u8 *start = zstr.p;
			if(!BBMP(i)) ERR_FONT;
			for(;;) {
				int f = read16(&zstr);
				bite(&zstr, 2); // glyph
				int n = f&1 ? 4 : 2;
				if(f & 1<<3) // scale
					n += 2;
				else if(f & 1<<6) // scale xy
					n += 4;
				else if(f & 1<<7) // matrix
					n += 8;
				bite(&zstr, n);

				ff |= f;
				if((f & 1<<5)==0)
					break;
			}
			ni = 0;
			ff &= 1<<8;
			if(ff)
				ni = r255(&gstr);
			xbspace(&out, zstr.p - start + (ff ? 2+ni : 0));
			xbcopy(&out, start, zstr.p - start);
			if(ff) {
				xb16(&out, ni);
				xbcopy(&out, bite(&istr, ni), ni);
			}
		}

		if(BBMP(i)) {
			u8 *p = out.buf.ptr + bb_offs;
			u8 *q = bite(&bstr, 8);
			memcpy(p, q, 8);
		}
	}

	set_loca(loca_p, loca_t, &out);

	my_free(flags.buf.ptr);
	my_free(xcoord.buf.ptr);
	my_free(ycoord.buf.ptr);

	assert(!glyf->free_buf);
	glyf->buf.ptr = out.buf.ptr;
	glyf->buf.len = XB_LENGTH(out);
	glyf->free_buf = 1;
}

static inline int flip(int v, int s) {return s ? v : -v;} // 0:minus

static void decode_coords(int f, struct range *s, int d[2])
{
	int dx, dy;
	if(f < 20) {
		int v = *bite(s,1);
		int i = f < 10;
		if(!i) f -= 10;
		v += f<<7 & 0x700;
		d[0] = d[1] = 0;
		d[i] = flip(v, f&1);
		return;
	}
	if(f < 84) {
		int v = *bite(s,1);
		f -= 20;
		dx = (f    & 0x30) + 1 + (v>>4);
		dy = (f<<2 & 0x30) + 1 + (v&15);
	} else if(f < 120) {
		u8 *p = bite(s,2);
		unsigned b0, b1;
		b0 = f-84>>2, b1 = b0 % 3, b0 /= 3;
		dx = (b0<<8) + 1 + p[0];
		dy = (b1<<8) + 1 + p[1];
	} else if(f < 124) {
		u8 *p = bite(s,3);
		unsigned v = p[0]<<16 | g16(p+1);
		dx = v>>12;
		dy = v&0xFFF;
	} else {
		u8 *p = bite(s,4);
		dx = g16(p);
		dy = g16(p+2);
	}
	d[0] = flip(dx, f&1);
	d[1] = flip(dy, f&2);
}

static u8 *set_loca(u8 *loca_p, int loca_t, struct xbuf *xb)
{
	switch(loca_t) {
	case 0:
		if(XB_LENGTH(*xb) & 1) {
			xbspace(xb,1);
			*xb->p++ = 0;
		}
		if(XB_LENGTH(*xb) >= 1<<17)
			ERR_FONT;
		p16(loca_p, XB_LENGTH(*xb) >> 1);
		break;
	case 1:
		p32(loca_p, XB_LENGTH(*xb));
		loca_p += 2;
	}
	return loca_p + 2;
}
