/*
 *	Copyright 1988 by Rayan S. Zachariassen, all rights reserved.
 *	This will be free software, but only when it is finished.
 *	Some functions Copyright 1991-1998 Matti Aarnio.
 */

/*
 * The routines in this file implement various C-coded functions that
 * are callable from the configuration file.
 */

#include "mailer.h"
#include <stdio.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <ctype.h>
#include <fcntl.h>
#include <sys/file.h>			/* O_RDONLY for run_praliases() */
#include <pwd.h>			/* for run_homedir() */
#include <grp.h>			/* for run_grpmems() */
#include <errno.h>

#ifdef HAVE_DIRENT_H
# include <dirent.h>
#else /* not HAVE_DIRENT_H */
# define dirent direct
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif /* HAVE_SYS_NDIR_H */
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif /* HAVE_SYS_DIR_H */
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif /* HAVE_NDIR_H */
#endif /* HAVE_DIRENT_H */


#ifdef HAVE_SYS_RESOURCE_H
#ifdef linux
# define _USE_BSD
#endif
#include <sys/resource.h>
#endif

#ifdef  HAVE_WAITPID
# include <sys/wait.h>
#else
# ifdef HAVE_WAIT3
#  include <sys/wait.h> /* Has BSD wait3() */
# else
#  ifdef HAVE_SYS_WAIT_H /* POSIX.1 compatible */
#   include <sys/wait.h>
#  else /* Not POSIX.1 compatible, lets fake it.. */
extern int wait();
#  endif
# endif
#endif

#ifndef WEXITSTATUS
# define WEXITSTATUS(s) (((s) >> 8) & 0377)
#endif
#ifndef WSIGNALSTATUS
# define WSIGNALSTATUS(s) ((s) & 0177)
#endif

#include "zmsignal.h"
#include "zsyslog.h"
#include "mail.h"
#include "interpret.h"
#include "io.h"
#include "libz.h"
#include "libc.h"

#include "prototypes.h"

#ifndef	_IOFBF
#define	_IOFBF	0
#endif	/* !_IOFBF */


extern const char *traps[];
extern int nobody;
extern struct group  *getgrnam __((const char *));
extern struct passwd *getpwnam __((const char *));
extern time_t time __((time_t *));
extern int routerdirloops;

#ifndef strchr
extern char *strchr(), *strrchr();
#endif

extern void free_gensym __((void));

static int gothup = 0;

static RETSIGTYPE sig_exit __((int));
static RETSIGTYPE
sig_exit(sig)
int sig;
{
	if (canexit) {
#ifdef	MALLOC_TRACE
		dbfree(); zshfree();
#endif	/* MALLOC_TRACE */
		die(0, "signal");
	}
	mustexit = 1;
	/* no need to reenable signal in USG, once will be enough */
}

RETSIGTYPE
sig_hup(sigarg)
int sigarg;
{
	gothup = 1;
	/* fprintf(stderr,"HUP\n"); */
	SIGNAL_HANDLE(SIGHUP, sig_hup);
}

static void dohup __((int));
static void
dohup(sig)
int sig;
{
	gothup = 0;
	if (traps[SIGHUP] != NULL)
		eval(traps[SIGHUP], "trap", NULL, NULL);
}

/*
 * Run the Router in Daemon mode.
 */

/*
 * We run in multiple processes mode; much of the same how scheduler
 * does its magic:  Children send log messages, and hunger announcements
 * to the master, and the master feeds jobs to the children.
 * Minimum number of child processes started is 1.
 */

#define MAXROUTERCHILDS 20
struct router_child {
  int   tochild;
  int   fromchild;
  int   childpid;
  int   hungry;

  char *linebuf;
  int   linespace;
  int   linelen;

  char  childline[64];
  int	childsize, childout;

  char  readbuf[64];
  int   readsize, readout;

  int   statloc;
#if defined(HAVE_WAIT3) && defined(HAVE_SYS_RESOURCE_H)
  struct rusage r;
#endif
};

struct router_child routerchilds[MAXROUTERCHILDS];

extern int nrouters;
extern const char *logfn;

/* ../transports/libta/nonblocking.c */
extern int  fd_nonblockingmode __((int fd));
extern int  fd_blockingmode __((int fd));
extern void fd_restoremode __((int fd, int mode));

/* ../scheduler/pipes.c */
extern int  pipes_create         __((int *tochild, int *fromchild));
extern void pipes_close_parent   __((int *tochild, int *fromchild));
extern void pipes_to_child_fds   __((int *tochild, int *fromchild));
extern void pipes_shutdown_child __((int tochild)); /* At parent, shutdown channel towards child */

/* ../scheduler/resources.c */
extern int  resources_query_nofiles  __((void));
extern void resources_maximize_nofiles __((void));
extern void resources_limit_nofiles __((int nfiles));
extern int  resources_query_pipesize __((int fildes));

