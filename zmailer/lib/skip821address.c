/*
 *  skip821address()
 *
 *  Routine to scan over a string representing an RFC-821 address
 *  with embedded white-space in it
 *
 *  Copyright Matti Aarnio 1996
 *
 */

char *skip821address(s)
     char *s;
{
  char quote = 0;
  char c;
  while ((c = *s)) {
    if (c == '\\') {
      ++s;
      if (*s == 0)
	break;
    }
    if (c == quote) /* 'c' is non-zero here */
      quote = 0;
    else if (c == '"')
      quote = '"';
    else if (!quote && (c == ' ' || c == '\t'))
      break;
    ++s;
  }
  return s;
}