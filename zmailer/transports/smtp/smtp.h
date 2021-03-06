/*
 *	Copyright 1988 by Rayan S. Zachariassen, all rights reserved.
 *	This will be free software, but only when it is finished.
 *	Copyright 1991-2006 by Matti Aarnio -- modifications, including
 *	MIME things...
 */

#define	RFC974		/* If BIND, check that TCP SMTP service is enabled */

#define DefCharset "ISO-8859-1"
#define CHUNK_MAX_SIZE 64000
#define DO_CHUNKING 1

#include "mailer.h"

#ifdef linux_xx
#define __USE_BSD 1
#endif
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include "zmsignal.h"
#include <sysexits.h>
/* #include <strings.h> */ /* poorly portable.. */
#ifdef HAVE_STDARG_H
# include <stdarg.h>
#else
# include <varargs.h>
#endif
#include <fcntl.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <string.h>

#include "zresolv.h"

#include "ta.h"
#include "mail.h"
#include "zsyslog.h"
#include "dnsgetrr.h"
#include "zmalloc.h"
#include "libz.h"
#include "libc.h"

#include "shmmib.h"

#include <sys/socket.h>
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef _AIX /* Defines NFDBITS, et.al. */
#include <sys/types.h>
#endif

#include <sys/time.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#include "zmpoll.h"

#undef string
#undef cstring

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/md5.h>
#endif /* - HAVE_OPENSSL */



#ifndef	SEEK_SET
#define	SEEK_SET	0
#endif	/* SEEK_SET */
#ifndef SEEK_CUR
#define SEEK_CUR   1
#endif
#ifndef SEEK_XTND
#define SEEK_XTND  2
#endif

#ifndef	IPPORT_SMTP
#define	IPPORT_SMTP	25
#endif 	/* IPPORT_SMTP */

#define	PROGNAME	"smtpclient"	/* for logging */
#define	CHANNEL		"smtp"	/* the default channel name we deliver for */

#ifndef	MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN 64
#endif	/* MAXHOSTNAMELEN */

#define MAXFORWARDERS	128	/* Max number of MX rr's that can be listed */

#define GETADDRINFODEBUG	0 /* XXX: Only w/ bundled libc/getaddrinfo.c */
#define GETMXRRDEBUG		1


extern const char *defcharset;
extern char myhostname[512];
extern int myhostnameopt;
extern char errormsg[ZBUFSIZ]; /* Global for the use of  dnsgetrr.c */
extern const char *progname;
extern const char *cmdline, *eocmdline, *logfile, *msgfile;
extern int pid;
extern int debug;
extern int verbosity;
extern int conndebug;
extern int dotmode;		/* At the SMTP '.' phase, DON'T LEAVE IMMEDIATELY!. */
extern int getout;		/* signal handler turns this on when we are wanted to abort! */
extern int timeout;		/* how long do we wait for response? (sec.) */
extern int conntimeout;		/* connect() timeout */
extern int gotalarm;		/* indicate that alarm happened! */
extern int noalarmjmp;		/* Don't long-jmp on alarm! */
extern jmp_buf alarmjmp;
extern jmp_buf procabortjmp;
extern int procabortset;
extern int readalready;		/* does buffer contain valid message data? */
extern int wantreserved;	/* open connection on secure (reserved) port */
extern int statusreport;	/* put status reports on the command line */
extern int force_8bit;		/* Claim to the remote to be 8-bit system, even
				   when it doesn't report itself as such..*/
extern int force_7bit;		/* and reverse the previous.. */
extern int keep_header8;	/* Don't do "MIME-2" to the headers */
extern int checkwks;
extern FILE *logfp;
extern int nobody;
extern char *localidentity;	/* If we are wanted to bind some altenate
				   interface than what the default is thru
				   normal kernel mechanisms.. */
extern int daemon_uid;
extern int first_uid;		/* Make the opening connect with the UID of the
				   sender (atoi(rp->addr->misc)), unless it is
				   "nobody", in which case use "daemon"      */

