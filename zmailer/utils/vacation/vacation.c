#include "hostenv.h"

#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <limits.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <time.h>
#include "zsyslog.h"
/* #include <tzfile.h> */  /* Not needed ? */
#include <errno.h>
#include <sysexits.h>
#ifdef	HAVE_NDBM_H
#include <ndbm.h>
#include <fcntl.h>
#else
#ifdef  HAVE_GDBM_H
#include <gdbm.h>
#include <fcntl.h>
#else
#ifdef  HAVE_DB_H
#include <db.h>
#else
:error:error:error "To compile, VACATION needs ndbm.h, gdbm.h, or db.h; none found!"
#endif
#endif
#endif
/* #include "useful.h"  */
/* #include "userdbm.h" */

#include "mail.h"
#include "malloc.h"
#include "libz.h"
#include "libc.h"

extern void   sendmessage __((const char *, const char *));
extern char * newstr __((const char *));
extern void   setinterval __((time_t));
extern void   setreply __((void));
extern void   usage __((void));

#ifndef LONG_MAX
# define LONG_MAX 1000000000 /* 100 million seconds - 3.2 years */
#endif

/* SCCSID(@(#)vacation.c	4.1		7/25/83); */

#undef VDEBUG

/*
** VACATION -- return a message to the sender when on vacation.
**
**	This program could be invoked as a message receiver
**	when someone is on vacation.  It returns a message
**	specified by the user to whoever sent the mail, taking
**	care not to return a message too often to prevent
**	"I am on vacation" loops.
**
**	For best operation, this program should run setuid to
**	root or uucp or someone else that sendmail will believe
**	a -f flag from.  Otherwise, the user must be careful
**	to include a header on his .vacation.msg file.
**
**	Positional Parameters:
**		the user to collect the vacation message from.
**
**	Flag Parameters:
**		-I	initialize the database.
**		-m FILE	set the filename to use for the reply message to
**			FILE.
**		-d	Turns off the logging of messages in the
**			~/.vacation.{dir,pag} files to determine
**			whom to reply to.
**
**	Side Effects:
**		A message is sent back to the sender.
**
**	Author:
**		Eric Allman
**		UCB/INGRES
*/

#define MAXLINE	1024		/* max size of a line */
#define VDB	".vacation"
#define VMSG	".vacation.msg"
#ifndef VMSGDEF
#define VMSGDEF "/usr/lib/vacation.def"
#endif

typedef struct alias {
	struct alias *next;
	char *name;
} ALIAS;
ALIAS *names = NULL;

#ifdef	HAVE_NDBM_H
DBM *db;
#define DBT datum
#else
#ifdef HAVE_GDBM_H
GDBM_FILE db;
#define DBT datum
#else /* HAVE_DB_H */
DB *db;
/* Natural datum type is: DBT */
#define dptr  data
#define dsize size
#endif /* GDBM */
#endif	/* NDBM */


char from[MAXLINE];
char *subject_str = NULL;	/* Glob subject from input */
int dblog = 1;

extern void purge_input __((void));
extern struct passwd *getpwnam();
extern int optind, opterr;
extern char *optarg;
extern FILE *freopen(), *tmpfile();
extern char *getenv();
#ifndef HAVE_STDLIB_H
extern char *malloc();
#endif
extern void usrerr __((char *));
extern void syserr __((char *));
extern void readheaders __((void));
extern char *strerror __((int));

const char *progname;

