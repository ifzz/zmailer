/*
 *    Copyright 1988 by Rayan S. Zachariassen, all rights reserved.
 *      This will be free software, but only when it is finished.
 */
/*
 *    Several extensive changes by Matti Aarnio <mea@nic.funet.fi>
 *      Copyright 1991-2004.
 */

/*
 * Protocol from client to server is of TEXT LINES that end with '\n'
 * and are without '\r'... (that are meaningless inside the system.)
 *
 * The fd-passing system sends one byte of random junk, and a bunch
 * of ancilliary data (the fd.)
 */


#include "smtpserver.h"

static const char *Hungry = "#hungry\n";

static int subdaemon_handler_rtr_init  __((void**));
static int subdaemon_handler_rtr_input __((void *, struct peerdata*));
static int subdaemon_handler_rtr_preselect  __((void*, fd_set *, fd_set *, int *));
static int subdaemon_handler_rtr_postselect __((void*, fd_set *, fd_set *));
static int subdaemon_handler_rtr_shutdown   __((void*));

struct subdaemon_handler subdaemon_handler_router = {
	subdaemon_handler_rtr_init,
	subdaemon_handler_rtr_input,
	subdaemon_handler_rtr_preselect,
	subdaemon_handler_rtr_postselect,
	subdaemon_handler_rtr_shutdown
};

typedef struct state_rtr {
	struct peerdata *replypeer;
	int   routerpid;
	FILE *tofp;
	int   fromfd;
	char *buf;
	int   bufsize;
	int   sawhungry;
} RtState;


static void subdaemon_killr __(( RtState * RTR ));

static void
subdaemon_killr(RTR)
     RtState *RTR;
{
	if (RTR->routerpid > 1) {
	  if (RTR->tofp == NULL)
	    fclose(RTR->tofp);
	  RTR->tofp   = NULL;
	  if (RTR->fromfd >= 0)
            close(RTR->fromfd);
	  RTR->fromfd = -1;
	  kill(RTR->routerpid, SIGKILL);
	  RTR->routerpid = 0;
	}
}


/* ============================================================ */

/*
 * The way VRFY and EXPN are implemented, and even MAIL FROM and RCPT TO
 * checking, we somehow connect to a router and ask it to do stuff for us.
 * There are three routines, one to connect to the router, one to kill it
 * off again, and the line-getting routine that gets everything the router
 * prints at us, one line at a time.
 */


#ifndef HAVE_PUTENV
static const char *newenviron[] =
  { "SMTPSERVER=y", NULL };
#endif

static int subdaemon_callr __((RtState * RTR));
static int subdaemon_callr (RTR)
     RtState *RTR;
{
	int rpid = 0, to[2], from[2], rc;
	char *cp;

	if (pipe(to) < 0 || pipe(from) < 0)
	  return -1;

	if (routerprog == NULL) {
	  cp = (char *)getzenv("MAILBIN");
	  if (cp == NULL) {
	    zsyslog((LOG_ERR, "MAILBIN unspecified in zmailer.conf"));
	    return -1;
	  }
	  routerprog = emalloc(strlen(cp) + sizeof "router" + 2);
	  if (!routerprog) return -1; /* malloc failed! */
	  sprintf(routerprog, "%s/router", cp);
	}

	rpid = fork();
	if (rpid == 0) {	/* child */
	  rpid = getpid();
	  if (to[0] != 0)
	    dup2(to[0], 0);
	  if (from[1] != 1)
	    dup2(from[1], 1);
	  dup2(1, 2);
	  if (to[0] > 2)   close(to[0]);
	  if (to[1] > 2)   close(to[1]);
	  if (from[0] > 2) close(from[0]);
	  if (from[1] > 2) close(from[1]);

	  runasrootuser();	/* XXX: security alert! */
#ifdef HAVE_PUTENV
	  putenv("SMTPSERVER=y");
#else
	  environ = (char **) newenviron;
#endif
	  execl(routerprog, "router", "-io-i", "-Ismtpserver", NULL);

#define	BADEXEC	"#BADEXEC\n\n"
	  write(1, BADEXEC, sizeof(BADEXEC)-1);
	  _exit(1);

	} else if (rpid < 0)
	  return -1;

	RTR->routerpid = rpid;

	close(to[0]);
	close(from[1]);

	RTR->tofp   = fdopen(to[1], "w");
	fd_blockingmode(to[1]);
	if (! RTR->tofp ) return -1; /* BAD BAD! */

	RTR->fromfd = from[0];
	fd_blockingmode(RTR->fromfd);

	for (;;) {
	  RTR->bufsize = 0;
	  rc = fdgets( & RTR->buf, & RTR->bufsize, RTR->fromfd, 10);
	  if ( rc < 1 || ! RTR->buf ) {
	    /* FIXME: ERROR PROCESSING ! */
	  }
	  if (strncmp( RTR->buf, BADEXEC, sizeof(BADEXEC) - 3) == 0) {
	    subdaemon_killr(RTR);
	    return -1;
	  }

	  if (strcmp( RTR->buf, Hungry ) == 0) {
	    RTR->sawhungry = 1;
	    break;
	  }
	}
	fd_nonblockingmode(RTR->fromfd);

	return rpid;
}


