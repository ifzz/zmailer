/*
 * Common tricks to check and downgrade headers from MIME 8BIT to
 * MIME QUOTED-PRINTABLE
 */

/*
 *	Copyright 1994-2000 by Matti Aarnio
 *
 * To really understand how headers (and their converted versions)
 * are processed you do need to draw a diagram.
 * Basically:
 *    rp->desc->headers[]    is index to ALL of the headers, and
 *    rp->desc->headerscvt[] is index to ALL of the CONVERTED headers.
 * Elements on these arrays are  "char *strings[]" which are the
 * actual headers.
 * There are multiple-kind headers depending upon how they have been
 * rewritten, and those do tack together for each recipients (rp->)
 * There
 *  rp->newmsgheader    is a pointer to an element on rp->desc->msgheaders[]
 *  rp->newmsgheadercvt is respectively an element on rp->desc->msgheaderscvt[]
 *
 * The routine-collection   mimeheaders.c  creates converted headers,
 * if the receiving system needs them. Converted header data is created
 * only once per  rewrite-rule group, so there should not be messages which
 * report  "Received: ... convert XXXX convert XXXX convert XXXX; ..."
 * for as many times as there there are recipients for the message.
 * [mea@utu.fi] - 25-Jul-94
 */

#include "hostenv.h"
#ifdef HAVE_STDARG_H
# include <stdarg.h>
#else
# include <varargs.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif

#include "ta.h"

#include "zmalloc.h"
#include "libz.h"

#ifndef strdup
extern char *strdup();
#endif
#ifndef strchr
extern char *strchr();
#endif
extern void *emalloc();
extern void *erealloc();
extern char *getzenv();

#ifndef	__
# ifdef __STDC__
#  define __(x) x
# else
#  define __(x) ()
# endif
#endif

extern char * dupnstr __((const char *str, const int len));

static int mime_received_convert __((struct rcpt *rp, char *convertstr));

/* extern FILE *verboselog; */

#define istokenchar(x) (isalnum(x) || x == '-' || x == '_' || x == '$' || \
			x == '#'   || x == '%' || x == '+')

/* strqcpy() -- store the string into buffer with quotes if it
   		is not entirely of alphanums+'-'+'_'.. */

static int strqcpy __((char **buf, int startpos, int *buflenp, char *str));

static int
strqcpy(bufp, startpos, buflenp, str)
	char **bufp, *str;
	int *buflenp;
{
	char *s = str;
	char *buf = *bufp;
	char *p = buf + startpos;
	int needquotes = 0;
	int buflen = *buflenp - startpos;
	int cnt = buflen;

	/* Copy while scanning -- redo if need quotes.. */
	while (*s) {
	  char c = *s;
	  if (!(('0' <= c && c <= '9') ||
		('A' <= c && c <= 'Z') ||
		('a' <= c && c <= 'z') ||
		c == '-' || c == '_' || c == '.')) {
	    needquotes = 1;
	    break;
	  }
	  if (cnt) {
	    *p++ = c;
	    --cnt;
	  }
	  if (cnt == 0) {
	    cnt = 32;
	    *buflenp += cnt;
	    buflen = *buflenp;
	    buf = realloc(buf, *buflenp + 5);
	  }
	  ++s;
	}
	if (cnt && !needquotes) { /* Space left, not need quotes */
	  *p = 0;
	  *bufp = buf;
	  return (buflen - cnt);
	}

	/* Ok, need quotes.. */
	p = buf + startpos;
	cnt = buflen -3;
	s = str;
	*p++ = '"';
	while (*s) {
	  if (cnt < 2) {
	    cnt += 32;
	    *buflenp += cnt;
	    buflen = *buflenp;
	    startpos = p - buf;
	    buf = realloc(buf, *buflenp + 5);
	    p = buf + startpos;
	  }
	  if (*s == '"' || *s == '\\')
	    *p++ = '\\', --cnt;
	  if (cnt>0)
	    *p++ = *s, --cnt;
	  ++s;
	}
	if (cnt<0) cnt = 0;

	*p++ = '"';
	*p   = 0;
	*bufp = buf;
	return (buflen - cnt);
}


void cvtspace_free(rp)
     struct rcpt *rp;
{
	char ***ss;

	if (!rp->newmsgheadercvt) return;

	ss = rp->newmsgheadercvt;
}


int cvtspace_copy(rp)
     struct rcpt *rp;
{
	int hdrcnt = 0;
	char **probe = *(rp->newmsgheader);
	char **newcvt;

	/* Count how many lines */
	while (*probe) {
	  ++probe;
	  ++hdrcnt;
	}

	/* Allocate, and copy ! */