extern int D_alloc;		/* Memory usage debug */
extern int no_pipelining;	/* In case the system just doesn't cope with it */
extern int prefer_ip6;
extern int use_ipv6;

#ifdef	lint
#undef	putc
#define	putc	fputc
#endif	/* lint */

/* Extended SMTP flags -- can downgrade from 8-bit to 7-bit while in transport
   IF  MIME-Version: is present, AND Content-Transfer-Encoding: 8BIT
   For selected "force_8bit" remotes can also DECODE Q-P MIME MSGS! */
/* If there is header:  Content-Conversion: prohibited
   DO NOT do conversions no matter what
   (even when it violates the protocol..) */

/* Following options can be declared in ESMTP  EHLO response  */
#define ESMTP_SIZEOPT     0x0001 /* RFC 1427/1653/1870 */
#define ESMTP_8BITMIME    0x0002 /* RFC 1426/1652 */
#define ESMTP_DSN         0x0004 /* RFC 1891	 */
#define ESMTP_PIPELINING  0x0008 /* RFC 1854/2197 */
#define ESMTP_ENHSTATUS   0x0010 /* RFC 2034	 */
#define ESMTP_CHUNKING    0x0020 /* RFC 1830	 */
#ifdef HAVE_OPENSSL
#define ESMTP_STARTTLS    0x0040 /* RFC 2487	 */
#endif /* - HAVE_OPENSSL */
#define ESMTP_DELIVERBY   0x0080 /* RFC 2852      */
#define ESMTP_AUTH        0x0100 /* RFC 2554+++   */


# ifdef RFC974

struct mxdata {
	char		*host;
	int		 pref;
	time_t		 expiry;
	struct addrinfo *ai;
};
# endif /* RFC974 */

struct smtpdisc {
  Sfdisc_t D;		/* Sfio Discipline structure		*/
  void *SS;		/* Ptr to SS context			*/
};


typedef enum {
  SMTPSTATE_CONNECT  = 0,
  SMTPSTATE_HELO     = 0,
  SMTPSTATE_MAILFROM = 0,
  SMTPSTATE_RCPTTO   = 1,
  SMTPSTATE_DATA     = 2,
  SMTPSTATE_DATADOT  = 3,
  SMTPSTATE_DATADOTRSET = 4,
  SMTPSTATE99        = 99
} SMTPSTATES;


#ifdef HAVE_OPENSSL
struct _SmtpState_SSL_aux {
  int   sslmode;		/* Set, when SSL/TLS in running */
  SSL * ssl;
  SSL_CTX * ctx;

  int   wantreadwrite;		/* <0: read, =0: nothing, >0: write */
#if 0
  char *sslwrbuf;
  int   sslwrspace, sslwrin, sslwrout;
  /* space, how much stuff in, where the output cursor is */
#endif

  const char *peername_save;	 /* strdup()ed string */
  const char *peer_subject;      /* strdup()ed string */
  const char *peer_issuer;       /* strdup()ed string */
  const char *peer_fingerprint;  /* strdup()ed string */
  const char *peer_CN;           /* strdup()ed string */
  const char *issuer_CN;         /* strdup()ed string */
  const char *peer_CN1;          /* strdup()ed string */
  const char *issuer_CN1;        /* strdup()ed string */
  const char *notBefore;         /* strdup()ed string */
  const char *notAfter;          /* strdup()ed string */

  unsigned char peername_md5[MD5_DIGEST_LENGTH];

  int	peer_verified;

  const char *protocol;		/* from SSL_get_version() */
  const char *cipher_name;	/* from SSL_CIPHER_get_name() */
  int	cipher_usebits;
  int	cipher_algbits;

  int verify_depth;
  int verify_error; /*  = X509_V_OK; */

  int enforce_verify_errors;
  int enforce_CN;
};

#endif /* - HAVE_OPENSSL */


