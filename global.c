/* global.c: global command routines for the ed line editor */
/* GNU ed - The GNU line editor.
   Copyright (C) 1993, 1994 Andrew L. Moore, Talke Studio
   Copyright (C) 2006-2026 Antonio Diaz Diaz.

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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "ed.h"


/* list of lines active in a global command */
static const line_node **active_list = 0;
static int active_size = 0;	/* capacity (in lines) of active_list */
static int active_len = 0;	/* number of lines in active_list */
static int active_idx = 0;	/* active_list index ( non-decreasing ) */
static int active_idxm = 0;	/* active_list index ( modulo active_len ) */


/* clear the global-active list */
void clear_active_list( void )
  {
  disable_interrupts();
  if( active_list ) free( active_list );
  active_list = 0;
  active_size = active_len = active_idx = active_idxm = 0;
  enable_interrupts();
  }


/* return the next global-active line node */
const line_node * next_active_node( void )
  {
  while( active_idx < active_len && !active_list[active_idx] )
    ++active_idx;
  return ( active_idx < active_len ) ? active_list[active_idx++] : 0;
  }


/* add a line node to the global-active list */
bool set_active_node( const line_node * const lp )
  {
  if( active_len >= active_size )
    {
    enum { max_lines = INT_MAX / sizeof active_list[0] };
    if( active_len >= max_lines )
      { set_error_msg( "Too many matching lines" ); return false; }
    const int new_size = ( ( active_len < 64 ) ? 64 :
      ( active_len >= max_lines / 2 ) ? max_lines : active_len * 2 );
    void * new_buf = 0;
    disable_interrupts();
    if( !active_list ) new_buf = malloc( new_size * sizeof active_list[0] );
    else new_buf = realloc( active_list, new_size * sizeof active_list[0] );
    if( !new_buf )
      { show_strerror( 0, errno );
        set_error_msg( mem_msg ); enable_interrupts(); return false; }
    active_size = new_size;
    active_list = (const line_node **)new_buf;
    enable_interrupts();
    }
  active_list[active_len++] = lp;
  return true;
  }


/* remove a range of lines from the global-active list */
void unset_active_nodes( const line_node * bp, const line_node * const ep )
  {
  while( bp != ep )
    {
    int i;
    for( i = 0; i < active_len; ++i )
      {
      if( ++active_idxm >= active_len ) active_idxm = 0;
      if( active_list[active_idxm] == bp )
        { active_list[active_idxm] = 0; break; }
      }
    bp = bp->q_forw;
    }
  }
