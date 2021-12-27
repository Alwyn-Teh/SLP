/* EDITION AC10 (REL002), ITD ACST.174 (95/05/28 20:26:30) -- OPEN */
/* PLS */
#ifndef lint
static char resid[] =
"$Id: getline.c,v 2.1 1991/11/25 15:58:19 thewalt Exp thewalt $";
#endif

/* Modified:
 *	30/07/92 - GJB ;	Prefixed "Slp_" to exported functions.
 *	04/01/94 - ACST ;	Put keystrokes in help text for export.
 *	13/01/94 - ACST ;	Put copyright information in help text	form.
 *	24/03/95 - ACST ;	Convert to ANSI C; tidy up headers and	declarations.
 *						Fix core dump due to #ifdef unix, need unix for
 *						ANSI C compiler.
 *	11/05/95 - ACST ;	Export gl_char_init() and gl_char_cleanup() via
 *						Slp_CharEchoOff() and Slp_CharEchoOn() to enable
 *						character echo for the duration	of Tcl_Eval()
 *						calls - e.g. when Tcl command “gets stdin" is used.
 *						In future Tcl releases, this may be changed to
 *						be invoked by command tracing.
 *	29/05/95 - ACST ;	reset gl_pos and gl_cnt to zero in gl_newline()
 *						fixes bug where gl_cnt > 0 causes line redraw
 *						of previous line
 */

