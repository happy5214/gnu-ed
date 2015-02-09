/*  Global declarations for the ed editor.  */
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

enum Gflags
  {
  GLB = 0x01,			/* global command */
  GLS = 0x02,			/* list after command */
  GNP = 0x04,			/* enumerate after command */
  GPR = 0x08,			/* print after command */
  GSG = 0x10			/* global substitute */
  };


typedef struct line		/* Line node */
  {
  struct line *q_forw;
  struct line *q_back;
  long pos;			/* position of text in scratch buffer */
  int len;			/* length of line */
  }
line_t;


typedef struct
  {
  enum { UADD = 0, UDEL = 1, UMOV = 2, VMOV = 3 } type;
  line_t *head;			/* head of list */
  line_t *tail;			/* tail of list */
  }
undo_t;

#ifndef max
#define max( a,b ) ( (( a ) > ( b )) ? ( a ) : ( b ) )
#endif
#ifndef min
#define min( a,b ) ( (( a ) < ( b )) ? ( a ) : ( b ) )
#endif


/* defined in buffer.c */
char append_lines( const char *ibufp, const int addr, const char isglobal );
char close_sbuf( void );
char copy_lines( const int first_addr, const int second_addr, const int addr );
int current_addr( void );
int dec_addr( int addr );
char delete_lines( const int from, const int to, const char isglobal );
int get_line_node_addr( const line_t *lp );
char *get_sbuf_line( const line_t *lp );
int inc_addr( int addr );
int inc_current_addr( void );
char init_buffers( void );
char isbinary( void );
char join_lines( const int from, const int to, const char isglobal );
int last_addr( void );
char modified( void );
char move_lines( const int first_addr, const int second_addr, const int addr,
                 const char isglobal );
char newline_added( void );
char open_sbuf( void );
int path_max( const char *filename );
char put_lines( const int addr );
const char *put_sbuf_line( const char *cs, const int addr );
line_t *search_line_node( const int n );
void set_binary( void );
void set_current_addr( const int addr );
void set_modified( const char m );
void set_newline_added( void );
char yank_lines( const int from, const int to );
void clear_undo_stack( void );
undo_t *push_undo_atom( const int type, const int from, const int to );
void reset_undo_state( void );
char undo( const char isglobal );

/* defined in global.c */
void clear_active_list( void );
const line_t *next_active_node( void );
char set_active_node( const line_t *lp );
void unset_active_nodes( const line_t *np, const line_t *mp );

/* defined in io.c */
char display_lines( int from, const int to, const int gflags );
const char *get_extended_line( const char *ibufp, int *lenp, const char nonl );
const char *get_tty_line( int *lenp );
int read_file( const char *filename, const int addr );
int write_file( const char * const filename, const char * const mode,
                const int from, const int to );

/* defined in main.c */
char is_regular_file( int fd );
char may_access_filename( const char *name );
char restricted( void );
char scripted( void );
void show_strerror( const char *filename, int errcode );
char traditional( void );

/* defined in main_loop.c */
int main_loop( const char loose );
void set_def_filename( const char *s );
void set_error_msg( const char *msg );
void set_prompt( const char *s );
void set_verbose( void );
void unmark_line_node( const line_t *lp );

/* defined in regex.c */
char build_active_list( const char **ibufpp, const int first_addr,
                        const int second_addr, const char match );
char extract_subst_tail( const char **ibufpp, int *gflagsp, int *snump,
                         const char isglobal );
int get_matching_node_addr( const char **ibufpp, const char forward );
char new_compiled_pattern( const char **ibufpp );
char prev_pattern( void );
char search_and_replace( const int first_addr, const int second_addr,
                         const int gflags, const int snum, const char isglobal );

/* defined in signal.c */
void disable_interrupts( void );
void enable_interrupts( void );
char parse_int( int *i, const char *str, const char **tail );
char resize_buffer( char **buf, int *size, int min_size );
char resize_line_buffer( const line_t ***buf, int *size, int min_size );
char resize_undo_buffer( undo_t **buf, int *size, int min_size );
void set_signals( void );
void set_window_lines( const int lines );
const char *strip_escapes( const char *s );
int window_columns( void );
int window_lines( void );
