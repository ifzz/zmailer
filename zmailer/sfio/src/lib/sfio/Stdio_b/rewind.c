#include	"sfstdio.h"

/*	Reposition IO pointer to origin
**	Written by Kiem-Phong Vo
*/


#if __STD_C
void rewind(reg FILE* fp)
#else
void rewind(fp)
reg FILE*	fp;
#endif
{
	reg Sfio_t*	sp;

	if(!(sp = _sfstream(fp)))
		return;
	_stdclrerr(fp,sp);

#if _xopen_stdio
	(void)sfseek(sp, (Sfoff_t)0, 0|SF_SHARE);
#else
	(void)sfseek(sp, (Sfoff_t)0, 0);
#endif
}
