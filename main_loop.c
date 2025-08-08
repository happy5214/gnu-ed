/* GNU ed - The GNU line editor.
   Copyright (C) 1993, 1994 Andrew L. Moore, Talke Studio
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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ed.h"


enum Status { QUIT = -1, ERR = -2, EMOD = -3, FATAL = -4 };

static const char * const inv_com_suf = "Invalid command suffix";
static const char * const inv_mark_ch = "Invalid mark character";
static const char * const no_cur_fn   = "No current filename";
static const char * const no_prev_com = "No previous command";
static const char * def_filename = "";	/* default filename */
static char errmsg[80] = "";		/* error message buffer */
static const char * prompt_str = "*";	/* command prompt */
static int first_addr = 0, second_addr = 0;
static bool prompt_on = false;		/* show command prompt */
static bool read_only = false;		/* loaded file is not writable */
static bool verbose = false;		/* print all error messages */


int first_e_command( const char * const filename )
  { return read_file( filename, 0, &read_only ); }

void invalid_address( void ) { set_error_msg( "Invalid address" ); }

bool set_def_filename( const char * const s )
  {
  static char * buf = 0;		/* filename buffer */
  static int bufsz = 0;			/* filename buffer size */
  const int len = strlen( s );

  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return false;
  memcpy( buf, s, len + 1 );
  def_filename = buf;
  read_only = false;
  return true;
  }

const char * error_msg( void ) { return errmsg; }

void set_error_msg( const char * const msg )
  {
  strncpy( errmsg, msg, sizeof errmsg );
  errmsg[(sizeof errmsg)-1] = 0;
  }

bool set_prompt( const char * const s )
  {
  static char * buf = 0;		/* prompt buffer */
  static int bufsz = 0;			/* prompt buffer size */
  const int len = strlen( s );

  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return false;
  memcpy( buf, s, len + 1 );
  prompt_str = buf;
  prompt_on = true;
  return true;
  }

void set_verbose( void ) { verbose = true; }


static const line_node * mark[26];		/* line markers */
static int markno;				/* line marker count */

static bool mark_line_node( const line_node * const lp, int c )
  {
  c -= 'a';
  if( c < 0 || c >= 26 ) { set_error_msg( inv_mark_ch ); return false; }
  if( !mark[c] ) ++markno;
  mark[c] = lp;
  return true;
  }


void unmark_line_node( const line_node * const lp )
  {
  int i;
  for( i = 0; markno && i < 26; ++i )
    if( mark[i] == lp )
      { mark[i] = 0; --markno; }
  }


/* return address of a marked line */
static int get_marked_node_addr( int c )
  {
  c -= 'a';
  if( c < 0 || c >= 26 ) { set_error_msg( inv_mark_ch ); return -1; }
  return get_line_node_addr( mark[c] );
  }


/* return pointer to copy of shell command in the command buffer */
static const char * get_shell_command( const char ** const ibufpp )
  {
  static char * buf = 0;		/* temporary buffer */
  static int bufsz = 0;
  static char * shcmd = 0;		/* shell command buffer */
  static int shcmdsz = 0;		/* shell command buffer size */
  static int shcmdlen = 0;		/* shell command length */
  int i = 0, len = 0;
  bool replacement = false;		/* true if '!' or '%' are replaced */

  if( restricted() ) { set_error_msg( "Shell access restricted" ); return 0; }
  if( !get_extended_line( ibufpp, &len, true ) ) return 0;
  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return 0;
  if( **ibufpp != '!' ) buf[i++] = '!';		/* prefix command w/ bang */
  else				/* replace '!' with the previous command */
    {
    if( shcmdlen <= 0 || ( traditional() && !shcmd[1] ) )
      { set_error_msg( no_prev_com ); return 0; }
    memcpy( buf, shcmd, shcmdlen );		/* bufsz >= shcmdlen */
    i += shcmdlen; ++*ibufpp; replacement = true;
    }
  while( **ibufpp != '\n' )
    {
    if( **ibufpp == '%' )	/* replace '%' with the default filename */
      {
      if( !def_filename[0] ) { set_error_msg( no_cur_fn ); return 0; }
      len = strlen( def_filename );
      if( !resize_buffer( &buf, &bufsz, i + len ) ) return 0;
      memcpy( buf + i, def_filename, len );
      i += len; ++*ibufpp; replacement = true;
      }
    else		/* copy char or escape sequence unescaping any '%' */
      {
      char ch = *(*ibufpp)++;
      if( !resize_buffer( &buf, &bufsz, i + 2 ) ) return 0;
      if( ch != '\\' ) { buf[i++] = ch; continue; }	/* normal char */
      ch = *(*ibufpp)++; if( ch != '%' ) buf[i++] = '\\';
      buf[i++] = ch;
      }
    }
  if( **ibufpp == '\n' ) ++*ibufpp;		/* skip newline for global */
  if( !resize_buffer( &shcmd, &shcmdsz, i + 1 ) ) return 0;
  memcpy( shcmd, buf, i );
  shcmd[i] = 0; shcmdlen = i;
  if( replacement ) { printf( "%s\n", shcmd + 1 ); fflush( stdout ); }
  return shcmd;
  }


