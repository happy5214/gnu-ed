/* signal.c: signal and miscellaneous routines for the ed line editor. */
/*  GNU ed - The GNU line editor.
    Copyright (C) 1993, 1994 Andrew Moore, Talke Studio
    Copyright (C) 2006, 2007, 2008, 2009 Antonio Diaz Diaz.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
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
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ed.h"


jmp_buf jmp_state;
static int mutex = 0;			/* If set, signals stay pending */
static int window_lines_ = 22;		/* scroll length: ws_row - 2 */
static int window_columns_ = 72;
static char sighup_pending = 0;
static char sigint_pending = 0;


static void sighup_handler( int signum )
  {
  signum = 0;			/* keep compiler happy */
  if( mutex ) sighup_pending = 1;
  else
    {
    char hb[] = "ed.hup";
    sighup_pending = 0;
    if( last_addr() && modified() &&
        write_file( hb, "w", 1, last_addr() ) < 0 )
      {
      char *s = getenv( "HOME" );
      int n = ( s ? strlen( s ) : 0 );
      int m = ( ( !n || *( s + n - 1 ) != '/' ) ? 1 : 0 );
      char *hup = ( ( n + m + (int)sizeof( hb ) < path_max( 0 ) ) ?
                    ( char *) malloc( n + m + sizeof( hb ) ) : 0 );
      if( n && hup )		/* hup filename */
        {
        memcpy( hup, s, n );
        if( m ) memcpy( hup + n, "/", 1 );
        memcpy( hup + n + m, hb, sizeof( hb ) );
        if( write_file( hup, "w", 1, last_addr() ) >= 0 ) exit( 0 );
        }
      exit( 1 );		/* hup file write failed */
      }
    exit( 0 );
    }
  }


static void sigint_handler( int signum )
  {
  if( mutex ) sigint_pending = 1;
  else
    {
    sigset_t set;
    sigint_pending = 0;
    sigemptyset( &set );
    sigaddset( &set, signum );
    sigprocmask( SIG_UNBLOCK, &set, 0 );
    longjmp( jmp_state, -1 );
    }
  }


static void sigwinch_handler( int signum )
  {
#ifdef TIOCGWINSZ
  struct winsize ws;            /* window size structure */

  if( ioctl( 0, TIOCGWINSZ, (char *) &ws ) >= 0 )
    {
    /* Sanity check values of environment vars */
    if( ws.ws_row > 2 && ws.ws_row < 600 ) window_lines_ = ws.ws_row - 2;
    if( ws.ws_col > 8 && ws.ws_col < 1800 ) window_columns_ = ws.ws_col - 8;
    }
#endif
  signum = 0;			/* keep compiler happy */
  }


static int set_signal( int signum, void (*handler )( int ) )
  {
  struct sigaction new_action;

  new_action.sa_handler = handler;
  sigemptyset( &new_action.sa_mask );
#ifdef SA_RESTART
  new_action.sa_flags = SA_RESTART;
#else
  new_action.sa_flags = 0;
#endif
  return sigaction( signum, &new_action, 0 );
  }


void enable_interrupts( void )
  {
  if( --mutex <= 0 )
    {
    mutex = 0;
    if( sighup_pending ) sighup_handler( SIGHUP );
    if( sigint_pending ) sigint_handler( SIGINT );
    }
  }


void disable_interrupts( void ) { ++mutex; }


void set_signals( void )
  {
#ifdef SIGWINCH
  sigwinch_handler( SIGWINCH );
  if( isatty( 0 ) ) set_signal( SIGWINCH, sigwinch_handler );
#endif
  set_signal( SIGHUP, sighup_handler );
  set_signal( SIGQUIT, SIG_IGN );
  set_signal( SIGINT, sigint_handler );
  }


void set_window_lines( const int lines ) { window_lines_ = lines; }
int window_columns( void ) { return window_columns_; }
int window_lines( void ) { return window_lines_; }


/* convert a string to int with out_of_range detection */
char parse_int( int *i, const char *str, const char **tail )
  {
  char *tmp;
  errno = 0;
  *i = strtol( str, &tmp, 10 );
  if( tail ) *tail = tmp;
  if( tmp == str )
    {
    set_error_msg( "Bad numerical result" );
    *i = 0;
    return 0;
    }
  if( errno == ERANGE )
    {
    set_error_msg( "Numerical result out of range" );
    *i = 0;
    return 0;
    }
  return 1;
  }


/* assure at least a minimum size for buffer `buf' */
char resize_buffer( char **buf, int *size, int min_size )
  {
  if( *size < min_size )
    {
    const int new_size = ( min_size < 512 ? 512 : ( min_size / 512 ) * 1024 );
    void *new_buf = 0;
    disable_interrupts();
    if( *buf ) new_buf = realloc( *buf, new_size );
    else new_buf = malloc( new_size );
    if( !new_buf )
      {
      show_strerror( 0, errno );
      set_error_msg( "Memory exhausted" );
      enable_interrupts();
      return 0;
      }
    *size = new_size;
    *buf = (char *)new_buf;
    enable_interrupts();
    }
  return 1;
  }


/* assure at least a minimum size for buffer `buf' */
char resize_line_buffer( const line_t ***buf, int *size, int min_size )
  {
  if( *size < min_size )
    {
    const int new_size = ( min_size < 512 ? 512 : ( min_size / 512 ) * 1024 );
    void *new_buf = 0;
    disable_interrupts();
    if( *buf ) new_buf = realloc( *buf, new_size );
    else new_buf = malloc( new_size );
    if( !new_buf )
      {
      show_strerror( 0, errno );
      set_error_msg( "Memory exhausted" );
      enable_interrupts();
      return 0;
      }
    *size = new_size;
    *buf = (const line_t **)new_buf;
    enable_interrupts();
    }
  return 1;
  }


/* assure at least a minimum size for buffer `buf' */
char resize_undo_buffer( undo_t **buf, int *size, int min_size )
  {
  if( *size < min_size )
    {
    const int new_size = ( min_size < 512 ? 512 : ( min_size / 512 ) * 1024 );
    void *new_buf = 0;
    disable_interrupts();
    if( *buf ) new_buf = realloc( *buf, new_size );
    else new_buf = malloc( new_size );
    if( !new_buf )
      {
      show_strerror( 0, errno );
      set_error_msg( "Memory exhausted" );
      enable_interrupts();
      return 0;
      }
    *size = new_size;
    *buf = (undo_t *)new_buf;
    enable_interrupts();
    }
  return 1;
  }


/* return unescaped copy of escaped string */
const char *strip_escapes( const char *s )
  {
  static char *buf = 0;
  static int bufsz = 0;
  const int len = strlen( s );

  int i = 0;

  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return 0;
  /* assert: no trailing escape */
  while( ( buf[i++] = ( (*s == '\\' ) ? *++s : *s ) ) )
    s++;
  return buf;
  }