/*
 *	Fairly portable (ANSI C), emacs style line editing input package.
 *	This package uses \b to move, and \007 to ring the bell.
 *	It uses a fixed screen width, as initialized in the gl_init() call,
 *	and does not draw in the last location to avoid line wraps.
 *	The only non-portable part is how to turn off character echoing.
 *	This code works for *NIX of BSD or SYSV flavor, as well as MSDOS (MSC6.0).
 *	No TERMCAP features are used, so long lines are scrolled on one line
 *	rather than extending over several lines. The function getline
 *	returns a pointer to a static buffer area which holds the input string,
 *	including the newline. On EOF the first character is set equal to '\0'.
 *	The caller supplies a desired prompt, as shown in the prototype:
 *
 *	char *getline(char *prompt)
 *
 *	Getline is different from GNU readline in that:
 *		- getline has much more limited editing capabilities, but it
 *		  is also much smaller and doesn't need termcap.
 *		- you don't free the buffer when done, since it is static
 *		  (make a copy yourself if you want one)
 *		- the newline is appended to the buffer
 *		- you don't add lines to history, it is done automatically.
 *
 *	The function gl_init(int screen_width) should be called before
 *	calling getline(char *prompt), and gl_cleanup(void) should be
 *	called before exiting. The function gl_redraw(void) may also
 *	be called to redraw the screen (as is done when AL is read).
 *	The function gl_replace() is also user callable and toggles getline
 *	between insert and replace (overwrite) mode. Getline begins in insert
 *	mode.
 *
 *	The editing keys are:
 *	^A, ^E	-	move to beginnning or end of line, respectively.
 *	^F, ^B	-	nondestructive move forward or back one location, respectively.
 *	^D		-	delete the character currently under the cursor, or
 *				send EOF if no characters in the buffer.
 *	^G		-	toggle replace (overwrite) mode, initally in insert mode.
 *	^H, DEL -	delete character left of the cursor.
 *	^K		-	delete from cursor to end of line.
 *	^L		-	redraw the current line.
 *	^P, ^N	-	move through history, previous and next, respectively.
 *	^T		-	transpose chars under and to left of cursor
 *	TAB		-	call user defined function if bound, or insert spaces
 *				to get to next TAB stop (just past every 8th column).
 *	NL, CR	-	places line on history list if nonblank, calls output
 *				cleanup function if bound, appends newline and returns
 *				to the caller.
 *	arrow keys - appropriate motion
 *
 *	In addition, the caller can modify the buffer in certain ways, which
 *	may be useful for things like auto-indent modes. There are three
 *	function pointers which can be bound to user functions.
 *	Each of these functions must return the index of the first location
 *	at which the buffer was modified, or -1 if the buffer wasn't modified.
 *	Each of the functions receive the current input buffer as the first
 *	argument. The screen is automatically cleaned up if the buffer is changed.
 *	The user is responsible for not writing beyond the end of the static
 *	buffer. The function pointer prototypes are:
 *
 *	int (*gl_in_hook)(char *buf)
 *		- called on entry to getline, and each time a new history
 *		  string is loaded, from a ~P or ~N. Initially NULL.
 *	int (*gl_out_hook)(char *buf)
 *		- called when a \n or \r is read, before appending \n and
 *		  returning to caller. Initially NULL.
 *	int (*gl_tab_hook)(char *buf, int offset, int *loc)
 *		- called whenever a TAB is read. The second argument is
 *		  the current line offset due to the width of the prompt.
 *		  The third argument is a pointer to the variable holding the
 *		  current location in the buffer. The location may be reset
 *		  by the user to move the cursor when the call returns.
 *		  Initially a built in tabbing function is bound.
 *
 *	Please send bug reports, fixes and enhancements to Chris Thewalt,
 *	thewalt@ce.berkeley.edu
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "slp.h"

char * Slp_KeystrokesHelpText[] = {
"Command line keystrokes for editing and history/retrieval -\n",
"^A, ^E		-	move to beginning or end of line, respectively.",
"^F, ^B		-	nondestructive move forward or back one	location, respectively.",
"^D			-	delete the character currently under the cursor, or",
"				send EOF if no characters in the buffer.",
"^G			-	toggle replace (overwrite) mode, initially in insert mode.",
"^H, DEL	-	delete character left of the cursor.",
"^K			-	delete from cursor to end of line.",
"^L			-	redraw the current line.",
"^P, ^N		-	move through history, previous and next, respectively.",
"^T			-	transpose characters under and to left of cursor",
"TAB		-	move to next TAB stop (just past every 8th column)",
"NL, CR		-	places line on history list if non-blank,",
"				appends newline and returns to the caller.",
"arrow keys	-	appropriate motion: up (^P), down (^N) , left ^B), right (^F)",
0
};

static char *copyright = "Copyright (C) 1991, Chris Thewalt";

/*
*	Copyright (C) 1991 by Chris Thewalt
★
*	Permission to use, copy, modify, and distribute this software
*	for any purpose and without fee is hereby granted, provided
*	that the above copyright notices appear in all copies and that both the
*	copyright notice and this permission notice appear in supporting
*	documentation. This software is provided "as is" without express or
*	implied warranty.
*/
char * Slp_Copyright[] = {
	0, /* place-holder for Slp_VersionInfo[0] */
	"",
	" getline is a emacs-style line editing input package.\n",

	" getline.c - Copyright (C) 1991 by Chris Thewalt\n",

	"	Permission to use, copy, modify, and distribute this",
	"	software for any purpose and without fee is hereby granted,",
	"	provided that the above copyright notices appear in all",
	"	copies and that both the copyright notice and this",
	"	permission notice appear in supporting documentation. This",
	"	software is provided \"as is\" without express or implied",
	"	warranty.\n",

	"	thewalt@ce.berkeley.edu",
	"	University of California, Berkeley, CA 94720, U.S.A.",
	"	Anon FTP - ce.berkeley.edu:/pub/thewalt/getline.tar.Z",
	0
};

char * Slp_VersionInfo[] = {
#ifndef PLSID_DEFINED
	/*
	 *	SLP.ac?? MUST be the one and only AND last string in the
	 *	version info msg because it is parsed in slptclfe.c
	 */
		"getline.c v2.1 incorporated in SLP.ac09",
#else
		"getline.c v2.1 incorporated in SLP.%s\0\0",
#endif
	0, /* place-holder to say which Tcl version SLP uses (see slptclfe.c) */
	0
};

#define BUF_SIZE	1024
#define SCROLL		30