static void skip_blanks( const char ** const ibufpp )
  {
  while( isspace( (unsigned char)**ibufpp ) && **ibufpp != '\n' ) ++*ibufpp;
  }


/* return pointer to copy of filename in the command buffer */
static const char * get_filename( const char ** const ibufpp,
                                  const bool traditional_f_command )
  {
  static char * buf = 0;
  static int bufsz = 0;

  skip_blanks( ibufpp );
  if( **ibufpp == '\n' )
    {
    if( !traditional_f_command && !def_filename[0] )
      { set_error_msg( no_cur_fn ); return 0; }
    ++*ibufpp; return "";			/* skip newline for global */
    }
  const char * hd = 0;
  int hdsize = 0, size = 0;
  if( !get_extended_line( ibufpp, &size, true ) ) return 0;
  if( **ibufpp == '!' ) { ++*ibufpp; return get_shell_command( ibufpp ); }
  if( **ibufpp == '~' && (*ibufpp)[1] == '/' )
    { hd = home_directory();
      if( hd && hd[0] ) { hdsize = strlen( hd ); --size; ++*ibufpp; } }
  if( hdsize + size > path_max( 0 ) )
    { set_error_msg( "Filename too long" ); return 0; }
  if( !resize_buffer( &buf, &bufsz, hdsize + size + 1 ) ) return 0;
  if( hdsize > 0 ) memcpy( buf, hd, hdsize );
  int i;
  for( i = hdsize; i < hdsize + size && **ibufpp != '\n'; ++i, ++*ibufpp )
    buf[i] = **ibufpp;
  buf[i] = 0;
  if( **ibufpp == '\n' ) ++*ibufpp;		/* skip newline for global */
  return may_access_filename( buf ) ? buf : 0;
  }


/* convert a string to int with out_of_range detection */
static bool parse_int( int * const i, const char ** const ibufpp )
  {
  char * tail;
  errno = 0;
  const long li = strtol( *ibufpp, &tail, 10 );

  if( tail == *ibufpp )
    { set_error_msg( "Invalid number" ); return false; }
  if( errno == ERANGE || li > INT_MAX || li < -INT_MAX )
    { set_error_msg( "Number out of range" ); return false; }
  *ibufpp = tail;
  *i = li;
  return true;
  }


