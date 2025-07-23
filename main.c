/* GNU ed - The GNU line editor.
   Copyright (C) 2006-2025 Antonio Diaz Diaz.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
   Exit status: 0 for a normal exit, 1 for environmental problems
   (invalid command-line options, memory exhausted, command failed, etc),
   2 for problems with the input file (file not found, buffer modified,
   I/O errors), 3 for an internal consistency error (e.g., bug) which caused
   ed to panic.
*/
/*
 * CREDITS
 *
 *      This program is based on the editor algorithm described in
 *      Brian W. Kernighan and P. J. Plauger's book "Software Tools
 *      in Pascal", Addison-Wesley, 1981.
 *
 *      The buffering algorithm is attributed to Rodney Ruddock of
 *      the University of Guelph, Guelph, Ontario.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <locale.h>

#include "carg_parser.h"
#include "ed.h"


static const char * const program_name = "ed";
static const char * const program_year = "2025";
static const char * invocation_name = "ed";		/* default value */

static bool extended_regexp_ = false;	/* use EREs */
static bool quiet = false;		/* suppress diagnostics */
static bool restricted_ = false;	/* run in restricted mode */
static bool safe_names = true;		/* reject chars 1-31 in file names */
static bool scripted_ = false;		/* suppress byte counts and ! prompt */
static bool strip_cr_ = false;		/* strip trailing CRs */
static bool traditional_ = false;	/* be backwards compatible */

/* Access functions for command-line flags. */
bool extended_regexp( void ) { return extended_regexp_; }
bool restricted( void ) { return restricted_; }
bool scripted( void ) { return scripted_; }
bool strip_cr( void ) { return strip_cr_; }
bool traditional( void ) { return traditional_; }


static void show_help( void )
  {
  printf( "GNU ed is a line-oriented text editor. It is used to create, display,\n"
          "modify and otherwise manipulate text files, both interactively and via\n"
          "shell scripts. A restricted version of ed, red, can only edit files in\n"
          "the current directory and cannot execute shell commands. Ed is the\n"
          "'standard' text editor in the sense that it is the original editor for\n"
          "Unix, and thus widely available. For most purposes, however, it is\n"
          "superseded by full-screen editors such as GNU Emacs or GNU Moe.\n"
          "\nUsage: %s [options] [[+line] file]\n", invocation_name );
  printf( "\nThe file name may be preceded by '+line', '+/RE', or '+?RE' to set the\n"
          "current line to the line number specified or to the first or last line\n"
          "matching the regular expression 'RE'.\n"
          "\nThe environment variable LINES can be used to set the initial window size.\n"
          "\nOptions:\n"
          "  -h, --help                 display this help and exit\n"
          "  -V, --version              output version information and exit\n"
          "  -E, --extended-regexp      use extended regular expressions\n"
          "  -G, --traditional          run in compatibility mode\n"
          "  -l, --loose-exit-status    exit with 0 status even if a command fails\n"
          "  -p, --prompt=STRING        use STRING as an interactive prompt\n"
          "  -q, --quiet, --silent      suppress diagnostics written to stderr\n"
          "  -r, --restricted           run in restricted mode\n"
          "  -s, --script               suppress byte counts and '!' prompt\n"
          "  -v, --verbose              be verbose; equivalent to the 'H' command\n"
          "      --strip-trailing-cr    strip carriage returns at end of text lines\n"
          "      --unsafe-names         allow control characters 1-31 in file names\n"
          "\nStart edit by reading in 'file' if given.\n"
          "If 'file' begins with a '!', read output of shell command.\n"
          "\nExit status: 0 for a normal exit, 1 for environmental problems\n"
          "(invalid command-line options, memory exhausted, command failed, etc),\n"
          "2 for problems with the input file (file not found, buffer modified,\n"
          "I/O errors), 3 for an internal consistency error (e.g., bug) which caused\n"
          "ed to panic.\n"
          "\nReport bugs to bug-ed@gnu.org\n"
          "Ed home page: http://www.gnu.org/software/ed/ed.html\n"
          "General help using GNU software: http://www.gnu.org/gethelp\n" );
  }