static void child_server __((int tofd, int frmfd));
static int  rd_doit __((const char *filename, const char *dirs));
static void parent_reader __((void));


static int  start_child   __((int idx));
static int  start_child(i)
     const int i;
{
  int pid;
  int tofd[2], frmfd[2];

  if (pipes_create(tofd, frmfd) < 0)
    return -1; /* D'uh :-( */

  pid = fork();
  if (pid == 0) { /* child */

    int idx;

    pipes_to_child_fds(tofd,frmfd);
    for (idx = resources_query_nofiles(); idx >= 3; --idx)
	close(idx);

    resources_maximize_nofiles();

    zcloselog();
    /* Each (sub-)process does openlog() all by themselves */
    zopenlog("router", LOG_PID, LOG_MAIL);

    child_server(0, 1);

    exit(1);

  } else if (pid < 0) { /* fork failed - yell and forget it! */
    close(tofd[0]);  close(tofd[1]);
    close(frmfd[0]); close(frmfd[1]);
    fprintf(stderr, "router: start_child(): Fork failed!\n");
    return -1;
  }
  /* Parent */

  pipes_close_parent(tofd,frmfd);

  fd_nonblockingmode(tofd[1]);
  fd_nonblockingmode(frmfd[0]);

  routerchilds[i].tochild   = tofd[1];
  routerchilds[i].fromchild = frmfd[0];
  routerchilds[i].childpid  = pid;
  routerchilds[i].hungry    = 0;
  routerchilds[i].childsize = 0;
  routerchilds[i].childout  = 0;
  return 0;
}

/*
 *	Catch each child-process death, and reap them..
 */
RETSIGTYPE sig_chld(signum)
int signum;
{
	int pid;
	int i;
	int statloc;
#ifdef HAVE_SYS_RESOURCE_H
	struct rusage r;
#endif

	for (;;) {

#ifdef  HAVE_WAIT3
#ifdef HAVE_SYS_RESOURCE_H
	  pid = wait3(&statloc,WNOHANG,&r);
#else
	  pid = wait3(&statloc,WNOHANG,NULL);
#endif
#else
#ifdef	HAVE_WAITPID
	  pid = waitpid(-1,&statloc,WNOHANG);
#else
	  pid = wait(&statloc);
#endif
#endif
	  if (pid <= 0) break;

	  for (i = 0; i < nrouters; ++i)
	    if (pid == routerchilds[i].childpid) {
	      routerchilds[i].childpid = -pid;
	      routerchilds[i].statloc = statloc;
#if defined(HAVE_WAIT3) && defined(HAVE_SYS_RESOURCE_H)
	      routerchilds[i].r = r;
#endif
	    }
	}

	/* re-instantiate the signal handler.. */
#ifdef SIGCLD
	SIGNAL_HANDLE(SIGCLD,  sig_chld);
#else
	SIGNAL_HANDLE(SIGCHLD, sig_chld);
#endif
}


static void start_childs  __((void));
static void start_childs()
{
  int i;

  memset(routerchilds, 0, sizeof(routerchilds));

  for (i = 0; i < nrouters; ++i)
    routerchilds[i].fromchild = -1;

  /* instantiate the signal handler.. */
#ifdef SIGCLD
  SIGNAL_HANDLE(SIGCLD,  sig_chld);
#else
  SIGNAL_HANDLE(SIGCHLD, sig_chld);
#endif

  for (i = 0; i < nrouters; ++i) {
    if (start_child(i))
      break; /* fork failed.. */
  }

  sleep(5); /* Wait a bit before continuting so that
	       child processes have change to boot */

  /* Collect start reports (initial "#hungry\n" lines) */
  parent_reader();

}

/*
 *  Read whatever there is, detect "#hungry\n" line, and return
 *  status of the hunger flag..
 */

static int reader_getc __((struct router_child *));
static int reader_getc(rc)
     struct router_child *rc;
{
  unsigned char c;

  /* Child exited but 'tochild' still set ? close it! */
  if (rc->childpid < 0 && rc->tochild >= 0) {
    pipes_shutdown_child(rc->tochild);
    rc->tochild = -1;

    /* Back to positive value */
    rc->childpid = - rc->childpid;

    if (logfn) {
      loginit(SIGHUP); /* Reinit/rotate the log every at line .. */
      fprintf(stdout, "[%d] ROUTER CHILD PROCESS TERMINATED; wait() status = ", rc->childpid);
      if (WIFSIGNALED(rc->statloc))
	fprintf(stdout, "SIGNAL %d", WSIGNALSTATUS(rc->statloc));
      else if (WIFEXITED(rc->statloc))
	fprintf(stdout, "EXIT %d", WEXITSTATUS(rc->statloc));
      else
	fprintf(stdout, "0x%04X ??", WEXITSTATUS(rc->statloc));
#if defined(HAVE_WAIT3) && defined(HAVE_SYS_RESOURCE_H)
      fprintf(stdout, "; time = %ld.%06ld usr %ld.%06ld sys",
	      (long)rc->r.ru_utime.tv_sec, (long)rc->r.ru_utime.tv_usec,
	      (long)rc->r.ru_stime.tv_sec, (long)rc->r.ru_stime.tv_usec);
#endif
      fprintf(stdout,"\n");
      fflush(stdout);
    }

  }