/* Get line addresses from the command buffer until an invalid address
   is seen. Return the number of addresses read, or -1 if error.
   If no addresses are found, both addresses are set to the current address.
   If one address is found, both addresses are set to that address.
*/
static int extract_addresses( const char ** const ibufpp )
  {
  bool first = true;			/* true == addr, false == offset */

  first_addr = second_addr = -1;	/* set to undefined */
  skip_blanks( ibufpp );

  while( true )
    {
    int n;
    const unsigned char ch = **ibufpp;
    if( isdigit( ch ) )
      {
      if( !parse_int( &n, ibufpp ) ) return -1;
      if( first ) { first = false; second_addr = n; } else second_addr += n;
      }
    else switch( ch )
      {
      case '\t':
      case ' ': ++*ibufpp; skip_blanks( ibufpp ); break;
      case '+':
      case '-': if( first ) { first = false; second_addr = current_addr(); }
                if( isdigit( (unsigned char)(*ibufpp)[1] ) )
                  {
                  if( !parse_int( &n, ibufpp ) ) return -1;
                  second_addr += n;
                  }
                else { ++*ibufpp;
                       if( ch == '+' ) ++second_addr; else --second_addr; }
                break;
      case '.':
      case '$': if( !first ) { invalid_address(); return -1; };
                first = false; ++*ibufpp;
                second_addr = ( ( ch == '.' ) ? current_addr() : last_addr() );
                break;
      case '/':
      case '?': if( !first ) { invalid_address(); return -1; };
                second_addr = next_matching_node_addr( ibufpp );
                if( second_addr < 0 ) return -1;
                first = false; break;
      case '\'':if( !first ) { invalid_address(); return -1; };
                first = false; ++*ibufpp;
                second_addr = get_marked_node_addr( *(*ibufpp)++ );
                if( second_addr < 0 ) return -1;
                break;
      case '%':
      case ',':
      case ';': if( first )
                  {
                  if( first_addr < 0 )
                    { first_addr = ( ( ch == ';' ) ? current_addr() : 1 );
                      second_addr = last_addr(); }
                  else first_addr = second_addr;
                  }
                else
                  {
                  if( second_addr < 0 || second_addr > last_addr() )
                    { invalid_address(); return -1; }
                  if( ch == ';' ) set_current_addr( second_addr );
                  first_addr = second_addr; first = true;
                  }
                ++*ibufpp;
                break;
      default:
        if( !first && ( second_addr < 0 || second_addr > last_addr() ) )
          { invalid_address(); return -1; }
        {
        int addr_cnt = 0;			/* limited to 2 */
        if( second_addr >= 0 ) addr_cnt = ( first_addr >= 0 ) ? 2 : 1;
        if( addr_cnt <= 0 ) second_addr = current_addr();
        if( addr_cnt <= 1 ) first_addr = second_addr;
        return addr_cnt;
        }
      }
    }
  }


/* get a valid address from the command buffer */
static bool get_third_addr( const char ** const ibufpp, int * const addr )
  {
  const int old1 = first_addr;
  const int old2 = second_addr;
  int addr_cnt = extract_addresses( ibufpp );

  if( addr_cnt < 0 ) return false;
  if( traditional() && addr_cnt == 0 )
    { set_error_msg( "Destination expected" ); return false; }
  if( second_addr < 0 || second_addr > last_addr() )
    { invalid_address(); return false; }
  *addr = second_addr;
  first_addr = old1; second_addr = old2;
  return true;
  }


/* set default range and return true if address range is valid */
static bool set_addr_range( const int n, const int m, const int addr_cnt )
  {
  if( addr_cnt == 0 ) { first_addr = n; second_addr = m; }
  if( first_addr < 1 || first_addr > second_addr || second_addr > last_addr() )
    { invalid_address(); return false; }
  return true;
  }

/* set defaults to current_addr and return true if address range is valid */
static bool set_addr_range2( const int addr_cnt )
  {
  return set_addr_range( current_addr(), current_addr(), addr_cnt );
  }

/* set default second_addr and return true if second_addr is valid */
static bool set_second_addr( const int addr, const int addr_cnt )
  {
  if( addr_cnt == 0 ) second_addr = addr;
  if( second_addr < 1 || second_addr > last_addr() )
    { invalid_address(); return false; }
  return true;
  }