extern int	Slp_Window_Resize_Detected;
extern int	Slp_Command_In_Progress;

static int	gl_init_done = 0;	/* -1 is terminal, 1 is batch */
static int	gl_screen = 80;		/* screen width */
static int	gl_width = 0;		/* net size available for input */
static int	gl_extent =0;		/* how far to redraw, 0	means all */
static int	gl_overwrite = 0;	/* overwrite mode */
static int	gl_pos, gl_cnt = 0;	/* position and size of	input */
static char	gl_buf[BUF_SIZE];	/* input buffer */
static char	*gl_prompt = NULL;	/* to save the prompt string */

void	Slp_gl_init ANSI_PROTO((int));
void	Slp_gl_cleanup ANSI_PROTO((void));	/* to undo gl_init */
void	Slp_ql_redraw ANSI_PROTO ((void));	/* issue \n and redraw all */
void	Slp_gl_replace ANSI_PROTO((void));	/* toggle replace/insert mode */

static void gl_char_init ANSI_PROTO ((void)) ;	/* get ready for no echo input */
void (*Slp_CharEchoOff) ANSI_PROTO((void)) = gl_char_init;

static void gl_char_cleanup ANSI_PROTO((void)); /* undo gl_char_init */
void (*Slp_CharEchoOn) ANSI_PROTO((void)) = gl_char_cleanup;

static int	gl_getc	ANSI_PROTO((void));			/* read one char from terminal	*/
static void	gl_putc	ANSI_PROTO((int));			/* write one char to terminal */
static void	gl_gets	ANSI_PROTO((char *,	int));	/* get a line from terminal */
static void	gl_puts	ANSI_PROTO((char *));		/* write a line to terminal */
static void gl_addchar ANSI_PROTO((int));		/* install specified char */
static void gl_transpose ANSI_PROTO((void));	/* transpose two chars */
static void gl_newline ANSI_PROTO((void));		/* handle \n or \r */
static void gl_fixup ANSI_PROTO((int, int));	/* fixup state variables and screen */

static void gl_del ANSI_PROTO((int)); /* del, either left (-1) or cur (0) */
static void gl_kill ANSI_PROTO((void)); /* delete to EOL */
static int gl_tab ANSI_PROTO((char *, int, int *)); /* default TAB handler */

static void hist_add ANSI_PROTO((void));	/* adds nonblank entries to hist */
static void hist_init ANSI_PROTO((void));	/* initializes hist pointers */
static void hist_next ANSI_PROTO((void));	/* copies next entry to input buf */
static void hist_prev ANSI_PROTO((void));	/* copies prev entry to input buf */
static char *hist_save ANSI_PROTO((char *));/* makes copy of a string */

int (*gl_in_hook) ANSI_PROTO((char *)) = 0;
int (*gl_out_hook) ANSI_PROTO((char *)) = 0;
int (*gl_tab_hook) ANSI_PROTO((char *, int, int *)) = gl_tab;

/************************* nonportable part *********************************/

#ifdef MSDOS
#include <bios.h>
#endif

#if defined(unix) || defined(__unix)
#	include <sys/ioctl.h>
#	ifndef TIOCGETP
#		include <termio.h>
		static struct termio tty, orig_tty;
#	else
#		include <sgtty.h>
		static struct sgttyb tty, orig_tty;
#	endif /* TIOCGETP */
#endif /* unix | | __unix */

#if defined(__APPLE__)
#	include <sys/ioctl.h>
#	include <termios.h>
#endif

#ifdef vms
#include <descrip.h>
#include <ttdef.h>
#include <iodef.h>
/* #include unixio - PLS doesn't like this - GJB */

static int setbuff[2];				/* buffer to set terminal attributes */
static short chan = -1;				/* channel to terminal */
struct dsc$descriptor_s descrip;	/* VMS descriptor */
#endif

static void
gl_char_init()
/* turn off input echo */