int
main(argc, argv)
     int argc;
     char *argv[];
{
	register char *p;
	struct passwd *pw;
	ALIAS *cur;
	time_t interval;
	char *msgfile = NULL;
	int ch, iflag;

	progname = argv[0];

	/* process arguments */
	opterr = iflag = 0;
	interval = -1;
	while ((ch = getopt(argc,argv,"a:Iir:t:m:d?")) != EOF) {
		switch ((char)ch) {
		  case 'a':	/* alias */
		        if (!(cur = (ALIAS*) malloc((u_int)sizeof(ALIAS))))
			  break;
			cur->name = optarg;
			cur->next = names;
			names = cur;
			break;
		  case 'I':	/* backwards compatible */
		  case 'i':	/* initialize the database*/
			iflag = 1;
			break;
		  case 'm':	/* set file to get message from */
			msgfile = optarg;
			break;
		  case 'd':	/* No dbm log of sender */
			dblog = 0;
			break;
		  case 't':
		  case 'r':
			if (isdigit(*optarg)) {
				interval = atol(optarg) * (24*60*60);
				if (interval < 0)
					usage();
			}
			else
				interval = INT_MAX;
			break;
		  case '?':
		  default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* verify recipient argument */
#ifdef ZMAILER
	if (argc == 0) {
		p = getenv("USER");
		if (p == NULL) {
			usrerr
			  ("Zmailer error: USER environment variable not set");
			exit(EX_USAGE+101);
		}
	}
#endif /* ZMAILER */

	if (argc != 1) {
		if (!iflag)
			usage();
		if (!(pw = getpwuid(getuid()))) {
			fprintf(stderr,
				"vacation: no such user uid %u.\n", getuid());
			exit(EX_NOUSER);
		}
	} else if (!(pw = getpwnam(*argv))) {
		fprintf(stderr, "vacation: no such user %s.\n", *argv);
		exit(EX_NOUSER);
	}
	if (chdir(pw->pw_dir)) {
		fprintf(stderr,
			"vacation: no such directory %s.\n", pw->pw_dir);
		exit(EX_NOUSER);
	}

	/* verify recipient argument */
#ifdef ZMAILER
	if (argc == 0) {
		p = getenv("USER");
		if (p == NULL) {
			usrerr
			  ("Zmailer error: USER environment variable not set");
			exit(EX_USAGE+102);
		}
	}
#endif /* ZMAILER */

#ifdef	HAVE_NDBM_H
	if (dblog)
		db = dbm_open(VDB, O_RDWR | (iflag ? O_TRUNC|O_CREAT : 0),
			      S_IRUSR|S_IWUSR);
#else	/* !NDBM */
#ifdef HAVE_GDBM_H
	if (dblog)
		db = gdbm_open(VDB ".pag" /* Catenates these strings */, 8192,
			       iflag ? GDBM_NEWDB : GDBM_WRITER,
			       S_IRUSR|S_IWUSR, NULL );
#else
	if (dblog)
	  db = dbopen(VDB ".db", iflag ? (O_RDWR|O_CREAT) : O_RDWR,
		      S_IRUSR|S_IWUSR, DB_BTREE, NULL);
#endif
#endif
	if (dblog && !db) {
		fprintf(stderr, "vacation: %s.* database file(s): %s\n", 
			VDB, strerror(errno));
		exit(EX_CANTCREAT);
	}

	if (interval != -1)
		setinterval(interval);

	if (iflag) {
#ifdef	HAVE_NDBM_H
		if (dblog)
			dbm_close(db);
#else
#ifdef HAVE_GDBM_H
		if (dblog)
			gdbm_close(db);
#else
		if (dblog)
			db->close(db);
#endif
#endif
		exit(EX_OK);
	}

	if (!(cur = (ALIAS *)malloc((u_int)sizeof(ALIAS)))) {
		exit(EX_TEMPFAIL);
	}
	cur->name = pw->pw_name;
	cur->next = names;
	names = cur;

	/* read message from standard input (just from line) */
	readheaders();
	purge_input();
	if (!recent()) {
		setreply();
#ifdef	HAVE_NDBM_H
		if (dblog)
			dbm_close(db);
#else
#ifdef HAVE_GDBM_H
		if (dblog)
			gdbm_close(db);
#else
		if (dblog)
			db->close(db);
#endif
#endif
		sendmessage(msgfile,pw->pw_name);
	}
#ifdef	HAVE_NDBM_H
		if (dblog)
			dbm_close(db);
#else
#ifdef HAVE_GDBM_H
		if (dblog)
			gdbm_close(db);
#else
		if (dblog)
			db->close(db);
#endif
#endif
	exit(EX_OK);
}
/*
**  SENDMESSAGE -- send a message to a particular user.
**
**	Parameters:
**		msgf -- filename containing the message.
**		user -- user who should receive it.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sends mail to 'user' using /usr/lib/sendmail.
*/

void
sendmessage(msgf, myname)
	const char *msgf;
	const char *myname;
{
	FILE *f;
	FILE *mf;
	char linebuf[512];
	char *s;

#ifdef VDEBUG
	fprintf(stderr, "sendmessage(%s, %s)\n", msgf, myname);
	fflush(stderr);
#endif
	/* find the message to send */
	f = NULL;
	if (msgf)
		f = freopen(msgf, "r", stdout);
	if (f == NULL)
		f = freopen(VMSG, "r", stdout);
	if (f == NULL)
		f = freopen(VMSGDEF, "r", stdout);
	if (f == NULL)
		syserr("No message to send");
	mf = mail_open(MSG_RFC822);
	fprintf(mf, "from %s\n",myname);
	fprintf(mf, "to %s\n", from);
	fprintf(mf, "env-end\n");
	fprintf(mf,"To: %s\n", from);
	while (!feof(f) && !ferror(f)) {
	  if (fgets(linebuf,sizeof(linebuf),f) == NULL) break;
	  if ((s = strchr(linebuf,'$')) != NULL) {
	    /* Possibly  $SUBJECT ? */
	    if (strncmp(s+1,"SUBJECT",7)==0) {
	      /* It is $SUBJECT */
	      *s = 0;
	      fputs(linebuf,mf);
	      if (subject_str)
		fputs(subject_str,mf);
	      s += 8;
	      fputs(s,mf);
	      continue;
	    }
	  }
	  fputs(linebuf,mf);
	}

	fclose(f);
	mail_close(mf);
}
/*
**  USRERR -- print user error
**
**	Parameters:
**		f -- format.
**		p -- first parameter.
**
**	Returns:
**		none.
**
**	Side Effects:
**		none.
*/

void
usrerr(msg)
	char *msg;
{
	fprintf(stderr, "vacation: %s\n",msg);
}
/*
**  SYSERR -- print system error
**
**	Parameters:
**		f -- format.
**		p -- first parameter.
**
**	Returns:
**		none.
**
**	Side Effects:
**		none.
*/

/* VARARGS 1 */
void
syserr(msg)
	char *msg;
{
	fprintf(stderr, "vacation: %s\n", msg);
	exit(EX_USAGE+103);
}
/*
**  NEWSTR -- copy a string
**
**	Parameters:
**		s -- the string to copy.
**
**	Returns:
**		A copy of the string.
**
**	Side Effects:
**		none.
*/

char *
newstr(s)
	const char *s;
{
	char *p;

	p = malloc((unsigned)strlen(s) + 1);
	if (p == NULL)
	{
		syserr("newstr: cannot alloc memory");
		exit(EX_OSERR);
	}
	strcpy(p, s);
	return p;
}
/*
 * readheaders --
 *	read mail headers
 */
void
readheaders()
{
	register ALIAS *cur;
	register char *p;
	int tome, cont;
	char buf[MAXLINE];
	char *sender;
	int has_from = 0;

#ifdef ZMAILER
	/* get SENDER from environment, ensure null-terminated. This is the
	   SMTP MAIL FROM address, i.e. the error return address if the
	   message comes from a mailing list. */

	if ( (sender=getenv("SENDER")) != NULL ) {
		strncpy(buf,sender,MAXLINE);
		if (buf[MAXLINE-1] != '\0') {
			usrerr("SENDER environment variable too long");
			exit(EX_USAGE+104);
		}
		strcpy(from,buf);
		has_from = 1;
		if (junkmail()) {
			purge_input();
			exit(EX_OK);
		}
	}
#endif
	cont = tome = 0;
	while (fgets(buf, sizeof(buf), stdin) && *buf != '\n')
		switch(*buf) {
		case 'F':		/* "From " */
			cont = 0;
			if (has_from) break;
			if (!strncmp(buf, "From ", 5)) {
				for (p = buf + 5; *p && *p != ' '; ++p);
				*p = '\0';
				strcpy(from, buf + 5);
				p = strchr(from, '\n');
				if (p != NULL)
					*p = '\0';
				if (junkmail()) {
					purge_input();
					exit(EX_OK);
				}
			}
			break;
		case 'P':		/* "Precedence:" */
			cont = 0;
			if (strncasecmp(buf, "Precedence", 10) ||
			    (buf[10] != ':' && buf[10] != ' ' && buf[10] != '\t'))
				break;
			if (!(p = strchr(buf, ':')))
				break;
			while (*++p && isspace(*p));
			if (!*p)
				break;
			if (!strncasecmp(p, "junk", 4) ||
			    !strncasecmp(p, "bulk", 4)) {
				purge_input();
				exit(EX_OK);
			}
			break;
		case 'C':		/* "Cc:" */
			if (strncmp(buf, "Cc:", 3))
				break;
			cont = 1;
			goto findme;
		case 'S':		/* "Subject:" */
			if (strncmp(buf, "Subject:", 8))
				break;
			cont = 1;
			subject_str = newstr(buf+9);
			p = strchr(subject_str,'\n');
			if (p) *p = 0; /* Zap the newline */
			break;
		case 'T':		/* "To:" */
			if (strncmp(buf, "To:", 3))
				break;
			cont = 1;
			goto findme;
		default:
			if (!isspace(*buf) || !cont || tome) {
				cont = 0;
				break;
			}
findme:			for (cur = names; !tome && cur; cur = cur->next)
				tome += nsearch(cur->name, buf);
		} /* switch() */
	if (!tome) {
		purge_input();
		exit(EX_OK);
	}
	if (!*from) {
	  zopenlog("vacation", LOG_PID, LOG_MAIL);
	  zsyslog((LOG_NOTICE, "vacation: no initial \"From\" line.\n"));
	  exit(EX_USAGE+105);
	}
}

/*
 * nsearch --
 *	do a nice, slow, search of a string for a substring.
 */
int
nsearch(name, str)
	register char *name, *str;
{
	register int len;

	for (len = strlen(name); *str; ++str)
		if (*str == *name && !strncasecmp(name, str, len))
			return(1);
	return(0);
}

/*
 * junkmail --
 *	read the header and return if automagic/junk/bulk mail
 */
int
junkmail()
{
	static struct ignore {
		const char	*name;
		int		len;
	} ignore[] = {
		{ "-request", 8 },
		{ "postmaster", 10 },
		{ "uucp", 4 },
		{ "mailer-daemon", 13 },
		{ "mailer", 6 },
		{ "-relay", 6 },
		{ NULL, 0 }
		
	};
	register struct ignore *cur;
	register int len;
	register char *p;

	/*
	 * This is mildly amusing, and I'm not positive it's right; trying
	 * to find the "real" name of the sender, assuming that addresses
	 * will be some variant of:
	 *
	 * From site!site!SENDER%site.domain%site.domain@site.domain
	 */
	if (!(p = strchr(from, '%')))
		if (!(p = strchr(from, '@'))) {
			p = strrchr(from, '!');
			if (p != NULL)
				++p;
			else
				p = from;
			for (; *p; ++p);
		}
	len = p - from;
	for (cur = ignore; cur->name; ++cur)
		if (len >= cur->len &&
		    !strncasecmp(cur->name, p - cur->len, cur->len))
			return(1);
	return(0);
}

/*
 *  purge_input()
 *
 *  Read in the stdin.
 *
 */
void
purge_input()
{
	char buf[256];
	int read_rc;

	while (!feof(stdin) && !ferror(stdin)) {
	  read_rc = fread(buf,1,sizeof(buf),stdin);
	  if (read_rc == 0) break;
	}
}

#define	VIT	"__VACATION__INTERVAL__TIMER__"

/*
 * recent --
 *	find out if user has gotten a vacation message recently.
 *	use memcpy for machines with alignment restrictions
 */
int recent()
{
	DBT key, data;
	time_t then, next;

	if (!dblog) return 0;

	/* get interval time */
	key.dptr = VIT;
	key.dsize = sizeof(VIT);
#ifdef HAVE_NDBM_H
	data = dbm_fetch(db, key);
#else
#ifdef HAVE_GDBM_H
	data = gdbm_fetch(db, key);
#else
	if (db->get(db, &key, &data, 0) != 0)
	  data.dptr = NULL;
#endif
#endif
	if (data.dptr == NULL)
		next = (60*60*24*7); /* One week */
	else
		memcpy(&next, data.dptr, sizeof(next));

	/* get record for this address */
	key.dptr = from;
	key.dsize = strlen(from);
#ifdef HAVE_NDBM_H
	data = dbm_fetch(db, key);
#else
#ifdef HAVE_GDBM_H
	data = gdbm_fetch(db, key);
#else
	if (db->get(db, &key, &data, 0) != 0)
	  data.dptr = NULL;
#endif
#endif
	if (data.dptr) {
		memcpy(&then, data.dptr, sizeof(then));
		if (next == INT_MAX || then + next > time(NULL))
			return(1);
	}
	return(0);
}

/*
 * setinterval --
 *	store the reply interval
 */
void
setinterval(interval)
	time_t interval;
{
	DBT key, data;

	if (!dblog) return;

	key.dptr = VIT;
	key.dsize = sizeof(VIT);
	data.dptr = (void*)&interval;
	data.dsize = sizeof(interval);
#ifdef HAVE_NDBM_H
	dbm_store(db, key, data, DBM_REPLACE);
#else
#ifdef HAVE_GDBM_H
	gdbm_store(db, key, data, GDBM_REPLACE);
#else
	db->put(db, &key, &data, 0);
#endif
#endif
}

/*
 * setreply --
 *	store that this user knows about the vacation.
 */
void
setreply()
{
	DBT key, data;
	time_t now;

	if (!dblog) return;

	key.dptr = from;
	key.dsize = strlen(from);
	time(&now);
	data.dptr = (void*)&now;
	data.dsize = sizeof(now);
#ifdef HAVE_NDBM_H
	dbm_store(db, key, data, DBM_REPLACE);
#else
#ifdef HAVE_GDBM_H
	gdbm_store(db, key, data, GDBM_REPLACE);
#else
	db->put(db, &key, &data, 0);
#endif
#endif
}

void
usage()
{
	fprintf(stderr,"vacation: [-i] [-d] [-a alias] [-m msgfile] [-r interval] login\n");
	exit(EX_USAGE);
}
