#! /bin/sh


echo "<HTML><HEAD><TITLE>"
echo "$1"
echo "</TITLE></HEAD><BODY BGCOLOR=white><PRE>"
groff -t -man -Tascii "$1" | \
    perl -e '
        #select STDIN; $| = 1;
        select STDERR; $| = 1;
        #select STDOUT; $| = 1;
        $h = "\010";
        $c0 = undef;
        while(read(STDIN,$c,1) > 0) {

#printf STDERR "c0 = \"%s\"  c = \"%s\"\n",
#	      !defined $c0 ? "<UNDEF>" : (ord($c0) < 32 ?
# 				          sprintf("\\%03o",ord($c0)): $c0),
#	      ord($c) < 32 ? sprintf("\\%03o",ord($c)) : $c;

          if (defined $c0) {
            if ($c eq $h) {
              # X ^H * -> bold/italic/something
              read(STDIN,$c1,1);

#printf STDERR " .. c1 = \"%s\"\n",
#	      ord($c1) < 32 ? sprintf("\\%03o",ord($c1)) : $c1;

              if ($c0 eq $c1) {
                # bold
	        if    ($c0 eq "&") { $c0 = "&amp;"; }
		elsif ($c0 eq "<") { $c0 = "&lt;";  }
		elsif ($c0 eq ">") { $c0 = "&gt;";  }
                printf STDOUT "<B>%s</B>",$c0;
              } elsif ($c0 eq "_") {
                # italic
	        if    ($c1 eq "&") { $c1 = "&amp;"; }
		elsif ($c1 eq "<") { $c1 = "&lt;";  }
		elsif ($c1 eq ">") { $c1 = "&gt;";  }
                printf STDOUT "<I>%s</I>",$c1;
              } elsif ($c0.$c1 eq "+o") {
                # Bullet
                printf STDOUT "<B>&#8226;</B>";
              } else {
                # something -- overstrike ?
	        if    ($c1 eq "&") { $c1 = "&amp;"; }
		elsif ($c1 eq "<") { $c1 = "&lt;";  }
		elsif ($c1 eq ">") { $c1 = "&gt;";  }
                printf STDOUT "<B>%s</B>",$c1;
              }
              $c0 = undef;
              if ($c1 eq "\n") { printf STDOUT "\n"; }
            } else {
              # Not  X ^H *, but X is defined.
              if    ($c0 eq "&") { $c0 = "&amp;"; }
	      elsif ($c0 eq "<") { $c0 = "&lt;";  }
	      elsif ($c0 eq ">") { $c0 = "&gt;";  }
              printf STDOUT "%s",$c0;
              $c0 = $c;
            }
          } else {
            # $c0 not defined!
            $c0 = $c;
          }
        } # ... while()
        if ($c0) { printf STDOUT "%s",$c0; }' |  \
    perl -ne '
        s{</B>(.*)<B>}{\1}og;
        s{</I>(.*)<I>}{\1}og;
        s{</U>(.*)<U>}{\1}og;
        s{</I><B>_</B><I>}{_}og;
	print;'
echo "</PRE></BODY></HTML>"
