/*
 *	Copyright (C) 2013-2017 Jan Bobrowski <jb@wizard.ae.krakow.pl>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <assert.h>
#include "ttf2woff.h"

struct table *find_table(struct ttf *ttf, char tag[4])
{
	u32 tg = g32((u8*)tag);
	int i;
	for(i=0; i<ttf->ntables; i++)
		if(ttf->tables[i].tag == tg)
			return &ttf->tables[i];
	return 0;
}

static void replace_table(struct table *t, u8 *p, int l)
{
	if(t->free_buf)
		t->buf.ptr = my_free(t->buf.ptr);
	t->free_buf = 1;
	t->modified = 1;
	t->buf.ptr = p;
	t->buf.len = l;
}

static void optimized(struct table *t, struct buf new)
{
	int eq = t->buf.len==new.len && memcmp(t->buf.ptr, new.ptr, new.len)==0;
	if(g.verbose && !eq) {
		if(t->buf.len==new.len)
			echo("Replaced %s table (%u)", t->name, t->buf.len);
		else
			echo("Optimized %s table: %u > %u (%+d bytes)", t->name, t->buf.len, new.len, new.len-t->buf.len);
	}
	replace_table(t, new.ptr, new.len);
}

static int overlap(struct buf a, struct buf b)
{
	int o = a.len<b.len ? a.len : b.len;
	while(o) {
		if(memcmp(a.len-o+a.ptr, b.ptr, o)==0)
			break;
		o--;
	}
	return o;
}

static u8 *bufbuf(struct buf a, struct buf b)
{
	u8 *p=a.ptr, *e=a.ptr+a.len-b.len;
	while(p<=e) {
		if(memcmp(p,b.ptr,b.len)==0)
			return p;
		p++;
	}
	return 0;
}

static int name_cmp_len(const void *va, const void *vb) {
	struct buf a = *(struct buf*)va;
	struct buf b = *(struct buf*)vb;
	int d = a.len - b.len;
	if(!d) d = memcmp(a.ptr, b.ptr, a.len);
	return d;
}

static void optimize_name(struct ttf *ttf)
{
	struct table *name = find_table(ttf, "name");
	struct buf str, new;
	struct buf *ent;
	u8 *p;
	int count,n,i;

	if(!name || name->buf.len<6+2*12+1 || g16(name->buf.ptr))
		return;

	n = g16(name->buf.ptr+4); // stringOffset
	if(name->buf.len < n)
		goto corrupted;

	str.ptr = name->buf.ptr+n;
	str.len = name->buf.len-n;

	count = g16(name->buf.ptr+2);
	if(name->buf.len < 6+12*count) {
corrupted:
		echo("Name table corrupted");
		return;
	}

	n = count;
	ent = my_alloc(n * sizeof *ent);

	p = name->buf.ptr+6;
	for(i=0; i<n; i++) {
		unsigned l = g16(p+8);
		unsigned o = l ? g16(p+10) : 0;
		if(o+l > str.len) {
			echo("Bad string location in name table");
			my_free(ent);
			return;
		}
		ent[i].ptr = str.ptr + o;
		ent[i].len = l;
		p += 12;
	}

	qsort(ent, n, sizeof *ent, name_cmp_len);

	for(;;) {
		int j,mo,mi,mj;
		struct buf a, b, c;

		mo = 0;
		for(j=0;j<n;j++) for(i=1;i<n;i++) if(i!=j) {
			int o;
			a = ent[i];
			b = ent[j];
			if(bufbuf(a,b))
				goto remove_b;
			o = overlap(a,b);
			if(o > mo) {
				mo = o;
				mi = i;
				mj = j;
			}
		}
		if(!mo)
			break;

		a = ent[mi];
		b = ent[mj];
		c.len = a.len + b.len - mo;
		c.ptr = my_alloc(c.len);
		p = append(c.ptr, a.ptr, a.len);
		append(p, b.ptr+mo, b.len-mo);
		if(a.ptr<str.ptr || a.ptr>=str.ptr+str.len)
			my_free(a.ptr);

		i = mi<mj ? mi : mj;
		j = mi<mj ? mj : mi;
		ent[i] = c;

remove_b:
		if(b.ptr<str.ptr || b.ptr>=str.ptr+str.len)
			my_free(b.ptr);
		n--;
		while(j < n) ent[j]=ent[j+1], j++;
	}

	{
		int sz = 6 + 12*count;
		for(i=0;i<n;i++)
			sz += ent[i].len;

		if(sz >= name->buf.len) {
			my_free(ent);
			return;
		}

		new.len = sz;
		new.ptr = my_alloc(sz);

		p = new.ptr + 6 + 12*count;
		for(i=0;i<n;i++) {
			struct buf a = ent[i];
			memcpy(p,a.ptr,a.len); p+=a.len;
			if(a.ptr<str.ptr || a.ptr>=str.ptr+str.len)
				my_free(a.ptr);
		}
		assert(p == new.ptr+new.len);
	}

	my_free(ent);

	memcpy(new.ptr, name->buf.ptr, 6+12*count);
	p16(new.ptr+4,6+12*count);

	{
		struct buf newstr;

		newstr.ptr = new.ptr + 6+12*count;
		newstr.len = new.len - 6+12*count;

		p = new.ptr + 6 + 10;
		for(i=0;i<count;i++) {
			struct buf a = {str.ptr+g16(p), g16(p-2)};
			u8 *s = bufbuf(newstr, a);
			assert(s);
			p16(p, s-newstr.ptr);
			p += 12;
		}
	}

#ifndef NDEBUG
	for(i=0; i<count; i++) {
		u8 *p0 = name->buf.ptr;
		u8 *p1 = new.ptr;
		p0 += g16(p0+4) + g16(p0+6+12*i+10);
		p1 += g16(p1+4) + g16(p1+6+12*i+10);
		assert(!memcmp(p0,p1,g16(new.ptr+6+12*i+8)));
	}
#endif

	optimized(name, new);
}

static void optimize_hmtx(struct ttf *ttf)
{
	struct table *hhea, *hmtx;
	struct buf buf;
	u8 *p, *q;
	int nlhm,adv,n;

	hhea = find_table(ttf, "hhea");
	hmtx = find_table(ttf, "hmtx");

	if(!hhea || !hmtx || hhea->buf.len < 36 || g32(hhea->buf.ptr)!=0x10000)
		return;

	nlhm = g16(hhea->buf.ptr + 34);
	buf = hmtx->buf;

	if(!nlhm || buf.len&1 || buf.len < 4*nlhm) {
		return;
	}

	if(nlhm<2)
		return;

	p = buf.ptr + 4*(nlhm-1);
	adv = g16(p);

	for(n=nlhm; n>1; n--) {
		p -= 4;
		if(adv != g16(p))
			break;
	}
	if(n < nlhm) {
		struct buf new;
		int i, nent = (buf.len>>1) - nlhm;

		new.len = 2*nent + 2*n;
		new.ptr = my_alloc(new.len);
		p = append(new.ptr, buf.ptr, n<<2);
		q = buf.ptr + (n<<2);
		for(i=n; i<nlhm; i++) {
			p = p16(p, g16(q+2));
			q += 4;
		}
		p = append(p, q, buf.ptr+buf.len-q);
		assert(p == new.ptr+new.len);

		optimized(hmtx, new);

		p16(hhea->buf.ptr+34, n);
		hhea->modified = 1;
	}
}

#define FAILED do {echo("Optimization failed [%s:%d]",__FILE__,__LINE__); return;} while(0)

enum {
	o_ON=1, o_XSHORT=2, o_YSHORT=4, o_REPEAT=8, o_XSIGN=16, o_YSIGN=32, o_RESERVED=192
};

enum {
	c_WORDS=1, c_XY=2, c_ROUND=4, c_SCALE=8, c_MORE=32, c_2SCALE=64, c_MATRIX=128, c_INSTR=256, c_METRICS=512, c_OVERLAP=1024
};

#define COMP_ARG_SIZE(F) (((F)&c_WORDS ? 4 : 2) + ((F)&c_SCALE ? 2 : (F)&c_2SCALE ? 4 : (F)&c_MATRIX ? 8 : 0))

static u8 *decode_coord(int *dv, int f, u8 *p, u8 *e);

static void optimize_glyf(struct ttf *ttf)
{
	struct table *head, *glyf, *loca;
	struct buf *glyphs;
	u8 *flags;
	struct xbuf coords = {0};
	struct xbuf glyph = {0};
	int ng, loca_fmt;
	int olen[2];
	int i;

	head = find_table(ttf, "head");
	glyf = find_table(ttf, "glyf");
	loca = find_table(ttf, "loca");
	if(!head || !glyf || !loca)
		return;

	if(head->buf.len < 54 || g32(head->buf.ptr)!=0x10000 || g16(head->buf.ptr+52))
		FAILED;
	loca_fmt = g16(head->buf.ptr+50);
	if(loca_fmt > 1)
		FAILED;

	ng = (loca->buf.len >> 1+loca_fmt) - 1;

	glyphs = my_alloc(ng * sizeof *glyphs);
	flags = my_alloc(65536);
	olen[0] = olen[1] = 0;

	for(i=0; i<ng; i++) {
		u8 *p, *e;
		int nc;
		{
#define read_LOCA(I) (loca_fmt ? g32(loca->buf.ptr + 4*(I)) : g16(loca->buf.ptr + 2*(I)) << 1)
			unsigned o0 = read_LOCA(i);
			unsigned o1 = read_LOCA(i+1);
			if(o1 < o0)
				FAILED;
			if(glyf->buf.len < o1)
				FAILED;
			p = glyf->buf.ptr + o0;
			e = glyf->buf.ptr + o1;
		}

		glyphs[i].ptr = p;
		glyphs[i].len = e - p;
		if(p == e)
			continue;
		if(e - p < 12)
			FAILED;
		nc = g16(p);
		if(nc == 0) {
			glyphs[i].len = 0;
		} else if(nc == 0xFFFF) { // composite
			int ff = 0, f;
			p += 10;
			do {
				if(e - p < 6)
					FAILED;
				f = g16(p);
				p += 4 + COMP_ARG_SIZE(f);
				if(p > e)
					FAILED;
				ff |= f;
			} while(f & c_MORE);
			if(ff & c_INSTR) {
				if(p + 2 > e)
					FAILED;
				p += 2 + g16(p);
				if(p > e)
					FAILED;
			}
			glyphs[i].len = p - glyphs[i].ptr;
		} else { // simple
			int n, ni, np;
			u8 *fp;
			p += 10 + 2*nc;
			if(p + 2 >= e)
				FAILED;
			np = g16(p - 2) + 1;
			ni = g16(p); // instr
			if(p + ni >= e)
				FAILED;
			p += 2 + ni;
			fp = flags;
			for(n=np; n;) {
				int f, nf=1;
				if(p == e)
					FAILED;
				f = *p++;
				if(f & o_REPEAT) {
					if(p == e)
						FAILED;
					nf += *p++;
					f &= ~o_REPEAT;
				}
				n -= nf;
				if(n < 0)
					FAILED;
				memset(fp, f, nf); fp += nf;
			}
			XB_RESET(coords);
			for(n=0; n<np; n++) {
				int dx;
				p = decode_coord(&dx, flags[n], p, e);
				if(!p)
					FAILED;
				flags[n] = flags[n]&(o_ON|o_YSHORT|o_YSIGN|o_RESERVED) | ttf_encode_coord(&coords, dx);
			}
			for(n=0; n<np; n++) {
				int dy;
				p = decode_coord(&dy, flags[n]>>1, p, e);
				if(!p)
					FAILED;
				flags[n] = flags[n]&(o_ON|o_XSHORT|o_XSIGN|o_RESERVED) | ttf_encode_coord(&coords, dy) << 1;
			}
			glyphs[i].len = p - glyphs[i].ptr;

			XB_RESET(glyph);
			xbspace(&glyph, 10+2*nc+2+ni + np);
			glyph.p = append(glyph.p, glyphs[i].ptr, 10+2*nc+2+ni);
			{
				struct flag_enc fe;
				fe_init(&fe, glyph.p);
				for(n=0; n<np; n++)
					fe_flag(&fe, flags[n]);
				glyph.p = fe_end(&fe);
			}
			xbspace(&glyph, XB_LENGTH(coords));
			glyph.p = append(glyph.p, coords.buf.ptr, XB_LENGTH(coords));

			if(XB_LENGTH(glyph) < glyphs[i].len) {
				glyphs[i].ptr = glyph.buf.ptr;
				glyphs[i].len = XB_LENGTH(glyph);
				glyph.buf.ptr=0; glyph.buf.len=0;
			}
		}

		olen[1] += glyphs[i].len;
		olen[0] += glyphs[i].len+1 & ~1;
	}
	my_free(flags);
	my_free(glyph.buf.ptr);

	{
		int lf = olen[0] >= 1<<17;

		if(olen[lf] < glyf->buf.len || lf < loca_fmt) {
			struct buf new_glyf, new_loca;
			new_glyf.ptr = my_alloc(new_glyf.len = olen[lf]);
			new_loca.ptr = my_alloc(new_loca.len = (ng + 1) << (lf + 1));
			u8 *p = new_glyf.ptr;

			for(i=0;; i++) {
				struct buf *g;
				int o;

				o = p - new_glyf.ptr;
				if(lf)
					p32(new_loca.ptr + 4*i, o);
				else {
					assert(!(o&1));
					p16(new_loca.ptr + 2*i, o>>1);
				}

				if(i == ng)
					break;

				g = &glyphs[i];
				if(g->len) {
					assert(p+g->len <= new_glyf.ptr+new_glyf.len);
					p = append(p, g->ptr, g->len);
					if(lf==0 && g->len&1) *p++ = 0;
					if(g->ptr < glyf->buf.ptr || g->ptr >= glyf->buf.ptr + glyf->buf.len)
						my_free(g->ptr);
				}
			}
			assert(p - new_glyf.ptr == new_glyf.len);
			optimized(glyf, new_glyf);
			optimized(loca, new_loca);
			if(lf != loca_fmt) {
				p16(head->buf.ptr+50, lf);
				head->modified = 1;
			}
		}
	}
}

static u8 *decode_coord(int *dv, int f, u8 *p, u8 *e) {
	if((f & 0x12) == 0x10)
		*dv = 0;
	else if((f & 0x02) == 0x02) {
		if(p == e) return 0;
		*dv = f&0x10 ? *p++ : -*p++;
	} else {
		if(p+2 > e) return 0;
		*dv = (s16)g16(p); p += 2;
	}
	return p;
}

int ttf_encode_coord(struct xbuf *xb, int dv)
{
	int f = o_XSIGN;
	if(!dv)
		return f;
	xbspace(xb, 2);
	if(-256<dv && dv<256) {
		f = o_XSIGN|o_XSHORT;
		if(dv<0) dv=-dv, f=o_XSHORT;
		*xb->p++ = dv;
		return f;
	}
	xb16(xb, dv);
	return 0;
}

void fe_flag(struct flag_enc *fe, int f) {
	if(f != fe->f || fe->n==256) {
		u8 *p = fe->p;
		switch(fe->n) {
		case 2: *p++ = fe->f;
		case 1: *p++ = fe->f;
		case 0: break;
		default: *p++ = fe->f|o_REPEAT; *p++ = fe->n-1;
		}
		fe->p = p;
		fe->f = f;
		fe->n = 0;
	}
	fe->n++;
}

void optimize(struct ttf *ttf)
{
	optimize_name(ttf);
	optimize_hmtx(ttf);
	optimize_glyf(ttf);
}