	newcvt = (char **)malloc(sizeof(char *)*(hdrcnt+1));
	if (newcvt != NULL) {

	  char **ss = newcvt;
	  probe = *(rp->newmsgheader);

	  while (*probe) { /* Copy over */
	    int len = strlen(*probe)+1;
	    *ss = (char *)malloc(len);
	    if (! *ss) return 0;
	    memcpy(*ss,*probe,len);
	    ++hdrcnt;
	    ++probe;
	    ++ss;
	  }
	  *ss = NULL;

	  *(rp->newmsgheadercvt) = newcvt;

	} else
	  return 0;	/* Out of memory ? */

	return 1;
}

#ifdef HAVE_STDARG_H
#ifdef __STDC__
int
append_header(struct rcpt *rp, const char *fmt, ...)
#else /* Not ANSI-C */
int
append_header(rp, fmt)
	struct rcpt *rp;
	const char *fmt;
#endif
#else
int
append_header(va_alist)
	va_dcl
#endif
{
	va_list pvar;
	char lbuf[2000]; /* XX: SHOULD be enough..  damn vsprintf()..*/
	u_int linelen;
	u_int linecnt;
	char ***hdrpp, **hdrp2;

#ifdef HAVE_STDARG_H
	va_start(pvar,fmt);
#else
	struct rcpt *rp;
	char *fmt;

	va_start(pvar);
	rp  = va_arg(pvar, struct rcpt*);
	fmt = va_arg(pvar, char *);
#endif
	lbuf[0] = 0;

#ifdef HAVE_VSNPRINTF
	vsnprintf(lbuf, sizeof(lbuf)-1, fmt, pvar);
#else
	vsprintf(lbuf, fmt, pvar);
#endif
	va_end(pvar);

	linelen = strlen(lbuf);
	if (linelen > sizeof(lbuf)) {
	  exit(240); /* BUG TIME! */
	}

/*
	if (*(rp->newmsgheadercvt) == NULL)
	  if (cvtspace_copy(rp) == 0)
	    return -1;
*/
	hdrpp   = rp->newmsgheadercvt;
	if (*hdrpp == NULL) /* Not copied ? */
	  hdrpp = rp->newmsgheader;
	linecnt = 0;

	while ((*hdrpp)[linecnt] != NULL) ++linecnt;

	hdrp2 = (char**)realloc(*hdrpp,sizeof(char **) * (linecnt+3));

	if (!hdrp2) return -1;
	hdrp2[linecnt] = (char*) malloc(linelen+3);
	if (! hdrp2[linecnt]) zmalloc_failure = 1;
	else {
	  memcpy(hdrp2[linecnt],lbuf,linelen+2);
	  hdrp2[++linecnt] = NULL;
	  *hdrpp = hdrp2;
	}
	return linecnt;
}

