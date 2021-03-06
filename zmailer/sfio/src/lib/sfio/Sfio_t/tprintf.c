#include	"sftest.h"

static char	Buf[128];

typedef struct _coord_
{	int	x;
	int	y;
} Coord_t;

Coord_t	Coord;

#if __STD_C
coordprint(Sfio_t* f, Void_t* v, Sffmt_t* fe)
#else
coordprint(f, v, fe)
Sfio_t*		f;
Void_t*		v;
Sffmt_t*	fe;
#endif
{
	char		type[128];
	Coord_t*	cp;

	if(fe->fmt != 'c')
		return -1;

	cp = va_arg(fe->args,Coord_t*);
	memcpy(type,fe->t_str,fe->n_str); type[fe->n_str] = 0;
	*((char**)v) = sfprints(type,cp->x,cp->y);
	fe->fmt = 's';
	fe->size = sfslen();
	fe->flags |= SFFMT_VALUE;
	return 0;
}

int	OXcount;
#if __STD_C
OXprint(Sfio_t* f, Void_t* v, Sffmt_t* fe)
#else
OXprint(f, v, fe)
Sfio_t*		f;
Void_t*		v;
Sffmt_t*	fe;
#endif
{
	OXcount += 1;

	switch(fe->fmt)
	{
	case 'd' :
		*((int*)v) = 10;
		fe->flags |= SFFMT_VALUE;
		return 0;

	case 'O' :
		fe->fmt = 'o';
		*((int*)v) = 11;
		fe->flags |= SFFMT_VALUE;
		return 0;

	case 'X' :
		fe->fmt = 'x';
		*((int*)v) = 12;
		fe->flags |= SFFMT_VALUE;
		return 0;
	}

	return 0;
}

#if __STD_C
abprint(Sfio_t* f, Void_t* v, Sffmt_t* fe)
#else
abprint(f, v, fe)
Sfio_t*		f;
Void_t*		v;
Sffmt_t*	fe;
#endif
{
	switch(fe->fmt)
	{
	case 'a' :
		fe->fmt = 'u';
		return 0;
	case 'b' :
		fe->fmt = 'd';
		return 0;
	case 'z' : /* test return value of extension function */
		fe->size = 10;
		fe->fmt = 's';
		return 0;
	case 'Z' : /* terminate format processing */
	default :
		return -1;
	}
}

#if __STD_C
intarg(Sfio_t* f, Void_t* val, Sffmt_t* fe)
#else
intarg(f, val, fe)
Sfio_t*		f;
Void_t*		val;
Sffmt_t*	fe;
#endif
{	static int	i = 1;
	*((int*)val) = i++;
	fe->flags |= SFFMT_VALUE;
	return 0;
}

#if __STD_C
shortarg(Sfio_t* f, Void_t* val, Sffmt_t* fe)
#else
shortarg(f, val, fe)
Sfio_t*		f;
Void_t*		val;
Sffmt_t*	fe;
#endif
{	static short	i = -2;

	*((short*)val) = i++;
	fe->size = sizeof(short);
	fe->flags |= SFFMT_VALUE;

	return 0;
}

#if __STD_C
transarg(Sfio_t* f, Void_t* val, Sffmt_t* fe)
#else
transarg(f, val, fe)
Sfio_t*		f;
Void_t*		val;
Sffmt_t*	fe;
#endif
{
	switch(fe->fmt)
	{ case 'D' :
		fe->fmt = 'd'; return 0;
	  case 'O' :
		fe->fmt = 'o'; return 0;
	  case 'F' :
		fe->fmt = 'f'; return 0;
	  case 'S' :
		fe->fmt = 's'; return 0;
	  case 'C' :
		fe->fmt = 'c'; return 0;
	  default : return -1;
	}
}


#if __STD_C
void stkprint(char* buf, int n, char* form, ...)
#else
void stkprint(buf,n,form,va_alist)
char*	buf;
int	n;
char*	form;
va_dcl
#endif
{	va_list	args;
	Sffmt_t	fe;
#if __STD_C
	va_start(args,form);
#else
	va_start(args);
#endif
	fe.form = form;
	va_copy(fe.args,args);
	fe.extf = NIL(Sffmtext_f);
	fe.eventf = NIL(Sffmtevent_f);
	sfsprintf(buf,n,"%! %d %d",&fe,3,4);
	va_end(args);
}