  errno = 0;

  if (rc->readout >= rc->readsize)
    rc->readout = rc->readsize = 0;
  if (rc->readsize <= 0)
    rc->readsize = read(rc->fromchild, rc->readbuf, sizeof(rc->readbuf));
  /* Now either we have something, or we don't.. */

  if (rc->readsize < 0) {
    errno = EAGAIN;
    return EOF;
  }
  if (rc->readsize == 0) {
    errno = 0;
    return EOF; /* REAL EOF */
  }

  c = rc->readbuf[rc->readout++];
  return (int)c;
}


/* Single child reader.. */
static int _parent_reader __((const int i));
static int _parent_reader(i)
     const int i;
{
  struct router_child *rc = &routerchilds[i];
  int c;

  if (rc->fromchild < 0) return 0;

  /* The FD is in non-blocking mode */

  for (;;) {
    c = reader_getc(rc);
    if (c == EOF) {
      /* Because the socket/pipe is in NON-BLOCKING mode, we
	 may drop here with an ERROR indication, which can be
	 cleared and thing resume latter.. */
      if (errno)
	break;

      /* An EOF ? -- child existed ?? */
      if (rc->tochild >= 0)
	close(rc->tochild);
      rc->tochild   = -1;
      rc->fromchild = -1;
      rc->hungry = 0;
      rc->childpid = 0;
      break;

    }
    if (rc->linebuf == NULL) {
      rc->linespace = 120;
      rc->linelen  = 0;
      rc->linebuf = emalloc(rc->linespace+2);
    }
    if (rc->linelen+2 >= rc->linespace) {
      rc->linespace <<= 1; /* Double the size */
      rc->linebuf = erealloc(rc->linebuf, rc->linespace+2);
    }
    rc->linebuf[rc->linelen++] = c;
    if (c == '\n') {
      /* End of line */
      rc->linebuf[rc->linelen] = 0;

      /*fprintf(stderr,"_parent_reader[%d] len=%d buf='%s'\n",rc->childpid,rc->linelen,rc->linebuf);*/

      if (rc->linelen == 1) {
	/* Just a newline.. */
	rc->linelen = 0;
	continue;
      }
      if (rc->linelen == 8 && strcmp(rc->linebuf,"#hungry\n")==0) {
	rc->linelen = 0;
	rc->hungry = 1;
	continue;
      }

      /* LOG THIS LINE! */

      if (logfn) {
	loginit(SIGHUP); /* Reinit/rotate the log every at line .. */
	fprintf(stdout, "[%d] ", rc->childpid);
	fputs(rc->linebuf, stdout);
	fflush(stdout);
      }

      rc->linelen = 0;
    }
  }
  return rc->hungry;
}

/* Single child writer.. */
static void _parent_writer __((const int i));
static void _parent_writer(i)
     const int i;
{
  struct router_child *rc = &routerchilds[i];
  int c, left;

  /* FD for writing ?? */
  if (rc->tochild < 0) return;

  /* Anything to write ? */
  for (;rc->childout < rc->childsize;) {
    left = rc->childsize - rc->childout;
    c = write(rc->tochild, rc->childline + rc->childout, left);
    if (c <= 0) break;
    rc->childout += c;
  }
  /* All written ?? */
  if (rc->childout >= rc->childsize)
    rc->childout = rc->childsize = 0;
}


#ifdef	HAVE_SELECT

#ifdef _AIX /* The select.h  defines NFDBITS, etc.. */
# include <sys/types.h>
# include <sys/select.h>
#endif


#if	defined(BSD4_3) || defined(sun)
#include <sys/file.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#ifndef	NFDBITS
/*
 * This stuff taken from the 4.3bsd /usr/include/sys/types.h, but on the
 * assumption we are dealing with pre-4.3bsd select().
 */

typedef long	fd_mask;

#ifndef	NBBY
#define	NBBY	8
#endif	/* NBBY */
#define	NFDBITS		((sizeof fd_mask) * NBBY)

/* SunOS 3.x and 4.x>2 BSD already defines this in /usr/include/sys/types.h */
#ifdef	notdef
typedef	struct fd_set { fd_mask	fds_bits[1]; } fd_set;
#endif	/* notdef */