static void show_version( void )
  {
  printf( "GNU %s %s\n", program_name, PROGVERSION );
  printf( "Copyright (C) 1994 Andrew L. Moore.\n"
          "Copyright (C) %s Antonio Diaz Diaz.\n", program_year );
  printf( "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
          "This is free software: you are free to change and redistribute it.\n"
          "There is NO WARRANTY, to the extent permitted by law.\n" );
  }


void print_filename( const char * const filename, const bool to_stdout )
  {
  FILE * const fp = to_stdout ? stdout : stderr;
  if( safe_names ) { fputs( filename, fp ); return; }
  const char * p;
  for( p = filename; *p; ++p )
    {
    const unsigned char ch = *p;
    if( ch == '\\' ) { putc( ch, fp ); putc( ch, fp ); continue; }
    if( ch >= 32 ) { putc( ch, fp ); continue; }
    putc( '\\', fp );
    putc( ( ( ch >> 6 ) & 7 ) + '0', fp );
    putc( ( ( ch >> 3 ) & 7 ) + '0', fp );
    putc( ( ch & 7 ) + '0', fp );
    }
  }


void show_warning( const char * const filename, const char * const msg )
  {
  if( !quiet )
    {
    if( filename && filename[0] )
      { print_filename( filename, false ); fputs( ": ", stderr ); }
    fprintf( stderr, "%s\n", msg );
    }
  }


void show_strerror( const char * const filename, const int errcode )
  {
  if( !quiet )
    {
    if( filename && filename[0] )
      { print_filename( filename, false ); fputs( ": ", stderr ); }
    fprintf( stderr, "%s\n", strerror( errcode ) );
    }
  }


static void show_error( const char * const msg, const int errcode, const bool help )
  {
  if( msg && msg[0] )
    fprintf( stderr, "%s: %s%s%s\n", program_name, msg,
             ( errcode > 0 ) ? ": " : "",
             ( errcode > 0 ) ? strerror( errcode ) : "" );
  if( help )
    fprintf( stderr, "Try '%s --help' for more information.\n",
             invocation_name );
  }


static int parse_addr( const char * const arg )
  {
  char * tail;
  errno = 0;
  const long tmp = strtol( arg, &tail, 10 );
  if( errno == 0 && tail != arg && tmp >= 1 && tmp <= INT_MAX ) return tmp;
  if( !quiet )
    fprintf( stderr, "%s: %s: Invalid line number; must be >= 1.\n",
             program_name, arg );
  exit( 1 );
  }


/* Return true if stdin is not a regular file.
   Piped scripts count as interactive (do not force ed to exit on error). */
bool interactive()
  {
  struct stat st;
  return fstat( 0, &st ) == 0 && !S_ISREG( st.st_mode );
  }


bool may_access_filename( const char * const name )
  {
  const int len = strlen( name );
  if( len <= 0 || name[len-1] == '/' )
    { set_error_msg( "Is a directory" ); return false; }
  if( restricted_ )
    {
    if( name[0] == '!' )
      { set_error_msg( "Shell access restricted" ); return false; }
    if( strcmp( name, ".." ) == 0 || strchr( name, '/' ) )
      { set_error_msg( "Directory access restricted" ); return false; }
    }
  if( safe_names )
    {
    const char * p;
    for( p = name; *p; ++p ) if( *p <= 31 && *p >= 1 )
      { set_error_msg( "Control characters 1-31 not allowed in file names" );
        return false; }
    }
  return true;
  }