/* check the command suffixes in the command buffer */
static bool get_command_suffix( const char ** const ibufpp,
                                int * const pflagsp )
  {
  while( true )
    {
    const unsigned char ch = **ibufpp;
    if( ch == 'l' ) { if( *pflagsp & pf_l ) break; else *pflagsp |= pf_l; }
    else if( ch == 'n' ) { if( *pflagsp & pf_n ) break; else *pflagsp |= pf_n; }
    else if( ch == 'p' ) { if( *pflagsp & pf_p ) break; else *pflagsp |= pf_p; }
    else break;
    ++*ibufpp;
    }
  /* skip newline for global */
  if( *(*ibufpp)++ != '\n' ) { set_error_msg( inv_com_suf ); return false; }
  return true;
  }


/* check the command suffixes for command s in the command buffer */
static bool get_command_s_suffix( const char ** const ibufpp,
                                  int * const pflagsp, int * const snump,
                                  bool * const ignore_casep )
  {
  bool rep = false;			/* repeated g/count */
  bool error = false;
  while( true )
    {
    const unsigned char ch = **ibufpp;
    if( ch >= '1' && ch <= '9' )
      {
      int n = 0;
      if( rep || !parse_int( &n, ibufpp ) || n <= 0 ) { error = true; break; }
      rep = true; *snump = n; continue;
      }
    else if( ch == 'g' )
      { if( rep ) break; else { rep = true; *snump = 0; } }
    else if( ch == 'i' || ch == 'I' )
      { if( *ignore_casep ) break; else *ignore_casep = true; }
    else if( ch == 'l' ) { if( *pflagsp & pf_l ) break; else *pflagsp |= pf_l; }
    else if( ch == 'n' ) { if( *pflagsp & pf_n ) break; else *pflagsp |= pf_n; }
    else if( ch == 'p' ) { if( *pflagsp & pf_p ) break; else *pflagsp |= pf_p; }
    else break;
    ++*ibufpp;
    }
  if( error || *(*ibufpp)++ != '\n' )		/* skip newline for global */
    { set_error_msg( inv_com_suf ); return false; }
  return true;
  }


static bool unexpected_address( const int addr_cnt )
  {
  if( addr_cnt > 0 ) { set_error_msg( "Unexpected address" ); return true; }
  return false;
  }

static bool unexpected_command_suffix( const unsigned char ch )
  {
  if( !isspace( ch ) )
    { set_error_msg( "Unexpected command suffix" ); return true; }
  return false;
  }


static bool command_s( const char ** const ibufpp, int * const pflagsp,
                       const int addr_cnt, const bool isglobal )
  {
  static int pflags = 0;	/* print suffixes */
  static int pmask = pf_p;	/* the print suffixes to be toggled */
  static int snum = 1;		/* > 0 count, <= 0 global substitute */
  enum Sflags {
    sf_g = 0x01,	/* complement previous global substitute suffix */
    sf_p = 0x02,	/* complement previous print suffix */
    sf_r = 0x04,	/* use regex of last search (if newer) */
    sf_none = 0x08	/* make sflags != 0 if no flags at all */
    };
  int sflags = 0;	/* if sflags != 0, repeat last substitution */

  if( !set_addr_range2( addr_cnt ) ) return false;
  do {
    bool error = false;
    if( **ibufpp >= '1' && **ibufpp <= '9' )
      {
      int n = 0;
      if( ( sflags & sf_g ) || !parse_int( &n, ibufpp ) || n <= 0 )
        error = true;
      else
        { sflags |= sf_g; snum = n; }
      }
    else switch( **ibufpp )
      {
      case '\n':sflags |= sf_none; break;
      case 'g': if( sflags & sf_g ) error = true;
                else { sflags |= sf_g; snum = !snum; ++*ibufpp; }
                break;
      case 'p': if( sflags & sf_p ) error = true;
                else { sflags |= sf_p; ++*ibufpp; } break;
      case 'r': if( sflags & sf_r ) error = true;
                else { sflags |= sf_r; ++*ibufpp; } break;
      default: if( sflags ) error = true;
      }
    if( error ) { set_error_msg( inv_com_suf ); return false; }
    }
  while( sflags && **ibufpp != '\n' );
  if( sflags )		/* repeat last substitution */
    {
    if( !subst_regex() ) { set_error_msg( no_prev_subst ); return false; }
    if( ( sflags & sf_r ) && !replace_subst_re_by_search_re() ) return false;
    if( sflags & sf_p ) pflags ^= pmask;
    }
  else			/* don't compile RE until suffix 'I' is parsed */
    {
    const char * pat = get_pattern_for_s( ibufpp );
    if( !pat ) return false;
    const char delimiter = **ibufpp;
    if( !extract_replacement( ibufpp, isglobal ) ) return false;
    pflags = 0; snum = 1;
    bool ignore_case = false;
    if( **ibufpp == '\n' )			/* omitted last delimiter */
      { ++*ibufpp; pflags = pf_p; }		/* skip newline for global */
    else
      { if( **ibufpp == delimiter ) ++*ibufpp;		/* skip delimiter */
        if( !get_command_s_suffix( ibufpp, &pflags, &snum, &ignore_case ) )
          return false; }
    pmask = pflags & ( pf_l | pf_n | pf_p ); if( pmask == 0 ) pmask = pf_p;
    if( !set_subst_regex( pat, ignore_case ) ) return false;
    }
  *pflagsp = pflags;
  if( !isglobal ) clear_undo_stack();
  if( !search_and_replace( first_addr, second_addr, snum, isglobal ) )
    return false;
  return true;
  }