main()
{
	char	buf1[1024], buf2[1024], *list[4], *s;
	double	x=0.0051;
	int	i, j;
	long	k;
	Sffmt_t	fe;
	Sfio_t*	f;

	f = sfopen(NIL(Sfio_t*), Kpv[0], "w+");
	unlink(Kpv[0]);
	sfsetbuf(f,buf1,10);
	sfprintf(f,"%40s\n","0123456789");
	sfsprintf(buf2,sizeof(buf2),"%40s","0123456789");
	sfseek(f,(Sfoff_t)0,0);
	if(!(s = sfgetr(f,'\n',1)) )
		terror("Failed getting string\n");
	if(strcmp(s,buf2) != 0)
		terror("Failed formatting %%s\n");

	sfsprintf(buf1,sizeof(buf1),"%4d %4o %4x", 10, 11, 11);
	sfsprintf(buf2,sizeof(buf2),"%2$4d %1$4o %1$4x", 11, 10);
	if(strcmp(buf1,buf2) != 0)
		terror("Failed testing $position\n");

	sfsprintf(buf1,sizeof(buf1),"%d %1$d %.*d %1$d", 10, 5, 300);
	sfsprintf(buf2,sizeof(buf2),"%d %1$d %3$.*2$d %1$d", 10, 5, 300);
	if(strcmp(buf1,buf2) != 0)
		terror("Failed testing $position with precision\n");

	fe.version = SFIO_VERSION;
	fe.form = NIL(char*);
	fe.extf = OXprint;
	fe.eventf = NIL(Sffmtevent_f);

	sfsprintf(buf1,sizeof(buf1),"%4d %4o %4x %4o %4x", 10, 11, 12, 11, 10);
	sfsprintf(buf2,sizeof(buf2),"%!%2$4d %3$4O %4$4X %3$4O %2$4x", &fe);
	if(strcmp(buf1,buf2) != 0)
		terror("Failed testing $position2\n");
	if(OXcount != 3)
		terror("Failed OXprint called wrong number of times %d\n",OXcount);

	sfsprintf(buf1,sizeof(buf1),"%6.2f",x);
	if(strcmp(buf1,"  0.01") != 0)
		terror("%%f rounding wrong\n");

	fe.version = SFIO_VERSION;
	fe.form = NIL(char*);
	fe.extf = abprint;
	fe.eventf = NIL(Sffmtevent_f);
	sfsprintf(buf1,sizeof(buf1),"%%sX%%d%..4u %..4d9876543210",-1,-1);
	sfsprintf(buf2,sizeof(buf2),"%!%%sX%%d%..4a %..4b%z%Zxxx",
			&fe, -1, -1, "9876543210yyyy" );
	if(strcmp(buf1,buf2) != 0)
		terror("%%!: Extension function failed1\n");

	fe.form = NIL(char*);
	fe.extf = intarg;
	sfsprintf(buf1,sizeof(buf1),"%d %d%",1,2);
	sfsprintf(buf2,sizeof(buf2),"%!%d %d%",&fe);
	if(strcmp(buf1,buf2) != 0)
		terror("%%!: Extension function failed2\n");

	fe.form = NIL(char*);
	sfsprintf(buf1,sizeof(buf1),"%d %d%%",3,4);
	sfsprintf(buf2,sizeof(buf2),"%!%d %d%%",&fe);
	if(strcmp(buf1,buf2) != 0)
		terror("%%!: Extension function failed3\n");

	fe.form = NIL(char*);
	fe.extf = shortarg;
	sfsprintf(buf1,sizeof(buf1),"%hu %ho %hi %hu %ho %hd", -2, -1, 0, 1, 2, 3);
	sfsprintf(buf2,sizeof(buf2),"%!%u %o %i %u %o %d",&fe);
	if(strcmp(buf1,buf2) != 0)
		terror("%%!: Extension function failed4\n");

	/* test extf translation */
	fe.form = NIL(char*);
	fe.extf = transarg;
	sfsprintf(buf1,sizeof(buf1),"%d %o %f %s %c", -1, -1, -1., "s", 'c');
	sfsprintf(buf2,sizeof(buf2),"%!%D %O %F %S %C",&fe, -1, -1, -1., "s", 'c');
	if(strcmp(buf1,buf2) != 0)
		terror("%%!: Extension function failed5\n");

	k = 1234567890;
	sfsprintf(buf1,sizeof(buf1),"%I*d",sizeof(k),k);
	if(strcmp(buf1,"1234567890") != 0)
		terror("%%I*d failed\n");

	Coord.x = 5;
	Coord.y = 7;
	sfsprintf(buf1,sizeof(buf1),"%d %d",Coord.x,Coord.y);

	fe.form = NIL(char*);
	fe.extf = coordprint;
	sfsprintf(buf2,sizeof(buf2),"%!%(%d %d)c",&fe,&Coord);
	if(strcmp(buf1,buf2) != 0)
		terror("%%()c failed\n");

	sfsprintf(buf1,sizeof(buf1),"%d %d %d %d",1,2,3,4);
	stkprint(buf2,sizeof(buf2),"%d %d",1,2);
	if(strcmp(buf1,buf2) != 0)
		terror("%%!: Stack function failed\n");

	sfsprintf(buf1,sizeof(buf1),"% +G",-1.2345);
	sfsprintf(buf2,sizeof(buf2),"-1.2345");
	if(strcmp(buf1,buf2) != 0)
		terror("Failed %% +G test\n");

	if(sizeof(int) == 4 &&  sizeof(short) == 2)
	{	char* s = sfprints("%hx",0xffffffff);
		if(!s || strcmp(s,"ffff") != 0)
			terror("Failed %%hx test\n");
	}

	sfsprintf(buf1,sizeof(buf1),"%#..16d",-0xabc);
	if(strcmp(buf1,"-16#abc") != 0)
		terror("Failed %%..16d test\n");

	sfsprintf(buf1,sizeof(buf1),"%#..16lu",(long)0xc2c01576);
	if(strcmp(buf1,"16#c2c01576") != 0)
		terror("Failed %%..16u test\n");

	sfsprintf(buf1,sizeof(buf1),"%0#4o",077);
	if(strcmp(buf1,"0077") != 0)
		terror("Failed %%0#4o test\n");

	sfsprintf(buf1,sizeof(buf1),"%0#4x",0xc);
	if(strcmp(buf1,"0x000c") != 0)
		terror("Failed %%0#4x test\n");

	sfsprintf(buf1,sizeof(buf1),"%c%c%c",'a','b','c');
	if(strcmp(buf1,"abc") != 0)
		terror("Failed %%c test\n");

	sfsprintf(buf1,sizeof(buf1),"%.4c",'a');
	if(strcmp(buf1,"aaaa") != 0)
		terror("Failed %%.4c test\n");

	sfsprintf(buf1,sizeof(buf1),"%hd%c%hd%ld",(short)1,'1',(short)1,1L);
	if(strcmp(buf1,"1111") != 0)
		terror("Failed %%hd test\n");

	sfsprintf(buf1,sizeof(buf1),"%10.5E",(double)0.0000625);
	if(strcmp(buf1,"6.25000E-05") != 0)
		terror("Failed %%E test\n");

	sfsprintf(buf1,sizeof(buf1),"%10.5f",(double)0.0000625);
	if(strcmp(buf1,"   0.00006") != 0)
		terror("Failed %%f test\n");

	sfsprintf(buf1,sizeof(buf1),"%10.5G",(double)0.0000625);
	if(strcmp(buf1,"  6.25E-05") != 0)
		terror("Failed %%G test\n");

	list[0] = "0";
	list[1] = "1";
	list[2] = "2";
	list[3] = 0;
	sfsprintf(buf1,sizeof(buf1),"%..*s", ',', list);
	if(strcmp(buf1,"0,1,2") != 0)
		terror("Failed %%..*s test\n");

	sfsprintf(buf1,sizeof(buf1),"%.2.*c", ',', "012");
	if(strcmp(buf1,"00,11,22") != 0)
		terror("Failed %%..*c test\n");

	sfsprintf(buf1,sizeof(buf1),"A%.0dB", 0);
	if(strcmp(buf1,"AB") != 0)
		terror("Failed precision+0 test\n");

	sfsprintf(buf1,sizeof(buf1),"A%.0dB", 1);
	if(strcmp(buf1,"A1B") != 0)
		terror("Failed precision+1 test\n");

	sfsprintf(buf1,sizeof(buf1),"A%4.0dB", 12345);
	if(strcmp(buf1,"A12345B") != 0)
		terror("Failed exceeding width test\n");

	sfsprintf(buf1,sizeof(buf1),"A%3dB",33);
	if(strcmp(buf1,"A 33B") != 0)
		terror("Failed justification test\n");

	sfsprintf(buf1,sizeof(buf1),"A%04dB",-1);
	if(strcmp(buf1,"A-001B") != 0)
		terror("Failed zero filling test\n");

	sfsprintf(buf1,sizeof(buf1),"A%+04dB",1);
	if(strcmp(buf1,"A+001B") != 0)
		terror("Failed signed and zero-filled test\n");

	sfsprintf(buf1,sizeof(buf1),"A% .0dB", -1);
	if(strcmp(buf1,"A-1B") != 0)
		terror("Failed blank and precision test\n");

	sfsprintf(buf1,sizeof(buf1),"A%+ .0dB", 1);
	if(strcmp(buf1,"A+1B") != 0)
		terror("Failed +,blank and precision test\n");

	sfsprintf(buf1,sizeof(buf1),"A%010.2fB", 1.2);
	if(strcmp(buf1,"A0000001.20B") != 0)
		terror("Failed floating point and zero-filled test\n");

	sfsprintf(buf1,sizeof(buf1),"A%-010.2fB", 1.2);
	if(strcmp(buf1,"A1.20      B") != 0)
		terror("Failed floating point and left justification test\n");

	sfsprintf(buf1,sizeof(buf1),"A%-#XB", 0xab);
	if(strcmp(buf1,"A0XABB") != 0)
		terror("Failed %%-#X conversion test\n");

#if !_ast_intmax_long
	{ Sflong_t	ll;
	  char		buf[128];
	  char*		s = sfprints("%#..16llu",~((Sflong_t)0));
	  sfsscanf(s,"%lli", &ll);
	  if(ll != (~((Sflong_t)0)) )
		terror("Failed inverting printf/scanf Sflong_t\n");

	  s = sfprints("%#..18lld",~((Sflong_t)0));
	  sfsscanf(s,"%lli", &ll);
	  if(ll != (~((Sflong_t)0)) )
		terror("Failed inverting printf/scanf Sflong_t\n");

	  s = sfprints("%#..lli",~((Sflong_t)0));
	  sfsscanf(s,"%lli", &ll);
	  if(ll != (~((Sflong_t)0)) )
		terror("Failed inverting printf/scanf Sflong_t\n");

	  sfsprintf(buf,sizeof(buf), "%llu", (~((Sflong_t)0)/2) );
	  s = sfprints("%Iu", (~((Sflong_t)0)/2) );
	  if(strcmp(buf,s) != 0)
		terror("Failed conversion with I flag\n");
	}
#endif

	i = (int)(~(~((uint)0) >> 1));
	s = sfprints("%d",i);
	j = atoi(s);
	if(i != j)
		terror("Failed converting highbit\n");
	
	for(i = -10000; i < 10000; i += 123)
	{	s = sfprints("%d",i);
		j = atoi(s);
		if(j != i)
			terror("Failed integer conversion\n");
	}

	/* test I flag for strings */
	{ char	*ls[3];
	  ls[0] = "0123456789";
	  ls[1] = "abcdefghij";
	  ls[2] = 0;
	  s = sfprints("%I5..*s", 0, ls);
	  if(strcmp(s, "01234abcde") != 0)
		terror("Failed I flag with %%s\n");
	}

	/* test justification */
	sfsprintf(buf1,sizeof(buf1),"%-10.5s","abcdefghij");
	sfsprintf(buf2,sizeof(buf2),"%*.5s",-10,"abcdefghij");
	if(strcmp(buf1,buf2) != 0)
		terror("Left-justification is wrong\n");

	sfsprintf(buf1,sizeof(buf1),"%10.5s","abcdefghij");
	sfsprintf(buf2,sizeof(buf2),"%*.5s",10,"abcdefghij");
	if(strcmp(buf1,buf2) != 0)
		terror("Justification is wrong\n");

	return 0;
}
