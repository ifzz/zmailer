/*
 *	Copyright 1990 by Rayan S. Zachariassen, all rights reserved.
 *	This will be free software, but only when it is finished.
 */

/*
 *	Copyright 1994-1997 by Matti Aarnio
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
 *    rp->newmsgheader    is a pointer to an element on  rp->desc->headers[]
 *    rp->newmsgheadercvt is respectively an elt on  rp->desc->headerscvt[]
 *
 * The routine-collection   mimeheaders.c  creates converted headers,
 * if the receiving system needs them. Converted data is created only
 * once per  rewrite-rule group, so there should not be messages which
 * report  "Received: ... convert XXXX convert XXXX convert XXXX; ..."
 * for as many times as there there are recipients for the message.
 * [mea@utu.fi] - 25-Jul-94
 */

#include "hostenv.h"
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#include <sysexits.h>
#include "mail.h"
#include "malloc.h"
#include "libz.h"
#include "ta.h"

#if defined(HAVE_MMAP) && defined(TA_USE_MMAP)
#include <sys/mman.h>
#endif

extern char *routermxes __((char *, struct taddress *));
extern int errno;

#ifndef strrchr
extern char *strrchr();
#endif

static struct taddress *ctladdr __((char *cp));


#ifndef	MAXPATHLEN
#define	MAXPATHLEN 1024
#endif	/* !MAXPATHLEN */


void
ctlfree(dp,anyp)
	struct ctldesc *dp;
	void *anyp;
{
	unsigned long lowlim = (unsigned long) dp->contents;
	unsigned long highlim = lowlim + dp->contentsize;
	if ((unsigned long)anyp < lowlim ||
	    (unsigned long)anyp > highlim)
	  free(anyp);	/* It isn't within DP->CONTENTS data.. */
}

void *
ctlrealloc(dp,anyp,size)
	struct ctldesc *dp;
	void *anyp;
	size_t size;
{
	unsigned long lowlim = (unsigned long) dp->contents;
	unsigned long highlim = lowlim + dp->contentsize;
	void *anyp2;

	if ((unsigned long)anyp < lowlim ||
	    (unsigned long)anyp > highlim)
	  return realloc(anyp,size);	/* It isn't within
					   DP->CONTENTS data.. */
	/* It is within the data, allocate a new storage.. */
	anyp2 = (void*) malloc(size);
	if (anyp2)
	  memcpy(anyp2,anyp,size);
	return anyp2;
}

void
ctlclose(dp)
	struct ctldesc *dp;
{
	struct taddress *ap, *nextap;
	struct rcpt *rp, *nextrp;
	char ***msghpp;

	for (rp = dp->recipients; rp != NULL; rp = rp->next) {
	  if (rp->lockoffset == 0)
	    continue;
	  diagnostic(rp, EX_TEMPFAIL, 0, "address was left locked!!");
	}
#if defined(HAVE_MMAP) && defined(TA_USE_MMAP)
	if (dp->let_buffer != NULL)
	  munmap((void*)dp->let_buffer, dp->let_end - dp->let_buffer);
	dp->let_buffer = NULL;
	if (dp->ctlmap != NULL)
	  munmap((void*)dp->ctlmap, dp->contentsize);
	dp->ctlmap = NULL;
#endif
	if (dp->ctlfd >= 0)
	  close(dp->ctlfd);
	if (dp->msgfd >= 0)
	  close(dp->msgfd);
	if (dp->contents != NULL)
	  free((void*)dp->contents);
	if (dp->offset != NULL)
	  free((char *)dp->offset);
	for (ap = dp->senders; ap != NULL; ap = nextap) {
	  nextap = ap->link;
	  free((char *)ap);
	}
	dp->senders = NULL; /* XX: Excessive caution.. */
	for (rp = dp->recipients; rp != NULL; rp = nextrp) {
	  nextrp = rp->next;
	  ap = rp->addr;
	  if (ap->routermxes) {
	    free((char *)ap->routermxes);
	  }
	  free((char *)rp);
	}
	dp->recipients = NULL; /* XX: Excessive caution.. */
	/* Free ALL dp->msgheader's, if they have been reallocated.
	   Don't free on individual recipients, only on this global set.. */
	for (msghpp = dp->msgheaders; msghpp &&  *msghpp; ++msghpp) {
	  char **msghp = *msghpp;
	  for ( ; msghp && *msghp ; ++msghp )
	    ctlfree(dp,*msghp);
	  free(*msghpp);
	}
	free(dp->msgheaders);
	dp->msgheaders = NULL; /* XX: Excessive caution.. */
	for (msghpp = dp->msgheaderscvt; msghpp &&  *msghpp; ++msghpp) {
	  char **msghp = *msghpp;
	  for ( ; msghp && *msghp ; ++msghp )
	    free(*msghp); /* These CVTs are always malloc()ed strings */
	  free(*msghpp);
	}
	free(dp->msgheaderscvt);
	dp->msgheaders = NULL; /* XX: Excessive caution.. */
}