/* Return a tmpname in the form "${TMPDIR}/ed-<coded_pid>".
   The pid is coded in little endian order.
*/
static const char * get_tmpname( const bool init )
  {
  static char * buf = 0;
  static int bufsz = 0;
  if( !buf && init )
    {
    enum { num_codes = 36 };
    const char * const codes = "0123456789abcdefghijklmnopqrstuvwxyz";
    const char * p = getenv( "TMPDIR" );
    if( !p ) p = "/tmp";
    const char * const prefix = "/ed-";
    int pos = strlen( p ) + strlen( prefix );
    if( !resize_buffer( &buf, &bufsz, pos + 7 + 1 ) ) return 0;
    snprintf( buf, bufsz, "%s%s", p, prefix );
    unsigned n = getpid();
    do buf[pos++] = codes[n % num_codes]; while( n /= num_codes );
    buf[pos] = 0;
    }
  return buf;
  }


static bool command_shell( const char ** const ibufpp, const int addr_cnt,
                           const bool isglobal )
  {
  const char * fnp = get_shell_command( ibufpp );
  if( !fnp ) return false;
  if( addr_cnt == 0 )		/* shell escape command */
    {
    if( system( fnp + 1 ) < 0 )
      { set_error_msg( "Can't create shell process" ); return false; }
    if( !scripted() ) fputs( "!\n", stdout );
    return true;
    }
  /* filter lines through command */
  if( !set_addr_range2( addr_cnt ) ) return false;
  const char * p;
  for( p = fnp + 1; *p; ++p )
    if( *p == '<' || *p == '>' )
      { set_error_msg( "Redirection not allowed" ); return false; }
  const char * const tmpname = get_tmpname( true );
  if( !tmpname ) { set_error_msg( mem_msg ); return false; }
  const int len = strlen( fnp ) + 3 + strlen( tmpname ) + 5 + 1;
  char * command = (char *)malloc( len );
  if( !command ) { set_error_msg( mem_msg ); return false; }
  snprintf( command, len, "%s > %s 2>&1", fnp, tmpname );
  if( write_file( command, "w", first_addr, second_addr ) < 0 )
    { remove( tmpname ); return false; }
  if( !isglobal ) clear_undo_stack();
  if( !delete_lines( first_addr, second_addr, isglobal ) )
    { remove( tmpname ); return false; }
  const int line_count =
    read_file( tmpname, current_addr() - ( current_addr() >= first_addr ), 0 );
  if( current_addr() <= 0 && last_addr() > 0 ) set_current_addr( 1 );
  remove( tmpname );
  return line_count >= 0;
  }


static int exec_global( const char ** const ibufpp, const int pflags,
                        const bool interactive );