{
#if defined(unix) || defined(__unix)
	static int first_time = 1;
#ifdef TIOCGETP
	if (first_time) {
	  ioctl(0, TIOCGETP, &orig_tty);
	  tty = orig_tty;
	  first_time = 0;
	}
	else
	  tty = orig_tty;
	tty.sg_flags |= CBREAK;
	tty.sg_flags &= ~ECHO;
	ioctl(0, TIOCSETP, &tty);
#else
	if (first_time) {
	  ioctl(0, TCGETA, &orig_tty);
	  tty = orig_tty;
	  first_time = 0;
	}
	else
	  tty = orig_tty;
	tty. c_lflag &= ~(ICANON|ECHO|ECHOE|ECHOK|ECHONL);
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 0;
	ioctl(0, TCSETA, &tty);
#endif
#endif /* unix || unix */

#ifdef vms
	descrip.dsc$w_length = strlen("tt:");
	descrip.dsc$b_dtype = DSC$K_DTYPE_T;
	descrip.dsc$b_class = DSC$K_CLASS_S;
	descrip.dsc$a_pointer = "tt:" ;
	(void)sys$assign(&descrip,&chan,0,0);
	(void)sys$qiow(0, chan,IO$_SEKSEMODE,0,0,0,setbuff,8,0,0,0,0);
	setbuff[1] |= TT$M_NOECHO;
	(void)sys$qiow(0,chan,IO$_SETMODE,0,0,0,setbuff,8,0,0,0,0);
#endif /* vms */
}

static void
gl_char_cleanup()
/* undo effects of gl_char_init, as necessary */
{
#if defined(unix) || defined(__unix)
#ifdef TIOCSETP
	ioctl(0, TIOCSETP, &orig_tty);
#else
	ioctl(0, TCSETA, &orig_tty);
#endif
#endif /* unix || __unix */

#ifdef vms
	setbuff[1] &= ~TT$M_NOECHO;
	(void)sys$qiow(0,chan,I0$_SETMODE,0,0,0,setbuff,8,0,0,0,0);
	sys$dassgn(chan);
	chan = -1;
#endif
}

static int
gl_getc ()
/* get a character without echoing it to screen */
{
	int		c;
	char	ch;

#if defined(unix) || defined(__unix) || defined(__APPLE__)
	do {
		c = 0;
		ch = '\0';
		errno = 0;

		c = ((read(0, &ch, 1)) > 0) ? ch : -1;

	} while (c == -1 && errno == EINTR);
#endif

#ifdef MSDOS
	c = _bios_keybrd(_NKEYBRD_READ);
	if ((c & 0377) == 224) {
		switch (c = (c >> 8) & 0377) {
			case 72: c = 16;	/* up -> ^P */
			break;
		case 80: c = 14;		/* down -> ^N */
			break;
		case 75: c = 2;			/* left -> ^B */
			break;
		case 77: c = 6;			/* right -> ^F */
			break;
		default: c = 254;		/* make it garbage */
		}
	} else {
		c = c & 0377;
	}
#endif

#ifdef vms
	if (chan < 0) {
	  c='\0';
	}
	(void)sys$qiow(0,chan,IO$_TTYREADALL,0,0,0,&c,1,0,0,0,0);
	c &= 0177;	/* get a char */
#endif
	return c;
}

static void
#ifdef __STDC__
gl_putc(int c)
#else
gl_putc(c)
int c;
#endif
{
	char ch = c;

	write (1, &ch, 1);
}

/******************** fairly portable part *********************************/

static void
#ifdef __STDC__
gl_gets(char *buf, int size) /* slow, only used in batch mode */
#else
gl_gets(buf, size)	/* slow, only used in batch mode */
char *buf;
int size;
#endif
{
	char *p = buf;
	int c;

	size -= 1;
	while (buf + size > p && (c = gl_getc()) > 0) {
		*p++ = c;
		if (c == '\n')
		  break;
	}
	*p = 0;
}

static void
#ifdef ___STDC__
gl_puts(char *buf)
#else
gl_puts(buf)
char *buf;
#endif
{
	while (*buf)
		gl_putc(*buf++);
}

