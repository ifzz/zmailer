/*
 *	Copyright 1988 by Rayan S. Zachariassen, all rights reserved.
 *	This will be free software, but only when it is finished.
 *
 */
/*
 *	Lots of modifications (new guts, more or less..) by
 *	Matti Aarnio <mea@nic.funet.fi>  (copyright) 1992-1995
 */

#define USE_SIGREAPER /* DO Use SIGCLD-driven reaper.. */

#include "hostenv.h"
#include "malloc.h"
#include "splay.h"
#include "shmmib.h"

struct config_entry {
	struct config_entry *next;
	char	*channel;	/* channel part of pattern		     */
	char	*host;		/* host part of pattern			     */
	time_t	interval;	/* how often to start up these things	     */
	time_t	expiry;		/* bounce the message after this long in q's */
	char	*expiryform;	/* use this form as the error message	     */
			/* Following two need to be SIGNED variables	     */
	int	uid;		/* what uid to run the transport agent under */
	int	gid;		/* what gid to run the transport agent under */
	char	*command;	/* the command to run			     */
	int	flags;		/* miscellaneous flags			     */
#define CFG_BYCHANNEL 0x0001
#define CFG_WITHHOST  0x0002
#define CFG_AGEORDER  0x0004	/* In order of the ctlfile->ctime -fields    */
#define CFG_QUEUEONLY 0x0008
#if 0
	int	bychannel;	/* indicates $channel occurs in command      */
	int	withhost;	/* indicated $host occurs in command	     */
	int	ctlageorder;	/* run attempts on oldest-first basis	     */
#endif
	int	maxkids;	/* run command only if # TA's running < this */
	int	maxkidChannel;	/* run only if # TA's running channel < this */
	int	maxkidThreads;	/*run only if # TA's running thread-ring<this*/
	int	idlemax;	/* max time to keep idle ta-procs around     */
	int	skew;		/* retry skew parameter			     */
	int	mark;		/* non-0 if we started a TA the last time    */
	int	priority;	/* Scheduling priority			     */
	int	overfeed;	/* How much overfeeding instead of sync processing ? */
	char	**argv;		/* execv parameters for the command	     */
	int	nretries;	/* number of retry factors known	     */
	int	*retries;	/* list of nretries retry factors	     */
	char	*deliveryform;	/* name of delivery error form		     */
};

#define	L_CTLFILE	0
#define L_HOST		1
#define L_CHANNEL	2
#define SIZE_L		3

extern struct sptree *spt_mesh[SIZE_L];

#define	V_NONE		0
#define	V_ALL		1
#define	V_SELECT	2

struct ctlfile {
	int	fd;		/* a file descriptor pointing at the file    */
	char	*vfpfn;		/* a filename for verbose logging of mail    */
	uid_t	uid;		/* the owner of the control file (= msg file)*/
	time_t	ctime;		/* when the original file arrived	     */
	time_t	envctime;	/* when the transport file was created	     */
	int	haderror;	/* some errors/diagnostics need processing   */
	struct vertex	*head;		/* head of the list of groups	     */
	int	nlines;		/* number of lines/entries in the file	     */
	char	*erroraddr;	/* error address(es)			     */
	int	iserrmesg;	/* error channel -- to detect 'MAIL FROM:<>' */
	char	*contents;	/* the control file as copied into memory    */
	char	*logident;	/* identification for log entries	     */
	char	*deliveryform;	/* [mea] Name of delivery error form	     */
	char	*envid;		/* DSN envid				     */
	char	*dsnretmode;	/* DSN ret-mode: FULL/HDRS		     */
	u_long	id;		/* identification # (inode# of control file) */
	char	*mid;		/* msg identification (name of message file) */
	int	dirind;		/* -1, if no hashed dir, >= 0, if subdired   */
	int	rcpnts_total;	/* how many recipients all in all ?	     */
	int	rcpnts_failed;	/* .. failed ones of them ?		     */
	int	rcpnts_work;	/* .. yet to deliver ?			     */
	int	mark;		/* flag used by selector() to pass filenames */
	int	msgbodyoffset;	/* size of original headers to skip on errrpt*/
	long	offset[1];	/* array of nlines byte offsets into the file*/
};

