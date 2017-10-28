#include <sys/types.h>
#include <string.h>
#include <assert.h>

#pragma clang diagnostic ignored "-Wshift-op-parentheses"
#pragma clang diagnostic ignored "-Wpointer-sign"

#ifndef NO_ERRWARN
#include <err.h>
#else
void err(int,char*,...);
void errx(int,char*,...);
void warn(char*,...);
void warnx(char*,...);
#endif

enum {
	fmt_UNKNOWN=0,
	fmt_TTF,
	fmt_WOFF,
	fmt_WOFF2
};

extern struct flags {
	unsigned otype:8;
	unsigned stdout_used:1;
	unsigned verbose:1;
	unsigned silent:1;
	unsigned mayoptim:1;
	unsigned optimize:1;
	unsigned dryrun:1;
	unsigned inplace:1;
	unsigned listonly:1;
} g;

void echo(char *, ...);

typedef unsigned char u8;
typedef unsigned int u32;
typedef signed short s16;

static inline int g16(u8 *p) {return p[0]<<8 | p[1];}
static inline u32 g32(u8 *p) {return (u32)p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3];}
static inline u8 *p16(u8 *p, int v) {p[0]=v>>8; p[1]=v; return p+2;}
static inline u8 *p32(u8 *p, u32 v) {p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; return p+4;}
static inline u8 *append(u8 *d, u8 *s, size_t n) {u8 *p=d+n; memcpy(d,s,n); return p;}

#define OVERFLOWS(A,B) ((u32)(A)+(u32)(B)<(u32)(A))

struct buf {
	u8 *ptr;
	unsigned len;
};

struct table {
	u32 tag;
	unsigned modified:1;
	unsigned free_buf:1;
	struct buf buf;
	u32 csum;
	u32 pos;
	char name[8];
	struct buf zbuf;
};

struct ttf {
	u32 flavor;
	int ntables;
	unsigned modified:1;
	unsigned modified_meta:1; // WOFF meta & priv
	struct table *tables; // sorted by name
	struct table **tab_pos; // sorted by file pos
	struct buf woff_meta, woff_priv;

	struct buf aux_buf;
};

void alloc_tables(struct ttf *ttf);
void name_table(struct table *t);
u8 *put_ttf_header(u8 buf[12], struct ttf *ttf);
struct table *find_table(struct ttf *ttf, char tag[4]);
void optimize(struct ttf *ttf);

void read_ttf(struct ttf *ttf, u8 *data, size_t length, unsigned offset);
void read_ttc(struct ttf *ttf, u8 *data, size_t length, int fontn);
void read_woff(struct ttf *ttf, u8 *data, size_t length);
void read_woff2(struct ttf *ttf, u8 *data, size_t length);
void gen_woff(struct buf *out, struct ttf *ttf);
void gen_ttf(struct buf *out, struct ttf *ttf);

#define ERR_FONT errx(2, "Bad font [%s:%d]",__FILE__,__LINE__)
#define ERR_TRUNCATED errx(2, "File truncated [%s:%d]",__FILE__,__LINE__)

int zlib_compress(struct buf *out, struct buf *inp);
extern char *copression_by;

#define _STR(X) #X
#define STR(X) _STR(X)

#define REALLY_SMALLER(A,B) (((A)+3&~3)<((B)+3&~3))

void *my_alloc(size_t sz);
void *my_free(void *p);
void *my_realloc(void *p, size_t sz);

struct xbuf {
	u8 *p;
	struct buf buf;
};

#define XB_RESET(B) ((B).p=(B).buf.ptr)
#define XB_LENGTH(B) ((B).p-(B).buf.ptr)

static inline void xbspace(struct xbuf *xb, unsigned n)
{
	unsigned o = xb->p - xb->buf.ptr;
	if(xb->buf.len < o + n) {
		xb->buf.len = o + n;
		xb->buf.ptr = my_realloc(xb->buf.ptr, xb->buf.len);
		xb->p = xb->buf.ptr + o;
	}
}

static inline void xb16(struct xbuf *xb, unsigned v) {
	assert(xb->p - xb->buf.ptr + 2 <= xb->buf.len);
	xb->p = p16(xb->p, v);
}

static inline void xbcopy(struct xbuf *xb, u8 *p, unsigned n) {
	assert(xb->p - xb->buf.ptr + n <= xb->buf.len);
	xb->p = append(xb->p, p, n);
}

int ttf_encode_coord(struct xbuf *xb, int dv);

struct flag_enc {int f, n; u8 *p;};
void fe_flag(struct flag_enc *fe, int f);
static inline void fe_init(struct flag_enc *fe, u8 *p) {fe->f=-1; fe->n=0; fe->p=p;}
static inline u8 *fe_end(struct flag_enc *fe) {fe_flag(fe,-1); return fe->p;}
