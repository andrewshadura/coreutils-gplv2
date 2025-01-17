/* od -- dump files in octal and other formats
   Copyright (C) 92, 1995-2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Written by Jim Meyering.  */

#include <config.h>

#include <stdio.h>
#include <assert.h>
#include <getopt.h>
#include <sys/types.h>
#include "system.h"
#include "error.h"
#include "quote.h"
#include "xstrtol.h"

/* The official name of this program (e.g., no `g' prefix).  */
#define PROGRAM_NAME "od"

#define AUTHORS "Jim Meyering"

#include <float.h>

#ifdef HAVE_LONG_DOUBLE
typedef long double LONG_DOUBLE;
#else
typedef double LONG_DOUBLE;
#endif

/* The default number of input bytes per output line.  */
#define DEFAULT_BYTES_PER_BLOCK 16

/* The number of decimal digits of precision in a float.  */
#ifndef FLT_DIG
# define FLT_DIG 7
#endif

/* The number of decimal digits of precision in a double.  */
#ifndef DBL_DIG
# define DBL_DIG 15
#endif

/* The number of decimal digits of precision in a long double.  */
#ifndef LDBL_DIG
# define LDBL_DIG DBL_DIG
#endif

#if HAVE_UNSIGNED_LONG_LONG_INT
typedef unsigned long long int unsigned_long_long_int;
#else
/* This is just a place-holder to avoid a few `#if' directives.
   In this case, the type isn't actually used.  */
typedef unsigned long int unsigned_long_long_int;
#endif

enum size_spec
  {
    NO_SIZE,
    CHAR,
    SHORT,
    INT,
    LONG,
    LONG_LONG,
    /* FIXME: add INTMAX support, too */
    FLOAT_SINGLE,
    FLOAT_DOUBLE,
    FLOAT_LONG_DOUBLE,
    N_SIZE_SPECS
  };

enum output_format
  {
    SIGNED_DECIMAL,
    UNSIGNED_DECIMAL,
    OCTAL,
    HEXADECIMAL,
    FLOATING_POINT,
    NAMED_CHARACTER,
    CHARACTER
  };

/* The maximum number of bytes needed for a format string,
   including the trailing null.  */
enum
  {
    FMT_BYTES_ALLOCATED =
      MAX ((sizeof " %0" - 1 + INT_STRLEN_BOUND (int)
	    + MAX (sizeof "ld",
		   MAX (sizeof PRIdMAX,
			MAX (sizeof PRIoMAX,
			     MAX (sizeof PRIuMAX,
				  sizeof PRIxMAX))))),
	   sizeof " %.Le" + 2 * INT_STRLEN_BOUND (int))
  };

/* Each output format specification (from `-t spec' or from
   old-style options) is represented by one of these structures.  */
struct tspec
  {
    enum output_format fmt;
    enum size_spec size;
    void (*print_function) (size_t, void const *, char const *);
    char fmt_string[FMT_BYTES_ALLOCATED];
    bool hexl_mode_trailer;
    int field_width;
  };

/* The name this program was run with.  */
char *program_name;

/* Convert the number of 8-bit bytes of a binary representation to
   the number of characters (digits + sign if the type is signed)
   required to represent the same quantity in the specified base/type.
   For example, a 32-bit (4-byte) quantity may require a field width
   as wide as the following for these types:
   11	unsigned octal
   11	signed decimal
   10	unsigned decimal
   8	unsigned hexadecimal  */

static unsigned int const bytes_to_oct_digits[] =
{0, 3, 6, 8, 11, 14, 16, 19, 22, 25, 27, 30, 32, 35, 38, 41, 43};

static unsigned int const bytes_to_signed_dec_digits[] =
{1, 4, 6, 8, 11, 13, 16, 18, 20, 23, 25, 28, 30, 33, 35, 37, 40};

static unsigned int const bytes_to_unsigned_dec_digits[] =
{0, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25, 27, 29, 32, 34, 37, 39};

static unsigned int const bytes_to_hex_digits[] =
{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32};

#define MAX_INTEGRAL_TYPE_SIZE sizeof (unsigned_long_long_int)

/* It'll be a while before we see integral types wider than 16 bytes,
   but if/when it happens, this check will catch it.  Without this check,
   a wider type would provoke a buffer overrun.  */
verify (MAX_INTEGRAL_TYPE_SIZE
	< sizeof bytes_to_hex_digits / sizeof *bytes_to_hex_digits);

/* Make sure the other arrays have the same length.  */
verify (sizeof bytes_to_oct_digits == sizeof bytes_to_signed_dec_digits);
verify (sizeof bytes_to_oct_digits == sizeof bytes_to_unsigned_dec_digits);
verify (sizeof bytes_to_oct_digits == sizeof bytes_to_hex_digits);

/* Convert enum size_spec to the size of the named type.  */
static const int width_bytes[] =
{
  -1,
  sizeof (char),
  sizeof (short int),
  sizeof (int),
  sizeof (long int),
  sizeof (unsigned_long_long_int),
  sizeof (float),
  sizeof (double),
  sizeof (LONG_DOUBLE)
};

/* Ensure that for each member of `enum size_spec' there is an
   initializer in the width_bytes array.  */
verify (sizeof width_bytes / sizeof width_bytes[0] == N_SIZE_SPECS);

/* Names for some non-printing characters.  */
static const char *const charname[33] =
{
  "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel",
  "bs", "ht", "nl", "vt", "ff", "cr", "so", "si",
  "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb",
  "can", "em", "sub", "esc", "fs", "gs", "rs", "us",
  "sp"
};

/* Address base (8, 10 or 16).  */
static int address_base;

/* The number of octal digits required to represent the largest
   address value.  */
#define MAX_ADDRESS_LENGTH \
  ((sizeof (uintmax_t) * CHAR_BIT + CHAR_BIT - 1) / 3)

/* Width of a normal address.  */
static int address_pad_len;

static size_t string_min;
static bool flag_dump_strings;

/* True if we should recognize the older non-option arguments
   that specified at most one file and optional arguments specifying
   offset and pseudo-start address.  */
static bool traditional;

/* True if an old-style `pseudo-address' was specified.  */
static bool flag_pseudo_start;

/* The difference between the old-style pseudo starting address and
   the number of bytes to skip.  */
static uintmax_t pseudo_offset;

/* Function that accepts an address and an optional following char,
   and prints the address and char to stdout.  */
static void (*format_address) (uintmax_t, char);

/* The number of input bytes to skip before formatting and writing.  */
static uintmax_t n_bytes_to_skip = 0;

/* When false, MAX_BYTES_TO_FORMAT and END_OFFSET are ignored, and all
   input is formatted.  */
static bool limit_bytes_to_format = false;

/* The maximum number of bytes that will be formatted.  */
static uintmax_t max_bytes_to_format;