/* execute the next command in command buffer; return error status */
static int exec_command( const char ** const ibufpp, const bool isglobal )
  {
  const char * fnp;				/* filename */
  int pflags = 0;				/* print suffixes */
  int addr, c, n;
  const int addr_cnt = extract_addresses( ibufpp );

  if( addr_cnt < 0 ) return ERR;
  skip_blanks( ibufpp );
  c = *(*ibufpp)++;
  switch( c )
    {
    case 'a': if( !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( !append_lines( ibufpp, second_addr, false, isglobal ) )
                return ERR;
              break;
    case 'c': if( !set_addr_range2( addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( !delete_lines( first_addr, second_addr, isglobal ) ||
                  !append_lines( ibufpp, current_addr(),
                                 current_addr() >= first_addr, isglobal ) )
                return ERR;
              break;
    case 'd': if( !set_addr_range2( addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( !delete_lines( first_addr, second_addr, isglobal ) )
                return ERR;
              break;
    case 'e': if( modified() && !warned() ) return EMOD;	/* fall through */
    case 'E': if( unexpected_address( addr_cnt ) ||
                  unexpected_command_suffix( **ibufpp ) ) return ERR;
              fnp = get_filename( ibufpp, false );
              if( !fnp || !delete_lines( 1, last_addr(), isglobal ) ||
                  !close_sbuf() ) return ERR;
              set_modified( false );		/* buffer is now empty */
              if( !open_sbuf() ) return FATAL;
              if( fnp[0] && fnp[0] != '!' && !set_def_filename( fnp ) )
                return ERR;
              if( read_file( fnp[0] ? fnp : def_filename, 0, &read_only ) < 0 )
                return ERR;
              reset_undo_state();	/* to prevent undoing the read */
              break;
    case 'f': if( unexpected_address( addr_cnt ) ||
                  unexpected_command_suffix( **ibufpp ) ) return ERR;
              fnp = get_filename( ibufpp, traditional() );
              if( !fnp ) return ERR;
              if( fnp[0] == '!' )
                { set_error_msg( "Invalid redirection" ); return ERR; }
              if( fnp[0] && !set_def_filename( fnp ) ) return ERR;
              print_filename( def_filename, true ); putchar('\n');
              break;
    case 'g':
    case 'v':
    case 'G':
    case 'V': if( isglobal )
                { set_error_msg( "Cannot nest global commands" ); return ERR; }
              n = ( c == 'g' || c == 'G' );	/* mark matching lines */
              if( !set_addr_range( 1, last_addr(), addr_cnt ) ||
                  !build_active_list( ibufpp, first_addr, second_addr, n ) )
                return ERR;
              n = ( c == 'G' || c == 'V' );		/* interactive */
              if( n && !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              n = exec_global( ibufpp, pflags, n ); if( n != 0 ) return n;
              break;
    case 'h':
    case 'H': if( unexpected_address( addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( c == 'H' ) verbose = !verbose;
              if( ( c == 'h' || verbose ) && errmsg[0] )
                printf( "%s\n", errmsg );
              break;
    case 'i': if( !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( !append_lines( ibufpp, second_addr, true, isglobal ) )
                return ERR;
              break;
    case 'j': if( !set_addr_range( current_addr(), current_addr() + 1, addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( first_addr < second_addr &&
                  !join_lines( first_addr, second_addr, isglobal ) ) return ERR;
              break;
    case 'k': n = *(*ibufpp)++;
              if( second_addr == 0 ) { invalid_address(); return ERR; }
              if( !get_command_suffix( ibufpp, &pflags ) ||
                  !mark_line_node( search_line_node( second_addr ), n ) )
                return ERR;
              break;
    case 'l': n = pf_l; goto pflabel;
    case 'n': n = pf_n; goto pflabel;
    case 'p': n = pf_p;
pflabel:      if( !set_addr_range2( addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ||
                  !print_lines( first_addr, second_addr, pflags | n ) )
                return ERR;
              pflags = 0;
              break;
    case 'm': if( !set_addr_range2( addr_cnt ) ||
                  !get_third_addr( ibufpp, &addr ) ) return ERR;
              if( addr >= first_addr && addr < second_addr )
                { set_error_msg( "Invalid destination" ); return ERR; }
              if( !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( !move_lines( first_addr, second_addr, addr, isglobal ) )
                return ERR;
              break;
    case 'P':
    case 'q':
    case 'Q': if( unexpected_address( addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( c == 'P' ) { prompt_on = !prompt_on; break; }
              return ( c == 'q' && modified() && !warned() ) ? EMOD : QUIT;
    case 'r': if( unexpected_command_suffix( **ibufpp ) ) return ERR;
              if( addr_cnt == 0 ) second_addr = last_addr();
              fnp = get_filename( ibufpp, false );
              if( !fnp ) return ERR;
              if( !def_filename[0] && fnp[0] != '!' && !set_def_filename( fnp ) )
                return ERR;
              if( !isglobal ) clear_undo_stack();
              addr = read_file( fnp[0] ? fnp : def_filename, second_addr, 0 );
              if( addr < 0 ) return ERR;
              if( addr ) set_modified( true );
              break;
    case 's': if( !command_s( ibufpp, &pflags, addr_cnt, isglobal ) )
                return ERR;
              break;
    case 't': if( !set_addr_range2( addr_cnt ) ||
                  !get_third_addr( ibufpp, &addr ) ||
                  !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( !copy_lines( first_addr, second_addr, addr ) ) return ERR;
              break;
    case 'u': if( unexpected_address( addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ||
                  !undo( isglobal ) ) return ERR;
              break;
    case 'w':
    case 'W': n = **ibufpp;
              if( n == 'q' || n == 'Q' ) ++*ibufpp;
              if( unexpected_command_suffix( **ibufpp ) ) return ERR;
              fnp = get_filename( ibufpp, false );
              if( !fnp ) return ERR;
              if( addr_cnt == 0 && last_addr() == 0 )
                first_addr = second_addr = 0;
              else if( !set_addr_range( 1, last_addr(), addr_cnt ) )
                return ERR;
              if( !def_filename[0] && fnp[0] != '!' && !set_def_filename( fnp ) )
                return ERR;
              addr = write_file( fnp[0] ? fnp : def_filename,
                     ( c == 'W' ) ? "a" : "w", first_addr, second_addr );
              if( addr < 0 ) return ERR;
              if( addr == last_addr() && fnp[0] != '!' ) set_modified( false );
              else if( n == 'q' && modified() && !warned() ) return EMOD;
              if( n == 'q' || n == 'Q' ) return QUIT;	/* wq */
              break;
    case 'x': if( second_addr < 0 || second_addr > last_addr() )
                { invalid_address(); return ERR; }
              if( !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              if( !isglobal ) clear_undo_stack();
              if( !put_lines( second_addr ) ) return ERR;
              break;
    case 'y': if( !set_addr_range2( addr_cnt ) ||
                  !get_command_suffix( ibufpp, &pflags ) ||
                  !yank_lines( first_addr, second_addr ) ) return ERR;
              break;
    case 'z': if( !set_second_addr( current_addr() + !isglobal, addr_cnt ) )
                return ERR;
              if( **ibufpp > '0' && **ibufpp <= '9' )
                { if( parse_int( &n, ibufpp ) ) set_window_lines( n );
                  else return ERR; }
              if( !get_command_suffix( ibufpp, &pflags ) ||
                  !print_lines( second_addr,
                    min( last_addr(), second_addr + window_lines() - 1 ),
                    pflags ) ) return ERR;
              pflags = 0;
              break;
    case '=': if( !get_command_suffix( ibufpp, &pflags ) ) return ERR;
              printf( "%d\n", addr_cnt ? second_addr : last_addr() );
              break;
    case '!': if( !command_shell( ibufpp, addr_cnt, isglobal ) ) return ERR;
              break;
    case '\n': if( !set_second_addr( current_addr() +
                     ( traditional() || !isglobal ), addr_cnt ) ||
                   !print_lines( second_addr, second_addr, 0 ) ) return ERR;
              break;
    case '#': while( *(*ibufpp)++ != '\n' ) {}	/* skip newline for global */
              break;
    default: set_error_msg( "Unknown command" ); return ERR;
    }
  if( pflags && !print_lines( current_addr(), current_addr(), pflags ) )
    return ERR;
  return 0;
  }


/* Apply command list in the command buffer to the active lines in a range.
   Stop at first error. Return status of last command executed. */
static int exec_global( const char ** const ibufpp, const int pflags,
                        const bool interactive )
  {
  static char * buf = 0;
  static int bufsz = 0;
  const char * cmd = 0;

  if( !interactive )
    {
    if( traditional() && strcmp( *ibufpp, "\n" ) == 0 )
      cmd = "p\n";			/* null cmd_list == 'p' */
    else
      {
      if( !get_extended_line( ibufpp, 0, false ) ) return ERR;
      cmd = *ibufpp;
      }
    }
  clear_undo_stack();
  while( true )
    {
    const line_node * const lp = next_active_node();
    if( !lp ) break;
    set_current_addr( get_line_node_addr( lp ) );
    if( current_addr() < 0 ) return ERR;
    if( interactive )
      {
      /* print current_addr; get a command in global syntax */
      int len = 0;
      if( !print_lines( current_addr(), current_addr(), pflags ) ) return ERR;
      *ibufpp = get_stdin_line( &len );
      if( !*ibufpp ) return ERR;			/* error */
      if( len <= 0 ) return ERR;			/* EOF */
      if( len == 1 && strcmp( *ibufpp, "\n" ) == 0 ) continue;
      if( len == 2 && strcmp( *ibufpp, "&\n" ) == 0 )
        { if( !cmd ) { set_error_msg( no_prev_com ); return ERR; } }
      else
        {
        if( !get_extended_line( ibufpp, &len, false ) ||
            !resize_buffer( &buf, &bufsz, len + 1 ) ) return ERR;
        memcpy( buf, *ibufpp, len + 1 );
        cmd = buf;
        }
      }
    *ibufpp = cmd;
    while( **ibufpp )
      {
      const int status = exec_command( ibufpp, true );
      if( status != 0 ) return status;
      }
    }
  return 0;
  }


int main_loop( const bool initial_error, const bool loose )
  {
  extern jmp_buf jmp_state;
  const char * ibufp;			/* pointer to command buffer */
  volatile int err_status = 0;		/* program exit status */
  int len = 0, status;

  disable_interrupts();
  set_signals();
  status = setjmp( jmp_state );
  if( status == 0 )			/* direct invocation of setjmp */
    { enable_interrupts(); if( initial_error ) status = err_status = 1; }
  else { status = -1; fputs( "\n?\n", stdout ); set_error_msg( "Interrupt" );
         if( get_tmpname( false ) ) remove( get_tmpname( false ) ); }

  while( true )
    {
    fflush( stdout ); fflush( stderr );
    if( status < 0 && verbose ) { printf( "%s\n", errmsg ); fflush( stdout ); }
    if( prompt_on ) { fputs( prompt_str, stdout ); fflush( stdout ); }
    ibufp = get_stdin_line( &len );
    if( !ibufp ) return 2;			/* an error happened */
    if( len <= 0 )				/* EOF on stdin ('q') */
      {
      if( !modified() || status == EMOD ) status = QUIT;
      else { status = EMOD; if( !loose ) err_status = 2; }
      }
    else status = exec_command( &ibufp, false );
    if( status == 0 )
      { if( read_only && modified() ) { read_only = false;
          show_warning( def_filename, "warning: read-only file" ); }
        continue; }
    if( status == QUIT ) return err_status;
    fputs( "?\n", stdout );			/* give warning */
    if( !loose && err_status == 0 ) err_status = 1;
    set_warned( status == EMOD );		/* errors reset warned */
    if( warned() ) set_error_msg( "Warning: buffer modified" );
    if( !interactive() )
      { if( verbose ) printf( "script, line %d: %s\n", linenum(), errmsg );
        return ( status == FATAL ) ? 1 : err_status; }
    if( status == FATAL )
      { if( verbose ) { printf( "%s\n", errmsg ); } return 1; }
    }
  }