typedef struct { /* SmtpState */
  int  ehlo_capabilities;	/* Capabilities of the remote system */
  int  esmtp_on_banner;
  int  within_ehlo;
  int  main_esmtp_on_banner;
  int  servport;
  int  literalport;
  int  firstmx;			/* error in smtpwrite("HELO"..) */
# ifdef RFC974
  int  mxcount;
  struct mxdata mxh[MAXFORWARDERS];
# endif /* RFC974 */
  int  smtp_bufsize;		/* Size of the buffer; this should be large
				   enough to contain MOST cases of pipelined
				   SMTP information, AND still fit within
				   roundtrip TCP buffers */
  int  smtp_outcount;		/* we used this much.. */
  int  block_written;		/* written anything in async phase */
  long ehlo_sizeval;
  long ehlo_deliverbyval;
  int  rcpt_limit;		/* Number of recipients that can be sent on
				   one session.. */

  int   smtpfd;			/* FD from the remote host		*/
  Sfio_t *smtpfp;		/* Sfio_t* to the remote host           */
  int   writeclosed;		/* The SMTP socket is closed for write  */
  struct smtpdisc smtpdisc;	/* SMTP outstream discipline data	*/
  int   lasterrno;		/* Last errno value			*/
  int	do_rset;		/* Will possibly need to do RSET	*/
  time_t lastactiontime;	/* When last (write) action was ?	*/

  char *myhostname;		/* strdup()ed name of my outbound interface */

  FILE *verboselog;		/* verboselogfile */

  int hsize;			/* Output state variables */
  int msize;

  int pipelining;		/* Are we pipelining ? */
  int pipebufsize;		/* Response collection buffering */
  int pipebufspace;
  char *pipebuf;
  int pipeindex;		/* commands associated w/ those responses */
  int pipespace;
  int pipereplies;		/* Replies handled so far */
  int continuation_line, first_line;
  char **pipecmds;
  struct rcpt **pipercpts;	/* recipients -""- */
  int *pipestates;

  int rcptcnt;			/* PIPELINING variables */
  int rcptstates;

#define HELOSTATE_OK   0x0001
#define HELOSTATE_400  0x0002
#define HELOSTATE_500  0x0004
#define FROMSTATE_OK   0x0010  /* MAIL FROM --> 2XX code */
#define FROMSTATE_400  0x0020  /* MAIL FROM --> 4XX code */
#define FROMSTATE_500  0x0040  /* MAIL FROM --> 5XX code */
#define RCPTSTATE_OK   0x0100  /* At least one OK   state   */
#define RCPTSTATE_400  0x0200  /* At least one TEMP failure */
#define RCPTSTATE_500  0x0400  /* At least one PERM failure */
#define DATASTATE_OK   0x1000  /* DATA/BDAT --> 2/3XX code */
#define DATASTATE_400  0x2000  /* DATA/BDAT --> 4XX code */
#define DATASTATE_500  0x4000  /* DATA/BDAT --> 5XX code */

  int state;
  int alarmcnt;
  int column;
  int lastch;
  int chunking;
  int doing_chunking;

  char *chunkbuf;		/* CHUNKING, RFC-1830 */
  int   chunksize, chunkspace;

  char remotemsg[2*ZBUFSIZ];
  char *remotemsgs[7];

  SMTPSTATES cmdstate, prevcmdstate;

  char remotehost[MAXHOSTNAMELEN+1];
  char ipaddress[200];

  struct addrinfo ai;		/* Lattest active connection */
  Usockaddr ai_addr;
  int ismx;

  char stdinbuf[8192];
  int  stdinsize; /* Available */
  int  stdincurs; /* Consumed  */

#ifdef HAVE_OPENSSL
  struct _SmtpState_SSL_aux TLS;
#endif /* - HAVE_OPENSSL */

  const char *taspoolid;

  /* Along with 'remotehost', these can be used to pick
     userid and password (readily BASE64 encoded and space
     separated!) for SMTPAUTH exchange. */

  const char *sel_channel;
  const char *sel_host;

} SmtpState;

extern const char *FAILED;
extern time_t now;