void
#ifdef __STDC__
Slp_gl_init(int scrn_wdth)
#else
Slp_gl_init(scrn_wdth)
int scrn_wdth;
#endif
/* set up variables and terminal; also, handle window resize */
{
	gl_screen = scrn_wdth;

	if (gl_init_done == 0) {
	  hist_init();
	  if (isatty(0)) {
	    gl_char_init();
	    gl_init_done = -1; /* -1 means terminal */
	  } else {
	    gl_init_done =1;	/* 1 means batch */
	  }
	}

	gl_prompt = Slp_GetPrompt();
	gl_width = gl_screen - strlen((gl_prompt == NULL) ? "" : gl_prompt);

	/*
		Redraw line upon window resize and...
		if input in progress and...
		if command is not in progress.
	 */
	if (Slp_Window_Resize_Detected &&
		gl_init_done == -1 && gl_cnt > 0 &&
		!Slp_Command_In_Progress)
	{
		register int x;

		/* Blank out input line. */
		gl_putc('\r'); /* Carriage return */
		for (x = 0; x < gl_screen; x++) gl_putc(' ');
		gl_putc('\r'); /* Carriage return */
		fflush(stdout);

		/* Buggy editing if resized window falls below 50 columns. */
		if (gl_screen < 50)
		{
			/* Redraw line with cursor at end */
			gl_putc('\007'); /* warn user */
			gl_puts(gl_prompt);
			fflush(stdout);
			gl_pos = 0;
			gl_extent = 0;
			gl_fixup(0, BUF_SIZE);
			fflush(stdout);
		}
		else
		{
			/* Redraw line with cursor at same position */
			gl_extent = 0;
			gl_puts(gl_prompt);
			fflush(stdout);
			gl_fixup(0, gl_pos);
			fflush(stdout);
		}
	}
	else
		gl_pos = gl_cnt = 0;
}

void
Slp_gl_cleanup()
/* undo effects of gl_init, as necessary */
{
	if (gl_init_done == -1)
	  gl_char_cleanup();

	gl_init_done = 0;
}

char *
#ifdef __STDC__
Slp_getline(char *prompt)
#else
Slp_getline(prompt)
char *prompt;
#endif
{
	int	c, loc, tmp;

	if (!gl_init_done)
	  Slp_gl_init(80);

	gl_buf[0] = 0;				/* used as end of input indicator */
	memset(gl_buf, '\0', BUF_SIZE);

	if (gl_init_done == 1) {	/* no input editing, and no prompt output */
	  gl_gets(gl_buf, BUF_SIZE);
	  return gl_buf;
	}

	gl_fixup(-1, 0);	/* this resets gl_fixup */
	gl_width = gl_screen - strlen(prompt);
	if (prompt == 0)
	  prompt = "";
	gl_prompt = prompt;
	gl_pos = gl_cnt = 0;
	/* Ditched, because using ipc_mainloop() to drive input handler -- GJB
	gl_puts(prompt);
	 */
	gl_puts(prompt); // ACST - 26/12/2021
	if (gl_in_hook) {
	  loc = gl_in_hook(gl_buf);
	  if (loc >= 0)
		gl_fixup (0, BUF_SIZE);
	}

	while ((c = gl_getc()) >= 0) {
		gl_extent = 0;
		if (isprint(c)) {
			gl_addchar(c);
		} else {
			switch (c) {
				case '\n': case '\r':	/* newline */
					gl_newline();
					return gl_buf;
					break;
				case '\001': gl_fixup(-1, 0);			/* ^A */
					break;
				case '\002': gl_fixup(-1, gl_pos-1);	/* ^B */
					break;
				case '\004':							/* ^D */
					if (gl_cnt == 0) {
						gl_buf[0] = 0;
						Slp_gl_cleanup();
						gl_putc ('\n') ;
						return gl_buf;
					} else {
						gl_del(0);
					}
					break;
				case '\005': gl_fixup(-1, gl_cnt);		/* ^E */
					break;
				case '\006': gl_fixup(-1, gl_pos+1);	/* ^F */
					break;
				case '\007': Slp_gl_replace();			/* ^G */
					break;
				case '\010': case '\177': gl_del(-1);	/* ^H and DEL */
					break;
				case '\t':								/* TAB */
					if (gl_tab_hook) {
						tmp = gl_pos;
						loc = gl_tab_hook(gl_buf, strlen(gl_prompt), &tmp);
						if (loc >= 0 || tmp != gl_pos)
							gl_fixup(loc, tmp);
					}
					break;
				case '\013': gl_kill();					/* ^K */
					break;
				case '\014': Slp_gl_redraw();			/* ~L */
					break;
				case '\016': hist_next();				/* ^N */
					break;
				case '\020': hist_prev();				/* ^P */
					break;
				case '\024': gl_transpose();			/* ^T */
					break;
				case '\033':	/* ansi arrow keys */
					c = gl_getc();
					if (c == '[' || c == '0') {
						switch(c = gl_getc()) {
							case 'A': hist_prev();		/* up */
								break;
							case 'B': hist_next();		/* down */
								break;
							case 'C': gl_fixup(-1, gl_pos+1); /* right */
								break;
							case 'D': gl_fixup(-1, gl_pos-1); /* left */
								break;
							default: gl_putc('\007') ;	/* who knows */
								break;
						}
					} else
						gl_putc('\007');
					break;
				default:
					gl_putc('\007');
					break;
			}
		}
	}

	Slp_gl_cleanup();

	return gl_buf;
}