#ifndef	_Z_FD_SET
#define	_Z_FD_SET(n, p)   ((p)->fds_bits[0] |= (1 << (n)))
#define	_Z_FD_CLR(n, p)   ((p)->fds_bits[0] &= ~(1 << (n)))
#define	_Z_FD_ISSET(n, p) ((p)->fds_bits[0] & (1 << (n)))
#define _Z_FD_ZERO(p)	  memset((char *)(p), 0, sizeof(*(p)))
#endif	/* !FD_SET */
#endif	/* !NFDBITS */

#ifdef FD_SET
#define _Z_FD_SET(sock,var) FD_SET(sock,&var)
#define _Z_FD_CLR(sock,var) FD_CLR(sock,&var)
#define _Z_FD_ZERO(var) FD_ZERO(&var)
#define _Z_FD_ISSET(i,var) FD_ISSET(i,&var)
#else
#define _Z_FD_SET(sock,var) var |= (1 << sock)
#define _Z_FD_CLR(sock,var) var &= ~(1 << sock)
#define _Z_FD_ZERO(var) var = 0
#define _Z_FD_ISSET(i,var) ((var & (1 << i)) != 0)
#endif

static void parent_reader()
{
  fd_set rdset, wrset;
  int i, highfd, fd, rc;
  struct timeval tv;

 redo_again:;

  _Z_FD_ZERO(rdset);
  _Z_FD_ZERO(wrset);
  highfd = -1;

  for (i = 0; i < nrouters; ++i) {
    /* Places to read from ?? */
    fd = routerchilds[i].fromchild;
    if (fd >= 0) {
      _Z_FD_SET(fd, rdset);
      if (highfd < fd)
	highfd = fd;
    }
    /* Something wanting to write ?? */
    fd = routerchilds[i].tochild;
    if (fd >= 0 &&
	routerchilds[i].childout < routerchilds[i].childsize) {
      _Z_FD_SET(fd, wrset);
      if (highfd < fd)
	highfd = fd;
    }
  }
  if (highfd < 0) return; /* Nothing to do! */

  tv.tv_sec = tv.tv_usec = 1;
  rc = select(highfd+1, &rdset, &wrset, NULL, &tv);

  if (rc == 0) return; /* Nothing to do, leave.. */

  if (rc < 0) {
    /* Drat, some error.. */
    if (errno == EINTR)
      goto redo_again;
    /* Hmm.. Do it just blindly (will handle error situations) */
    for (i = 0; i < nrouters; ++i) {
      _parent_writer(i);
      _parent_reader(i);
    }
    return;
  }

  /* Ok, select gave indication of *something* being ready for read */
  for (i = 0; i < nrouters; ++i) {
    fd = routerchilds[i].tochild;
    if (fd >= 0 && _Z_FD_ISSET(fd, wrset))
      _parent_writer(i);
    fd = routerchilds[i].fromchild;
    if (fd >= 0 && _Z_FD_ISSET(fd, rdset))
      _parent_reader(i);
  }
}

#else /* NO HAVE_SELECT */
static void parent_reader()
{
  int i;
  /* No select, but can do non-blocking -- we hope.. */
  for (i = 0; i < nrouters; ++i)
    _parent_reader(i);
}
#endif


/*
 * Child-process job feeder - distributes jobs in even round-robin
 * manner to all children.  Might some day do resource control a'la
 * "Priority: xyz" -> process subset 2,3,4
 */

/* "rd_doit()" at the feeding parent server */
static int parent_feed_child __((const char *fname, const char *dir));
static int parent_feed_child(fname,dir)
     const char *fname, *dir;
{
  static int rridx = -1;
  struct router_child *rc = NULL;
  int i;
  char *s;

  s = strchr(fname,'-');
  if (s && isdigit(*fname)) {
    long thatpid = atoi(s+1);
    if (thatpid > 1 &&
	kill(thatpid,0)==0) {
      /* Process of that PID does exist, and possibly even something
	 we can kick.. (we should be *root* here anyway!) */
      for (i = 0; i < nrouters; ++i) {
	/* Is it one of ours children ?? */
	if (routerchilds[i].childpid == thatpid)
	  return 0; /* Yes! No refeed! */
      }
    }
    /* Hmm..  perhaps it is safe to feed to a subprocess */
  }

  parent_reader();

  ++rridx; if (rridx >= nrouters) rridx = 0;
  for (i = 0; i < nrouters; ++i) {
    rc = &routerchilds[rridx];

    /* If no child at this slot, start one!
       (whatever has been the reason for its death..) */
    if (rc->childpid == 0) {
      start_child(rridx);
      sleep(2); /* Allow a moment for child startup */
      parent_reader();
    }

    /* If we have a hungry child with all faculties intact.. */
    if (rc->tochild >= 0 && rc->fromchild >= 0 && rc->hungry)
      break;

    /* Next index.. */
    ++rridx; if (rridx >= nrouters) rridx = 0;
  }
  if (!rc || !rc->hungry || rc->tochild < 0 || rc->fromchild < 0)
    return 0; /* Failed to find a hungry child!?
		  We should not have been called in the first place..  */

  /* Ok, we are feeding him.. */
  rc->hungry = 0;

  /* What shall we feed ?? */
  if (!dir || *dir == 0)
    sprintf(rc->childline, "%s\n", fname);
  else
    sprintf(rc->childline, "%s/%s\n", dir, fname);
  rc->childsize = strlen(rc->childline);
  rc->childout  = 0;

  /* Lets try to write it in one go.. */
  i = write(rc->tochild, rc->childline, rc->childsize);
  if (i > 0)
    rc->childout = i;
  if (rc->childout >= rc->childsize)
    rc->childout = rc->childsize = 0;

  /* .. or if not, we wait here until it has been fed.. */
  while (rc->childout < rc->childsize)
    parent_reader();

  return 1; /* Did feed successfully ?? */
}