/* ------------------------------------------------------------ */


static int
subdaemon_handler_rtr_init (statep)
     void **statep;
{
	struct state_rtr *state = calloc(1, sizeof(struct state_rtr));
	*statep = state;

	if (state) {
	  state->routerpid = 0;
	  state->fromfd = -1;
	}

	{
	  extern int logstyle;
	  extern char *logfile;
	  extern void openlogfp __((SmtpState * SS, int insecure));

	  logstyle = 0;
	  if (logfp) fclose(logfp); logfp = NULL;
	  logfile = "smtpserver-rtr-subdaemons.log";
	  openlogfp(NULL, 1);
	  setlinebuf(logfp);
	}


	return 0;
}

/*
 * subdaemon_handler_xx_input()
 *   ret > 0:  XOFF... busy right now!
 *   ret == 0: XON... give me more work!
 */
static int
subdaemon_handler_rtr_input (state, peerdata)
     void *state;
     struct peerdata *peerdata;
{
	RtState *RTR = state;
	int rc = 0;

	if (RTR->routerpid <= 1) {
	  rc = subdaemon_callr(RTR);
	  if (rc < 2) {
	    /* FIXME: error processing! */
	  }

	  /* Now   RTR->fromfd   is in NON-BLOCKING MODE!
	     However  RTR->tofp  is definitely in blocking! */
	}

	if (!RTR->sawhungry)
	  return EAGAIN; /* Do come again! */

	RTR->replypeer = peerdata;

	fwrite(peerdata->inpbuf, peerdata->inlen, 1, RTR->tofp);
	fflush(RTR->tofp);

	RTR->bufsize    = 0;
	RTR->sawhungry  = 0;
	peerdata->inlen = 0;

	return EAGAIN;
}


static int
subdaemon_handler_rtr_preselect (state, rdset, wrset, topfdp)
     void *state;
     fd_set *rdset, *wrset;
     int *topfdp;
{
	RtState *RTR = state;

	if (! RTR) return 0; /* No state to monitor */

	/* If we have router underneath us,
	   check if it has something to say! */
 
	if (RTR->fromfd >= 0) {
	  _Z_FD_SETp(RTR->fromfd, rdset);
	  if (*topfdp < RTR->fromfd)
	    *topfdp = RTR->fromfd;
	}

	return -1;
}

static int
subdaemon_handler_rtr_postselect (state, rdset, wrset)
     void *state;
     fd_set *rdset, *wrset;
{
	RtState *RTR = state;
	int rc = 0;

	if (! RTR) return -1; /* No state to monitor */
	if (RTR->fromfd < 0) return -1; /* No router there.. */

	if ( _Z_FD_ISSETp(RTR->fromfd, rdset) ) {
	  /* We have something to read ! */

	  rc = fdgets( & RTR->buf, & RTR->bufsize, RTR->fromfd, -1);

	  if (rc < 0 && errno == EAGAIN) return -EAGAIN;  /* */

	  if (rc > 0) {
	    if (RTR->buf[rc-1] == '\n') {
	      /* Whole line accumulated, send it out! */

	      subdaemon_send_to_peer(RTR->replypeer, RTR->buf, rc);
	      RTR->bufsize = 0; /* Zap it.. */
	    }

	    if (strcmp( RTR->buf, Hungry ) == 0) {
	      RTR->sawhungry = 1;
	      RTR->replypeer = NULL;
	    }
	  }
	}

	return RTR->sawhungry;
}