static void
#ifdef __STDC__
gl_addchar(int c)
#else
gl_addchar(c)
int c;
#endif
/* adds the character c to the input buffer at current location */
{
	int i ;

	if (gl_cnt >= BUF_SIZE - 1) {
		gl_puts("Slp_getline: input buffer overflow\n");
		_exit(1);
	}
	if (gl_overwrite == 0 || gl_pos == gl_cnt) {
		for (i=gl_cnt; i >= gl_pos; i--)
			gl_buf[i+1] = gl_buf[i];
		gl_buf[gl_pos] = c;
		gl_fixup(gl_pos, gl_pos+1);
	} else {
		gl_buf[gl_pos] = c;
		gl_extent = 1;
		gl_fixup(gl_pos, gl_pos+1);
	}
}

static void
#ifdef __STDC__
gl_transpose(void)
#else
gl_transpose()
#endif
/* switch character under cursor and to left of cursor */
{
	int	c;

	if (gl_pos > 0 && gl_cnt > gl_pos) {
		c = gl_buf[gl_pos-1];
		gl_buf[gl_pos-1] = gl_buf[gl_pos];
		gl_buf[gl_pos] = c;
		gl_extent = 2;
		gl_fixup(gl_pos-1, gl_pos);
	} else
		gl_putc('\007');
}

void
Slp_gl_replace()
{
	gl_overwrite = !gl_overwrite;
}

static void
gl_newline()
/*
 *	Cleans up entire line before returning to caller. A \n is appended.
 *	If line longer than screen, we redraw starting at beginning
 */
{
	int change = gl_cnt;
	int len = gl_cnt;
	int loc = gl_width - 5;	/* shifts line back to start position */

	if (gl_cnt >= BUF_SIZE - 1) {
		gl_puts("Slp_qetline: input buffer overflow\n");
		_exit(1);
	}
	hist_add() ;	/* only adds if nonblank */
	if (gl_out_hook) {
		change = gl_out_hook(gl_buf);
		len = strlen(gl_buf);
	}
	if (loc > len)
		loc = len;
	gl_fixup(change, loc);	/* must do this before appending \n */
	gl_buf[len] = '\n';
	gl_buf[len+1] = '\0';
	gl_putc('\n');
	gl_pos = gl_cnt = 0;
}