static struct taddress *
ctladdr(cp)
	char *cp;
{
	struct taddress *ap;

	ap = (struct taddress *)emalloc(sizeof (struct taddress));
	if (ap == NULL)
		return NULL;
	ap->link = NULL;
	/* While space: */
	while (*cp != 0 && (*cp == ' ' || *cp == '\t')) ++cp;
	/* CHANNEL: */
	ap->channel = cp;
	/* While not space: */
	cp = skip821address(cp);
	*cp++ = '\0';
	/* While space: */
	while (*cp != 0 && (*cp == ' ' || *cp == '\t')) ++cp;
	/* HOST: */
	ap->host = cp;
	ap->routermxes = NULL;
	if (*cp == '(')
	  cp = routermxes(cp,ap);  /* It is a bit complex.. */
	else {
	  /* While not space: */
	  cp = skip821address(cp);
	  *cp++ = '\0';
	}
	/* While space: */
	while (*cp != 0 && (*cp == ' ' || *cp == '\t')) ++cp;
	ap->user = cp;
	/* the user value is allowed to have embedded whitespace */
	cp = skip821address(cp);
	*cp++ = '\0';
	ap->misc = cp;
	return ap;
}

#ifdef __STDC__

struct ctldesc *
ctlopen(const char *file, const char *channel, const char *host,
	int *exitflagp, int (*selectaddr)(const char *, const char *, void *),
	void *saparam,  int (*matchrouter)(const char *, struct taddress *, void *),
	void *mrparam)

#else

struct ctldesc *
ctlopen(file, channel, host, exitflagp, selectaddr, saparam, matchrouter, mrparam)
	const char *file, *channel, *host;
	int *exitflagp;
	int (*selectaddr)  __((const char *, const char *, void *));
	void *saparam;
	int (*matchrouter) __((const char *, struct taddress *, void *));
	void *mrparam;