/*
 * child_server()
 *    The real workhorse at the child, receives work, reports status
 *
 */
static void child_server(tofd,frmfd)
     int tofd, frmfd;
{
  FILE *fromfp = fdopen(tofd,  "r");
  FILE *tofp   = fdopen(frmfd, "w");
  char linebuf[8000];
  char *s, *fn;

  setvbuf(fromfp, NULL, _IOLBF, 0);
  setvbuf(tofp,   NULL, _IOFBF, 0);

  fprintf(tofp, "ROUTER CHILD PROCESS STARTED\n");
  fflush(tofp);

  linebuf[sizeof(linebuf)-1] = 0;

  while (!feof(fromfp) && !ferror(fromfp)) {
    fprintf(tofp, "\n#hungry\n");
    fflush(tofp);
    if (fgets(linebuf, sizeof(linebuf)-1, fromfp) == NULL)
      break; /* EOF ?? */

    s = strchr(linebuf,'\n');
    if (s) *s = 0;
    if (*linebuf == 0)
      break; /* A newline -> exit */

    /* Input is either:  "file.name" or "../path/file.name" */

    fn = strrchr(linebuf,'/');
    if (fn) {
      *fn++ = 0;
      s = linebuf; /* 'Dirs' */
    } else {
      fn = linebuf;
      s = linebuf + strlen(linebuf);
    }
    rd_doit(fn, s);
  }
  /* Loop ends for some reason, perhaps parent died and
     pipe got an EOF ?  We leave... Our caller exits. */

  fprintf(tofp, "ROUTER CHILD PROCESS TERMINATING\n");
  fflush(tofp);
}




/*
 * STABILITY option will make the router process incoming messages in
 * arrival (modtime) order, instead of randomly determined by position
 * in the router directory.  The scheme is to read in all the names,
 * and stat the files.  It would be possible to reuse the stat information
 * later, but I'm not convinced it is worthwhile since the later stat is
 * an fstat().  On the other hand, if we used splay tree insertion to
 * sort the entries, then the stat buffer could easily be reused in
 * makeLetter().
 *
 * SECURITY WARNING: iff makeLetter does not stat again, then there is
 * a window of opportunity for a Bad Guy to remove a file after it has
 * been stat'ed (with root privs), and before it has been processed.
 * This can be avoided partially by sticky-bitting the router directory,
 * and entirely by NOT saving the stat information we get here.
 */

struct de {
	int		f_name;
	time_t		mtime;
};

static int decmp __((const void *, const void *));
static int
decmp(a, b)
     const void *a, *b;
{
	register const struct de *aa = (const struct de *)a;
	register const struct de *bb = (const struct de *)b;

	return bb->mtime - aa->mtime;
}

static int desize, nbsize;
static struct de *dearray = NULL;
static char *nbarray = NULL;

static void rd_initstability __((void));
static void
rd_initstability()
{
	desize = 1;	/* max. number of directory entries */
	nbsize = 1;
	dearray = (struct de *)emalloc(desize * sizeof (struct de));
	nbarray = (char *)emalloc(nbsize * sizeof (char));
}

static void rd_endstability __((void));
static void
rd_endstability()
{
	if (dearray != NULL)
		free((char *)dearray);
	if (nbarray != NULL)
		free((char *)nbarray);
}

int
run_doit(argc, argv)
	int argc;
	const char *argv[];
{
	const char *filename;
	char *sh_memlevel = getlevel(MEM_SHCMD);
	int r;
	const char *av[3];

	if (argc != 2) {
	  fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
	  return 1;
	}

	filename = argv[1];

	/* Do one file, return value is 0 or 1,
	   depending on actually doing something
	   on a file */

	gensym = 1;
	av[0] = "process"; /* I think this needs to be here */
	av[1] = filename;
	av[2] = NULL;
	r = s_apply(2, av); /* "process" filename (within  rd_doit() ) */
	free_gensym();

	setlevel(MEM_SHCMD,sh_memlevel);

	return r;
}