static void
#ifdef __STDC__
gl_del(int loc)
#else
gl_del(loc)
int loc;
#endif
/*
 *	Delete a character. The loc variable can be:
 *		-1 : delete character to left of cursor
 *		 0 : delete character under cursor
 */
{
	int i;

	if ((loc == -1 && gl_pos > 0) || (loc == 0 && gl_pos < gl_cnt)) {
		for (i=gl_pos+loc; i < gl_cnt; i++)
			gl_buf[i] = gl_buf[i+1];
		gl_fixup(gl_pos+loc, gl_pos+loc);
	} else
		gl_putc('\007');
}

static void
gl_kill ()
/* delete from current position to the end of line */
{
	if (gl_pos < gl_cnt) {
		gl_buf[gl_pos] = '\0';
		gl_fixup(gl_pos, gl_pos);
	} else
		gl_putc('\007');
}

void
Slp_gl_redraw()
/* emit a newline, reset and redraw prompt and current input line */
{
	if (gl_init_done == -1) {
		gl_putc('\n');
		gl_puts(gl_prompt);
		gl_pos = 0;
		gl_fixup(0, BUF_SIZE);
	}
}

static void
#ifdef __STDC__
gl_fixup(int change, int cursor)
#else
gl_fixup(change, cursor)
int change, cursor;
#endif
/*
 *	This function is used both for redrawing when input changes or for
 *	moving within the input line. The parameters are:
 *	change : the index of the start of changes in the input buffer,
 *	with -1 indicating no changes.
 *	cursor : the desired location of the cursor after the call.
 *	A value of BUF_SIZE can be used to indicate the cursor should
 *	move just past the end of the input line.
 */
{
	static int	gl_shift = 0;	/* index of first on screen character */
	static int	off_right = 0;	/*	true if more text right of screen */
	static int	off_left = 0;	/*	true if more text left of screen */
	int			left = 0, right = -1;	/* bounds for redraw */
	int			pad;			/*	how much to erase at end of line */
	int			backup;			/*	how far to backup before fixing */
	int			new_shift;		/*	value of shift based on cursor */
	int			extra;			/* adjusts when shift (scroll) happens */
	int			i;
	int			new_right;		/* alternate right bound, using gl_extent */

	if (change == -1 && cursor == 0 && gl_buf[0] == 0) {	/* reset */
		gl_shift = off_right = off_left = 0;
		return;
	}
	pad = (off_right)? gl_width - 1 : gl_cnt - gl_shift; /* old length */
	backup = gl_pos - gl_shift;
	if (change >= 0) {
		gl_cnt = strlen(gl_buf);
		if (change > gl_cnt)
			change = gl_cnt;
	}
	if (cursor > gl_cnt) {
	if (cursor != BUF_SIZE)	/* BUF_SIZE means end of line */
		gl_putc('\007');
	cursor = gl_cnt;
	}
	if (cursor < 0) {
		gl_putc('\007');
		cursor = 0;
	}
	if (off_right || (off_left && cursor < gl_shift + gl_width - SCROLL / 2))
		extra =2;							/* shift the scrolling boundary */
	else
		extra =0;
	new_shift = cursor + extra + SCROLL - gl_width;
	if (new_shift > 0) {
		new_shift /= SCROLL;
		new_shift *= SCROLL;
	} else
		new_shift = 0;
	if (new_shift != gl_shift) {	/* scroll occurs */
		gl_shift = new_shift;
		off_left = (gl_shift) ? 1 : 0;
		off_right = (gl_cnt > gl_shift + gl_width - 1) ? 1 : 0;
		left = gl_shift;
		new_right = right = (off_right) ? gl_shift + gl_width - 2 : gl_cnt;
	} else if (change >= 0) {	/* no scroll, but text changed */
		if (change < gl_shift + off_left) {
			left = gl_shift;
		} else {
			left = change;
			backup = gl_pos - change;
		}
		off_right = (gl_cnt > gl_shift + gl_width - 1) ? 1 : 0;
		right = (off_right)? gl_shift + gl_width - 2 : gl_cnt;
		new_right = (gl_extent && (right > left + gl_extent)) ?
					left + gl_extent : right;
	}
	pad -= (off_right)? gl_width - 1 : gl_cnt - gl_shift;
	pad = (pad < 0)? 0 : pad;
	if (left <= right) {	/* clean up screen */
		if (!Slp_Window_Resize_Detected)
			for (i=0; i < backup; i++)
				gl_putc('\b');
		if (left == gl_shift && off_left) {
			gl_putc ('$');
			left++;
		}
		for (i=left; i < new_right; i++)
			gl_putc(gl_buf[i]);
		gl_pos = new_right;
		if (off_right && new_right == right) {
			gl_putc('$');
			gl_pos++;
		} else
			if (!Slp_Window_Resize_Detected) {
				for (i=0; i < pad; i++)	/* erase remains of prev line */
					gl_putc(' ');
				gl_pos += pad;
			}
	}
	i = gl_pos - cursor;	/* move to final cursor location */
	if (i > 0) {
		while (i--)
			gl_putc('\b');
	} else {
		for (i=gl_pos; i < cursor; i++)
			gl_putc(gl_buf[i]);
	}
	gl_pos = cursor;
}

