/*
 *    Copyright 1988 by Rayan S. Zachariassen, all rights reserved.
 *      This will be free software, but only when it is finished.
 */
/*
 *    Several extensive changes by Matti Aarnio <mea@nic.funet.fi>
 *      Copyright 1991-1999.
 */

/*
 * ZMailer SMTP server.
 */

#include "smtpserver.h"

/* as in: SKIPWHILE(isascii,cp) */
#define SKIPWHILE(X,Y)  while (*Y != '\0' && isascii(*Y) && X(*Y)) { ++Y; }

static void cfparam __((char *));
static void cfparam(str)
     char *str;
{
    char *name, *param1, *param2;

    name = strchr(str, '\n');	/* The trailing newline chopper ... */
    if (name)
	*name = 0;

    SKIPWHILE(!isspace, str);
    SKIPWHILE(isspace, str);
    name = str;
    SKIPWHILE(!isspace, str);
    if (*str != 0)
	*str++ = 0;

    if (cistrcmp(name, "help") == 0) {
	int i = 0, helpmax = HELPMAX;
	while (helplines[i] != NULL && i < helpmax)
	    ++i;
	param2 = strchr(str, '\n');
	if (param2) *param2 = 0;
	helplines[i] = strdup(str);
	helplines[i + 1] = NULL;	/* This will always stay within the array... */
	return;
    }
    if (cistrcmp(name, "hdr220") == 0) {
	int i = 0, hdrmax = HDR220MAX;
	while (hdr220lines[i] != NULL && i < hdrmax)
	  ++i;
	param2 = strchr(str, '\n');
	if (param2) *param2 = 0;
	hdr220lines[i] = strdup(str);
	hdr220lines[i+1] = NULL;
	return;
    }

    SKIPWHILE(isspace, str);
    param1 = str;

    SKIPWHILE(!isspace, str);
    if (*str != 0)
	*str++ = 0;
    SKIPWHILE(isspace, str);
    param2 = str;
    SKIPWHILE(!isspace, str);
    if (*str != 0)
	*str++ = 0;

    /* How many parallel clients a servermode smtpserver allows
       running in parallel, and how many parallel sessions can
       be coming from same IP address */

    if (cistrcmp(name, "same-ip-source-parallel-max") == 0) {
	sscanf(param1, "%d", &MaxSameIpSource);
    } else if (cistrcmp(name, "MaxSameIpSource") == 0) {
	sscanf(param1, "%d", &MaxSameIpSource);
    } else if (cistrcmp(name, "MaxParallelConnections") == 0) {
	sscanf(param1, "%d", &MaxParallelConnections);
    } else if (cistrcmp(name, "max-parallel-connections") == 0) {
	sscanf(param1, "%d", &MaxParallelConnections);

    /* TCP related parameters */

    } else if (cistrcmp(name, "ListenQueueSize") == 0) {
	sscanf(param1, "%d", &ListenQueueSize);
    } else if (cistrcmp(name, "tcprcvbuffersize") == 0) {
	sscanf(param1, "%d", &TcpRcvBufferSize);
    } else if (cistrcmp(name, "tcpxmitbuffersize") == 0) {
	sscanf(param1, "%d", &TcpXmitBufferSize);
    }

    /* SMTP Protocol limit & policy tune options */

    else if (cistrcmp(name, "maxsize") == 0) {
	sscanf(param1, "%ld", &maxsize);
    } else if (cistrcmp(name, "RcptLimitCnt") == 0) {
	sscanf(param1, "%d", &rcptlimitcnt);
	if (rcptlimitcnt < 100) rcptlimitcnt = 100;
    } else if (cistrcmp(name, "Rcpt-Limit-Count") == 0) {
	sscanf(param1, "%d", &rcptlimitcnt);
	if (rcptlimitcnt < 100) rcptlimitcnt = 100;
    } else if (cistrcmp(name, "accept-percent-kludge") == 0) {
	percent_accept = 1;
    } else if (cistrcmp(name, "reject-percent-kludge") == 0) {
	percent_accept = -1;
    } else if (cistrcmp(name, "allowsourceroute") == 0) {
      allow_source_route = 1;
    } else if (cistrcmp(name, "max-error-recipients") == 0) {
	sscanf(param1, "%d", &MaxErrorRecipients);
    }

    /* Two parameter policydb option: DBTYPE and DBPATH */

    else if (cistrcmp(name, "policydb") == 0) {
	policydefine(&policydb, param1, param2);
    }

    /* A few facility enablers: (default: off) */

    else if (cistrcmp(name, "debugcmd") == 0) {
      debugcmdok = 1;
    } else if (cistrcmp(name, "expncmd") == 0) {
      expncmdok = 1;
    } else if (cistrcmp(name, "vrfycmd") == 0) {
      vrfycmdok = 1;
    } else if (cistrcmp(name, "enable-router") == 0) {
      enable_router = 1;
    } else if (cistrcmp(name, "smtp-auth") == 0) {
      auth_ok = 1;
    } else if (cistrcmp(name, "auth-login-also-without-tls") == 0) {
      auth_login_without_tls = 1;
    }

    /* Store various things into 'rvcdfrom' header per selectors */

    else if (cistrcmp(name, "rcvd-ident") == 0) {
      log_rcvd_ident = 1;
    } else if (cistrcmp(name, "rcvd-whoson") == 0) {
      log_rcvd_whoson = 1;
    } else if (cistrcmp(name, "rcvd-auth-user") == 0) {
      log_rcvd_authuser = 1;
    } else if (cistrcmp(name, "rcvd-tls-mode") == 0) {
      log_rcvd_tls_mode = 1;
    } else if (cistrcmp(name, "rcvd-tls-ccert") == 0) {
      log_rcvd_tls_ccert = 1;
    }

    /* Some Enhanced-SMTP facility disablers: (default: on ) */

    else if (cistrcmp(name, "nopipelining") == 0) {
      pipeliningok = 0;
    } else if (cistrcmp(name, "noenhancedstatuscodes") == 0) {
      enhancedstatusok = 0;
    } else if (cistrcmp(name, "noenhancedstatus") == 0) {
      enhancedstatusok = 0;
    } else if (cistrcmp(name, "no8bitmime") == 0) {
      mime8bitok = 0;
    } else if (cistrcmp(name, "nochunking") == 0) {
      chunkingok = 0;
    } else if (cistrcmp(name, "nodsn") == 0) {
      dsn_ok = 0;
    } else if (cistrcmp(name, "noehlo") == 0) {
      ehlo_ok = 0;
    } else if (cistrcmp(name, "noetrn") == 0) {
      etrn_ok = 0;
    } else if (cistrcmp(name, "no-multiline-replies") == 0) {
      multilinereplies = 0;
    }

    /* TLSv1/SSLv* options */

    else if (cistrcmp(name, "use-tls") == 0) {
      starttls_ok = 1;		/* Default: OFF */

    } else if (cistrcmp(name, "tls-cert-file") == 0 && param1) {
      if (tls_cert_file) free(tls_cert_file);
      tls_cert_file = strdup(param1);
      if (!tls_key_file)	/* default the other */
	tls_key_file = strdup(param1);

    } else if (cistrcmp(name, "tls-key-file")  == 0 && param1) {
      if (tls_key_file) free(tls_key_file);
      tls_key_file = strdup(param1);
      if (!tls_cert_file)	/* default the other */
	tls_cert_file = strdup(param1);

    } else if (cistrcmp(name, "tls-CAfile")    == 0 && param1) {
      if (tls_CAfile) free(tls_CAfile);
      tls_CAfile = strdup(param1);

    } else if (cistrcmp(name, "tls-CApath")    == 0 && param1) {
      if (tls_CApath) free(tls_CApath);
      tls_CApath = strdup(param1);

    } else if (cistrcmp(name, "tls-loglevel")  == 0 && param1) {
      sscanf(param1,"%d", & tls_loglevel);

    } else if (cistrcmp(name, "tls-enforce-tls")==0 && param1) {
      sscanf(param1,"%d", & tls_enforce_tls);

    } else if (cistrcmp(name, "tls-ccert-vd")  == 0 && param1) {
      sscanf(param1,"%d", & tls_ccert_vd);

    } else if (cistrcmp(name, "tls-ask-cert")  == 0 && param1) {
      sscanf(param1,"%d", & tls_ask_cert);

    } else if (cistrcmp(name, "tls-require-cert")  == 0 && param1) {
      sscanf(param1,"%d", & tls_req_cert);
    }
    /* XX: report error for unrecognized PARAM keyword ?? */
}

