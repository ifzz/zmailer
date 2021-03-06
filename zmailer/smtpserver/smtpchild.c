/*
 *  ZMailer smtpserver child-registry
 *
 *  Registers/keeps track of IP addresses that are currently
 *  talking with us in order to figure out of there are too
 *  many parallel connections from same IP address out there..
 *
 *  Copyright Matti Aarnio <mea@nic.funet.fi> 1998-1999, 2003-2004
 *
 */

#include "smtpserver.h"

static int child_space = 0;
static int child_top   = 0;

static struct {
  int pid;	/* PID of the working smtpserver (subprocess) */
  int tag;	/* 0: smtp, 1: smtps, 2: submit, 3: lmtp, ... */
  time_t when;	/* When to next check on this child */
  Usockaddr addr; /* Address the connection comes from */
} *childs = NULL;

static int child_poll_interval = 30;
static time_t child_now = 0;


int childsameip(addr, socktag, childcntp)
     Usockaddr *addr;
     int socktag, *childcntp;
{
    int i, cnt = 1; /* Ourself */
    int childcnt = 1; /* Ourself */

    time(&child_now);
    
    *childcntp = 1;
    if (childs == NULL) return 0;

    for (i = 0; i < child_top; ++i) {
      /* Do we have a child to check for possible sneaky
	 disappearance ? */
      if (childs[i].pid != 0 &&
	  childs[i].when > child_now) {
	/* Ok, we check on this, reset the timer */
	childs[i].when = child_now + child_poll_interval;
	/* Does this process exist ? */
	if (kill(childs[i].pid, 0) != 0) {
	  /* No such process in there anymore ? */
	  memset(&childs[i], 0, sizeof(childs[i]));
	  continue;
	}
      }
      if (childs[i].pid != 0  /* PID non zero */ &&
	  childs[i].tag == socktag /* Same type! */ &&
	  addr->v4.sin_family == childs[i].addr.v4.sin_family) {
	/* Same AddressFamily */
	if ((addr->v4.sin_family == AF_INET &&
	     /* Address is IPv4 one */
	     memcmp(& addr->v4.sin_addr, & childs[i].addr.v4.sin_addr, 4) == 0)
#if defined(AF_INET6) && defined(INET6)
	    ||
	    ((addr->v6.sin6_family == AF_INET6 &&
	      /* ... or Address is IPv6 one */
	      memcmp(& addr->v6.sin6_addr, & childs[i].addr.v6.sin6_addr, 16) == 0))
#endif
	    )
	  ++cnt;
      }
      if (childs[i].tag == socktag /* Same type! */ &&
	  childs[i].pid != 0)
	++childcnt;
    }

    *childcntp = childcnt;
    return cnt;
}

void childregister(cpid, addr, socktag)
     int cpid, socktag;
     Usockaddr *addr;
{
	int i;

	/* Called with SIGCHLD held ! */

	if (kill(cpid, 0) < 0) {
	  /* When there is no subprocess with this PID, DON'T
	     register anything!  The subprocess is already
	     gone for some reason... */
	  return;
	}

	/* Now count those processes.. */

	MIBMtaEntry->ss.IncomingSMTPSERVERprocesses += 1;
	MIBMtaEntry->ss.IncomingSMTPSERVERforks     += 1;

	/* And do registering!         */

	time(&child_now);

	if (child_top == child_space) {
	  if (child_space == 0) {
	    child_space = 8;
	  } else {
	    child_space <<= 1;
	  }
	  /* 'childs' can be NULL here.. */
	  childs = erealloc(childs, child_space * sizeof(*childs));
	  /* Now it is NULL only in error.. and we are in deep trouble.. */
	  if (!childs) {
	    child_space = 0;
	    return;
	  }

	  for (i = child_top; i < child_space; ++i)
	    memset(&childs[i], 0, sizeof(childs[i]));
	}
	for (i = 0; i < child_space; ++i) {
	  /* Do we have a child to check for possible sneaky
	     disappearance ? */
	  if (childs[i].pid != 0 &&
	      childs[i].when > child_now) {
	    /* Ok, we check on this, reset the timer */
	    childs[i].when = child_now + child_poll_interval;
	    /* Does this process exist ? */
	    if (kill(childs[i].pid, 0) != 0) {
	      /* No such process in there anymore !?
		 We free this slot now! */
	      childreap(childs[i].pid);
	    }
	  }
	  if (childs[i].pid == 0) { /* Free slot! */
	    childs[i].pid  = cpid;
	    childs[i].when = child_now + child_poll_interval;
	    childs[i].tag  = socktag;
	    if (addr->v4.sin_family == AF_INET) {
	      childs[i].addr.v4.sin_family = AF_INET;
	      memcpy(&childs[i].addr.v4.sin_addr, &addr->v4.sin_addr, 4);
	    }
#if defined(AF_INET6) && defined(INET6)
	    else if (addr->v6.sin6_family == AF_INET6) {
	      childs[i].addr.v6.sin6_family = AF_INET6;
	      memcpy(&childs[i].addr.v6.sin6_addr, &addr->v6.sin6_addr, 16);
	    }
#endif
	    if (i >= child_top)
	      child_top = i+1;

	    return;
	  }
	}
}

/* Started children call this to disable this tracking code */
void disable_childreap()
{
	childs = NULL;
	child_top = 0;
}

void childreap(cpid)
int cpid;
{
	int i;

	if (childs == NULL) return;

	for (i = 0; i < child_top; ++i)
	  if (childs[i].pid == cpid) {

	    MIBMtaEntry->ss.IncomingSMTPSERVERprocesses -= 1;

	    /* Drop the class-full counts; the main loop does
	       setting of these gauges, we just speed their
	       decrementing, just in case.  */

	    switch (childs[i].tag) {
	    case 0:
	      MIBMtaEntry->ss.IncomingParallelSMTPconnects   -= 1;
	      break;
	    case 1:
	      MIBMtaEntry->ss.IncomingParallelSMTPSconnects  -= 1;
	      break;
	    case 2:
	      MIBMtaEntry->ss.IncomingParallelSUBMITconnects -= 1;
	      break;
	    default:
	      break;
	    }

	    memset(&childs[i], 0, sizeof(childs[i]));

	    break;
	  }
}