static int
#ifdef __STDC__
gl_tab(char *buf, int offset, int *loc)
#else
gl_tab(buf, offset, loc)
char *buf;
int offset, *loc;
#endif
/* default tab handler, acts like tabstops every 8 cols */
{
	int i, count, len;

	len = strlen(buf);
	count = 8 - (offset + *loc) % 8;
	for (i=len; i >= *loc; i--)
		buf[i+count] = buf[i];
	for (i=0; i < count; i++)
		buf[*loc+i] = ' ';
	i = *loc;
	*loc = i + count;
	return i;
}

/******************* History stuff **************************************/

#ifndef HIST_SIZE
#define HIST_SIZE 100
#endif

int		hist_pos, hist_last;
char	*hist_buf[HIST_SIZE];

static void
hist_init()
{
	int i;

	for (i=0; i < HIST_SIZE; i++)
		hist_buf[i] = (char *)0;
}

static void
hist_add()
{
	char *p = gl_buf;

	while (*p == ' ' || *p == '\t')	/* only save nonblank line */
		p++;
	if (*p) {
		hist_buf[hist_last] = hist_save(gl_buf);
		hist_last = (hist_last +1) % HIST_SIZE;
		if (hist_buf[hist_last]) {	/* erase next location */
			free(hist_buf[hist_last]);
			hist_buf[hist_last] = 0;
		}
	}
	hist_pos = hist_last;
}

static void
hist_prev()
/* loads previous hist entry into input buffer, sticks on first */
{
	int next;

	next = (hist_pos - 1 + HIST_SIZE) % HIST_SIZE;
	if (next != hist_last) {
		if (hist_buf[next]) {
			hist_pos = next;
			strcpy(gl_buf, hist_buf[hist_pos]);
		} else
			gl_putc('\007');
	} else
		gl_putc('\007');
	if (gl_in_hook)
		gl_in_hook(gl_buf);
	gl_fixup(0, BUF_SIZE);
}

static void
hist_next()
/* loads next hist entry into input buffer, clears on last */
{
	if (hist_pos != hist_last) {
		hist_pos = (hist_pos + 1) % HIST_SIZE;
		if (hist_buf[hist_pos]) {
			strcpy(gl_buf, hist_buf[hist_pos]);
		} else {
			gl_buf[0] = 0;
		}
	} else
		gl_putc('\007') ;
	if (gl_in_hook)
		gl_in_hook(gl_buf);
	gl_fixup(0, BUF_SIZE);
}

static char *
#ifdef __STDC__
hist_save(char *p)
#else
hist_save(p)
char *p;
#endif
/* makes a copy of the string */
{
	char *s = 0;

	if (p && ((s = malloc(strlen(p)+1)) != 0)) {
		strcpy(s, p);
	}
	return s;
}