#endif
{
	register char *s, *contents;
	char *mfpath, *delayslot;
	int i, n;
	struct taddress *ap;
	struct rcpt *rp = NULL, *prevrp = NULL;
	struct stat stbuf;
	char ***msgheaders = NULL;
	char ***msgheaderscvt = NULL;
	int headers_cnt = 0;
	int headers_spc = 0;
	int largest_headersize = 80; /* Some magic minimum.. */
	char dirprefix[8];
	int mypid = getpid();

	static struct ctldesc d; /* ONLY ONE OPEN AT THE TIME! */

	if (selectaddr == ctlsticky)
	  ctlsticky(NULL,NULL,NULL); /* Reset the internal state.. */

	memset(&d,0,sizeof(d));
	if (*file >= 'A') {
	  char *p;
	  /* Has some hash subdirectory in front of itself */
	  strncpy(dirprefix,file,sizeof(dirprefix));
	  dirprefix[sizeof(dirprefix)-1] = 0;
	  p = strrchr(dirprefix,'/');
	  if (p) *++p = 0;
	  /*  "A/B/"  */
	} else
	    dirprefix[0] = 0;


	d.msgfd = -1; /* The zero is not always good for your health .. */
	d.ctlfd = open(file, O_RDWR, 0);
	if (d.ctlfd < 0) {
	  char cwd[MAXPATHLEN], buf[MAXPATHLEN+MAXPATHLEN+100];
	  int e = errno;	/* Save it over the getwd() */

#ifdef	HAVE_GETCWD
	  getcwd(cwd,MAXPATHLEN);
#else
	  getwd(cwd);
#endif
	  sprintf(buf,
		  "Cannot open control file \"%%s\" from \"%s\" for \"%%s/%%s\" as uid %d! (%%m)",
		  cwd, geteuid());
	  errno = e;
	  if (host == NULL)
	    host = "-";
	  warning(buf, file, channel, host);
	  return NULL;
	}
	if (fstat(d.ctlfd, &stbuf) < 0) {
	  warning("Cannot stat control file \"%s\"! (%m)", file);
	  close(d.ctlfd);
	  return NULL;
	}
	if (!S_ISREG(stbuf.st_mode)) {
	  warning("Control file \"%s\" is not a regular file!", file);
	  close(d.ctlfd);
	  return NULL;
	}
	/* 4 is the minimum number of characters per line */
	n = sizeof (long) * (stbuf.st_size / 4);
	if ((d.contents = (s = emalloc((u_int)stbuf.st_size+1))) == NULL
	    || (d.offset = (long *)emalloc((u_int)n)) == NULL) {
	  warning("Out of virtual memory!", (char *)NULL);
	  exit(EX_SOFTWARE);
	}
	contents = s;

	fcntl(d.ctlfd, F_SETFD, 1); /* Close-on-exec */

#if defined(HAVE_MMAP) && defined(TA_USE_MMAP)
#ifndef MAP_VARIABLE
# define MAP_VARIABLE 0
#endif
#ifndef MAP_FILE
# define MAP_FILE 0
#endif
	/* We do recipient locking via MMAP_SHARED RD/WR ! Less syscalls.. */
	d.ctlmap = (char *)mmap(NULL, stbuf.st_size,
				PROT_READ|PROT_WRITE,
				MAP_FILE|MAP_SHARED|MAP_VARIABLE,
				d.ctlfd, 0);
	if ((int)d.ctlmap == -1)
	  d.ctlmap = NULL; /* Failed ?? */
#else
	d.ctlmap = NULL;
#endif
	d.contentsize = (int) stbuf.st_size;
	s[ d.contentsize ] = 0; /* Treat it as a long string.. */
	if (read(d.ctlfd, s, d.contentsize) != d.contentsize) {
	  warning("Wrong size read from control file \"%s\"! (%m)",
		  file);
	  free((void*)d.contents);
	  free((void*)d.offset);
	  close(d.ctlfd);
	  return NULL;
	}
	n = markoff(contents, d.contentsize, d.offset, file);
	if (n < 4) {
	  /*
	   * If it is less than the minimum possible number of control
	   * lines, then there is something wrong...
	   */
	  free(contents);
	  free((char *)d.offset);
	  close(d.ctlfd);
#if defined(HAVE_MMAP) && defined(TA_USE_MMAP)
	  if (d.ctlmap != NULL)
	    munmap((void*)d.ctlmap, d.contentsize);
#endif	  
	  warning("Truncated or illegal control file \"%s\"!", file);
	  /* exit(EX_PROTOCOL); */
	  sleep(60);
	  return NULL;
	}

	s = strrchr(file,'/');	/* In case the file in in a subdir.. */
	if (s)
	  d.ctlid = atol(s+1);
	else
	  d.ctlid = atol(file);
	d.senders = NULL;
	d.recipients = NULL;
	d.rcpnts_total = 0;
	d.rcpnts_remaining = 0;
	d.rcpnts_failed = 0;
	d.logident   = "none";
	d.envid      = NULL;
	d.dsnretmode = NULL;
	d.verbose    = NULL;

	/* run through the file and set up the information we need */
	for (i = 0; i < n; ++i) {
	  if (*exitflagp && d.recipients == NULL)
	    break;
	  /* Shudder... we trash the memory block here.. */
	  s = contents + d.offset[i];
	  switch (*s) {
	  case _CF_SENDER:
	    ap = ctladdr(s+2);
	    ap->link  = d.senders;
	    /* Test if this is "error"-channel..
	       If it is,  ap->user  points to NUL string. */
	    /* mea: altered the scheme, we must detect the "error" channel
	       otherwise */
	    /* if (strcmp(ap->channel,"error")==0)
	       ap->user = ""; */
	    d.senders = ap;
	    break;
	  case _CF_RECIPIENT:
	    ++s;
	    /* Calculate statistics .. Scheduler asks for it.. */
	    d.rcpnts_total += 1;
	    if (*s == _CFTAG_NOTOK) {
	      d.rcpnts_failed    += 1;
	      prevrp = NULL;
	    } else if (*s != _CFTAG_OK) {
	      d.rcpnts_remaining += 1;
	    }

	    if (*s != _CFTAG_NORMAL || d.senders == NULL)
	      break;
	    ++s;
	    s += _CFTAG_RCPTPIDSIZE;
	    if (*s >= 'a' && *s <= 'z') {
	      /* The old style file.. w/o delay indicator slot */
	      if ((ap = ctladdr(s)) == NULL) {
		warning("Out of virtual memory!", (char *)NULL);
		*exitflagp = 1;
		break;
	      }
	      delayslot = NULL;
	    } else if ((ap = ctladdr(s+_CFTAG_RCPTDELAYSIZE)) == NULL) {
	      warning("Out of virtual memory!", (char *)NULL);
	      *exitflagp = 1;
	      break;
	    } else
	      delayslot = s;

	    /* [mea] understand  'host' of type:
	       "((mxer)(mxer mxer))"   */
	    if ((channel != NULL
		 && strcmp(channel, ap->channel) != 0)
#if 1
		|| (ap->routermxes != NULL && matchrouter != NULL
		    && !(*matchrouter)(host, ap, mrparam))
#endif
		|| (ap->routermxes == NULL && selectaddr != NULL
		    && !(*selectaddr)(host, (const char *)ap->host, saparam))
		|| (ap->routermxes == NULL && selectaddr == NULL
		    && host != NULL && cistrcmp(host,ap->host) !=0)
		|| !lockaddr(d.ctlfd, d.ctlmap, d.offset[i]+1,
			     _CFTAG_NORMAL, _CFTAG_LOCK, file, host, mypid)) {
	      if (ap->routermxes)
		free((char *)ap->routermxes);
	      ap->routermxes = NULL;
	      free((char *)ap);
	      break;
	    }
	    ap->link = d.senders; /* point at sender address */
	    rp = (struct rcpt *)emalloc(sizeof (struct rcpt));
	    if (rp == NULL) {
	      lockaddr(d.ctlfd, d.ctlmap, d.offset[i]+1,
		       _CFTAG_LOCK, _CFTAG_DEFER, file, host, mypid);
	      warning("Out of virtual memory!", (char *)NULL);
	      *exitflagp = 1;
	      break;
	    }
	    rp->addr = ap;
	    rp->delayslot = delayslot;
	    rp->id = d.offset[i];
	    /* XX: XOR locks are different */
	    rp->lockoffset = rp->id + 1;
	    rp->next = d.recipients;
	    rp->desc = &d;
	    rp->orcpt = NULL;
	    rp->inrcpt = NULL;
	    rp->notify = NULL;
	    rp->notifyflgs = _DSN_NOTIFY_FAILURE; /* Default behaviour */
	    d.recipients = rp;
	    rp->status = EX_OK;
	    rp->newmsgheader = NULL;
	    rp->drptoffset   = -1;
	    rp->headeroffset = -1;
	    prevrp = rp;
	    break;
	  case _CF_RCPTNOTARY:
	    /*  IETF-NOTARY-DSN  DATA */
	    ++s;
	    if (prevrp != NULL) {
	      prevrp->drptoffset = d.offset[i];
	      while (*s) {
		while (*s && (*s == ' ' || *s == '\t')) ++s;
		if (cistrncmp("NOTIFY=",s,7)==0) {
		  char *p;
		  s += 7;
		  prevrp->notify = p = s;
		  while (*s && *s != ' ' && *s != '\t') ++s;
		  if (*s) *s++ = 0;
		  prevrp->notifyflgs = 0;
		  while (*p) {
		    if (cistrncmp("NEVER",p,5)==0) {
		      p += 5;
		      prevrp->notifyflgs |= _DSN_NOTIFY_NEVER;
		    } else if (cistrncmp("DELAY",p,5)==0) {
		      p += 5;
		      prevrp->notifyflgs |= _DSN_NOTIFY_DELAY;
		    } else if (cistrncmp("SUCCESS",p,7)==0) {
		      p += 7;
		      prevrp->notifyflgs |= _DSN_NOTIFY_SUCCESS;
		    } else if (cistrncmp("FAILURE",p,7)==0) {
		      p += 7;
		      prevrp->notifyflgs |= _DSN_NOTIFY_FAILURE;
		    } else
		      break; /* Burp !? */
		    if (*p == ',') ++p;
		  }
		  continue;
		}
		if (cistrncmp("ORCPT=",s,6)==0) {
		  s += 6;
		  prevrp->orcpt = s;
		  while (*s && *s != ' ' && *s != '\t') ++s;
		  if (*s) *s++ = 0;
		  continue;
		}
		if (cistrncmp("INRCPT=",s,7)==0) {
		  s += 7;
		  prevrp->inrcpt = s;
		  while (*s && *s != ' ' && *s != '\t') ++s;
		  if (*s) *s++ = 0;
		  continue;
		}
		/* XX: BOO! Unknown value! */
		while (*s && *s != ' ' && *s != '\t') ++s;
	      }
	      /* Previous entry added, no more..! */
	      prevrp = NULL;
	    }
	    break;
	  case _CF_MSGHEADERS:
	    {
	      char **msgheader = NULL;
	      char *ss;
	      int  headerlines = 0;
	      int  headerspace = 0;
	      int  headersize  = strlen(s);

	      if (headersize > largest_headersize)
		largest_headersize = headersize;
	      /* position pointer at start of the header */
	      while (*s && *s != '\n')
		++s;
	      ++s;

	      /* Collect all the header lines into
		 individual lines.. [mea] */
	      while (*s) {
		if (headerlines == 0) {
		  msgheader = (char **)malloc(sizeof(char**)*8);
		  headerspace = 7;
		}
		if (headerlines+1 >= headerspace) {
		  headerspace += 8;
		  msgheader =
		    (char**)realloc(msgheader,
				    sizeof(char**)*headerspace);
		}
		ss = s;
		while (*ss && *ss != '\n') ++ss;
		if (*ss == '\n') *ss++ = '\0';
		msgheader[headerlines++] = s;
		msgheader[headerlines] = NULL;
		s = ss;
	      }

	      /* And the global connection.. */
	      if (headers_cnt == 0) {
		msgheaders = (char***)malloc(sizeof(char***)*8);
		msgheaderscvt = (char***)malloc(sizeof(char***)*8);
		headers_spc = 7;
	      }
	      if (headers_cnt+1 >= headers_spc) {
		headers_spc += 8;
		msgheaders =
		  (char***)realloc(msgheaders,
				   sizeof(char***)*headers_spc);
		msgheaderscvt =
		  (char***)realloc(msgheaderscvt,
				   sizeof(char***)*headers_spc);
	      }
	      msgheaders   [headers_cnt] = msgheader;
	      msgheaderscvt[headers_cnt] = NULL;

	      /* fill in header * of recent recipients */
	      for (rp = d.recipients;
		   rp != NULL && rp->newmsgheader == NULL;
		   rp = rp->next) {
		rp->newmsgheader    = &msgheaders   [headers_cnt];
		rp->newmsgheadercvt = &msgheaderscvt[headers_cnt];
		rp->headeroffset    = d.offset[i] + 2;
	      }

	      msgheaders   [++headers_cnt] = NULL;
	      msgheaderscvt[  headers_cnt] = NULL;
	    }
	    break;
	  case _CF_MESSAGEID:
	    d.msgfile = s+2;
	    break;
	  case _CF_DSNENVID:
	    d.envid = s+2;
	    break;
	  case _CF_DSNRETMODE:
	    d.dsnretmode = s+2;
	    break;
	  case _CF_BODYOFFSET:
	    d.msgbodyoffset = (long)atoi(s+2);
	    break;
	  case _CF_LOGIDENT:
	    d.logident = s+2;
	    break;
	  case _CF_VERBOSE:
	    d.verbose = s+2;
	    break;
	  default:		/* We don't use them all... */
	    break;
	  }
	}

	d.msgheaders    = msgheaders;		/* Original headers	*/
	d.msgheaderscvt = msgheaderscvt;	/* Modified set		*/

	if (d.recipients == NULL) {
	  ctlclose(&d);
	  return NULL;
	}

#ifdef USE_ALLOCA
	mfpath = alloca((u_int)5 + sizeof(QUEUEDIR)
			+ strlen(dirprefix) + strlen(d.msgfile));
#else
	mfpath = emalloc((u_int)5 + sizeof(QUEUEDIR)
			 + strlen(dirprefix) + strlen(d.msgfile));
#endif
	sprintf(mfpath, "../%s/%s%s", QUEUEDIR, dirprefix, d.msgfile);
	if ((d.msgfd = open(mfpath, O_RDONLY, 0)) < 0) {
	  int e = errno;
	  for (rp = d.recipients; rp != NULL; rp = rp->next) {
	    diagnostic(rp, EX_UNAVAILABLE, 0,
		       "message file is missing(!) -- possibly due to delivery scheduler restart.  Consider resending your message");
	  }
	  errno = e;
	  warning("Cannot open message file \"%s\"! (%m)", mfpath);
#ifndef USE_ALLOCA
	  free(mfpath);
#endif
	  ctlclose(&d);
	  return NULL;
	}
	if (fstat(d.msgfd,&stbuf) < 0) {
	  stbuf.st_mode = S_IFCHR; /* Make it to be something what it
				      clearly can't be.. */
	}
	if (!S_ISREG(stbuf.st_mode)) {
	  for (rp = d.recipients; rp != NULL; rp = rp->next) {
	    diagnostic(rp, EX_UNAVAILABLE, 0,
		       "Message file is not a regular file!");
	  }
	  warning("Cannot open message file \"%s\"! (%m)", mfpath);
#ifndef USE_ALLOCA
	  free(mfpath);
#endif
	  ctlclose(&d);
	  close(d.msgfd);
	  return NULL;
	}

	fcntl(d.msgfd, F_SETFD, 1); /* Close-on-exec */

#if defined(HAVE_MMAP) && defined(TA_USE_MMAP)
	d.let_buffer = (char *)mmap(NULL, stbuf.st_size, PROT_READ,
				    MAP_FILE|MAP_SHARED|MAP_VARIABLE,
				    d.msgfd, 0);
	if ((long)d.let_buffer == -1L) {
	  warning("Out of MMAP() memory! Tried to map in (r/o) %d bytes (%m)",
		  stbuf.st_size);
#ifndef USE_ALLOCA
	  free(mfpath);
#endif
	  ctlclose(&d);
	  close(d.msgfd);
	  return NULL;
	}
	d.let_end    = d.let_buffer + stbuf.st_size;
#endif

#ifndef USE_ALLOCA
	  free(mfpath);
#endif

	/* The message file mtime -- arrival of the message to the system */
	d.msgmtime = stbuf.st_mtime;

	/* Estimate the size of the message file when sent out.. */
	d.msgsizeestimate  = stbuf.st_size - d.msgbodyoffset;
	d.msgsizeestimate += largest_headersize;
	/* A nice fudge factor, usually this is enough..                 */
	/* Add 3% for CRLFs.. -- assume average line length of 35 chars. */
	d.msgsizeestimate += (3 * d.msgsizeestimate) / 100;

	return &d;
}

int
ctlsticky(spec_host, addr_host, cbparam)
	const char *spec_host, *addr_host;
	void *cbparam;
{
	static const char *hostref = NULL;

	if (hostref == NULL) {
	  if (spec_host != NULL)
	    hostref = spec_host;
	  else
	    hostref = addr_host;
	}
	if (spec_host == NULL && addr_host == NULL) {
	  hostref = NULL;
	  return 0;
	}
	return cistrcmp(hostref, addr_host) == 0;
}