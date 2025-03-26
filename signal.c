/* signal.c: signal and miscellaneous routines for the ed line editor. */
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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ed.h"


jmp_buf jmp_state;			/* jumps to main_loop */
static int mutex = 0;			/* if > 0, signals stay pending */
static int user_lines = -1;		/* LINES or argument of z command */
					/* if > 0, overrides window_lines_ */
static int window_lines_ = 22;		/* scroll lines set by sigwinch_handler */
static int window_columns_ = 76;
static bool sighup_pending = false;
static bool sigint_pending = false;


const char * home_directory( void )
  {
  static bool first_time = true;
  static char * buf = 0;
  static int bufsz = 0;

  if( first_time )
    {
    first_time = false;
    const char * const hd = getenv( "HOME" );
    if( !hd || !hd[0] ) return 0;
    const int hdsize = strlen( hd );
    if( !resize_buffer( &buf, &bufsz, hdsize + 1 ) ) return 0;
    memcpy( buf, hd, hdsize );
    buf[hdsize] = 0;
    }
  return buf;
  }


static void sighup_handler( int signum )
  {
  if( signum ) {}			/* keep compiler happy */
  if( mutex ) { sighup_pending = true; return; }
  sighup_pending = false;
  const char hb[] = "ed.hup";
  if( last_addr() <= 0 || !modified() ||
      write_file( hb, "w", 1, last_addr() ) >= 0 ) exit( 0 );
  const char * const hd = home_directory();
  if( !hd || !hd[0] ) exit( 1 );
  const int hdsize = strlen( hd );
  const int need_slash = hd[hdsize-1] != '/';
  char * const hup = ( hdsize + need_slash + (int)sizeof hb < path_max( 0 ) ) ?
                     (char *)malloc( hdsize + need_slash + sizeof hb ) : 0;
  if( !hup ) exit( 1 );			/* hup file name */
  memcpy( hup, hd, hdsize );
  if( need_slash ) hup[hdsize] = '/';
  memcpy( hup + hdsize + need_slash, hb, sizeof hb );
  if( write_file( hup, "w", 1, last_addr() ) >= 0 ) exit( 0 );
  exit( 1 );				/* hup file write failed */
  }


static void sigint_handler( int signum )
  {
  if( mutex ) sigint_pending = true;
  else
    {
    sigset_t set;
    sigint_pending = false;
    sigemptyset( &set );
    sigaddset( &set, signum );
    sigprocmask( SIG_UNBLOCK, &set, 0 );
    longjmp( jmp_state, -1 );
    }
  }


static void sigwinch_handler( int signum )
  {
#ifdef TIOCGWINSZ
  struct winsize ws;			/* window size structure */

  if( ioctl( 0, TIOCGWINSZ, (char *) &ws ) >= 0 )
    {
    /* Sanity check values of environment vars */
    if( ws.ws_row > 2 && ws.ws_row < 600 ) window_lines_ = ws.ws_row - 2;
    if( ws.ws_col > 8 && ws.ws_col < 1800 ) window_columns_ = ws.ws_col - 4;
    }
#endif
  if( signum ) {}			/* keep compiler happy */
  }


static int set_signal( const int signum, void (*handler)( int ) )
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


void set_window_lines( const int lines ) { user_lines = lines; }
int window_columns( void ) { return window_columns_; }


int window_lines( void )
  {
  if( user_lines < 0 )				/* set initial size */
    {
    const char * const p = getenv( "LINES" );
    if( p && p[0] )
      {
      char * tail;
      errno = 0;
      const long n = strtol( p, &tail, 10 );
      if( errno == 0 && tail != p && n > 0 && n <= INT_MAX ) user_lines = n;
      }
    if( user_lines < 0 ) user_lines = 0;	/* LINES not found or invalid */
    }
  return ( user_lines > 0 ) ? user_lines : window_lines_;
  }


/* assure at least a minimum size for buffer 'buf' up to INT_MAX - 1 */
bool resize_buffer( char ** const buf, int * const size, const unsigned min_size )
  {
  if( (unsigned)*size < min_size )
    {
    if( min_size >= INT_MAX )
      { set_error_msg( "Line too long" ); return false; }
    const int new_size = ( ( min_size < 512 ) ? 512 :
      ( min_size >= INT_MAX / 2 ) ? INT_MAX - 1 : ( min_size / 512 ) * 1024 );
    void * new_buf = 0;
    disable_interrupts();
    if( *buf ) new_buf = realloc( *buf, new_size );
    else new_buf = malloc( new_size );
    if( !new_buf )
      { show_strerror( 0, errno );
        set_error_msg( mem_msg ); enable_interrupts(); return false; }
    *size = new_size;
    *buf = (char *)new_buf;
    enable_interrupts();
    }
  return true;
  }