extern int  errno;
#ifndef MALLOC_TRACE
extern void * emalloc __((size_t));
extern void * erealloc __((void *, size_t));
#endif
/*
   extern int  atoi __((char*));
   extern long atol __((char*));
 */
extern char *strerror();
#ifndef strchr
extern char *strchr();
extern char *strrchr();
#endif
extern char *dottedquad();
extern char *optarg;
extern int  optind;

extern char **environ;

extern int  deliver    __((SmtpState *SS, struct ctldesc *dp, struct rcpt *startrp, struct rcpt *endrp, const char *host, const int noMX));
extern int  writebuf   __((SmtpState *SS, const char *buf, int len));
extern int  writemimeline __((SmtpState *SS, const char *buf, int len, CONVERTMODE cvtmode));
extern int  appendlet  __((SmtpState *SS, struct ctldesc *dp, CONVERTMODE convertmode, struct ct_data *));
extern int  smtpopen   __((SmtpState *SS, const char *host, int noMX));
extern int  smtpconn   __((SmtpState *SS, const char *host, int noMX));
extern int  smtp_ehlo  __((SmtpState *SS, const char *strbuf));
extern int  ehlo_check __((SmtpState *SS, const char *buf));
extern void smtp_flush __((SmtpState *SS));
extern int  smtp_sync  __((SmtpState *SS, int, int));
extern int  smtpwrite  __((SmtpState *SS, int saverpt, const char *buf, int pipelining, struct rcpt *syncrp));
extern void smtppipestowage  __((SmtpState *SS, const char *buf, struct rcpt *syncrp));
extern int  process    __((SmtpState *SS, struct ctldesc*, int, const char*, int));

extern int  check_7bit_cleanness __((struct ctldesc *dp));
extern void notarystatsave __((SmtpState *SS, char *smtpstatline, char *status));

extern int  makeconn  __((SmtpState *SS, const char *, struct addrinfo *, int));
extern int  makereconn __((SmtpState *SS));
extern int  vcsetup  __((SmtpState *SS, struct sockaddr *, int*, char*));
#ifdef	BIND
extern int  rightmx  __((const char*, const struct taddress *, const void*));
extern int  h_errno;
extern int  res_mkquery(), res_send(), dn_skipname(), dn_expand();
# ifdef RFC974
extern int  getmxrr __((SmtpState *SS, const char *host, struct mxdata mx[], int maxmx, int depth, char *realname, const int realnamesize, time_t *realnamettl));
# endif /* RFC974 */
extern void mxsetsave __((SmtpState *SS, const char *));
#endif	/* BIND */
extern int  matchroutermxes __((const char*, struct taddress*, void*));
extern RETSIGTYPE sig_pipe __((int));
extern int  getmyhostname();
extern void stashmyaddresses();
extern void getdaemon();
extern int  has_readable __((int));
extern int  bdat_flush __((SmtpState *SS, int lastflg));
extern void smtpclose __((SmtpState *SS, int failure));
extern int  pipeblockread __((SmtpState *SS));
extern ssize_t smtp_sfwrite __((Sfio_t *, const void *, size_t, Sfdisc_t *));
extern int  zsfsetfd     __((Sfio_t *, int));
extern int  smtp_nbread  __((SmtpState *, void *, int));


extern void rmsgappend __((SmtpState *, int, const char *, ...));

#if defined(HAVE_STDARG_H) && defined(__STDC__)
extern void report __((SmtpState *SS, char *fmt, ...));
#else
extern void report();
#endif

#ifdef HAVE_OPENSSL
extern int  tls_init_clientengine __((SmtpState *SS, char *cfgpath));
extern int  tls_start_clienttls   __((SmtpState *SS, const char *peername));
extern int  tls_stop_clienttls    __((SmtpState *SS, int failure));
#endif /* - HAVE_OPENSSL */

extern char *logtag __((void));

extern time_t starttime, endtime;

extern int smtpauth  __((SmtpState *SS));
extern char *authpasswdfile;