static int
rd_doit(filename, dirs)
	const char *filename, *dirs;
{
	/* Do one file, return value is 0 or 1,
	   depending on actually doing something
	   on a file */

#ifdef	USE_ALLOCA
	char *buf;
#else
	static char *buf = NULL;
	static u_int blen = 0;
#endif
	const char *av[3];
	char *p;
	int len;
	char pathbuf[512];
	char *sh_memlevel = getlevel(MEM_SHCMD);
	int thatpid;
	struct stat stbuf;

	router_id = getpid();

	*pathbuf = 0;
	if (*dirs) {	/* If it is in alternate dir, move to primary one,
			   and process there! */
	  strcpy(pathbuf,dirs);
	  strcat(pathbuf,"/");
	}
	strcat(pathbuf,filename);

	len = strlen(filename);
	thatpid = 0;
	p = strchr(filename, '-');
	if (p != NULL) {
	  /* message file is "inode-pid" */
	  thatpid = atoi(p+1);

#if 0 /* very old thing, may harm at Solaris 2.6 ! */
	  if (thatpid < 10) {	/* old-style locking.. */
	    thatpid = 0;
	  }
#endif
	  /* Probe it!
	     Does the process exist ? */
	  if (thatpid && kill(thatpid,0)==0 && thatpid != router_id) {
	    /*
	     * an already locked message file,
	     * belonging to another process
	     */
	    if (*dirs) {
	      fprintf(stderr,
		      "** BUG **: %s%s not in primary router directory!\n",
		      dirs,filename);
	    }
	    return 0;
	    /*
	     * This should not happen anywhere but at
	     * primary router directory.  If  xxxx-nn
	     * format file exists anywhere else, it is
	     * a bug time!
	     */
	  }
	}
	if (strncmp(filename,"core",4) != 0 &&
	    (p == NULL || thatpid != router_id)) {
	  /* Not a core file, and ...
	     not already in format of 'inode-pid' */
	  /* If the pid did exist, we do not touch on that file,
	     on the other hand, we need to rename the file now.. */
#ifdef	USE_ALLOCA
	  buf = (char*)alloca(len+16);
#else
	  if (blen == 0) {
	    blen = len+16;
	    buf = (char *)malloc(len+16);
	  }
	  while (len + 12 > blen) {
	    blen = 2 * blen;
	    buf = (char *)realloc(buf, blen);
	  }
#endif
	  /* Figure out its inode number */
	  if (lstat(pathbuf,&stbuf) != 0) return 0; /* Failed ?  Well, skip it */
	  if (!S_ISREG(stbuf.st_mode))   return 0; /* Not a regular file ??   */

	  sprintf(buf, "%ld-%d", (long)stbuf.st_ino, router_id);

	  if (eqrename(pathbuf, buf) < 0)
	    return 0;		/* something is wrong, erename() complains.
				   (some other process picked it ?) */
	  filename = buf;
	  /* message file is now "file-#" and belongs to this process */
	}

#ifdef	MALLOC_TRACE
	mal_contents(stdout);
#endif

	gensym = 1;
	av[0] = "process"; /* I think this needs to be here */
	av[1] = filename;
	av[2] = NULL;
	s_apply(2, av); /* "process" filename (within  rd_doit() ) */
	free_gensym();

	setlevel(MEM_SHCMD,sh_memlevel);

#ifdef MALLOC_TRACE
	mal_contents(stdout);
#endif

	return 1;
}

