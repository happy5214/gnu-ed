/* Global declarations for the ed editor.  */
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

#include <stdbool.h>

enum Pflags			/* print suffixes */
  {
  pf_l = 0x01,			/* list after command */
  pf_n = 0x02,			/* enumerate after command */
  pf_p = 0x04			/* print after command */
  };


typedef struct line_node		/* Line node */
  {
  struct line_node * q_forw;
  struct line_node * q_back;
  long pos;			/* position of text in scratch buffer */
  int len;			/* length of line ('\n' is not stored) */
  }
line_node;


typedef struct				/* Undo atom */
  {
  enum { UADD = 0, UDEL = 1, UMOV = 2, VMOV = 3 } type;
  line_node * head;			/* head of list */
  line_node * tail;			/* tail of list */
  }
undo_atom;

#ifndef max
#define max( a, b ) ( (( a ) > ( b )) ? ( a ) : ( b ) )
#endif
#ifndef min
#define min( a, b ) ( (( a ) < ( b )) ? ( a ) : ( b ) )
#endif

static const char * const mem_msg = "Memory exhausted";
static const char * const no_prev_subst = "No previous substitution";

/* defined in buffer.c */
bool append_lines( const char ** const ibufpp, const int addr,
                   bool insert, const bool isglobal );
bool close_sbuf( void );
bool copy_lines( const int first_addr, const int second_addr, const int addr );
int current_addr( void );
int dec_addr( int addr );
bool delete_lines( const int from, const int to, const bool isglobal );
int get_line_node_addr( const line_node * const lp );
char * get_sbuf_line( const line_node * const lp );
int inc_addr( int addr );
int inc_current_addr( void );
bool init_buffers( void );
bool isbinary( void );
bool join_lines( const int from, const int to, const bool isglobal );
int last_addr( void );
bool modified( void );
bool warned( void );
bool move_lines( const int first_addr, const int second_addr, const int addr,
                 const bool isglobal );
bool open_sbuf( void );
int path_max( const char * filename );
bool put_lines( const int addr );
const char * put_sbuf_line( const char * const buf, const int size );
line_node * search_line_node( const int addr );
void set_binary( void );
void set_current_addr( const int addr );
void set_modified( const bool b );
void set_warned( const bool b );
bool yank_lines( const int from, const int to );
void clear_undo_stack( void );
undo_atom * push_undo_atom( const int type, const int from, const int to );
void reset_undo_state( void );
bool undo( const bool isglobal );

/* defined in global.c */
void clear_active_list( void );
const line_node * next_active_node( void );
bool set_active_node( const line_node * const lp );
void unset_active_nodes( const line_node * bp, const line_node * const ep );

/* defined in io.c */
bool get_extended_line( const char ** const ibufpp, int * const lenp,
                        const bool strip_escaped_newlines );
const char * get_stdin_line( int * const sizep );
int linenum( void );
bool print_lines( int from, const int to, const int pflags );
int read_file( const char * const filename, const int addr,
               bool * const read_onlyp );
int write_file( const char * const filename, const char * const mode,
                const int from, const int to );
void reset_unterminated_line( void );
void unmark_unterminated_line( const line_node * const lp );

/* defined in main.c */
bool extended_regexp( void );
bool interactive();
bool may_access_filename( const char * const name );
void print_filename( const char * const filename, const bool to_stdout );
bool restricted( void );
bool scripted( void );
void show_strerror( const char * const filename, const int errcode );
void show_warning( const char * const filename, const char * const msg );
bool strip_cr( void );
bool traditional( void );

/* defined in main_loop.c */
const char * error_msg( void );
int first_e_command( const char * const filename );
void invalid_address( void );
int main_loop( const bool initial_error, const bool loose );
bool set_def_filename( const char * const s );
void set_error_msg( const char * const msg );
bool set_prompt( const char * const s );
void set_verbose( void );
void unmark_line_node( const line_node * const lp );

/* defined in regex.c */
bool build_active_list( const char ** const ibufpp, const int first_addr,
                        const int second_addr, const bool match );
const char * get_pattern_for_s( const char ** const ibufpp );
bool extract_replacement( const char ** const ibufpp, const bool isglobal );
int next_matching_node_addr( const char ** const ibufpp );
bool search_and_replace( const int first_addr, const int second_addr,
                         const int snum, const bool isglobal );
bool set_subst_regex( const char * const pat, const bool ignore_case );
bool replace_subst_re_by_search_re( void );
bool subst_regex( void );

/* defined in signal.c */
void disable_interrupts( void );
void enable_interrupts( void );
const char * home_directory( void );
bool resize_buffer( char ** const buf, int * const size, const unsigned min_size );
void set_signals( void );
void set_window_lines( const int lines );
int window_columns( void );
int window_lines( void );