void
output_content_type(rp,ct,old)
     struct rcpt *rp;
     struct ct_data *ct;
     char **old;
{
	/* Output the  Content-Type: with  append_header()
	   Sample output:
		Content-Type: text/plain; charset=ISO-8859-1;
			boundary="12fsjdhf-j4.83+712";
			name="ksjd.ksa";
			attr_1="83r8310032 askj39";
			attr_2="ajdsh 8327ead"
	 */

	char *lines[400]; /* XX: Hopefully enough.. */
	int  linelens[400];
	int  linecnt = 0, i;
	char **newmsgheaders;
	char **h1, **o2, ***basep;
	int  hdrlines;
	char *bp;
	char **unk = ct->unknown;
	int bufsiz, len, totlen = 0;
	char *buf;

	bufsiz  = 18;
	if (ct->basetype)
	  bufsiz += strlen(ct->basetype);
	if (ct->subtype)
	  bufsiz += strlen(ct->subtype) + 4;
	if (ct->charset)
	  bufsiz += strlen(ct->charset) + 14;

	if (ct->boundary) {
	  int siz2 = strlen(ct->boundary) + 16 ;
	  if (siz2 > bufsiz) bufsiz = siz2;
	}
	if (ct->name) {
	  int siz2 = strlen(ct->name) + 10 ;
	  if (siz2 > bufsiz) bufsiz = siz2;
	}

	sprintf(buf,"Content-Type:\t%s",ct->basetype);
	bp = buf + strlen(buf);

	if (ct->subtype) {
	  sprintf(bp,"/%s",ct->subtype);
	  bp = bp + strlen(bp);
	}
	if (ct->charset) {
	  strcat(bp,"; charset=");
	  bp = bp + strlen(bp);
	  strqcpy(&buf, bp - buf, &bufsiz, ct->charset);
	  bp = bp + strlen(bp);
	  if (ct->boundary || ct->name || unk)
	    strcat(bp, ";");
	} else if (ct->boundary || ct->name || unk)
	  strcat(bp, ";");

/*if (verboselog) fprintf(verboselog,"CT_out: '%s'\n",buf);*/
	len = strlen(buf);
	linelens[linecnt] = len;
	lines[linecnt++] = dupnstr(buf, len);
	totlen += len;

	*buf = 0; bp = buf;

	if (ct->boundary) {
	  strcat(bp,"\tboundary=");
	  bp = bp + strlen(bp);
	  strqcpy(&buf, bp - buf, &bufsiz, ct->boundary);
	  bp = bp + strlen(bp);
	  if (ct->name || unk)
	    strcat(bp, ";");

/*if (verboselog) fprintf(verboselog,"CT_out: '%s'\n",buf);*/
	  len = strlen(buf);
	  linelens[linecnt] = len;
	  lines[linecnt++] = dupnstr(buf, len);
	  totlen += len + 1;
	  *buf = 0; bp = buf;
	}

	if (ct->name) {
	  strcat(bp,"\tname=");
	  bp = bp + strlen(bp);
	  strqcpy(&buf, bp - buf, &bufsiz, ct->name);
	  bp = bp + strlen(bp);
	  if (unk)
	    strcat(bp, ";");

/*if (verboselog) fprintf(verboselog,"CT_out: '%s'\n",buf);*/
	  len = strlen(buf);
	  linelens[linecnt] = len;
	  lines[linecnt++] = dupnstr(buf, len);
	  totlen += len +1;
	  *buf = 0; bp = buf;
	}

	while (unk && *unk) {
	  int ulen = strlen(*unk);
	  if (bufsiz < ((bp - buf) + 2 + ulen)) {
	    bufsiz = (bp - buf) + 2 + ulen + 8;
	    buf = realloc(buf, bufsiz);
	  }
	  if (((bp - buf) + 2 + ulen) >= 78 && *buf) {
	    /* There is something already, and more wants to push in,
	       but would overflow the 78 column marker! */
	    memcpy(bp, ";", 2);
/*if (verboselog) fprintf(verboselog,"CT_out: '%s'\n",buf);*/
	    len = strlen(buf);
	    linelens[linecnt] = len;
	    lines[linecnt++] = dupnstr(buf, len);
	    totlen += len +1;
	    *buf = 0; bp = buf;
	  }
	  *bp++ = '\t';
	  memcpy(bp, *unk, ulen);
	  bp += ulen;
	  *bp = 0;
	  ++unk;
	}

	if (*buf != 0) {
/*if (verboselog) fprintf(verboselog,"CT_out: '%s'\n",buf);*/
	  len = strlen(buf);
	  linelens[linecnt] = len;
	  lines[linecnt++] = dupnstr(buf, len);
	  totlen += len +1;
	}

	/* The lines are formed, now save them.. */

	basep = (rp->newmsgheader);
	if (*(rp->newmsgheadercvt) != NULL)
	  basep = (rp->newmsgheadercvt);
	hdrlines = 0;
	h1 = *basep;
	for (; *h1; ++h1, ++hdrlines) ;

	newmsgheaders = (char**)malloc(sizeof(char*)*(hdrlines +2));
	if (! newmsgheaders) {
	  zmalloc_failure = 1;
	  if (buf) free(buf);
	  return;
	}
	o2 = newmsgheaders;
	h1 = *basep;
	while (*h1 && h1 != old) {
	  *o2++ = *h1++;
	}
	if (h1 == old) {
	  /* Found the old entry ?  Skip over it.. */
	  ++h1;
	}
	buf = realloc(buf, totlen + 3);
	bp = buf;
	for (i = 0; i < linecnt; ++i) {
	  memcpy(bp, lines[i], linelens[i]);
	  free(lines[i]);
	  bp += linelens[i];
	  if (i+1 < linecnt)
	    *bp++ = '\n';
	}
	*bp = 0;
	*o2++ = buf; /* DO NOT FREE buf HERE! */

	while (*h1)
	  *o2++ = *h1++;
	*o2 = NULL;
	/* Whew...  Copied them over.. */
	/* Scrap the old one: */
	free(*basep);
	/* And replace it with the new one */
	*basep = newmsgheaders;
}

static char * skip_822linearcomments __((char *p));

#if 0 /* This code not needed */
static char * skip_822dtext __((char *p));
static char * skip_822dtext(p)
     char *p;
{
	/* This shall do skipping of RFC 822:
	   3.1.4: Structured Field Bodies  defined ATOMs */

	char *s = p;

#warning "skip_822dtext() code missing!"

	return s;
}
static char * skip_822domainliteral __((char *p));
static char * skip_822domainliteral(p)
     char *p;
{
	/* This shall do skipping of RFC 822: DOMAIN LITERALs */

	char *s = p;

#warning " skip_822dliteral() missing code!"

	return s;
}
#endif

