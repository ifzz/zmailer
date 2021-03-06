/*
 *	"Expirer", copyright Matti Aarnio 1997.
 *
 *	Copyright 1988 by Rayan S. Zachariassen, all rights reserved.
 *	This will be free software, but only when it is finished.
 *	Copyright 1992-2005 Matti Aarnio.
 */

/*
 * This program is based on 'mailbox', but the usage semantics are special
 * and hard-coded inside the scheduler system.
 *
 * This implements target recipient manually ordered expiry.
 *
 * This has unusual parametrization convention, as each input line this
 * program receives can have up to 3 TAB separated fields:
 *  - spoolfilename
 *  - optional host selector (empty if third parameter is given)
 *  - optional explanatory string (up to newline)
 *
 * The scheduler will supply '-c' option to drive the channel selection.
 *
 */

#include "hostenv.h"
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <sysexits.h>
#include <sys/param.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h> /* F_LOCK is there at some systems.. */
#endif
#include <string.h>

#include "ta.h"

#include "mail.h"
#include "zsyslog.h"
#include "zmsignal.h"

#include "zmalloc.h"
#include "libz.h"
#include "libc.h"

#include "shmmib.h"


const char *defcharset;
const char *progname;
const char *channel;
const char *logfile;
FILE *logfp = NULL;
int dryrun;

extern RETSIGTYPE wantout __((int));
extern int optind;
extern char *optarg;
extern void prversion __((const char *));
extern void process __((struct ctldesc *dp, const char *optmsg, const int, FILE *verboselog));
extern void deliver __((struct ctldesc *dp, struct rcpt *rp));

extern void hp_init();
extern char **hp_getaddr();
extern int errno;
#ifndef strchr
extern char *strchr(), *strrchr();
#endif
extern int  lstat __((const char *, struct stat *)); /* remaps to  stat() if USE_LSTAT is not defined.. */
extern FILE *fdopen();
#ifndef MALLOC_TRACE
extern void * emalloc __((size_t));
#else
struct conshell *envarlist = NULL;
#endif
extern int stickymem;	/* for strsave() */
int	D_alloc = 0;

static void sig_alrm __((int));
static void sig_alrm(sig)
int sig;
{
	/* Sigh, actually dummy routine.. */
}

static void MIBcountCleanup __((void))
{
	MIBMtaEntry->taexpi.TaProcCountG -= 1;
	if (MIBMtaEntry->taexpi.TaProcCountG > 99999U)
	  MIBMtaEntry->taexpi.TaProcCountG = 0;
}

static void SHM_MIB_diag(rc)
     const int rc;
{
  switch (rc) {
  case EX_OK:
    /* OK */
    MIBMtaEntry->taexpi.TaRcptsOk ++;
    break;
  case EX_TEMPFAIL:
  case EX_IOERR:
  case EX_OSERR:
  case EX_CANTCREAT:
  case EX_SOFTWARE:
  case EX_DEFERALL:
    /* DEFER */
    MIBMtaEntry->taexpi.TaRcptsRetry ++;
    break;
  case EX_NOPERM:
  case EX_PROTOCOL:
  case EX_USAGE:
  case EX_NOUSER:
  case EX_NOHOST:
  case EX_UNAVAILABLE:
  default:
    /* FAIL */
    MIBMtaEntry->taexpi.TaRcptsFail ++;
    break;
  }
}



#ifndef	MAXPATHLEN
#define	MAXPATHLEN 1024
#endif

struct _saparam {
  const char *host;
  const char *user;
};

static int selectaddr __((const char *spec_host, const struct taddress *ap, const void *saparam_));
static int
selectaddr(spec_host, ap, saparam_)
     const char *spec_host;
     const struct taddress *ap;
     const void *saparam_;
{
	const struct _saparam *saparam = saparam_;

	/* When we are called, the CHANNEL has already been matched
	   (or can be ignored..) */
	/* Now we verify that host is matched (or ignored for match)
	   and finally that user is matched (or ignored for match) */
	if (saparam->host) {
	  if (cistrcmp(saparam->host, ap->host) != 0) return 0;
	} else if (spec_host) {
	  if (cistrcmp(spec_host, ap->host) != 0) return 0;
	}

	if (saparam->user) {
	  if (cistrcmp(saparam->user, ap->user) != 0) return 0;
	}

	/* And we have a match... */

	return 1;
}