static int
subdaemon_handler_rtr_shutdown (state)
     void *state;
{
	return -1;
}


/* --------------------------------------------------------------- */
/*  client caller interface                                        */
/* --------------------------------------------------------------- */

/* extern int  router_rdz_fd; */

struct rtr_state {
	int fd_io;
	FILE *outfp;
	int buflen;
	char *buf;
	char *pbuf;
	int sawhungry; /* remote may yield  N  lines of output, 
			  until  Hungry  */
};


static void smtprouter_kill __((struct rtr_state *));
static void
smtprouter_kill ( state )
     struct rtr_state * state;
{
	if (state->outfp) fclose(state->outfp);
	state->outfp = NULL;

	if (state->fd_io >= 0) {
	  close(state->fd_io);
	  state->fd_io = -1;
	}
	if (state->buf)  free(state->buf);
	state->buf = NULL;
}


static int smtprouter_init __((struct rtr_state **));

static int
smtprouter_init ( statep )
     struct rtr_state **statep;
{
	struct rtr_state *state = *statep;
	int toserver[2];
	int rc;

	if (!state)
	  state = *statep = malloc(sizeof(*state));
	if (!state) return -1;

	memset( state, 0, sizeof(*state) );
	state->fd_io = -1;

	/* Abusing the thing, to be exact, but... */
	rc = socketpair(PF_UNIX, SOCK_STREAM, 0, toserver);
	if (rc != 0) return -2; /* create fail */

	state->fd_io = toserver[1];
	rc = fdpass_sendfd(router_rdz_fd, toserver[0]);

	if (debug)
	  type(NULL,0,NULL,"smtprouter_init: fdpass_sendfd(%d,%d) rc=%d, errno=%s",
	       ratetracker_rdz_fd, toserver[0], rc, strerror(errno));

	if (rc != 0) {
	  /* did error somehow */
	  close(toserver[0]);
	  close(toserver[1]);
	  return -3;
	}
	close(toserver[0]); /* Sent or not, close the remote end
			       from our side. */

	if (debug)
	  type(NULL,0,NULL,"smtprouter_init; 9");

	fd_blockingmode(state->fd_io);

	state->outfp = fdopen(state->fd_io, "w");

	if (debug)
	  type(NULL,0,NULL,"smtprouter_init; 10");
	errno = 0;

	if (state->buf) state->buf[0] = 0;
	state->buflen = 0;
	if (fdgets( & state->buf, & state->buflen, state->fd_io, 10 ) < 0) {
	  /* something failed! -- timeout in 10 secs ?? */
	  if (debug)
	    type(NULL,0,NULL,"smtprouter_init; FAILURE 10-B");
	  smtprouter_kill( state );
	  return -4;
	}

	if (debug)
	  type(NULL,0,NULL,"smtprouter_init; 11; errno=%s",
	       strerror(errno));

	if ( !state->buf  || (strcmp(state->buf, Hungry) != 0) )
	  return -5; /* Miserable failure.. Not getting proper protocol! */

	if (debug)
	  type(NULL,0,NULL,"smtprouter_init; 12");

	state->sawhungry = 1;

	return 0;

}



/*
 * The way VRFY and EXPN are implemented, and even MAIL FROM and RCPT TO
 * checking, we somehow connect to a router and ask it to do stuff for us.
 * There are three routines, one to connect to the router, one to kill it
 * off again, and the line-getting routine that gets everything the router
 * prints at us, one line at a time.
 */


/*
 * Now we can do VRFY et al using the router we have connected to.
 */