static char * skip_822qtext __((char *p));
static char * skip_822qtext(p)
     char *p;
{
	/* This shall do skipping of RFC 822:  qtext  items */

	char *s = p;

	while (*s && *s != '"' && *s != '\\') ++s;

	return s;
}

static char * skip_822qpair __((char *p));
static char * skip_822qpair(p)
     char *p;
{
	/* This shall do skipping of RFC 822:  quoted-pair  items */

	char *s = p;

	++s; /* We come in with '\\' at *s */
	/* End-of-header ?? */
	if (*s != 0)
	  ++s;

	return s;
}

static char * skip_822quotedstring __((char *p));
static char * skip_822quotedstring(p)
     char *p;
{
	/* This shall do skipping of RFC 822:  quoted-string  items */

	char *s = p;

	/* At arrival:  *s == '"' */
	++s;

	while (*s && *s != '"') {
	  if (*s == '\\')
	    s = skip_822qpair(s);
	  else
	    s = skip_822qtext(s);
	}
	if (*s == '"')
	  ++s;

	return s;
}

static char * skip_comment __((char *p));
static char * skip_comment(p)
     char *p;
{
	/* This shall do skipping of RFC 822:
	   3.1.4: Structured Field Bodies
	   defined COMMENTS */

	char *s = p;

	++s; /* We are called with *s == '(' */
	while (*s && *s != ')') {

	  if (*s == '\\') { /* Quoted-Pair */
	    s = skip_822qpair(s);
	    continue;
	  }
	  if (*s == '(') {
	    s = skip_comment(s);
	    continue;
	  }
	  /* Anything else, just advance... */
	  ++s;
	}
	if (*s == ')')
	  ++s;

	return s;
}

static char * skip_822linearcomments(p)
     char *p;
{
	/* This shall do skipping of RFC 822:
	   3.1.4: Structured Field Bodies
	   defined LINEAR-WHITESPACES and COMMENTS */

	char *s = p;

	while (*s) {

	  while (*s == '\n' || *s == ' ' || *s == '\t') ++s;

	  if (*s == '(') {
	    s = skip_comment(s);
	    continue;
	  }

	  if (*s != 0) /* Not line-end */
	    break; /* Anything else is worth ending the scan ? */
	}

	return s;
}

static char * _skip_822atom __((char *p, int tspecial));
static char * _skip_822atom(p, tspecial)
     char *p;
     int tspecial;
{
	/* This shall do skipping of RFC 822:
	   3.1.4: Structured Field Bodies  defined ATOMs */

	char *s = p;

	while (*s) {
	  int isdelim = 0;
	  char c = *s;
	  if (c == 127 || (0 <= c && c <= ' '))
	    break; /* Delimiters (controls, SPACE) */

	  if (c == '.' && !tspecial) /* RFC 822 at odds with RFC 2045 */
	    isdelim = 1;

	  switch (c) { /* Any of specials ? */
	  case '/':
	  case '?':
	  case '=': /* TokenSpecial as defined at MIME / RFC 2045 et.al. */
	    if (!tspecial) break;
	  case '(':
	  case ')':
	  case '\\':
	  case '<':
	  case '>':
	  case '@':
	  case ',':
	  case ';':
	  case ':':
	  case '"':
	  case '[':
	  case ']':
	    isdelim = 1;
	  default:
	    break;
	  }
	  if (isdelim) break;
	  ++s;
	}

	return s;
}

static char * skip_mimetoken __((char *p));
static char * skip_mimetoken(p)
     char *p;
{
  return _skip_822atom(p, 1);
}


static char * foldmalloccopy __((char *start, char *end));
static char * foldmalloccopy (start, end)
     char *start;
     char *end;
{
	int len = end - start;
	int space = len + 1;
	char *b;

	b = malloc(space);
	if (!b) return NULL; /* UARGH! */

	if (*start == '"') {
	  /* QUOTED-STRING, which we UNQUOTE here */
	  memcpy(b, start+1, len -2);
	  len -= 2;
	} else
	  memcpy(b, start, len);
	b[len] = 0;
	return b;
}