int
main(argc, argv)
	int argc;
	const char **argv;
{
	char file[2048];
	char *s;
	char *optmsg = NULL;
	int c, errflg, fd;
	int silent = 0;
	struct ctldesc *dp;
	FILE *verboselog = NULL;
	struct _saparam saparam = { NULL, NULL };

	RETSIGTYPE (*oldsig) __((int));

	SIGNAL_HANDLESAVE(SIGINT, SIG_IGN, oldsig);
	if (oldsig != SIG_IGN)
	  SIGNAL_HANDLE(SIGINT, wantout);
	SIGNAL_HANDLESAVE(SIGTERM, SIG_IGN, oldsig);
	if (oldsig != SIG_IGN)
	  SIGNAL_HANDLE(SIGTERM, wantout);
	SIGNAL_HANDLESAVE(SIGQUIT, SIG_IGN, oldsig);
	if (oldsig != SIG_IGN)
	  SIGNAL_HANDLE(SIGQUIT, wantout);
	SIGNAL_HANDLESAVE(SIGHUP, SIG_IGN, oldsig);
	if (oldsig != SIG_IGN)
	  SIGNAL_HANDLE(SIGHUP, wantout);
	SIGNAL_HANDLE(SIGALRM, sig_alrm); /* Actually ignored, but
					     fcntl() will break ? */

	SIGNAL_IGNORE(SIGPIPE);

	/* This assignment MUST precede readzenv() ! */
	progname = strrchr(argv[0], '/');
	if (progname == NULL)
	  progname = argv[0];
	else
	  ++progname;

	if (getenv("ZCONFIG")) readzenv(getenv("ZCONFIG"));


	Z_SHM_MIB_Attach(1); /* we don't care if it succeeds or fails.. */

	MIBMtaEntry->taexpi.TaProcessStarts += 1;
	MIBMtaEntry->taexpi.TaProcCountG    += 1;

	atexit(MIBcountCleanup);

	errflg = 0;
	logfile = NULL;
	channel = "";
	while (1) {
	  c = getopt(argc, (char*const*)argv, "?c:Vh:m:nl:su:");
	  if (c == EOF)
	    break;
	  switch (c) {
	  case 'c':		/* specify channel scanned for */
	    channel = optarg;
	    break;
	  case 'V':
	    prversion("expirer");
	    exit(EX_OK);
	    break;
	  case 'h':
	    saparam.host = strdup(optarg);
	    break;
	  case 'l':		/* log file */
	    logfile = optarg;
	    break;
	  case 'm':
	    optmsg = optarg;
	    break;
	  case 'n':
	    dryrun = 1;
	    break;
	  case 's':
	    silent = 1;
	    break;
	  case 'u':
	    saparam.user = strdup(optarg);
	    break;
	  default:
	    ++errflg;
	    break;
	  }
	}
	if (errflg || optind != argc) {
	  fprintf(stderr, "Usage: %s [-s] [-V] [-l logfile] [-c channel] [-h host] [-u user@domain] [-m msgstr]\n",
		  argv[0]);
	  exit(EX_USAGE);
	}

	if (geteuid() != 0 || getuid() != 0) {
	  fprintf(stderr, "%s: not running as root!\n", progname);
	  exit(EX_NOPERM);
	}

	SETUID(0);		/* make us root all over */
	SETEUID(0);		/* make us root all over */


	logfp = NULL;
	if (logfile != NULL) {
	  fd = open(logfile, O_CREAT|O_APPEND|O_WRONLY, 0644);
	  if (fd < 0)
	    fprintf(stderr,
		    "%s: open(\"%s\") failed: %s\n",
		    progname, logfile, strerror(errno));
	  else {
	    logfp = fdopen(fd, "a");
	    fcntl(fd, F_SETFD, 1);
	  }
	}

	/* We need this later on .. */
	zopenlog("expirer", LOG_PID, LOG_MAIL);

	getnobody();

	while (!getout) {

	  /* Input:
	       spool/file/name [ \t host.info [ \t explanation ]] \n
	   */

	  printf("#hungry\n");
	  fflush(stdout);

	  if (fgets(file, sizeof file, stdin) == NULL)
	    break;
	  if (strchr(file, '\n') == NULL) break; /* No ending '\n' !  Must
						    have been partial input! */
	  if (strcmp(file, "#idle\n") == 0) {
	    MIBMtaEntry->taexpi.TaIdleStates += 1;
	    continue; /* Ah well, we can stay idle.. */
	  }
	  if (emptyline(file, sizeof file))
	    break;

	  MIBMtaEntry->taexpi.TaMessages += 1;


	  s = strchr(file,'\t');
	  if (s != NULL) {
	    if (saparam.host) free((void*)saparam.host);
	    saparam.host = strdup(s+1);
	    *s = 0;
	  }

	  SETUID(0); /* We begin as roots..  process() may change us */
	  SETEUID(0); /* We begin as roots..  process() may change us */

	  notary_setxdelay(0); /* Our initial speed estimate is
				  overtly optimistic.. */

	  dp = ctlopen(file, channel, saparam.host, &getout, selectaddr, &saparam);
	  if (dp == NULL) {
	    printf("#resync %s\n",file);
	    fflush(stdout);
	    continue;
	  }
	  if (verboselog) {
	    fclose(verboselog);
	    verboselog = NULL;
	  }
	  if (dp->verbose) {
	    verboselog = (FILE*)fopen(dp->verbose,"a");
	    if (verboselog) {
	      setbuf(verboselog,NULL);
	      fcntl(FILENO(verboselog), F_SETFD, 1);
	    }
	  }
	  process(dp, optmsg, silent, verboselog);
	  ctlclose(dp);

	}
	exit(EX_OK);
	/* NOTREACHED */
	return 0;
}


void
process(dp, optmsg, silent, verboselog)
	struct ctldesc *dp;
	const char *optmsg;
	const int silent;
	FILE *verboselog;
{
	struct rcpt *rp;

	if (optmsg == NULL || *optmsg == 0)
	  optmsg = "x-local; 500 (Administrative message deletion from delivery queue)";

	MIBMtaEntry->taexpi.TaDeliveryStarts += 1;


	for (rp = dp->recipients; rp != NULL; rp = rp->next) {

	  notary_setxdelay(0);
	  if (dryrun) {
	    notaryreport(rp->addr->user, "delayed", "4.0.0 (dryrun)", "");
	    diagnostic(verboselog, rp, EX_TEMPFAIL, 0, "");
	    SHM_MIB_diag(EX_TEMPFAIL);
	  }
	  if (silent) {
	    notaryreport(rp->addr->user, "delivered", "2.0.0 (Silent ok)", "");
	    diagnostic(verboselog, rp, EX_OK, 0, "");
	    SHM_MIB_diag(EX_OK);
	  } else {
	    notaryreport(rp->addr->user, "failed", "5.7.0 (Administrative deletion command)", optmsg);
	    diagnostic(verboselog, rp, EX_UNAVAILABLE, 0, "%s", optmsg);
	    SHM_MIB_diag(EX_UNAVAILABLE);
	  }
	}
}