struct threadgroup {
	int		groupid;	/* Unique id */
	int		threads;	/* Number of threads in the group   */
	int		transporters;	/* Number of transporters running   */
	int		idlecnt;	/* Number of idle transporters	    */
	struct procinfo	*idleproc;	/* Idle procs in this group	    */
	struct web	*wchan;		/* Channel identity web		    */
	struct web	*whost;		/* Host identity web		    */
	char		*hostpatt;
	int		withhost;	/* When set, wchan matters.	    */
	struct thread	*thread;	/* First of the thread in the group */
	struct thread	*thrtail;	/* Last of the threads in the group */
	struct threadgroup *next, *prev; /* Ring of thread groups	    */
	struct config_entry *cep;	/* Pointer to a config database     */
	struct config_entry ce;		/* consed scheduler config file entry*/
};

struct thread {
	long		threadid;	/* Unique id */
	time_t		wakeup;		/* When to wake up ?		    */
	int		attempts;	/* How many times activated ?	    */
	int		retryindex;	/* when, what ?			    */
	char		*channel, *host; /* documenting */
	struct web	*wchan;		/* Web of CHANNELs		    */
	struct web	*whost;		/* Web of HOSTs			    */
	struct thread	*nexttr;	/* Next one in threads queue	    */
	struct thread	*prevtr;	/* previous one ...		    */
	struct thread	*nextthg;	/* Next one in thread GROUP	    */
	struct thread	*prevthg;	/* previous one..		    */
	struct threadgroup *thgrp;	/* our group-leader		    */
	struct vertex	*vertices;	/* First one of the vertices	    */
	struct vertex	*lastvertex;	/* Last one of the vertices	    */
	struct procinfo	*proc;		/* NULL or ptr to xport proc	    */
	int		jobs;		/* How many items in this thread    */
};


struct web {
	char		*name;		/* name of the L_? thingy	    */
	int		linkcnt;
	int		kids;	/* how many transport agents running for me */
	struct vertex	*link;		/* points at group of addresses     */
	struct vertex	*lastlink;	/* for efficiency at link_in()	    */
};

struct procinfo {
	pid_t	pid;		/* Process-id				*/
	int	reaped;
	int	tofd;		/* tell transporter job data thru this	*/
	int	hungry;		/* State of reports from child		*/
	time_t	hungertime;	/* .. when presented			*/
	int	fed;
	int	overfed;	/* Now many jobs fed to it over the normal 1?*/
	time_t	feedtime;	/* .. when fed				*/
	struct web *ch;		/* Web of CHANNELs			*/
	struct web *ho;		/* Web of HOSTs				*/
	struct thread *thread;	/* The thread we are processing		*/
	struct vertex *vertex;	/* vertex within that thread		*/
	struct procinfo *next;	/* next one in IDLE queue		*/
	struct threadgroup *thg; /* The thread-ring we are in		*/
	char	*carryover;	/* Long responces..			*/
	int	cmdlen;		/* buffer content size			*/
	char	cmdbuf[1000];	/* outgoing pipe leftovers..		*/
	char	cmdline[1000];	/* Approximation of the execl() params	*/
};

/* Stores the offset indices of all addresses that have same channel and host*/

struct vertex {		
	struct ctlfile	*cfp;		/* control file containing this group*/
	int		qid;		/* mailq report id - filled at qprint*/
	struct web	*orig[SIZE_L];	/* original names (channel,host,etc) */
	struct vertex	*next[SIZE_L];	/* next group with same L_?	     */
	struct vertex	*prev[SIZE_L];	/* previous group with same L_?      */
	struct thread	*thread;	/* the thread we are in		     */
	struct threadgroup *thgrp;	/* the group we are in		     */
	struct vertex	*nextitem;	/* next in list of scheduled vertices*/
	struct vertex	*previtem;	/* prev in list of scheduled vertices*/
	char		*message;	/* some text associated with node    */
	long		headeroffset;	/* Message headers for this rcpt     */
	long		drptoffset;	/* IETF-NOTARY DRPT  data	     */
	char		*notary;	/* IETF Notary report data	     */
	int		notaryflg;	/* IETF DSN notary control flags     */
#define NOT_NEVER   001
#define NOT_DELAY   002
#define NOT_SUCCESS 004
#define NOT_FAILURE 010
	int		ce_pending;	/* pending on what ?		     */
	time_t		ce_expiry;	/* when this vertex expires ?        */
	struct procinfo *proc;		/* current TA process description    */
	int		attempts;	/* count of number of TA invocations */
	int		retryindex;	/* cur index into ce->retries array  */
	time_t		wakeup;		/* time to wake up and run this      */
	char		*sender;	/* Message Sender/error recipient    */
	int		ngroup;		/* number of addresses in group      */
	int		index[1];	/* index of cfp->offset for group    */
};