struct ct_data *
parse_content_type(ct_linep)
     char **ct_linep;	/* Could be multiline! */
{
	char *s, *p;
	struct ct_data *ct = (struct ct_data*)malloc(sizeof(struct ct_data));
	int unknowncount = 0;

	if (!ct) return NULL; /* Failed to parse it! */

	ct->basetype = NULL;
	ct->subtype  = NULL;
	ct->charset  = NULL;
	ct->boundary = NULL;
	ct->name     = NULL;
	ct->unknown  = NULL;

	s = *ct_linep;
	s += 13;	/* "Content-Type:" */

	p = skip_822linearcomments(s);
	s = skip_mimetoken(p);

	ct->basetype = foldmalloccopy(p, s);

	p = skip_822linearcomments(s);

	if (*p == '/') {	/* Subtype defined */
	  ++p;
	  p = skip_822linearcomments(p);
	  s = skip_mimetoken(p);

	  ct->subtype = foldmalloccopy(p, s);
	}

	while (*s) {
	  /* Check for possible parameters on the first and/or continuation
	     line(s) of the header line... */
	  char *paramname;
	  char *parval;

	  s = skip_822linearcomments(s);
	  if (!*s) break;
	  if (*s == ';') ++s;
	  else {
	    /* Not found semicolon at expected phase ?? Whoo..
	       Now shall we scan towards the end, and HOPE for the best,
	       or what shall we do ? */
	  }
	  p = skip_822linearcomments(s);
	  if (!*p) return ct;
	  s = skip_mimetoken(p);
	  if (s == p && *s == 0) break; /* Nothing anymore */

	  paramname = foldmalloccopy(p, s);

	  /* Picked up a param name, now scan the value */

	  /* Have seen cases where there was:
		charset = "foo-bar"
	     That is, it had whitespaces around the "=" sign. */

	  s = skip_822linearcomments(s);

	  if (*s == '=') {	    /* What if no `=' ?? */
	    ++s;
	  }
	  p = skip_822linearcomments(s);

	  if (*p == '"') {
	    s = skip_822quotedstring(p);
	  } else {
	    s = skip_mimetoken(p);
	  }

	  parval = foldmalloccopy(p, s);

	  if (CISTREQ("charset",paramname)) {
	    /* Parameter:  charset="..." */
	    ct->charset = parval;
	  } else if (CISTREQ("boundary",paramname)) {
	    /* Parameter:  boundary="...." */
	    ct->boundary = parval;
	  } else if (CISTREQ("name",paramname)) {
	    /* Parameter:  name="...." */
	    ct->name     = parval;
	  } else {
	    /* Unknown parameter.. */
	    int unklen = strlen(parval)+5+strlen(paramname);
	    int unkpos;
	    char *unk;
	    if (!ct->unknown) {
	      ct->unknown = (char**)malloc(sizeof(char*)*2);
	/* FIXME: malloc problem check ?? */
	      ct->unknown[1] = NULL;
	    } else {
	      ct->unknown = (char**)realloc(ct->unknown,
					    sizeof(char*)*(unknowncount+2));
	/* FIXME: malloc problem check ?? */
	    }
	    unk = malloc(unklen);
	/* FIXME: malloc problem check ?? */
	    sprintf(unk, "%s=", paramname);
	    unkpos = strlen(unk);
	    strqcpy(&unk, unkpos, &unklen, parval);
	    ct->unknown[unknowncount] = unk;
	    ct->unknown[unknowncount] = NULL;
	    ++unknowncount;
	    free (parval);
	  }
	  if (paramname)
	    free(paramname);
	}
	return ct;
}

struct cte_data *
parse_content_encoding(cte_linep)
     char **cte_linep;	/* Probably is not a multiline entry.. */
{
	char *s;
	struct cte_data *cte = malloc(sizeof(struct cte_data));

	if (!cte) return NULL;

	s = (*cte_linep) + 26;
	/* Skip over the 'Content-Transfer-Encoding:' */
	s = skip_822linearcomments(s);
	if (*s == '"') {
	  char *p = skip_822quotedstring(s);
	  cte->encoder = foldmalloccopy(s, p);
	  /* FIXME: malloc problem check ?? */
	  s = p;
	} else {
	  char *p = skip_mimetoken(s);
	  cte->encoder = foldmalloccopy(s, p);
	  /* FIXME: malloc problem check ?? */
	  s = p;
	}

	/* while (*s == ' ' || *s == '\t') ++s; */
	/* XX: if (*s) -- errornoeus data */

	return cte;
}


/*
 *  Check for  "Content-conversion: prohibited" -header, and return
 *  non-zero when found it. ---  eh, "-1", return '7' when QP-coding
 *  is mandated.. (test stuff..)
 */
int
check_conv_prohibit(rp)
     struct rcpt *rp;
{
	char **hdrs = *(rp->newmsgheader);
	if (!hdrs) return 0;

	while (*hdrs) {
	  if (CISTREQN(*hdrs,"Content-conversion:", 19)) {
	    char *s = *hdrs + 19;
	    char *p = skip_822linearcomments(s);
	    if (*p == '"') ++p;
	    if (CISTREQN(p,"prohibited",10)) return -2;
	    if (CISTREQN(p,"forced-qp",9)) return 7;
	    /* Prohibits (?) the content conversion.. */
	  }
	  ++hdrs;
	}
	return 0;	/* No "Content-Conversion:" header */
}

