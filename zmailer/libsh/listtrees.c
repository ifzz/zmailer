/*
 *	Copyright 1989 by Rayan S. Zachariassen, all rights reserved.
 *	This will be free software, but only when it is finished.
 */

/*
 * List manipulation utility functions.
 */

#include "hostenv.h"
#include "mailer.h"
#include <ctype.h>
/*#include "sh.h"
  #include "io.h"
  #include "shconfig.h"  */

extern conscell *envarlist;

#ifdef	MALLOC_TRACE
#undef	s_copy_tree
#undef	s_free_tree
/* to ensure we use the definitions in libmalloc_d.a */
#define	s_copy_tree	sx_copy_tree
#define	s_free_tree	sx_free_tree
extern void      s_free_tree __((conscell *));
extern conscell *s_copy_tree __((conscell *));
#endif	/* MALLOC_TRACE */

#if 0 /* Not needed for GCed cells */
/*
 * Free a linked structure that may have been allocated by s_copy_tree().
 */

extern void __s_free_tree __((conscell *, const char *, const char *));

void
__s_free_tree(list,filename,linename)
	register conscell *list;
	const char *filename, *linename;
{
	s_free_tree(list);
}

void
s_free_tree(list)
	register conscell *list;
{
	register conscell *rest;

	if (list == NULL)
		return;
	for (; list != NULL; list = rest) {
		rest = cdr(list);
		if (LIST(list))
#ifdef	MALLOC_TRACE
			__s_free_tree(car(list),__FILE__,__LINE__);
#else	/* !MALLOC_TRACE */
			s_free_tree(car(list));
#endif	/* MALLOC_TRACE */
		else if (ISNEW(list) && list->string != NULL)
			free(list->string);
		free((char *)list);
		if (rest == envarlist)
			return;
	}
}
#endif

/*
 * Return a list where all the components (recursively) are allocated
 * in the current context (typically MEM_MALLOC)
 */
extern conscell * __s_copy_tree __((conscell *, const char *, const char *));

conscell *
__s_copy_tree(list,filename,linename)
	register conscell *list;
	const char *filename, *linename;
{
	return s_copy_tree(list);
}

conscell *
_s_copy_tree(list)
	conscell *list;  /* input list is gc-protected */
{
	conscell *new = NULL, *foo = NULL;
	GCVARS3;

	if (list == NULL)
		return NULL;

	GCPRO3(list, new, foo);

	if (STRING(list)) {
		/* malloc new string and conscell to store it in */
		new = copycell(list);
		new->flags = (list->flags & ~CONSTSTRING) | NEWSTRING;
		cdr(new) = NULL;

	} else if ((foo = _s_copy_tree(car(list))) != NULL) {
		/* malloc new conscell to store foo in car of */
		new = copycell(list);
		car(new) = foo;
	}
	if ((foo = _s_copy_tree(cdr(list))) != NULL) {
		/* malloc new conscell to store foo in cdr of */
		if (new == NULL) new = copycell(list);
		cdr(new) = foo;
	}
	if (new == NULL) {
		/* malloc new conscell as a copy of this one */
		new = copycell(list);
	}
	UNGCPRO3;
	return new;
}

extern int D_conscell;

conscell *
s_copy_tree(list)
	conscell *list;  /* input list is gc-protected */
{
	list = _s_copy_tree(list);
#ifdef __GNUC__
	if (D_conscell)
	  fprintf(stderr," s_copy_tree() returns %p to caller at %p\n", list,
		  __builtin_return_address(0));
#else
	if (D_conscell)
	  fprintf(stderr," s_copy_tree() returns %p\n", list);
#endif
	return list;
}