static int rd_stability __((DIR *dirp, const char *dirs));
static int
rd_stability(dirp,dirs)
	DIR *dirp;
	const char *dirs;
{
	int deindex, nbindex, did_cnt;
	int namelen;
	struct dirent *dp;
	struct stat statbuf;
	char pathbuf[512]; /* Enough ? */

	deindex = 0;
	nbindex = 0;
	/* collect the file names */
	while ((dp = readdir(dirp)) != NULL) {
		if (mustexit)
			break;
		if (dp->d_name[0] == '.')
			continue;
		/* Handle only files beginning with number -- plus "core"-
		   files.. */
		if (!(dp->d_name[0] >= '0' && dp->d_name[0] <= '9') &&
		    strncmp(dp->d_name,"core",4) != 0)
			continue;

		/* See that the file is a regular file! */
		sprintf(pathbuf,"%s%s%s", dirs, *dirs?"/":"", dp->d_name);
		if (lstat(pathbuf,&statbuf) != 0) continue; /* ??? */
		if (!S_ISREG(statbuf.st_mode)) continue; /* Hmm..  */

		if (deindex >= desize) {
			desize *= 2;
			dearray =
			  (struct de *)realloc((char *)dearray,
					       desize * sizeof (struct de));
		}

		namelen = strlen(dp->d_name); /* Not everybody has d_namlen.. */

		while (nbindex + namelen + 1 >= nbsize) {
			nbsize *= 2;
			nbarray =
			  (char *)realloc(nbarray,
					  nbsize * sizeof (char));
		}

		/*
		 * The explicit alloc is done because alloc/dealloc
		 * of such small chunks should not affect fragmentation
		 * too much, and allocating large chunks would require
		 * management code and might still do bad things with
		 * the malloc algorithm.
		 */
		dearray[deindex].mtime = statbuf.st_mtime;
		dearray[deindex].f_name = nbindex;
		memcpy(nbarray + nbindex, dp->d_name, namelen+1);
		nbindex += namelen + 1;

		++deindex;
	}
	if (mustexit) {
		return deindex;
	}

	qsort((void *)dearray, deindex, sizeof dearray[0], decmp);

	did_cnt = 0;
	while (deindex-- > 0) {
		if (mustexit)
			break;
		if (gothup) 
			dohup(0);
		if (nbarray[dearray[deindex].f_name])
		  did_cnt += parent_feed_child(nbarray + dearray[deindex].f_name, dirs);
		nbarray[dearray[deindex].f_name] = 0;

		/* Maybe only process few files out of the low-priority
		   subdirs, so we can go back and see if any higher-priority
		   jobs have been created */
		if ((*dirs) &&
		    ((routerdirloops) && (routerdirloops == did_cnt)))
		  break;

	}
	return did_cnt;
}


static int rd_instability __((DIR *dirp, char *dirs));
static int
rd_instability(dirp, dirs)
	DIR *dirp;
	char *dirs;
{
	struct dirent *dp;
	int did_cnt = 0;
	struct stat statbuf;
	char pathbuf[512];

	while ((dp = readdir(dirp)) != NULL) {
		if (mustexit)
			break;
		if (gothup)
			dohup(0);

		/* Handle only files beginning with number -- plus "core"-
		   files.. */
		if (!(dp->d_name[0] >= '0' && dp->d_name[0] <= '9') &&
		    strncmp(dp->d_name,"core",4) != 0)
			continue;

		/* See that the file is a regular file! */
		sprintf(pathbuf,"%s%s%s", dirs, *dirs?"/":"", dp->d_name);
		if (lstat(pathbuf,&statbuf) != 0) continue; /* ??? */
		if (!S_ISREG(statbuf.st_mode)) continue; /* Hmm..  */

		did_cnt += parent_feed_child(dp->d_name, dirs);

		/* Only process one file out of the low-priority subdirs,
		   so we can go back and see if any higher-priority
		   jobs have been created */
		if (*dirs)
			break;

	}
	return did_cnt;
}


int
run_stability(argc, argv)
	int argc;
	const char *argv[];
{
	switch (argc) {
	case 1:
		printf("%s %s\n", argv[0], stability ? "on" : "off");
		break;
	case 2:
		if (strcmp(argv[1], "on") == 0) {
			real_stability = 1;
		} else if (strcmp(argv[1], "off") == 0) {
			real_stability = 0;
		} else {
	default:
			fprintf(stderr, "Usage: %s [ on | off ]\n", argv[0]);
			return 1;
		}
		break;
	}
	return 0;
}