static const char *cCTE = "Content-Transfer-Encoding:";
static const char *cCT  = "Content-Type:";

int
cte_check(rp)
     struct rcpt *rp;
{	/* "Content-Transfer-Encoding: 8BIT" */

	char **hdrs = *(rp->newmsgheader);
	int cte = 0;
	int mime = 0;

	/* if (*(rp->newmsgheadercvt) != NULL)
	   hdrs = *(rp->newmsgheadercvt); */
	/* here we check the ORIGINAL headers.. */

	if (!hdrs) return 0;

	while (*hdrs && (!mime || !cte)) {
	  char *buf = *hdrs;
	  if (!cte && CISTREQN(buf,cCTE,26)) {
	    buf += 26;
	    buf = skip_822linearcomments(buf);
	    if (*buf == '"') ++buf;
	    if (*buf == '8' /* 8BIT */) cte = 8;
	    else if (*buf == '7' /* 7BIT */) cte = 7;
	    else if (*buf == 'Q' || *buf == 'q') cte = 9; /*QUOTED-PRINTABLE*/
	    else cte = 1; /* Just something.. BASE64 most likely .. */
	  } else if (!mime && CISTREQN(buf,"MIME-Version:",13)) {
	    mime = 1;
	  }
	  ++hdrs;
	}
	if (mime && cte == 0) cte = 2;
	if (!mime) cte = 0;
	return cte;
}

char **  /* Return a pointer to header line pointer */
has_header(rp,keystr)
     struct rcpt *rp;
     const char *keystr;
{
	char **hdrs = *(rp->newmsgheader);
	int keylen = strlen(keystr);

	if (*(rp->newmsgheadercvt) != NULL)
	  hdrs = *(rp->newmsgheadercvt);

	if (hdrs)
	  while (*hdrs) {
	    if (CISTREQN(*hdrs,keystr,keylen)) return hdrs;
	    ++hdrs;
	  }
	return NULL;
}

void
delete_header(rp,hdrp)	/* Delete the header, and its possible
			   continuation lines */
     struct rcpt *rp;
     char **hdrp;
{
	char **h1 = hdrp;
	char **h2 = hdrp+1;
	ctlfree(rp->desc,*hdrp);
	while (*h2)
	  *h1++ = *h2++;
	/* And one more time.. To copy the terminating NULL ptr. */
	*h1++ = *h2++;
}

int
downgrade_charset(rp, verboselog)
     struct rcpt *rp;
     FILE *verboselog;
{
	char **CT   = NULL;
	char **CTE  = NULL;
	struct ct_data *ct;

	/* Convert IN PLACE! -- if there is a need.. */
	CT = has_header(rp,cCT);
	CTE  = has_header(rp,cCTE);
	if (CT == NULL || CTE == NULL) return 0; /* ??? */

	ct = parse_content_type(CT);

	if (ct->basetype == NULL ||
	    ct->subtype  == NULL ||
	    !CISTREQ(ct->basetype,"text") ||
	    !CISTREQ(ct->subtype,"plain")) return 0; /* Not TEXT/PLAIN! */

	if (ct->charset &&
	    !CISTREQN(ct->charset,"ISO-8859",8) &&
	    !CISTREQN(ct->charset,"KOI8",4)    ) return 0; /* Not ISO-* */

	if (ct->charset)
	  free(ct->charset);

	strcpy(*CTE, "Content-Transfer-Encoding: 7BIT");

	ct->charset = strdup("US-ASCII");

	/* Delete the old one, and place there the new version.. */
	output_content_type(rp,ct,CT);

	return 1;
}