char *
router(SS, function, holdlast, arg, len)
     SmtpState *SS;
     const char *function, *arg;
     const int holdlast, len;
{
	int rc;
	struct rtr_state *state;
	unsigned char *p;

	if (arg == NULL) {
	  type(SS, 501, NULL, NULL);
	  return NULL;
	}
	if (!enable_router) {
	  type(SS, 400, "4.4.0","Interactive routing subsystem is not enabled");
	  return NULL;
	}

	state = SS->irouter_state;
	if (! state || !state->outfp) {
	  smtprouter_init( &state );
	  if (! state || !state->outfp || !state->sawhungry) {
	    type(SS, 440, "4.4.0", "Failed to init interactive router subsystem");
	    smtprouter_kill( state );
	    return NULL;
	  }
	}
	/* if (state && state->outfp)  <<-- always true here ... */
	SS->irouter_state = state;

	if (! state->sawhungry ) {
	  /* Wrong initial state at this point in call! */
	  smtprouter_kill( state );
	  type(SS, 440, "4.4.0", "Interactive router subsystem lost internal sync ??");
	  return NULL;
	}

	/* We have seen  "#hungry\n",  now we go and send our little thingie
	   down the pipe... */

	fprintf(state->outfp, "%s\t", function);
	fwrite(arg, len, 1, state->outfp);
	fprintf(state->outfp, "\n");
	fflush(state->outfp);
	if (ferror(state->outfp)) {
	  fclose(state->outfp);
	  state->outfp = NULL;
	  return NULL;
	}

	/* Now we collect replies, until we see "#hungry" again.. */
	/* .. we do strip the trailing newline first.. */
	state->sawhungry = 0;

	while ( ! state->sawhungry ) {

	  /* The reply is better to reach us within 60 seconds.. */

	  if (state->buf) state->buf[0] = 0;
	  state->buflen = 0;
	  rc = fdgets( & state->buf, & state->buflen, state->fd_io, 60 );

	  if (state->buf && (rc > 0))
	    if (state->buf[rc-1] == '\n')
	      state->buf[--rc] = 0;

	  if (debug)
	    type(NULL,0,NULL, "fdgets()->%p rc=%d buf=\"%s\"",
		 state->buf, rc, (state->buf ? state->buf : "<NULL>"));

	  if (rc <= 0) {
	    /* TIMED OUT !  BRR... */
	    smtprouter_kill( state );
	    type(SS, 450, "4.5.0", "Interactive router %s!",
		 (rc < 0) ? "timed out" : "EOFed" );
	    return NULL;
	  }

	  if ( strcmp(state->buf, "#hungry") == 0 ) {
	    state->sawhungry = 1;
	    /* Got "#hungry",  bail out, and yield pbuf .. */
	    if (debug)
	      type(NULL,0,NULL," GOT #hungry !");
	    break;
	  }


	  /* We have a new reply line here.. 
	     do present the previous one, if it exists... */

	  if (state->pbuf) {
	    p = (unsigned char *)state->pbuf; /* Previous buffer */
	    if (strlen(state->pbuf) > 3 &&
		isdigit(p[0]) && isdigit(p[1]) && isdigit(p[2]) && 
		(p[3] == ' ' || p[3] == '-')) {
	      int code = atoi(state->pbuf);

	      type(SS, -code, NULL, "%s", state->pbuf + 4);
	    } else {
	      type(SS, -250, NULL, "%s", state->pbuf);
	    }
	    free(state->pbuf);
	    state->pbuf = NULL;
	  }

	  state->pbuf = state->buf;
	  state->buf = NULL;

	}

	/* End of reply collection loop, here  state->pbuf  should
	   have content! */
	if (! state->pbuf ) {
	  smtprouter_kill( state );
	  if (holdlast)
	    return strdup("250 2.7.1 Interactive policy router failed, letting this thru...");
	  type(SS, 250, "2.7.1", "Interactive policy router failed, letting this thru...");
	  return NULL;
	}

	if (holdlast) {
	  /* Caller wants to have this! */
	  char *retp = state->pbuf;
	  state->pbuf = NULL;
	  return retp;

	} else {

	  /* Not holding last, type out the final thing */
	  p = (unsigned char *)state->pbuf; /* Previous buffer */
	  if (strlen(state->pbuf) > 3 &&
	      isdigit(p[0]) && isdigit(p[1]) && isdigit(p[2]) && 
	      (p[3] == ' ' || p[3] == '-')) {
	    int code = atoi(state->pbuf);

	    type(SS, code, NULL, "%s", state->pbuf + 4);
	  } else {
	    type(SS, 250, NULL, "%s", state->pbuf);
	  }
	  free(state->pbuf);
	  state->pbuf = NULL;

	}
	typeflush(SS);
	return malloc(1); /* Just something freeable.. */
}