int
run_daemon(argc, argv)
	int argc;
	const char *argv[];
{
#define ROUTERDIR_CNT 30
	DIR *dirp[ROUTERDIR_CNT];  /* Lets say we have max 30 router dirs.. */
	char *dirs[ROUTERDIR_CNT];
	int did_cnt, i;
	char *s, *rd, *routerdirs = getzenv("ROUTERDIRS");
	char pathbuf[256];
	memtypes oval = stickymem;

	if (nrouters > MAXROUTERCHILDS)
	  nrouters = MAXROUTERCHILDS;

	start_childs();

	SIGNAL_HANDLE(SIGTERM, sig_exit);	/* mustexit = 1 */
	for (i=0; i<ROUTERDIR_CNT; ++i) {
		dirp[i] = NULL; dirs[i] = NULL;
	}
	/* dirp[0] = opendir("."); */	/* assert dirp != NULL ... */
#if 0
#ifdef BSD
	dirp[0]->dd_size = 0;	/* stupid Berkeley bug workaround */
#endif
#endif
	stickymem = MEM_MALLOC;
	dirs[0] = strnsave("",1);
	if (routerdirs) {
		/* Store up secondary router dirs! */
		rd = routerdirs;
		for (i = 1; i < ROUTERDIR_CNT && *rd; ) {
			s = strchr(rd,':');
			if (s)  *s = 0;
			sprintf(pathbuf,"../%s",rd);
			/* strcat(pathbuf,"/"); */

			dirs[i] = strdup(pathbuf);
			++i;

			if (s)
			  *s = ':';
			if (s)
			  rd = s+1;
			else
			  break;
		}
	}
	setfreefd();
	if (stability)
		rd_initstability();
	stickymem = oval;
	did_cnt = 0;
	i = -1;
	for (; !mustexit ;) {
		++i;	/* Increment it */
		/* The last of the alternate dirs ?  Reset.. */
		if (i >= ROUTERDIR_CNT || dirs[i] == NULL) {
			i = 0;

			canexit = 1;
		/*
		 * If a shell signal interrupts us here, there is
		 * potential for problems knowing which file descriptors
		 * are free.  One could add a setfreefd() to the trap
		 * routine, in that case.
		 */
			sleep(sweepintvl);
			canexit = 0;
		}

		/*
		 * We would like to do a seekdir()/rewinddir()
		 * instead of opendir()/closedir()  inside this
		 * loop to avoid allocating and freeing a chunk
		 * of memory all the time.  This can lead to
		 * memory fragmentation and thus growing VM.
		 *
		 * However several systems do NOT guarantee that
		 * rewinddir() will find any new data from the
		 * system...
		 */
		/* rewinddir(dirp[i]); */	/* some system w/o this ? */

#if 0
#ifdef	BUGGY_CLOSEDIR
		/*
		 * Major serious bug time here;  some closedir()'s
		 * free dirp before referring to dirp->dd_fd. GRRR.
		 * XX: remove this when bug is eradicated from System V's.
		 */
		close(dirp[i]->dd_fd);
#endif
		closedir(dirp[i]);
#endif
		dirp[i] = opendir(dirs[i][0] == 0 ? "." : dirs[i]);

		did_cnt = 0;
		if (dirp[i] != NULL) {
		  if (stability)
		    did_cnt = rd_stability(dirp[i],dirs[i]);
		  else
		    did_cnt = rd_instability(dirp[i],dirs[i]);

		  if (stability != real_stability) {
		    stability = real_stability;
		    if (stability == 0)
		      rd_endstability();
		    else
		      rd_initstability();
		  }
#if 1
#ifdef	BUGGY_CLOSEDIR
		  /*
		   * Major serious bug time here;  some closedir()'s
		   * free dirp before referring to dirp->dd_fd. GRRR.
		   * XX: remove this when bug is eradicated from System V's.
		   */
		  close(dirp[i]->dd_fd);
#endif
		  closedir(dirp[i]);
		  dirp[i] = NULL;
#endif
		}

		if (mustexit)
			break;

		/* Alter router directory.  If processed directory
		   had any job, reset the  index  to the begin.   */
		if (did_cnt > 0)
			i = -1;
	}
	for (i=0; i < ROUTERDIR_CNT; ++i)
	  if (dirs[i]) {
	    if (dirp[i]) {
#if 0
#ifdef	BUGGY_CLOSEDIR
	      /*
	       * Major serious bug time here;  some closedir()'s
	       * free dirp before referring to dirp->dd_fd. GRRR.
	       * XX: remove this when bug is eradicated from System V's.
	       */
	      close(dirp[i]->dd_fd);
#endif
	      closedir(dirp[i]);
#endif
	    }
	    free(dirs[i]);
	  }
	return 0;
}

/*
 * Based on the name of a message file, figure out what to do with it.
 */

struct protosw {
	const char *pattern;
	const char *function;
} psw[] = {
/*{	"[0-9]*.x400",		"x400"		}, */
/*{	"[0-9]*.fax",		"fax"		}, */
/*{	"[0-9]*.uucp",		"uucp"		}, */
{	"[0-9]*",		"rfc822"	},
};


int
run_process(argc, argv)
	int argc;
	const char *argv[];
{
	struct protosw *pswp;
	char *file;
	int r;
	char *sh_memlevel = getlevel(MEM_SHCMD);

	if (argc != 2 || argv[1][0] == '\0') {
		fprintf(stderr, "Usage: %s messagefile\n", argv[0]);
		return PERR_USAGE;
	}
#ifdef	USE_ALLOCA
	file = (char*)alloca(strlen(argv[1])+1);
#else
	file = (char*)emalloc(strlen(argv[1])+1);
#endif
	strcpy(file, argv[1]);

	r = 0;	/* by default, ignore it */
	for (pswp = &psw[0]; pswp < &psw[(sizeof psw / sizeof psw[0])]; ++pswp)
		if (strmatch(pswp->pattern, file)) {
			printf("process %s %s\n", pswp->function, file);
			argv[0] = pswp->function;
			r = s_apply(argc, argv); /* process-by-FUNC filename */
			printf("done with %s\n", file);
			if (r)
				printf("status %d\n", r);
			break;
		}

#ifndef	USE_ALLOCA
	free(file);
#endif
	setlevel(MEM_SHCMD,sh_memlevel);

	return r;
}