int
downgrade_headers(rp, convertmode, verboselog)
     struct rcpt *rp;
     CONVERTMODE convertmode;
     FILE *verboselog;
{
	char ***oldmsgheader;
	char **CT   = NULL;
	char **CTE  = NULL;
	char **MIME = NULL;
	/* char **receivedp = NULL; */
	struct ct_data *ct;
	int lines = 0;
	int i;
	int is_textplain;
	int newlen;

	if (*(rp->newmsgheadercvt) != NULL)
	  return 0; /* Already converted ! */

	if (!cvtspace_copy(rp)) return 0; /* XX: auch! */

	oldmsgheader = rp->newmsgheadercvt;

	if (oldmsgheader)
	  while ((*oldmsgheader)[lines]) ++lines;

	MIME = has_header(rp,"MIME-Version:");
	CT   = has_header(rp,cCT);
	CTE  = has_header(rp,cCTE);

	if (verboselog)
	  fprintf(verboselog,"Header conversion control code: %d\n",convertmode);

	if (convertmode == _CONVERT_UNKNOWN) {
	  /* We downgrade by changing it to Q-P as per RFC 1428/Appendix A */
	  static const char *warning_lines[] = {
"X-Warning: Original message contained 8-bit characters, however during",
"\t   the SMTP transport session the receiving system did not announce",
"\t   capability of receiving 8-bit SMTP (RFC 1651-1653), and as this",
"\t   message does not have MIME headers (RFC 2045-2049) to enable",
"\t   encoding change, we had very little choice.",
"X-Warning: We ASSUME it is less harmful to add the MIME headers, and",
"\t   convert the text to Quoted-Printable, than not to do so,",
"\t   and to strip the message to 7-bits.. (RFC 1428 Appendix A)",
"X-Warning: We don't know what character set the user used, thus we had to",
"\t   write these MIME-headers with our local system default value.",
"MIME-Version: 1.0",
"Content-Transfer-Encoding: QUOTED-PRINTABLE",
NULL };

	  char **newmsgheaders = (char**)malloc(sizeof(char**)*(lines+15));
	  char *defcharset = getzenv("DEFCHARSET");
	  char *newct;

	/* FIXME: malloc problem check ?? */

	  if (!defcharset)
	    defcharset = "ISO-8859-1";
#ifdef HAVE_ALLOCA
	  newct = alloca(strlen(defcharset)+2+sizeof("Content-Type: TEXT/PLAIN; charset="));
#else
	  newct = malloc(strlen(defcharset)+2+sizeof("Content-Type: TEXT/PLAIN; charset="));
	/* FIXME: malloc problem check ?? */
#endif
	  sprintf(newct,"Content-Type: TEXT/PLAIN; charset=%s",defcharset);

	  if (!newmsgheaders) return 0; /* XX: Auch! */

	  for (lines = 0; warning_lines[lines] != NULL; ++lines)
	    newmsgheaders[lines] = strdup(warning_lines[lines]);
	  newmsgheaders[lines++] = strdup(newct);
#ifndef HAVE_ALLOCA
	  free(newct);
#endif
	  if (CT)	/* XX: This CAN be wrong action for
			       some esoteric SysV mailers.. */
	    delete_header(rp,CT);
	  /* These most probably won't happen, but the delete_header()
	     does scram the pointers anyway.. */
	  if (MIME)
	    delete_header(rp,MIME);
	  if (CTE)
	    delete_header(rp,CTE);

	  for (i = 0; (*oldmsgheader)[i] != NULL; ++i)
	    newmsgheaders[lines+i] = (*oldmsgheader)[i];
	  newmsgheaders[lines+i] = NULL;

	  free(*oldmsgheader); /* Free the old one.. */
	  *oldmsgheader = newmsgheaders;
	  return 0;
	}

	/* Now look for the  Content-Transfer-Encoding:  header */

	/*receivedp = has_header(rp,"Received:");*/

	if (CTE == NULL) return 0; /* No C-T-E: ??? */

	/* strlen("Content-Transfer-Encoding: QUOTED-PRINTABLE") == 43
	   strlen("Content-Transfer-Encoding: 7BIT") == 31		*/

	/* Allocate space for the new value of  C-T-E */
	newlen = 31;
	if (convertmode == _CONVERT_QP) newlen = 43;

	*CTE = (char *)ctlrealloc(rp->desc,*CTE,newlen+2);

	/* Ok, this was C-T-E: 7BIT, turn charset to US-ASCII if it
	   was  ISO-*  */

	if (CT == NULL) { /* ???? Had C-T-E, but no C-T ?? */
	  if (verboselog)
	    fprintf(verboselog,"Had Content-Transfer-Encoding -header, but no Content-Type header ???  Adding C-T..\n");
	  append_header(rp,"Content-Type: TEXT/PLAIN; charset=US-ASCII");
	  return 0;
	}

	ct = parse_content_type(CT);

	is_textplain = (ct->basetype != NULL &&
			ct->subtype  != NULL &&
			CISTREQ(ct->basetype,"text") &&
			CISTREQ(ct->subtype,"plain"));

	if (ct->charset && is_textplain &&
	    (convertmode != _CONVERT_QP) &&
	    /* Change to US-ASCII for known 7-bit clean
	       inputs where the claimed charset(prefix) has
	       all its charsets equal to US-ASCII in the
	       low 128 characters. */
	    (CISTREQN(ct->charset,"ISO-8859",8) ||
	     CISTREQN(ct->charset,"KOI8",4)        )) {

	  if (ct->charset)
	    free(ct->charset);

	  ct->charset = strdup("US-ASCII");
	  strcpy(*CTE, "Content-Transfer-Encoding: 7BIT");

	}

	/* Delete the old one, and place there the new version.. */
	output_content_type(rp,ct,CT);

	if (convertmode == _CONVERT_QP) {

	  /* Preceding 'output_content_type()' call scrambled lists,
	     we have to reget the CTE pointer */
	  CTE  = has_header(rp,cCTE);

	  strcpy(*CTE, "Content-Transfer-Encoding: QUOTED-PRINTABLE");
	  mime_received_convert(rp," convert rfc822-to-quoted-printable");

	}

	return 1; /* Non-zero for success! */
}