struct smtpconf *
readcffile(name)
     const char *name;
{
    FILE *fp;
    struct smtpconf scf, *head, *tail = NULL;
    char c, *cp, buf[1024], *s, *s0;

    if ((fp = fopen(name, "r")) == NULL)
	return NULL;
    head = NULL;
    buf[sizeof(buf) - 1] = 0;
    while (fgets(buf, sizeof buf, fp) != NULL) {
	c = buf[0];
	if (c == '#' || (isascii(c) && isspace(c)))
	    continue;
	if (buf[sizeof(buf) - 1] != 0 &&
	    buf[sizeof(buf) - 1] != '\n') {
	    int cc;
	    while ((cc = getc(fp)) != '\n' &&
		   cc != EOF);	/* Scan until end-of-line */
	}
	buf[sizeof(buf) - 1] = 0;	/* Trunc, just in case.. */

	cp = buf;
	SKIPWHILE(isspace, cp);
	if (strncmp(cp, "PARAM", 5) == 0) {
	    cfparam(cp);
	    continue;
	}
	scf.flags = "";
	scf.next = NULL;
	s0 = cp;
	SKIPWHILE(!isspace, cp);
	c = *cp;
	*cp = '\0';
	s0 = strdup(s0);
	for (s = s0; *s; ++s)
	    if (isascii(*s & 0xFF) && isupper(*s & 0xFF))
		*s = tolower(*s & 0xFF);
	scf.pattern = s0;
	scf.maxloadavg = 999;
	if (c != '\0') {
	    ++cp;
	    SKIPWHILE(isspace, cp);
	    if (*cp && isascii(*cp) && isdigit(*cp)) {
		/* Sanity-check -- 2 is VERY LOW */
		if ((scf.maxloadavg = atoi(cp)) < 2)
		    scf.maxloadavg = 2;
		SKIPWHILE(isdigit, cp);
		SKIPWHILE(isspace, cp);
	    }
	    scf.flags = strdup(cp);
	    if ((cp = strchr(scf.flags, '\n')) != NULL)
		*cp = '\0';
	}
	if (head == NULL) {
	    head = tail = (struct smtpconf *) emalloc(sizeof scf);
	    *head = scf;
	} else {
	    tail->next = (struct smtpconf *) emalloc(sizeof scf);
	    *(tail->next) = scf;
	    tail = tail->next;
	}
    }
    fclose(fp);
    return head;
}

struct smtpconf *
findcf(h)
     const char *h;
{
    struct smtpconf *scfp;
    register char *cp, *s;
    int c;

#ifndef	USE_ALLOCA
    cp = (char*)emalloc(strlen(h) + 1);
#else
    cp = (char*)alloca(strlen(h) + 1);
#endif
    for (s = cp; *h != '\0'; ++h) {
	c = (*h) & 0xFF;
	if (isascii(c) && isalpha(c) && isupper(c))
	    *s++ = tolower(c);
	else
	    *s++ = c;
    }
    *s = '\0';
    for (scfp = cfhead; scfp != NULL; scfp = scfp->next) {
	if (strmatch(scfp->pattern, cp)) {
#ifndef USE_ALLOCA
	    free(cp);
#endif
	    return scfp;
	}
    }
#ifndef USE_ALLOCA
    free(cp);
#endif
    return NULL;
}