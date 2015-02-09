/* io.c: i/o routines for the ed line editor */
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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ed.h"


/* print text to stdout */
static char put_tty_line( const char *s, int len, const int gflags )
  {
  const char escapes[] = "\a\b\f\n\r\t\v\\";
  const char escchars[] = "abfnrtv\\";
  int col = 0;

  if( gflags & GNP ) { printf( "%d\t", current_addr() ); col = 8; }
  while( --len >= 0 )
    {
    const unsigned char ch = *s++;
    if( !( gflags & GLS ) ) putchar( ch );
    else
      {
      if( ++col > window_columns() ) { col = 1; fputs( "\\\n", stdout ); }
      if( ch >= 32 && ch <= 126 && ch != '\\' ) putchar( ch );
      else
        {
        char *cp = strchr( escapes, ch );
        ++col; putchar('\\');
        if( cp ) putchar( escchars[cp-escapes] );
        else
          {
          col += 2;
          putchar( ( ( ch >> 6 ) & 7 ) + '0' );
          putchar( ( ( ch >> 3 ) & 7 ) + '0' );
          putchar( ( ch & 7 ) + '0' );
          }
        }
      }
    }
  if( !traditional() && ( gflags & GLS ) ) putchar('$');
  putchar('\n');
  return 1;
  }


/* print a range of lines to stdout */
char display_lines( int from, const int to, const int gflags )
  {
  line_t *ep = search_line_node( inc_addr( to ) );
  line_t *bp = search_line_node( from );

  if( !from ) { set_error_msg( "Invalid address" ); return 0; }
  while( bp != ep )
    {
    char *s = get_sbuf_line( bp );
    if( !s ) return 0;
    set_current_addr( from++ );
    if( !put_tty_line( s, bp->len, gflags ) ) return 0;
    bp = bp->q_forw;
    }
  return 1;
  }


/* return the parity of escapes at the end of a string */
static char trailing_escape( const char * const s, int len )
  {
  char parity = 0;
  while( --len >= 0 && s[len] == '\\' ) parity = !parity;
  return parity;
  }


/* get an extended line from stdin */
const char *get_extended_line( const char *ibufp, int *lenp, const char nonl )
  {
  static char *buf = 0;
  static int bufsz = 0;
  int len;

  for( len = 0; ibufp[len++] != '\n'; ) ;
  if( len < 2 || !trailing_escape( ibufp, len - 1 ) )
    { if( lenp ) *lenp = len; return ibufp; }
  if( !resize_buffer( &buf, &bufsz, len ) ) return 0;
  memcpy( buf, ibufp, len );
  --len; buf[len-1] = '\n';		/* strip trailing esc */
  if( nonl ) --len;			/* strip newline */
  while( 1 )
    {
    int len2;
    if( !( ibufp = get_tty_line( &len2 ) ) ) return 0;
    if( len2 == 0 || ibufp[len2-1] != '\n' )
      { set_error_msg( "Unexpected end-of-file" ); return 0; }
    if( !resize_buffer( &buf, &bufsz, len + len2 ) ) return 0;
    memcpy( buf + len, ibufp, len2 );
    len += len2;
    if( len2 < 2 || !trailing_escape( buf, len - 1 ) ) break;
    --len; buf[len-1] = '\n';		/* strip trailing esc */
    if( nonl ) --len;			/* strip newline */
    }
  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return 0;
  buf[len] = 0;
  if( lenp ) *lenp = len;
  return buf;
  }


/* read a line of text from stdin; return pointer to buffer and line length */
const char *get_tty_line( int *lenp )
  {
  static char *buf = 0;
  static int bufsz = 0;
  int i = 0, oi = -1;

  while( 1 )
    {
    const int c = getchar();
    if( c == EOF )
      {
      if( ferror( stdin ) )
        {
        show_strerror( "stdin", errno ); set_error_msg( "Cannot read stdin" );
        clearerr( stdin ); if( lenp ) *lenp = 0;
        return 0;
        }
      else
        {
        clearerr( stdin ); if( i != oi ) { oi = i; continue; }
        if( i ) buf[i] = 0; if( lenp ) *lenp = i;
        return buf;
        }
      }
    else
      {
      if( !resize_buffer( &buf, &bufsz, i + 2 ) )
        { if( lenp ) *lenp = 0; return 0; }
      buf[i++] = c; if( !c ) set_binary(); if( c != '\n' ) continue;
      buf[i] = 0; if( lenp ) *lenp = i;
      return buf;
      }
    }
  }