static int /* Return non-zero for success */
mime_received_convert(rp, convertstr)
	struct rcpt *rp;
	char *convertstr;
{
	int convertlen = strlen(convertstr);
	char *semic = NULL;
	char **schdr = NULL;
	int semicindex = -1;

	char **inhdr = *(rp->newmsgheadercvt);
	char **hdro = NULL;
	char *sc;

	/* We have one advantage: The "Received:" header we
	   want to fiddle with is the first one of them. */

	if (!inhdr || !CISTREQN(*inhdr,"Received:",9)) {
	  return 0; /* D'uh ??  Not 'Received:' ??? */
	}

	/* Look for the LAST semicolon in this Received: header.. */

	sc = strrchr(*inhdr, ';');
	if (sc) {
	  semic = sc;
	  schdr = inhdr;
	  semicindex = sc - *inhdr;
	}
	hdro = inhdr;

	/* Now 'semic' is either set, or not; if set, 'schdr' points
	   to the begin of the line where it is.
	   If 'semic' is not set, 'hdro' will point to the last line
	   of the 'Received:' header in question */

	if (!semic) {
	  schdr = hdro;
	  semicindex = strlen(*schdr);
	  semic = *schdr + semicindex;
	}

	{
	  int receivedlen = strlen(*schdr);
	  char *newreceived = malloc(receivedlen + convertlen + 1);
	  int  semicindex;

	  if (!newreceived) return 0; /* Failed malloc.. */

	  semicindex = semic - *schdr;
	  memcpy(newreceived, *schdr, semicindex);
	  memcpy(newreceived+semicindex, convertstr, convertlen);
	  strcpy(newreceived+semicindex+convertlen, (*schdr) + semicindex);
	  newreceived[receivedlen + convertlen] = '\0';

	  ctlfree(rp->desc,*schdr);
	  *schdr = newreceived;

	  /* if (verboselog) {
	     fprintf(verboselog,"Rewriting 'Received:' headers.\n");
	     fprintf(verboselog,"The new line is: '%s'\n",*hdro);
	     }
	  */
	}
	return 1;
}

/* [mea] Now change  C-T-E: QUOTED-PRINTABLE  to  C-T-E: 8BIT -- in place.. */
int /* Return non-zero for success */
qp_to_8bit(rp)
	struct rcpt *rp;
{
	char **inhdr;
	char *hdr, *p;
	char **CTE, **CT;
	struct ct_data *ct;

	if (!cvtspace_copy(rp))
	  return 0;	/* Failed to copy ! */

	inhdr = *(rp->newmsgheadercvt);

	CTE = has_header(rp,cCTE);
	CT  = has_header(rp,cCT);

	if (!CTE || !CT) return 0; /* No C-T-E header! */

	ct = parse_content_type(CT);

	if (ct->basetype == NULL ||
	    ct->subtype  == NULL ||
	    !CISTREQ(ct->basetype,"text") ||
	    !CISTREQ(ct->subtype,"plain") ) return 0; /* Not TEXT/PLAIN! */

	hdr = *CTE;

	p = hdr + 26;
	p = skip_822linearcomments(p);

	if (*p == 'Q' || *p == 'q') {
	  if (strlen(hdr+26) >= 5)
	    strcpy(hdr+26," 8BIT");
	  else { /* No room ??  What junk ?? */
	    delete_header(rp,CTE);
	    append_header(rp,"Content-Transfer-Encoding: 8BIT");
	  }

	  if (!mime_received_convert(rp," convert rfc822-to-8bit"))
	    return 0;	/* "Received:" conversion failed! */

	} /* else probably already decoded */

	return 1;
}


/* Return non-zero for any 8-bit char in the headers */
int
headers_need_mime2(rp)
	struct rcpt *rp;
{
	char **inhdr = *(rp->newmsgheader);
	while (inhdr && *inhdr) {
	  u_char *hdr = (u_char *)*inhdr;
	  for ( ; *hdr != 0 ; ++hdr)
	    if (*hdr != '\t' && (*hdr < ' ' || *hdr > 126))
	      return 1;
	  ++inhdr;
	}
	return 0; /* No 8-bit chars in the headers */
}