/* The offset of the first byte after the last byte to be formatted.  */
static uintmax_t end_offset;

/* When true and two or more consecutive blocks are equal, format
   only the first block and output an asterisk alone on the following
   line to indicate that identical blocks have been elided.  */
static bool abbreviate_duplicate_blocks = true;

/* An array of specs describing how to format each input block.  */
static struct tspec *spec;

/* The number of format specs.  */
static size_t n_specs;

/* The allocated length of SPEC.  */
static size_t n_specs_allocated;

/* The number of input bytes formatted per output line.  It must be
   a multiple of the least common multiple of the sizes associated with
   the specified output types.  It should be as large as possible, but
   no larger than 16 -- unless specified with the -w option.  */
static size_t bytes_per_block;

/* Human-readable representation of *file_list (for error messages).
   It differs from file_list[-1] only when file_list[-1] is "-".  */
static char const *input_filename;

/* A NULL-terminated list of the file-arguments from the command line.  */
static char const *const *file_list;

/* Initializer for file_list if no file-arguments
   were specified on the command line.  */
static char const *const default_file_list[] = {"-", NULL};

/* The input stream associated with the current file.  */
static FILE *in_stream;

/* If true, at least one of the files we read was standard input.  */
static bool have_read_stdin;

/* Map the size in bytes to a type identifier.  */
static enum size_spec integral_type_size[MAX_INTEGRAL_TYPE_SIZE + 1];

#define MAX_FP_TYPE_SIZE sizeof (LONG_DOUBLE)
static enum size_spec fp_type_size[MAX_FP_TYPE_SIZE + 1];

static char const short_options[] = "A:aBbcDdeFfHhIij:LlN:OoS:st:vw::Xx";

/* For long options that have no equivalent short option, use a
   non-character as a pseudo short option, starting with CHAR_MAX + 1.  */
enum
{
  TRADITIONAL_OPTION = CHAR_MAX + 1
};