int main( const int argc, const char * const argv[] )
  {
  bool initial_error = false;		/* fatal error reading file */
  bool loose = false;
  enum { opt_cr = 256, opt_un };
  const ap_Option options[] =
    {
    { 'E', "extended-regexp",      ap_no  },
    { 'G', "traditional",          ap_no  },
    { 'h', "help",                 ap_no  },
    { 'l', "loose-exit-status",    ap_no  },
    { 'p', "prompt",               ap_yes },
    { 'q', "quiet",                ap_no  },
    { 'q', "silent",               ap_no  },
    { 'r', "restricted",           ap_no  },
    { 's', "script",               ap_no  },
    { 'v', "verbose",              ap_no  },
    { 'V', "version",              ap_no  },
    { opt_cr, "strip-trailing-cr", ap_no  },
    { opt_un, "unsafe-names",      ap_no  },
    { 0, 0,                        ap_no  } };

  Arg_parser parser;
  if( argc > 0 ) invocation_name = argv[0];

  if( !ap_init( &parser, argc, argv, options, 0 ) )
    { show_error( "Memory exhausted.", 0, false ); return 1; }
  if( ap_error( &parser ) )				/* bad option */
    { show_error( ap_error( &parser ), 0, true ); return 1; }

  int argind = 0;
  for( ; argind < ap_arguments( &parser ); ++argind )
    {
    const int code = ap_code( &parser, argind );
    if( !code ) break;					/* no more options */
    const char * const arg = ap_argument( &parser, argind );
    switch( code )
      {
      case 'E': extended_regexp_ = true; break;
      case 'G': traditional_ = true; break;	/* backward compatibility */
      case 'h': show_help(); return 0;
      case 'l': loose = true; break;
      case 'p': if( set_prompt( arg ) ) break; else return 1;
      case 'q': quiet = true; break;
      case 'r': restricted_ = true; break;
      case 's': scripted_ = true; break;
      case 'v': set_verbose(); break;
      case 'V': show_version(); return 0;
      case opt_cr: strip_cr_ = true; break;
      case opt_un: safe_names = false; break;
      default: show_error( "internal error: uncaught option.", 0, false );
               return 3;
      }
    } /* end process options */

  setlocale( LC_ALL, "" );
  if( !init_buffers() ) return 1;

  const char * start_re_arg = 0;		/* '+/RE' or '+?RE' */
  int start_addr = 0;				/* '+line' */
  for( ; argind < ap_arguments( &parser ); ++argind )
    {
    const char * const arg = ap_argument( &parser, argind );
    /* a hyphen operand '-' is equivalent to the option '-s' */
    if( strcmp( arg, "-" ) == 0 ) { scripted_ = true; continue; }
    if( arg[0] == '+' )
      {
      const unsigned char ch = arg[1];
      if( ch == '/' || ch == '?' ) start_re_arg = arg;	/* store for later */
      else if( isdigit( ch ) ) start_addr = parse_addr( arg + 1 );
      else { if( !quiet ) fprintf( stderr, "%s: %s: Invalid line number or "
                     "regular expression.\n", program_name, arg ); return 1; }
      continue;
      }
    if( may_access_filename( arg ) )
      {
      if( arg[0] != '!' && !set_def_filename( arg ) ) return 1;
      /* first e can't be undone because u_current_addr = u_last_addr = -1 */
      const int ret = first_e_command( arg );	/* line count, < 0 if error */
      if( ret < 0 && !interactive() ) return 2;
      if( ret == -2 ) initial_error = true;
      if( ret > 0 && start_addr > 0 )
        { if( start_addr <= last_addr() ) set_current_addr( start_addr ); }
      else if( ret > 0 && start_re_arg )
        {
        set_current_addr( 0 );		/* start searching from address 0 */
        const char * p = start_re_arg + 1;
        const int addr = next_matching_node_addr( &p );
        if( addr > 0 && addr <= last_addr() ) set_current_addr( addr );
        else
          {
          set_current_addr( ( start_re_arg[1] == '/' ) ? 1 : last_addr() );
          if( !quiet )
            fprintf( stderr, "%s: %s: No match found.\n", start_re_arg, arg );
          if( !interactive() ) return 1;
          }
        }
      }
    else { initial_error = true; if( !interactive() ) return 2; }
    if( initial_error ) show_warning( arg, error_msg() );
    break;		/* extra arguments after file are ignored */
    }
  ap_free( &parser );
  return main_loop( initial_error, loose );
  }