/* read a line of text from a stream */
static const char * read_stream_line( FILE *fp, int *lenp, char *newline_added_now )
  {
  static char *buf = 0;
  static int bufsz = 0;
  int c, i = 0;

  while( 1 )
    {
    if( !resize_buffer( &buf, &bufsz, i + 2 ) ) return 0;
    c = getc( fp ); if( c == EOF ) break;
    buf[i++] = c;
    if( !c ) set_binary(); else if( c == '\n' ) break;
    }
  buf[i] = 0;
  if( c == EOF )
    {
    if( ferror( fp ) )
      {
      show_strerror( 0, errno );
      set_error_msg( "Cannot read input file" );
      return 0;
      }
    else if( i )
      {
      buf[i] = '\n'; buf[i+1] = 0; *newline_added_now = 1;
      if( !isbinary() ) ++i;
      }
    }
  *lenp = i;
  return buf;
  }


/* read a stream into the editor buffer; return size of data read */
static long read_stream( FILE *fp, const int addr )
  {
  line_t *lp = search_line_node( addr );
  undo_t *up = 0;
  long size = 0;
  const char o_isbinary = isbinary();
  const char appended = ( addr == last_addr() );
  char newline_added_now = 0;

  set_current_addr( addr );
  while( 1 )
    {
    int len = 0;
    const char *buf = read_stream_line( fp, &len, &newline_added_now );
    if( !buf ) return -1;
    if( len > 0 ) size += len; else break;
    disable_interrupts();
    if( !put_sbuf_line( buf, current_addr() ) )
      { enable_interrupts(); return -1; }
    lp = lp->q_forw;
    if( up ) up->tail = lp;
    else if( !( up = push_undo_atom( UADD, -1, -1 ) ) )
      { enable_interrupts(); return -1; }
    enable_interrupts();
    }
  if( addr && appended && size && o_isbinary && newline_added() )
    fputs( "Newline inserted\n", stderr );
  else if( newline_added_now && appended )
    fputs( "Newline appended\n", stderr );
  if( isbinary() && !o_isbinary && newline_added_now && !appended ) ++size;
  if( !size ) newline_added_now = 1;
  if( appended && newline_added_now ) set_newline_added();
  return size;
  }


/* read a named file/pipe into the buffer; return line count */
int read_file( const char *filename, const int addr )
  {
  FILE *fp;
  long size;

  if( *filename == '!' ) fp = popen( filename + 1, "r" );
  else fp = fopen( strip_escapes( filename ), "r" );
  if( !fp )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot open input file" );
    return -1;
    }
  if( ( size = read_stream( fp, addr ) ) < 0 ) return -1;
  if( ( (*filename == '!' ) ? pclose( fp ) : fclose( fp ) ) < 0 )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot close input file" );
    return -1;
    }
  if( !scripted() ) fprintf( stderr, "%lu\n", size );
  return current_addr() - addr;
  }


/* write a range of lines to a stream */
static long write_stream( FILE *fp, int from, const int to )
  {
  line_t *lp = search_line_node( from );
  long size = 0;

  while( from && from <= to )
    {
    int len;
    char *s = get_sbuf_line( lp );
    if( !s ) return -1;
    len = lp->len;
    if( from != last_addr() || !isbinary() || !newline_added() )
      s[len++] = '\n';
    size += len;
    while( --len >= 0 )
      if( fputc( *s++, fp ) < 0 )
        {
        show_strerror( 0, errno );
        set_error_msg( "Cannot write file" );
        return -1;
        }
    ++from; lp = lp->q_forw;
    }
  return size;
  }


/* write a range of lines to a named file/pipe; return line count */
int write_file( const char * const filename, const char * const mode,
                const int from, const int to )
  {
  FILE *fp;
  long size;

  if( *filename == '!' ) fp = popen( filename + 1, "w" );
  else fp = fopen( strip_escapes( filename ), mode );
  if( !fp )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot open output file" );
    return -1;
    }
  if( ( size = write_stream( fp, from, to ) ) < 0 ) return -1;
  if( ( (*filename == '!' ) ? pclose( fp ) : fclose( fp ) ) < 0 )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot close output file" );
    return -1;
    }
  if( !scripted() ) fprintf( stderr, "%lu\n", size );
  return ( from && from <= to ) ? to - from + 1 : 0;
  }