static struct option const long_options[] =
{
  {"skip-bytes", required_argument, NULL, 'j'},
  {"address-radix", required_argument, NULL, 'A'},
  {"read-bytes", required_argument, NULL, 'N'},
  {"format", required_argument, NULL, 't'},
  {"output-duplicates", no_argument, NULL, 'v'},
  {"strings", optional_argument, NULL, 'S'},
  {"traditional", no_argument, NULL, TRADITIONAL_OPTION},
  {"width", optional_argument, NULL, 'w'},

  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    fprintf (stderr, _("Try `%s --help' for more information.\n"),
	     program_name);
  else
    {
      printf (_("\
Usage: %s [OPTION]... [FILE]...\n\
  or:  %s [-abcdfilosx]... [FILE] [[+]OFFSET[.][b]]\n\
  or:  %s --traditional [OPTION]... [FILE] [[+]OFFSET[.][b] [+][LABEL][.][b]]\n\
"),
	      program_name, program_name, program_name);
      fputs (_("\n\
Write an unambiguous representation, octal bytes by default,\n\
of FILE to standard output.  With more than one FILE argument,\n\
concatenate them in the listed order to form the input.\n\
With no FILE, or when FILE is -, read standard input.\n\
\n\
"), stdout);
      fputs (_("\
All arguments to long options are mandatory for short options.\n\
"), stdout);
      fputs (_("\
  -A, --address-radix=RADIX   decide how file offsets are printed\n\
  -j, --skip-bytes=BYTES      skip BYTES input bytes first\n\
"), stdout);
      fputs (_("\
  -N, --read-bytes=BYTES      limit dump to BYTES input bytes\n\
  -S, --strings[=BYTES]       output strings of at least BYTES graphic chars\n\
  -t, --format=TYPE           select output format or formats\n\
  -v, --output-duplicates     do not use * to mark line suppression\n\
  -w, --width[=BYTES]         output BYTES bytes per output line\n\
      --traditional           accept arguments in traditional form\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      fputs (_("\
\n\
Traditional format specifications may be intermixed; they accumulate:\n\
  -a   same as -t a,  select named characters, ignoring high-order bit\n\
  -b   same as -t o1, select octal bytes\n\
  -c   same as -t c,  select ASCII characters or backslash escapes\n\
  -d   same as -t u2, select unsigned decimal 2-byte units\n\
"), stdout);
      fputs (_("\
  -f   same as -t fF, select floats\n\
  -i   same as -t dI, select decimal ints\n\
  -l   same as -t dL, select decimal longs\n\
  -o   same as -t o2, select octal 2-byte units\n\
  -s   same as -t d2, select decimal 2-byte units\n\
  -x   same as -t x2, select hexadecimal 2-byte units\n\
"), stdout);
      fputs (_("\
\n\
If first and second call formats both apply, the second format is assumed\n\
if the last operand begins with + or (if there are 2 operands) a digit.\n\
An OFFSET operand means -j OFFSET.  LABEL is the pseudo-address\n\
at first byte printed, incremented when dump is progressing.\n\
For OFFSET and LABEL, a 0x or 0X prefix indicates hexadecimal;\n\
suffixes may be . for octal and b for multiply by 512.\n\
"), stdout);
      fputs (_("\
\n\
TYPE is made up of one or more of these specifications:\n\
\n\
  a          named character, ignoring high-order bit\n\
  c          ASCII character or backslash escape\n\
"), stdout);
      fputs (_("\
  d[SIZE]    signed decimal, SIZE bytes per integer\n\
  f[SIZE]    floating point, SIZE bytes per integer\n\
  o[SIZE]    octal, SIZE bytes per integer\n\
  u[SIZE]    unsigned decimal, SIZE bytes per integer\n\
  x[SIZE]    hexadecimal, SIZE bytes per integer\n\
"), stdout);
      fputs (_("\
\n\
SIZE is a number.  For TYPE in doux, SIZE may also be C for\n\
sizeof(char), S for sizeof(short), I for sizeof(int) or L for\n\
sizeof(long).  If TYPE is f, SIZE may also be F for sizeof(float), D\n\
for sizeof(double) or L for sizeof(long double).\n\
"), stdout);
      fputs (_("\
\n\
RADIX is d for decimal, o for octal, x for hexadecimal or n for none.\n\
BYTES is hexadecimal with 0x or 0X prefix, and may have a multiplier suffix:\n\
b 512, kB 1000, K 1024, MB 1000*1000, M 1024*1024,\n\
GB 1000*1000*1000, G 1024*1024*1024, and so on for T, P, E, Z, Y.\n\
Adding a z suffix to any type displays printable characters at the end of each\n\
output line.  \
"), stdout);
      fputs (_("\
--string without a number implies 3.  --width without a number\n\
implies 32.  By default, od uses -A o -t d2 -w16.\n\
"), stdout);
      emit_bug_reporting_address ();
    }
  exit (status);
}

/* Define the print functions.  */

static void
print_s_char (size_t n_bytes, void const *block, char const *fmt_string)
{
  signed char const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_char (size_t n_bytes, void const *block, char const *fmt_string)
{
  unsigned char const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_s_short (size_t n_bytes, void const *block, char const *fmt_string)
{
  short int const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_short (size_t n_bytes, void const *block, char const *fmt_string)
{
  unsigned short int const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_int (size_t n_bytes, void const *block, char const *fmt_string)
{
  unsigned int const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_long (size_t n_bytes, void const *block, char const *fmt_string)
{
  unsigned long int const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_long_long (size_t n_bytes, void const *block, char const *fmt_string)
{
  unsigned_long_long_int const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_float (size_t n_bytes, void const *block, char const *fmt_string)
{
  float const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

static void
print_double (size_t n_bytes, void const *block, char const *fmt_string)
{
  double const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}

#ifdef HAVE_LONG_DOUBLE
static void
print_long_double (size_t n_bytes, void const *block, char const *fmt_string)
{
  long double const *p = block;
  size_t i;
  for (i = n_bytes / sizeof *p; i != 0; i--)
    printf (fmt_string, *p++);
}
#endif

static void
dump_hexl_mode_trailer (size_t n_bytes, const char *block)
{
  size_t i;
  fputs ("  >", stdout);
  for (i = n_bytes; i > 0; i--)
    {
      unsigned char c = *block++;
      unsigned char c2 = (isprint (c) ? c : '.');
      putchar (c2);
    }
  putchar ('<');
}

static void
print_named_ascii (size_t n_bytes, void const *block,
		   const char *unused_fmt_string ATTRIBUTE_UNUSED)
{
  unsigned char const *p = block;
  size_t i;
  for (i = n_bytes; i > 0; i--)
    {
      int masked_c = *p++ & 0x7f;
      const char *s;
      char buf[5];

      if (masked_c == 127)
	s = "del";
      else if (masked_c <= 040)
	s = charname[masked_c];
      else
	{
	  sprintf (buf, "  %c", masked_c);
	  s = buf;
	}

      printf (" %3s", s);
    }
}

static void
print_ascii (size_t n_bytes, void const *block,
	     const char *unused_fmt_string ATTRIBUTE_UNUSED)
{
  unsigned char const *p = block;
  size_t i;
  for (i = n_bytes; i > 0; i--)
    {
      unsigned char c = *p++;
      const char *s;
      char buf[5];

      switch (c)
	{
	case '\0':
	  s = " \\0";
	  break;

	case '\a':
	  s = " \\a";
	  break;

	case '\b':
	  s = " \\b";
	  break;

	case '\f':
	  s = " \\f";
	  break;

	case '\n':
	  s = " \\n";
	  break;

	case '\r':
	  s = " \\r";
	  break;

	case '\t':
	  s = " \\t";
	  break;

	case '\v':
	  s = " \\v";
	  break;

	default:
	  sprintf (buf, (isprint (c) ? "  %c" : "%03o"), c);
	  s = buf;
	}

      printf (" %3s", s);
    }
}

/* Convert a null-terminated (possibly zero-length) string S to an
   unsigned long integer value.  If S points to a non-digit set *P to S,
   *VAL to 0, and return true.  Otherwise, accumulate the integer value of
   the string of digits.  If the string of digits represents a value
   larger than ULONG_MAX, don't modify *VAL or *P and return false.
   Otherwise, advance *P to the first non-digit after S, set *VAL to
   the result of the conversion and return true.  */

static bool
simple_strtoul (const char *s, const char **p, unsigned long int *val)
{
  unsigned long int sum;

  sum = 0;
  while (ISDIGIT (*s))
    {
      int c = *s++ - '0';
      if (sum > (ULONG_MAX - c) / 10)
	return false;
      sum = sum * 10 + c;
    }
  *p = s;
  *val = sum;
  return true;
}

/* If S points to a single valid modern od format string, put
   a description of that format in *TSPEC, make *NEXT point at the
   character following the just-decoded format (if *NEXT is non-NULL),
   and return true.  If S is not valid, don't modify *NEXT or *TSPEC,
   give a diagnostic, and return false.  For example, if S were
   "d4afL" *NEXT would be set to "afL" and *TSPEC would be
     {
       fmt = SIGNED_DECIMAL;
       size = INT or LONG; (whichever integral_type_size[4] resolves to)
       print_function = print_int; (assuming size == INT)
       fmt_string = "%011d%c";
      }
   S_ORIG is solely for reporting errors.  It should be the full format
   string argument.
   */

static bool
decode_one_format (const char *s_orig, const char *s, const char **next,
		   struct tspec *tspec)
{
  enum size_spec size_spec;
  unsigned long int size;
  enum output_format fmt;
  const char *pre_fmt_string;
  void (*print_function) (size_t, void const *, char const *);
  const char *p;
  char c;
  int field_width;
  int precision;

  assert (tspec != NULL);

  switch (*s)
    {
    case 'd':
    case 'o':
    case 'u':
    case 'x':
      c = *s;
      ++s;
      switch (*s)
	{
	case 'C':
	  ++s;
	  size = sizeof (char);
	  break;

	case 'S':
	  ++s;
	  size = sizeof (short int);
	  break;

	case 'I':
	  ++s;
	  size = sizeof (int);
	  break;

	case 'L':
	  ++s;
	  size = sizeof (long int);
	  break;

	default:
	  if (! simple_strtoul (s, &p, &size))
	    {
	      /* The integer at P in S would overflow an unsigned long int.
		 A digit string that long is sufficiently odd looking
		 that the following diagnostic is sufficient.  */
	      error (0, 0, _("invalid type string %s"), quote (s_orig));
	      return false;
	    }
	  if (p == s)
	    size = sizeof (int);
	  else
	    {
	      if (MAX_INTEGRAL_TYPE_SIZE < size
		  || integral_type_size[size] == NO_SIZE)
		{
		  error (0, 0, _("invalid type string %s;\n\
this system doesn't provide a %lu-byte integral type"), quote (s_orig), size);
		  return false;
		}
	      s = p;
	    }
	  break;
	}

#define ISPEC_TO_FORMAT(Spec, Min_format, Long_format, Max_format)	\
  ((Spec) == LONG_LONG ? (Max_format)					\
   : ((Spec) == LONG ? (Long_format)					\
      : (Min_format)))							\

      size_spec = integral_type_size[size];

      switch (c)
	{
	case 'd':
	  fmt = SIGNED_DECIMAL;
	  sprintf (tspec->fmt_string, " %%%d%s",
		   (field_width = bytes_to_signed_dec_digits[size]),
		   ISPEC_TO_FORMAT (size_spec, "d", "ld", PRIdMAX));
	  break;

	case 'o':
	  fmt = OCTAL;
	  sprintf (tspec->fmt_string, " %%0%d%s",
		   (field_width = bytes_to_oct_digits[size]),
		   ISPEC_TO_FORMAT (size_spec, "o", "lo", PRIoMAX));
	  break;

	case 'u':
	  fmt = UNSIGNED_DECIMAL;
	  sprintf (tspec->fmt_string, " %%%d%s",
		   (field_width = bytes_to_unsigned_dec_digits[size]),
		   ISPEC_TO_FORMAT (size_spec, "u", "lu", PRIuMAX));
	  break;

	case 'x':
	  fmt = HEXADECIMAL;
	  sprintf (tspec->fmt_string, " %%0%d%s",
		   (field_width = bytes_to_hex_digits[size]),
		   ISPEC_TO_FORMAT (size_spec, "x", "lx", PRIxMAX));
	  break;

	default:
	  abort ();
	}

      assert (strlen (tspec->fmt_string) < FMT_BYTES_ALLOCATED);

      switch (size_spec)
	{
	case CHAR:
	  print_function = (fmt == SIGNED_DECIMAL
			    ? print_s_char
			    : print_char);
	  break;

	case SHORT:
	  print_function = (fmt == SIGNED_DECIMAL
			    ? print_s_short
			    : print_short);
	  break;

	case INT:
	  print_function = print_int;
	  break;

	case LONG:
	  print_function = print_long;
	  break;

	case LONG_LONG:
	  print_function = print_long_long;
	  break;

	default:
	  abort ();
	}
      break;

    case 'f':
      fmt = FLOATING_POINT;
      ++s;
      switch (*s)
	{
	case 'F':
	  ++s;
	  size = sizeof (float);
	  break;

	case 'D':
	  ++s;
	  size = sizeof (double);
	  break;

	case 'L':
	  ++s;
	  size = sizeof (LONG_DOUBLE);
	  break;

	default:
	  if (! simple_strtoul (s, &p, &size))
	    {
	      /* The integer at P in S would overflow an unsigned long int.
		 A digit string that long is sufficiently odd looking
		 that the following diagnostic is sufficient.  */
	      error (0, 0, _("invalid type string %s"), quote (s_orig));
	      return false;
	    }
	  if (p == s)
	    size = sizeof (double);
	  else
	    {
	      if (size > MAX_FP_TYPE_SIZE
		  || fp_type_size[size] == NO_SIZE)
		{
		  error (0, 0, _("invalid type string %s;\n\
this system doesn't provide a %lu-byte floating point type"),
			 quote (s_orig), size);
		  return false;
		}
	      s = p;
	    }
	  break;
	}
      size_spec = fp_type_size[size];

      switch (size_spec)
	{
	case FLOAT_SINGLE:
	  print_function = print_float;
	  /* Don't use %#e; not all systems support it.  */
	  pre_fmt_string = " %%%d.%de";
	  precision = FLT_DIG;
	  break;

	case FLOAT_DOUBLE:
	  print_function = print_double;
	  pre_fmt_string = " %%%d.%de";
	  precision = DBL_DIG;
	  break;

#ifdef HAVE_LONG_DOUBLE
	case FLOAT_LONG_DOUBLE:
	  print_function = print_long_double;
	  pre_fmt_string = " %%%d.%dLe";
	  precision = LDBL_DIG;
	  break;
#endif

	default:
	  abort ();
	}

      field_width = precision + 8;
      sprintf (tspec->fmt_string, pre_fmt_string, field_width, precision);
      break;

    case 'a':
      ++s;
      fmt = NAMED_CHARACTER;
      size_spec = CHAR;
      print_function = print_named_ascii;
      field_width = 3;
      break;

    case 'c':
      ++s;
      fmt = CHARACTER;
      size_spec = CHAR;
      print_function = print_ascii;
      field_width = 3;
      break;

    default:
      error (0, 0, _("invalid character `%c' in type string %s"),
	     *s, quote (s_orig));
      return false;
    }

  tspec->size = size_spec;
  tspec->fmt = fmt;
  tspec->print_function = print_function;

  tspec->field_width = field_width;
  tspec->hexl_mode_trailer = (*s == 'z');
  if (tspec->hexl_mode_trailer)
    s++;

  if (next != NULL)
    *next = s;

  return true;
}

/* Given a list of one or more input filenames FILE_LIST, set the global
   file pointer IN_STREAM and the global string INPUT_FILENAME to the
   first one that can be successfully opened. Modify FILE_LIST to
   reference the next filename in the list.  A file name of "-" is
   interpreted as standard input.  If any file open fails, give an error
   message and return false.  */

static bool
open_next_file (void)
{
  bool ok = true;

  do
    {
      input_filename = *file_list;
      if (input_filename == NULL)
	return ok;
      ++file_list;

      if (STREQ (input_filename, "-"))
	{
	  input_filename = _("standard input");
	  in_stream = stdin;
	  have_read_stdin = true;
	  if (O_BINARY && ! isatty (STDIN_FILENO))
	    freopen (NULL, "rb", stdin);
	}
      else
	{
	  in_stream = fopen (input_filename, (O_BINARY ? "rb" : "r"));
	  if (in_stream == NULL)
	    {
	      error (0, errno, "%s", input_filename);
	      ok = false;
	    }
	}
    }
  while (in_stream == NULL);

  if (limit_bytes_to_format & !flag_dump_strings)
    setvbuf (in_stream, NULL, _IONBF, 0);

  return ok;
}

/* Test whether there have been errors on in_stream, and close it if
   it is not standard input.  Return false if there has been an error
   on in_stream or stdout; return true otherwise.  This function will
   report more than one error only if both a read and a write error
   have occurred.  IN_ERRNO, if nonzero, is the error number
   corresponding to the most recent action for IN_STREAM.  */

static bool
check_and_close (int in_errno)
{
  bool ok = true;

  if (in_stream != NULL)
    {
      if (ferror (in_stream))
	{
	  error (0, in_errno, _("%s: read error"), input_filename);
	  if (! STREQ (file_list[-1], "-"))
	    fclose (in_stream);
	  ok = false;
	}
      else if (! STREQ (file_list[-1], "-") && fclose (in_stream) != 0)
	{
	  error (0, errno, "%s", input_filename);
	  ok = false;
	}

      in_stream = NULL;
    }

  if (ferror (stdout))
    {
      error (0, 0, _("write error"));
      ok = false;
    }

  return ok;
}

/* Decode the modern od format string S.  Append the decoded
   representation to the global array SPEC, reallocating SPEC if
   necessary.  Return true if S is valid.  */

static bool
decode_format_string (const char *s)
{
  const char *s_orig = s;
  assert (s != NULL);

  while (*s != '\0')
    {
      const char *next;

      if (n_specs_allocated <= n_specs)
	spec = X2NREALLOC (spec, &n_specs_allocated);

      if (! decode_one_format (s_orig, s, &next, &spec[n_specs]))
	return false;

      assert (s != next);
      s = next;
      ++n_specs;
    }

  return true;
}

/* Given a list of one or more input filenames FILE_LIST, set the global
   file pointer IN_STREAM to position N_SKIP in the concatenation of
   those files.  If any file operation fails or if there are fewer than
   N_SKIP bytes in the combined input, give an error message and return
   false.  When possible, use seek rather than read operations to
   advance IN_STREAM.  */

static bool
skip (uintmax_t n_skip)
{
  bool ok = true;
  int in_errno = 0;

  if (n_skip == 0)
    return true;

  while (in_stream != NULL)	/* EOF.  */
    {
      struct stat file_stats;

      /* First try seeking.  For large offsets, this extra work is
	 worthwhile.  If the offset is below some threshold it may be
	 more efficient to move the pointer by reading.  There are two
	 issues when trying to seek:
	   - the file must be seekable.
	   - before seeking to the specified position, make sure
	     that the new position is in the current file.
	     Try to do that by getting file's size using fstat.
	     But that will work only for regular files.  */

      if (fstat (fileno (in_stream), &file_stats) == 0)
	{
	  /* The st_size field is valid only for regular files
	     (and for symbolic links, which cannot occur here).
	     If the number of bytes left to skip is at least
	     as large as the size of the current file, we can
	     decrement n_skip and go on to the next file.  */

	  if (S_ISREG (file_stats.st_mode) && 0 <= file_stats.st_size)
	    {
	      if ((uintmax_t) file_stats.st_size <= n_skip)
		n_skip -= file_stats.st_size;
	      else
		{
		  if (fseeko (in_stream, n_skip, SEEK_CUR) != 0)
		    {
		      in_errno = errno;
		      ok = false;
		    }
		  n_skip = 0;
		}
	    }

	  /* If it's not a regular file with nonnegative size,
	     position the file pointer by reading.  */

	  else
	    {
	      char buf[BUFSIZ];
	      size_t n_bytes_read, n_bytes_to_read = BUFSIZ;

	      while (0 < n_skip)
		{
		  if (n_skip < n_bytes_to_read)
		    n_bytes_to_read = n_skip;
		  n_bytes_read = fread (buf, 1, n_bytes_to_read, in_stream);
		  n_skip -= n_bytes_read;
		  if (n_bytes_read != n_bytes_to_read)
		    {
		      in_errno = errno;
		      ok = false;
		      n_skip = 0;
		      break;
		    }
		}
	    }

	  if (n_skip == 0)
	    break;
	}

      else   /* cannot fstat() file */
	{
	  error (0, errno, "%s", input_filename);
	  ok = false;
	}

      ok &= check_and_close (in_errno);

      ok &= open_next_file ();
    }

  if (n_skip != 0)
    error (EXIT_FAILURE, 0, _("cannot skip past end of combined input"));

  return ok;
}

static void
format_address_none (uintmax_t address ATTRIBUTE_UNUSED, char c ATTRIBUTE_UNUSED)
{
}

static void
format_address_std (uintmax_t address, char c)
{
  char buf[MAX_ADDRESS_LENGTH + 2];
  char *p = buf + sizeof buf;
  char const *pbound;

  *--p = '\0';
  *--p = c;
  pbound = p - address_pad_len;

  /* Use a special case of the code for each base.  This is measurably
     faster than generic code.  */
  switch (address_base)
    {
    case 8:
      do
	*--p = '0' + (address & 7);
      while ((address >>= 3) != 0);
      break;

    case 10:
      do
	*--p = '0' + (address % 10);
      while ((address /= 10) != 0);
      break;

    case 16:
      do
	*--p = "0123456789abcdef"[address & 15];
      while ((address >>= 4) != 0);
      break;
    }

  while (pbound < p)
    *--p = '0';

  fputs (p, stdout);
}

static void
format_address_paren (uintmax_t address, char c)
{
  putchar ('(');
  format_address_std (address, ')');
  if (c)
    putchar (c);
}

static void
format_address_label (uintmax_t address, char c)
{
  format_address_std (address, ' ');
  format_address_paren (address + pseudo_offset, c);
}

/* Write N_BYTES bytes from CURR_BLOCK to standard output once for each
   of the N_SPEC format specs.  CURRENT_OFFSET is the byte address of
   CURR_BLOCK in the concatenation of input files, and it is printed
   (optionally) only before the output line associated with the first
   format spec.  When duplicate blocks are being abbreviated, the output
   for a sequence of identical input blocks is the output for the first
   block followed by an asterisk alone on a line.  It is valid to compare
   the blocks PREV_BLOCK and CURR_BLOCK only when N_BYTES == BYTES_PER_BLOCK.
   That condition may be false only for the last input block -- and then
   only when it has not been padded to length BYTES_PER_BLOCK.  */

static void
write_block (uintmax_t current_offset, size_t n_bytes,
	     const char *prev_block, const char *curr_block)
{
  static bool first = true;
  static bool prev_pair_equal = false;

#define EQUAL_BLOCKS(b1, b2) (memcmp (b1, b2, bytes_per_block) == 0)

  if (abbreviate_duplicate_blocks
      && !first && n_bytes == bytes_per_block
      && EQUAL_BLOCKS (prev_block, curr_block))
    {
      if (prev_pair_equal)
	{
	  /* The two preceding blocks were equal, and the current
	     block is the same as the last one, so print nothing.  */
	}
      else
	{
	  printf ("*\n");
	  prev_pair_equal = true;
	}
    }
  else
    {
      size_t i;

      prev_pair_equal = false;
      for (i = 0; i < n_specs; i++)
	{
	  if (i == 0)
	    format_address (current_offset, '\0');
	  else
	    printf ("%*s", address_pad_len, "");
	  (*spec[i].print_function) (n_bytes, curr_block, spec[i].fmt_string);
	  if (spec[i].hexl_mode_trailer)
	    {
	      /* space-pad out to full line width, then dump the trailer */
	      int datum_width = width_bytes[spec[i].size];
	      int blank_fields = (bytes_per_block - n_bytes) / datum_width;
	      int field_width = spec[i].field_width + 1;
	      printf ("%*s", blank_fields * field_width, "");
	      dump_hexl_mode_trailer (n_bytes, curr_block);
	    }
	  putchar ('\n');
	}
    }
  first = false;
}

/* Read a single byte into *C from the concatenation of the input files
   named in the global array FILE_LIST.  On the first call to this
   function, the global variable IN_STREAM is expected to be an open
   stream associated with the input file INPUT_FILENAME.  If IN_STREAM
   is at end-of-file, close it and update the global variables IN_STREAM
   and INPUT_FILENAME so they correspond to the next file in the list.
   Then try to read a byte from the newly opened file.  Repeat if
   necessary until EOF is reached for the last file in FILE_LIST, then
   set *C to EOF and return.  Subsequent calls do likewise.  Return
   true if successful.  */

static bool
read_char (int *c)
{
  bool ok = true;

  *c = EOF;

  while (in_stream != NULL)	/* EOF.  */
    {
      *c = fgetc (in_stream);

      if (*c != EOF)
	break;

      ok &= check_and_close (errno);

      ok &= open_next_file ();
    }

  return ok;
}

/* Read N bytes into BLOCK from the concatenation of the input files
   named in the global array FILE_LIST.  On the first call to this
   function, the global variable IN_STREAM is expected to be an open
   stream associated with the input file INPUT_FILENAME.  If all N
   bytes cannot be read from IN_STREAM, close IN_STREAM and update
   the global variables IN_STREAM and INPUT_FILENAME.  Then try to
   read the remaining bytes from the newly opened file.  Repeat if
   necessary until EOF is reached for the last file in FILE_LIST.
   On subsequent calls, don't modify BLOCK and return true.  Set
   *N_BYTES_IN_BUFFER to the number of bytes read.  If an error occurs,
   it will be detected through ferror when the stream is about to be
   closed.  If there is an error, give a message but continue reading
   as usual and return false.  Otherwise return true.  */

static bool
read_block (size_t n, char *block, size_t *n_bytes_in_buffer)
{
  bool ok = true;

  assert (0 < n && n <= bytes_per_block);

  *n_bytes_in_buffer = 0;

  if (n == 0)
    return true;

  while (in_stream != NULL)	/* EOF.  */
    {
      size_t n_needed;
      size_t n_read;

      n_needed = n - *n_bytes_in_buffer;
      n_read = fread (block + *n_bytes_in_buffer, 1, n_needed, in_stream);

      *n_bytes_in_buffer += n_read;

      if (n_read == n_needed)
	break;

      ok &= check_and_close (errno);

      ok &= open_next_file ();
    }

  return ok;
}

/* Return the least common multiple of the sizes associated
   with the format specs.  */

static int
get_lcm (void)
{
  size_t i;
  int l_c_m = 1;

  for (i = 0; i < n_specs; i++)
    l_c_m = lcm (l_c_m, width_bytes[spec[i].size]);
  return l_c_m;
}

/* If S is a valid traditional offset specification with an optional
   leading '+' return true and set *OFFSET to the offset it denotes.  */

static bool
parse_old_offset (const char *s, uintmax_t *offset)
{
  int radix;

  if (*s == '\0')
    return false;

  /* Skip over any leading '+'. */
  if (s[0] == '+')
    ++s;

  /* Determine the radix we'll use to interpret S.  If there is a `.',
     it's decimal, otherwise, if the string begins with `0X'or `0x',
     it's hexadecimal, else octal.  */
  if (strchr (s, '.') != NULL)
    radix = 10;
  else
    {
      if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
	radix = 16;
      else
	radix = 8;
    }

  return xstrtoumax (s, NULL, radix, offset, "Bb") == LONGINT_OK;
}

/* Read a chunk of size BYTES_PER_BLOCK from the input files, write the
   formatted block to standard output, and repeat until the specified
   maximum number of bytes has been read or until all input has been
   processed.  If the last block read is smaller than BYTES_PER_BLOCK
   and its size is not a multiple of the size associated with a format
   spec, extend the input block with zero bytes until its length is a
   multiple of all format spec sizes.  Write the final block.  Finally,
   write on a line by itself the offset of the byte after the last byte
   read.  Accumulate return values from calls to read_block and
   check_and_close, and if any was false, return false.
   Otherwise, return true.  */

static bool
dump (void)
{
  char *block[2];
  uintmax_t current_offset;
  bool idx = false;
  bool ok = true;
  size_t n_bytes_read;

  block[0] = xnmalloc (2, bytes_per_block);
  block[1] = block[0] + bytes_per_block;

  current_offset = n_bytes_to_skip;

  if (limit_bytes_to_format)
    {
      while (1)
	{
	  size_t n_needed;
	  if (current_offset >= end_offset)
	    {
	      n_bytes_read = 0;
	      break;
	    }
	  n_needed = MIN (end_offset - current_offset,
			  (uintmax_t) bytes_per_block);
	  ok &= read_block (n_needed, block[idx], &n_bytes_read);
	  if (n_bytes_read < bytes_per_block)
	    break;
	  assert (n_bytes_read == bytes_per_block);
	  write_block (current_offset, n_bytes_read,
		       block[!idx], block[idx]);
	  current_offset += n_bytes_read;
	  idx = !idx;
	}
    }
  else
    {
      while (1)
	{
	  ok &= read_block (bytes_per_block, block[idx], &n_bytes_read);
	  if (n_bytes_read < bytes_per_block)
	    break;
	  assert (n_bytes_read == bytes_per_block);
	  write_block (current_offset, n_bytes_read,
		       block[!idx], block[idx]);
	  current_offset += n_bytes_read;
	  idx = !idx;
	}
    }

  if (n_bytes_read > 0)
    {
      int l_c_m;
      size_t bytes_to_write;

      l_c_m = get_lcm ();

      /* Make bytes_to_write the smallest multiple of l_c_m that
	 is at least as large as n_bytes_read.  */
      bytes_to_write = l_c_m * ((n_bytes_read + l_c_m - 1) / l_c_m);

      memset (block[idx] + n_bytes_read, 0, bytes_to_write - n_bytes_read);
      write_block (current_offset, bytes_to_write,
		   block[!idx], block[idx]);
      current_offset += n_bytes_read;
    }

  format_address (current_offset, '\n');

  if (limit_bytes_to_format && current_offset >= end_offset)
    ok &= check_and_close (0);

  free (block[0]);

  return ok;
}

/* STRINGS mode.  Find each "string constant" in the input.
   A string constant is a run of at least `string_min' ASCII
   graphic (or formatting) characters terminated by a null.
   Based on a function written by Richard Stallman for a
   traditional version of od.  Return true if successful.  */

static bool
dump_strings (void)
{
  size_t bufsize = MAX (100, string_min);
  char *buf = xmalloc (bufsize);
  uintmax_t address = n_bytes_to_skip;
  bool ok = true;

  while (1)
    {
      size_t i;
      int c;

      /* See if the next `string_min' chars are all printing chars.  */
    tryline:

      if (limit_bytes_to_format
	  && (end_offset < string_min || end_offset - string_min <= address))
	break;

      for (i = 0; i < string_min; i++)
	{
	  ok &= read_char (&c);
	  address++;
	  if (c < 0)
	    {
	      free (buf);
	      return ok;
	    }
	  if (! isprint (c))
	    /* Found a non-printing.  Try again starting with next char.  */
	    goto tryline;
	  buf[i] = c;
	}

      /* We found a run of `string_min' printable characters.
	 Now see if it is terminated with a null byte.  */
      while (!limit_bytes_to_format || address < end_offset)
	{
	  if (i == bufsize)
	    {
	      buf = X2REALLOC (buf, &bufsize);
	    }
	  ok &= read_char (&c);
	  address++;
	  if (c < 0)
	    {
	      free (buf);
	      return ok;
	    }
	  if (c == '\0')
	    break;		/* It is; print this string.  */
	  if (! isprint (c))
	    goto tryline;	/* It isn't; give up on this string.  */
	  buf[i++] = c;		/* String continues; store it all.  */
	}

      /* If we get here, the string is all printable and null-terminated,
	 so print it.  It is all in `buf' and `i' is its length.  */
      buf[i] = 0;
      format_address (address - i - 1, ' ');

      for (i = 0; (c = buf[i]); i++)
	{
	  switch (c)
	    {
	    case '\a':
	      fputs ("\\a", stdout);
	      break;

	    case '\b':
	      fputs ("\\b", stdout);
	      break;

	    case '\f':
	      fputs ("\\f", stdout);
	      break;

	    case '\n':
	      fputs ("\\n", stdout);
	      break;

	    case '\r':
	      fputs ("\\r", stdout);
	      break;

	    case '\t':
	      fputs ("\\t", stdout);
	      break;

	    case '\v':
	      fputs ("\\v", stdout);
	      break;

	    default:
	      putc (c, stdout);
	    }
	}
      putchar ('\n');
    }

  /* We reach this point only if we search through
     (max_bytes_to_format - string_min) bytes before reaching EOF.  */

  free (buf);

  ok &= check_and_close (0);
  return ok;
}

int
main (int argc, char **argv)
{
  int c;
  int n_files;
  size_t i;
  int l_c_m;
  size_t desired_width IF_LINT (= 0);
  bool modern = false;
  bool width_specified = false;
  bool ok = true;
  static char const multipliers[] = "bEGKkMmPTYZ0";

  /* The old-style `pseudo starting address' to be printed in parentheses
     after any true address.  */
  uintmax_t pseudo_start IF_LINT (= 0);

  initialize_main (&argc, &argv);
  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdout);

  for (i = 0; i <= MAX_INTEGRAL_TYPE_SIZE; i++)
    integral_type_size[i] = NO_SIZE;

  integral_type_size[sizeof (char)] = CHAR;
  integral_type_size[sizeof (short int)] = SHORT;
  integral_type_size[sizeof (int)] = INT;
  integral_type_size[sizeof (long int)] = LONG;
#if HAVE_UNSIGNED_LONG_LONG_INT
  /* If `long int' and `long long int' have the same size, it's fine
     to overwrite the entry for `long' with this one.  */
  integral_type_size[sizeof (unsigned_long_long_int)] = LONG_LONG;
#endif

  for (i = 0; i <= MAX_FP_TYPE_SIZE; i++)
    fp_type_size[i] = NO_SIZE;

  fp_type_size[sizeof (float)] = FLOAT_SINGLE;
  /* The array entry for `double' is filled in after that for LONG_DOUBLE
     so that if `long double' is the same type or if long double isn't
     supported FLOAT_LONG_DOUBLE will never be used.  */
  fp_type_size[sizeof (LONG_DOUBLE)] = FLOAT_LONG_DOUBLE;
  fp_type_size[sizeof (double)] = FLOAT_DOUBLE;

  n_specs = 0;
  n_specs_allocated = 0;
  spec = NULL;

  format_address = format_address_std;
  address_base = 8;
  address_pad_len = 7;
  flag_dump_strings = false;

  while ((c = getopt_long (argc, argv, short_options, long_options, NULL))
	 != -1)
    {
      uintmax_t tmp;
      enum strtol_error s_err;

      switch (c)
	{
	case 'A':
	  modern = true;
	  switch (optarg[0])
	    {
	    case 'd':
	      format_address = format_address_std;
	      address_base = 10;
	      address_pad_len = 7;
	      break;
	    case 'o':
	      format_address = format_address_std;
	      address_base = 8;
	      address_pad_len = 7;
	      break;
	    case 'x':
	      format_address = format_address_std;
	      address_base = 16;
	      address_pad_len = 6;
	      break;
	    case 'n':
	      format_address = format_address_none;
	      address_pad_len = 0;
	      break;
	    default:
	      error (EXIT_FAILURE, 0,
		     _("invalid output address radix `%c'; \
it must be one character from [doxn]"),
		     optarg[0]);
	      break;
	    }
	  break;

	case 'j':
	  modern = true;
	  s_err = xstrtoumax (optarg, NULL, 0, &n_bytes_to_skip, multipliers);
	  if (s_err != LONGINT_OK)
	    STRTOL_FATAL_ERROR (optarg, _("skip argument"), s_err);
	  break;

	case 'N':
	  modern = true;
	  limit_bytes_to_format = true;

	  s_err = xstrtoumax (optarg, NULL, 0, &max_bytes_to_format,
			      multipliers);
	  if (s_err != LONGINT_OK)
	    STRTOL_FATAL_ERROR (optarg, _("limit argument"), s_err);
	  break;

	case 'S':
	  modern = true;
	  if (optarg == NULL)
	    string_min = 3;
	  else
	    {
	      s_err = xstrtoumax (optarg, NULL, 0, &tmp, multipliers);
	      if (s_err != LONGINT_OK)
		STRTOL_FATAL_ERROR (optarg, _("minimum string length"), s_err);

	      /* The minimum string length may be no larger than SIZE_MAX,
		 since we may allocate a buffer of this size.  */
	      if (SIZE_MAX < tmp)
		error (EXIT_FAILURE, 0, _("%s is too large"), optarg);

	      string_min = tmp;
	    }
	  flag_dump_strings = true;
	  break;

	case 't':
	  modern = true;
	  ok &= decode_format_string (optarg);
	  break;

	case 'v':
	  modern = true;
	  abbreviate_duplicate_blocks = false;
	  break;

	case TRADITIONAL_OPTION:
	  traditional = true;
	  break;

	  /* The next several cases map the traditional format
	     specification options to the corresponding modern format
	     specs.  GNU od accepts any combination of old- and
	     new-style options.  Format specification options accumulate.
	     The obsolescent and undocumented formats are compatible
	     with FreeBSD 4.10 od.  */

#define CASE_OLD_ARG(old_char,new_string)		\
	case old_char:					\
	  ok &= decode_format_string (new_string);	\
	  break

	  CASE_OLD_ARG ('a', "a");
	  CASE_OLD_ARG ('b', "o1");
	  CASE_OLD_ARG ('c', "c");
	  CASE_OLD_ARG ('D', "u4"); /* obsolescent and undocumented */
	  CASE_OLD_ARG ('d', "u2");
	case 'F': /* obsolescent and undocumented alias */
	  CASE_OLD_ARG ('e', "fD"); /* obsolescent and undocumented */
	  CASE_OLD_ARG ('f', "fF");
	case 'X': /* obsolescent and undocumented alias */
	  CASE_OLD_ARG ('H', "x4"); /* obsolescent and undocumented */
	  CASE_OLD_ARG ('i', "dI");
	case 'I': case 'L': /* obsolescent and undocumented aliases */
	  CASE_OLD_ARG ('l', "dL");
	  CASE_OLD_ARG ('O', "o4"); /* obsolesent and undocumented */
	case 'B': /* obsolescent and undocumented alias */
	  CASE_OLD_ARG ('o', "o2");
	  CASE_OLD_ARG ('s', "d2");
	case 'h': /* obsolescent and undocumented alias */
	  CASE_OLD_ARG ('x', "x2");

#undef CASE_OLD_ARG

	case 'w':
	  modern = true;
	  width_specified = true;
	  if (optarg == NULL)
	    {
	      desired_width = 32;
	    }
	  else
	    {
	      uintmax_t w_tmp;
	      s_err = xstrtoumax (optarg, NULL, 10, &w_tmp, "");
	      if (s_err != LONGINT_OK)
		STRTOL_FATAL_ERROR (optarg, _("width specification"), s_err);
	      if (SIZE_MAX < w_tmp)
		error (EXIT_FAILURE, 0, _("%s is too large"), optarg);
	      desired_width = w_tmp;
	    }
	  break;

	case_GETOPT_HELP_CHAR;

	case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

	default:
	  usage (EXIT_FAILURE);
	  break;
	}
    }

  if (!ok)
    exit (EXIT_FAILURE);

  if (flag_dump_strings && n_specs > 0)
    error (EXIT_FAILURE, 0,
	   _("no type may be specified when dumping strings"));

  n_files = argc - optind;

  /* If the --traditional option is used, there may be from
     0 to 3 remaining command line arguments;  handle each case
     separately.
	od [file] [[+]offset[.][b] [[+]label[.][b]]]
     The offset and label have the same syntax.

     If --traditional is not given, and if no modern options are
     given, and if the offset begins with + or (if there are two
     operands) a digit, accept only this form, as per POSIX:
	od [file] [[+]offset[.][b]]
  */

  if (!modern | traditional)
    {
      uintmax_t o1;
      uintmax_t o2;

      switch (n_files)
	{
	case 1:
	  if ((traditional || argv[optind][0] == '+')
	      && parse_old_offset (argv[optind], &o1))
	    {
	      n_bytes_to_skip = o1;
	      --n_files;
	      ++argv;
	    }
	  break;

	case 2:
	  if ((traditional || argv[optind + 1][0] == '+'
	       || ISDIGIT (argv[optind + 1][0]))
	      && parse_old_offset (argv[optind + 1], &o2))
	    {
	      if (traditional && parse_old_offset (argv[optind], &o1))
		{
		  n_bytes_to_skip = o1;
		  flag_pseudo_start = true;
		  pseudo_start = o2;
		  argv += 2;
		  n_files -= 2;
		}
	      else
		{
		  n_bytes_to_skip = o2;
		  --n_files;
		  argv[optind + 1] = argv[optind];
		  ++argv;
		}
	    }
	  break;

	case 3:
	  if (traditional
	      && parse_old_offset (argv[optind + 1], &o1)
	      && parse_old_offset (argv[optind + 2], &o2))
	    {
	      n_bytes_to_skip = o1;
	      flag_pseudo_start = true;
	      pseudo_start = o2;
	      argv[optind + 2] = argv[optind];
	      argv += 2;
	      n_files -= 2;
	    }
	  break;
	}

      if (traditional && 1 < n_files)
	{
	  error (0, 0, _("extra operand %s"), quote (argv[optind + 1]));
	  error (0, 0, "%s\n",
		 _("Compatibility mode supports at most one file."));
	  usage (EXIT_FAILURE);
	}
    }

  if (flag_pseudo_start)
    {
      if (format_address == format_address_none)
	{
	  address_base = 8;
	  address_pad_len = 7;
	  format_address = format_address_paren;
	}
      else
	format_address = format_address_label;
    }

  if (limit_bytes_to_format)
    {
      end_offset = n_bytes_to_skip + max_bytes_to_format;
      if (end_offset < n_bytes_to_skip)
	error (EXIT_FAILURE, 0, _("skip-bytes + read-bytes is too large"));
    }

  if (n_specs == 0)
    decode_format_string ("oS");

  if (n_files > 0)
    {
      /* Set the global pointer FILE_LIST so that it
	 references the first file-argument on the command-line.  */

      file_list = (char const *const *) &argv[optind];
    }
  else
    {
      /* No files were listed on the command line.
	 Set the global pointer FILE_LIST so that it
	 references the null-terminated list of one name: "-".  */

      file_list = default_file_list;
    }

  /* open the first input file */
  ok = open_next_file ();
  if (in_stream == NULL)
    goto cleanup;

  /* skip over any unwanted header bytes */
  ok &= skip (n_bytes_to_skip);
  if (in_stream == NULL)
    goto cleanup;

  pseudo_offset = (flag_pseudo_start ? pseudo_start - n_bytes_to_skip : 0);

  /* Compute output block length.  */
  l_c_m = get_lcm ();

  if (width_specified)
    {
      if (desired_width != 0 && desired_width % l_c_m == 0)
	bytes_per_block = desired_width;
      else
	{
	  error (0, 0, _("warning: invalid width %lu; using %d instead"),
		 (unsigned long int) desired_width, l_c_m);
	  bytes_per_block = l_c_m;
	}
    }
  else
    {
      if (l_c_m < DEFAULT_BYTES_PER_BLOCK)
	bytes_per_block = l_c_m * (DEFAULT_BYTES_PER_BLOCK / l_c_m);
      else
	bytes_per_block = l_c_m;
    }

#ifdef DEBUG
  for (i = 0; i < n_specs; i++)
    {
      printf (_("%d: fmt=\"%s\" width=%d\n"),
	      i, spec[i].fmt_string, width_bytes[spec[i].size]);
    }
#endif

  ok &= (flag_dump_strings ? dump_strings () : dump ());

cleanup:;

  if (have_read_stdin && fclose (stdin) == EOF)
    error (EXIT_FAILURE, errno, _("standard input"));

  exit (ok ? EXIT_SUCCESS : EXIT_FAILURE);
}
