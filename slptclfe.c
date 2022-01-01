/* EDITION AC10 (REL002), ITD ACST.174 (95/05/28 20:26:30) -- OPEN */
/* PLS */
/******************************************************************************\
********************************************************************************
**																			  **
**								SLPTCLFE									  **
**																			  **
********************************************************************************
\******************************************************************************/

/*+*****************************************************************************

NAME
	slptclfe - SLP's Tcl (Tool Command Language) Front-end.

DESCRIPTION
	This suite of routines provides the front-end to Professor Ousterhout's
	Tool Command Language (Tcl).
	Since Tcl does implement the mechanism for getting input from the user,
	this suite of routines goes out to achieve that, in the simple case
	when input and output is to and from standard I/O.

SEE ALSO
	Tcl

AUTHOR
	Greg Boyes

MODIFICATION HISTORY
	Greg Boyes 920730	Initial Creation

	Alwyn Teh 921123	Debugged Slp_SetPrompt(). Second call from
						Slp_InitStdio() removes prompt set by first
						call from application. This is because the
						argument prompt pointer passed to
						Slp_SetPrompt() is the same as the one that
						gets freed.

	Alwyn Teh 930628	Introduced (*Slp_Printf)() to support output
						paging by application.

	Alwyn Teh 930702	Ported to SunOS 4

	Alwyn Teh 930712	Make compatible with Tcl v6.7
						Turn flag USING_FULL_EVALFILE on,
						needs to use header tclInt.h however.

	Alwyn Teh 940112	Change "echo" command to concatenate argv tokens
						into interp->result instead of printf to stdout.
						This is so that the result can be used in command
						substitution.

	Alwyn Teh 940113	Initialize Slp_Versionlnfo and copyright information
						in Slp_InitTclInterp().

	Alwyn Teh 940711	Port to Tcl v7.3
	Alwyn Teh 950324	Port to ANSI C
	Alwyn Teh 950426	Handle window resize.
	Alwyn Teh 950825	Update Slp_TclEvalFile() with code from Tcl_EvalFile()
						in Tcl7.4
	Alwyn Teh 211227	Use Tcl_GetStringResult()

*****************************************************************************-*/

/********************************** DEFINES ***********************************/
#define DEF_PROMPT "Tcl-> "
// #define USING_FULL_EVALFILE
#define USE_INTERP_ERRORLINE

/********************************** INCLUDES **********************************/
#include <stdlib.h> /* For malloc(3C) */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <signal.h>
#include <sys/ioctl.h>
#ifndef TIOCGWINSZ
#  include <termios.h>
#endif

#ifdef USING_FULL_EVALFILE
#include <tclInt.h>
#endif

#include <tcl.h>
#include <tclDecls.h>
#include "slp.h"

EXTERN int TclUpdateReturnInfo(Tcl_Interp *interp);
// EXTERN int Tcl_GetErrorLine(Tcl_Interp *interp);

/****************************** GLOBAL VARIABLES ******************************/
/*
 *	Miscellaneous global variables grouped in a structure, to make subsequent
 *	use obvious. Defined in slp.h
 */

static struct
{
	char *prompt;		/*	Prompt being used on command line output	*/
	int (*cleanup_proc)();	/* Function to call, just before exiting	*/
	Tcl_Interp *interp; /* Currently active Tcl interpreter				*/
	Tcl_DString buffer; /* For input									*/
} slptclfe =
{
	NULL,	/*	No prompt yet 			*/
	NULL,	/*	No cleanup proc defined	*/
	NULL,	/* No interpreter defined	*/
	NULL	/* No buffer created		*/
};

typedef int (*printf_type)ANSI_PROTO((char *,...));
int (*Slp_Printf )ANSI_PROTO( (char *fmtstr,...)) = (printf_type)printf;

/* Must NOT let Slp_Printf be overwritten to NULL, if so, reset to printf. */
#define SLP_PRINTF ((Slp_Printf != NULL) ? Slp_Printf : (Slp_Printf = (printf_type)printf))

/* signal handlers for window resize */
static void resize_handler ANSI_PROTO((int));
int	Slp_Window_Resize_Detected = 0;
int	Slp_Command_In_Progress = 0;

/*+**********************************************************************
 *
 *	NAME
 *		Slp_SetPrompt()
 *
 *	DESCRIPTION
 *		Defines the prompt, to be displayed when using Tcl with a command
 *		line interpreter.
 *
 *	RETURNS
 *		A standard Tcl result.
 *
 *-*********************************************************************/
#ifdef __STDC__
int Slp_SetPrompt(char *prompt)
#else
int Slp_SetPrompt(prompt)
char *prompt;
#endif
{
	/*
		If prompt has already been set, return. Otherwise, the
		following code will free the same prompt string to be used,
		and HP-UX v8.0's new free() implementation will make it into a
		NULL string. (A.C.S. Teh, 23/11/92)
	*/
	if ((strcmp((prompt)?prompt:"",(slptclfe.prompt)?slptclfe.prompt:"")==0) &&
			(prompt == slptclfe.prompt))
		return(TCL_OK);

	if (slptclfe.prompt != NULL)
	{
		/* A prompt is already defined */
		free(slptclfe.prompt);
		slptclfe.prompt = NULL;
	}

	if ((slptclfe.prompt = malloc(strlen(prompt) + 1)) == NULL)
		return(TCL_ERROR);

	strcpy(slptclfe.prompt, prompt);

	return(TCL_OK);
}

/*+************************************************************************
 *
 *	NAME
 *		Slp_GetPrompt()
 *
 *	DESCRIPTION
 *		Returns the prompt, being displayed by Tcl with a command line
 *		interpreter.
 *
 *	RETURNS
 *		(char *) - the prompt
 *					or NULL, if not available/defined.
 *
 *-*************************************************************************/
char * Slp_GetPrompt()
{
	return(slptclfe.prompt);
}

/*+************************************************************************
 *
 *	NAME
 *		Slp_OutputPrompt()
 *
 *	DESCRIPTION
 *		Displays currently active prompt on screen.
 *
 *	RETURNS
 *		void
 *
 *-************************************************************************/
void Slp_OutputPrompt()
{
	fputs(Slp_GetPrompt(), stdout);
	fflush(stdout);
}

/*+**********************************************************************
 *
 *	NAME
 *		Slp_TclEvalFile()
 *
 *	DESCRIPTION
 *		SLP's replacement for the standard Tcl routine Tcl_EvalFile().
 *		This version handles line-continuation, introduced by "\c".
 *		Hence, support for in-line comments.
 *
 *		The file is read into a buffer, comments are squeezed out of the
 *		buffer, then the whole buffer is processed as one gigantic command.
 *
 *	RETURNS
 *		A standard Tcl result, which is either the result of executing
 *		the file or an error indicating why the file couldn't be read.
 *
 *	SIDE EFFECTS
 *		Depends on the commands in the file.
 *
 *-**********************************************************************/
#ifdef __STDC__
int Slp_TclEvalFile(Tcl_Interp *interp, const char *fileName)
#else
int Slp_TclEvalFile(interp, fileName)
	Tcl_Interp *interp;		/* Interpreter in which to process file. */
	const char *fileName;	/* Name of file to process. Tilde-substitution
	 	 	 	 	 	 	 * will be performed on this name. */
#endif
{
	int fileId, result;
	struct stat statBuf;
	char *cmdBuffer;
	Tcl_DString buffer;

#ifdef USING_FULL_EVALFILE
	char *oldScriptFile;
	Interp *iPtr = (Interp *) interp;
#else
	Tcl_Interp *iPtr = interp;
#endif

	Tcl_ResetResult(interp);

#ifdef USING_FULL_EVALFILE
	oldScriptFile = iPtr->scriptFile;
	iPtr->scriptFile = fileName;
#endif

	fileName = Tcl_TildeSubst(interp, fileName, &buffer);
	if (fileName == NULL) {
		goto error;
	}

	fileId = open(fileName, O_RDONLY, 0) ;
	if (fileId < 0) {
		Tcl_AppendResult(interp, "couldn't read file \"", fileName,
							"\": ", Tcl_PosixError(interp), (char *) NULL);
		goto error;
	}

	if (fstat(fileId, &statBuf) == -1) {
		Tcl_AppendResult(interp, "couldn't stat file \"", fileName,
							"\": ", Tcl_PosixError(interp), (char *) NULL);
		close(fileId);
		goto error;
	}

	cmdBuffer = (char *) ckalloc((unsigned) statBuf.st_size+1);
	if (read(fileId, cmdBuffer, (size_t) statBuf.st_size) != statBuf.st_size) {
		Tcl_AppendResult(interp, "error in reading file \"", fileName,
							"\": ", Tcl_PosixError(interp), (char *) NULL);
		close(fileId);
		ckfree(cmdBuffer);
		goto error;
	}

	if (close(fileId) != 0) {
		Tcl_AppendResult(interp, "error closing file \"", fileName,
							"\": ", Tcl_PosixError(interp), (char *) NULL);
		ckfree(cmdBuffer);
		goto error;
	}
	cmdBuffer[statBuf.st_size] = 0;

	/* Parse '\c' line continuation - GJB */
	{
		char *src_ptr;
		char *dst_ptr;

		src_ptr = dst_ptr = cmdBuffer;

		while (*src_ptr != '\0')
		{
			if ((*dst_ptr++ = *src_ptr++) == '\\')
			{
				switch(* src_ptr)
				{
					case 'c':
						src_ptr++;
						*dst_ptr++ = '\n';
						while (*src_ptr++ != '\n')
							;
						break;
					default:
						*dst_ptr++ = *src_ptr++;
						break;
				}
			}
		}
		*dst_ptr = '\0';
	}

	Slp_Command_In_Progress = 1;

	Slp_CharEchoOn(); /* in case Tcl command "gets stdin" is used */

	result = Tcl_Eval(interp, cmdBuffer);

	if (result == TCL_RETURN) {
		result = TclUpdateReturnInfo(iPtr);
	}
	else
	if (result == TCL_ERROR) {
		char msg[200];

		/*
		 * Record information telling where the error occurred.
		 */

		sprintf(msg, "\n (file \"%.150s\" line %d)", fileName, interp->errorLine);
		Tcl_AddErrorInfo(interp, msg);
	}

	ckfree(cmdBuffer);

	Slp_Command_In_Progress = 0;

	Slp_CharEchoOff();

#ifdef USING_FULL_EVALFILE
	iPtr->scriptFile = oldScriptFile;
#endif

	Tcl_DStringFree (&buffer) ;

	return result;

error:
#ifdef USING_FULL_EVALFILE
	iPtr->scriptFile = oldScriptFile;
#endif

	Tcl_DStringFree(&buffer);

	Slp_CharEchoOff();

	return TCL_ERROR;
}

/*+**********************************************************************
 *
 *	NAME
 *		Slp_TclCmdSource()
 *
 *	DESCRIPTION
 *		SLP's replacement for the standard built-in Tcl "source" command.
 *		This version calls Slp_TclEvalFile(), instead of the standard
 *		Tcl_EvalFile(), to handle in-line comments.
 *
 *	RETURNS
 *		A standard Tcl result.
 *
 *-**********************************************************************/
#ifdef __STDC__
int Slp_TclCmdSource(ClientData dummy, Tcl_Interp *interp,
					 int argc, const char **argv)
#else
int Slp_TclCmdSource(dummy, interp, argc, argv)
	ClientData dummy;	/* Not used. */
	Tcl_Interp *interp;	/* Current interpreter. */
	int argc;			/* Number of arguments. */
	const char **argv;	/* Argument strings. */
#endif
{
	if (argc != 2) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
				" fileName\"", (char *) NULL);
		return TCL_ERROR;
	}
	return Slp_TclEvalFile(interp, argv[1]);
}

/*+**********************************************************************
★
*	NAME
*	Slp_TclCmdExit{)
*
*	DESCRIPTION
*	SLP's replacement for the standard built-in Tcl "exit" command.
*
*	This version cleans up the input handler, and executes any
*	other clean-up routine specified by the application programmer
*	using the Slp_TclSetCleanupProc();
*
*	RETURNS
*	A standard Tcl result.
*
*_**********************************************************************/
#ifdef __STDC__
int Slp_TclCmdExit(ClientData dummy, Tcl_Interp *interp, int argc, const char **argv)
#else
int Slp_TclCmdExit(dummy, interp, argc, argv)
	ClientData dummy;	/* Not used. */
	Tcl_Interp *interp;	/* Current interpreter. */
	int argc;			/* Number of arguments. */
	const char **argv;	/* Argument strings. */
#endif
{
	int value;

	if ((argc != 1) && (argc != 2)) {
		Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
							" ?returnCode?\"", (char *) NULL);
		return TCL_ERROR;
	}

	if (argc == 1) {
		/* These are the non-standard-Tcl additions - GJB */
		/* +++++ */
		fflush(stdout);
		Slp_gl_cleanup();

		if (slptclfe.cleanup_proc)
			slptclfe.cleanup_proc();

		Tcl_DStringFree (&slptclfe.buffer) ;
		/* ----- */

		exit(0);
	}

	if (Tcl_GetInt(interp, argv[1], &value) != TCL_OK) {
		return TCL_ERROR;
	}

	exit(value);

	/*NOTREACHED*/
	return TCL_OK;	/* Better not ever reach this! */
}

/*+**********************************************************************
 *
 *	NAME
 *		Slp_TclCmdEcho()
 *
 *	DESCRIPTION
 *		Adds the "echo" command to all Tcl interpreters using SLP.
 *
 *	RETURNS
 *		A standard Tcl result.
 *
 *_**********************************************************************/
#ifdef __STDC__
int Slp_TclCmdEcho(ClientData dummy, Tcl_Interp *interp, int argc, const char **argv)
#else
int Slp_TclCmdEcho(dummy, interp, argc, argv)
	ClientData dummy;	/* Not used. */
	Tcl_Interp *interp;	/* Current interpreter. */
	int argc;			/* Number of arguments. */
	const char *argv[];	/* Argument strings. */
#endif
{
	char *string = NULL;

	string = Tcl_Concat(argc-1, &argv[1]);

	if (string == NULL)
	  Tcl_SetResult(interp, NULL, TCL_STATIC);
	else
	  Tcl_SetResult(interp, string, TCL_DYNAMIC);

	return TCL_OK;
}

/*+*******************************************************************
*
*	NAME
*		Slp_SetCleanupProc()
*
*	DESCRIPTION
*		Additional routine to call just before exiting.
*
*	RETURNS
*		void
*
*-********************************************************************/
#ifdef __STDC__
void Slp_SetCleanupProc(int (*proc) ())
#else
void Slp_SetCleanupProc(proc)
	int (*proc)();
#endif
{
	slptclfe.cleanup_proc = proc;
}

/*+**********************************************************************
*
*	NAME
*		Slp_SetTclInterp()
*
*	DESCRIPTION
*		Defines the currently active interpreter.
*
*	RETURNS
*		A standard Tcl result.
*
*-*********************************************************************-*/
#ifdef __STDC__
int Slp_SetTclInterp(Tcl_Interp *interp)
#else
int Slp_SetTclInterp(interp)
	Tcl_Interp *interp;
#endif
{
	slptclfe.interp = interp;
	return(TCL_OK);
}

/*+**********************************************************************
*
*	NAME
*		Slp_GetTclInterp ()
*
*	DESCRIPTION
*		Returns the currently active interpreter.
*
*	RETURNS
*		(Tcl_Interp *)interp
*
*_**********************************************************************/
Tcl_Interp *Slp_GetTclInterp()
{
	return(slptclfe.interp);
}

/*+**********************************************************************
 *
 *	NAME
 *		Slp_InitTclInterp()
 *
 *	DESCRIPTION
 *		Initializes a Tcl interpreter.
 *		This initialization redefines the following built-in Tcl commands:
 *			"source" - to add in-line comment parsing.
 *			"exit"   - to provide a clean-up mechanism before exiting.
 *
 *	RETURNS
 *		A standard Tcl result.
 *
 *-**********************************************************************/
#ifdef __STDC__
char *Slp_InitTclInterp(Tcl_Interp *interp)
#else
char *Slp_InitTclInterp(interp)
	Tcl_Interp *interp; /* Tcl interpreter */
#endif
{
	static char SlpUsesTcl[80];

	/* Replace standard Tcl "source" command */
	Tcl_CreateCommand(
		interp,
		"source",
		Slp_TclCmdSource,
		(ClientData)NULL,
		(Tcl_CmdDeleteProc *)NULL
		);

	/* Replace standard Tcl "exit" command */
	Tcl_CreateCommand(
		interp,
		"exit",
		Slp_TclCmdExit,
		(ClientData)NULL,
		(Tcl_CmdDeleteProc *)NULL
		);

	/* Add "echo" command, by default */
	Tcl_CreateCommand(
		interp,
		"echo",
		Slp_TclCmdEcho,
		(ClientData) "echo",
		(Tcl_CmdDeleteProc *) NULL
		);

	/* Initialize Slp_VersionInfo and 1st line of Slp_Copyright */
#ifdef PLSID_DEFINED
	{
		char *fmtstr;
		(void) sprintf( Slp_VersionInfo[0],
						(fmtstr = strdup(Slp_VersionInfo[0])),
						plsid);
		free((void *)fmtstr);
	}
#endif

	Slp_Copyright[0] = Slp_VersionInfo[0];

	sprintf(SlpUsesTcl, "%s uses Tcl v%s",
	strstr(Slp_VersionInfo[0], "SLP"), TCL_VERSION);
	Slp_VersionInfo[1] = SlpUsesTcl;

	Slp_SetTclInterp(interp);

	return(TCL_OK);
}

/*+**********************************************************************
 *
 *	NAME
 *		Slp_StdinHandler()
 *
 *	DESCRIPTION
 *		Reads, records and evaluates a command line from stdin.
 *
 *	RETURNS
 *		A standard Tcl result.
 *
 *-**********************************************************************/
void Slp_StdinHandler()
{
	static int gotPartial;
	int result;
	char *cmd;
	char *input;
	char *parsed;
	Tcl_Interp *interp;

	static char *save_prompt = NULL;

	gotPartial = 0;

	printf("slptclfe.c: Slp_StdinHandler() calling Slp_GetTclInterp()\n");
	interp = Slp_GetTclInterp();
	printf("slptclfe.c: Slp_StdinHandler() interp = %d\n", interp);

	if (save_prompt == NULL)
		save_prompt = strdup(Slp_GetPrompt());

	printf("slptclfe.c: Slp_StdinHandler() calling Slp_getline()\n");
	parsed = input = Slp_getline(Slp_GetPrompt());
	printf("slptclfe.c: input = >>>%s<<<\n", input);

	/* Parse for "\c" line continuation */
	while ((*input != '\n') && (*input != '\0'))
	{
		printf("slptclfe.c: while loop = %c\n", *input);
		if (*input++ == '\\')
		{
			switch(*input)
			{
				case 'c':
					*input++ = '\n';
					*input = '\0';
					break;
				case '\n':
					break;
				default:
					input++;
					break;
			}
		}
	}

	printf("slptclfe.c: Slp_StdinHandler() calling Tcl_DStringAppend()\n");
	cmd = Tcl_DStringAppend(&slptclfe.buffer, parsed, -1);
	printf("slptclfe.c: cmd = %s\n", cmd);

	if ((parsed[0] != 0) && !Tcl_CommandComplete(cmd))
	{
		printf("slptclfe.c: Inside if !Tcl_CommandComplete(%s)\n", cmd);
		gotPartial = 1;
		Slp_SetPrompt("> ");
		printf("%s", Slp_GetPrompt());	/* Like PS2 */
		fflush(stdout);
		printf("slptclfe.c: returning\n");
		return;
	}

	gotPartial = 0;

	printf("slptclfe.c: Slp_StdinHandler - Slp_SetPrompt(%s)\n", save_prompt);
	Slp_SetPrompt(save_prompt);
	if (save_prompt != NULL)
		free(save_prompt);
	save_prompt = NULL;

	Slp_Command_In_Progress = 1;

	printf("slptclfe.c: Slp_CharEchoOn()\n");
	Slp_CharEchoOn(); /* in case Tcl command "gets stdin" is used */

	printf("slptclfe.c: Tcl_RecordAndEval(%s)\n", cmd);
	result = Tcl_RecordAndEval(interp, cmd, 0);

	printf("slptclfe.c: Tcl_DStringTrunc()\n");
	Tcl_DStringTrunc(&slptclfe.buffer, 0); /* empty buffer after use */

	if (result == TCL_OK)
	{
		printf("slptclfe.c: result == TCL_OK\n");
		if (*Tcl_GetStringResult(interp) != 0)
			SLP_PRINTF("%s\n", Tcl_GetStringResult(interp));
	}
	else
	{
		if (result == TCL_ERROR)
			fprintf(stderr, "Error");
		else
			fprintf(stderr, "Error %d", result);

		if (*Tcl_GetStringResult(interp) != '\0')
			fprintf(stderr, ": %s\n", Tcl_GetStringResult(interp));
		else
			fprintf(stderr, "\n");
	}

	Slp_Command_In_Progress = 0;

	printf("slptclfe.c: Slp_CharEchoOff()\n");
	Slp_CharEchoOff();

	printf("slptclfe.c: clearerr(stdin)\n");
	clearerr(stdin);

	if (!gotPartial) {
		printf("slptclfe.c: if (!gotPartial) Slp_OutputPrompt()\n");
		Slp_OutputPrompt();
	}
}

#ifdef __STDC__
static void resize_handler(int sigval)
#else
static void resize_handler(sigval)
int sigval; /* 0 indicates should initialize signal () */
#endif
{
	static void (*prev_resize_handler) ANSI_PROTO((int)) = NULL;

	int Columns = 0, Lines = 0;
	static char columns_env[25] ;
	static char lines_env[25];
	struct winsize window_size;

	if (sigval == 0)
	{
		prev_resize_handler = signal(SIGWINCH, resize_handler);
	}
	else
	if (sigval == SIGWINCH)
	{
		/* Obtain new window size, can't rely on value of COLUMNS. */
		ioctl(1, TIOCGWINSZ, &window_size);
		Columns = (int) window_size.ws_col;
		Lines = (int) window_size.ws_row;
		(void) sprintf(columns_env, "COLUMNS=%d", Columns);
		(void) sprintf(lines_env, "LINES=%d", Lines);
		putenv(columns_env);
		putenv(lines_env);

		/* Set line width to new value. */
		Slp_Window_Resize_Detected = 1;
		Slp_gl_init(Columns);
		Slp_Window_Resize_Detected = 0;

		/* Execute a previous resize signal handler. */
		if (prev_resize_handler != NULL)
			prev_resize_handler (SIGWINCH);

		/* Signal processed, reset signal handler. */
		(void) signal(SIGWINCH, resize_handler);
	}
}

/*+**********************************************************************
 *
 *	NAME
 *		Slp_InitStdio()
 *
 *	DESCRIPTION
 *		Initializes Tcl input/output routines.
 *		If the prompt has not yet been defined, it is set to DEF_PROMPT;
 *		Set window resize signal handler.
 *
 *	RETURNS
 *		A standard Tcl result.
 *
 *-**********************************************************************/
int Slp_InitStdio()
{
	char *prompt;
	char *columns_value, *lines_value;
	int Columns = 80; /* default */
	int Lines = 24; /* default */
	struct winsize window_size;
	static char columns_env[25];
	static char lines_env[25];

	/* Initialize Slp_getline() to environment variable COLUMNS or 80 */
	columns_value = getenv("COLUMNS");
	lines_value = getenv("LINES");
	if (columns_value == NULL || *columns_value == '\0')
	{
		/* COLUMNS not set, try ioctl to find out. */
		ioctl(1, TIOCGWINSZ, &window_size);
		Columns = (int) window_size.ws_col;
		(void) sprintf(columns_env, "COLUMNS=%d", Columns);
		putenv(columns_env);
	}
	else
		Columns = atoi(columns_value);

	if (lines_value == NULL || *lines_value == '\0')
	{
		/* LINES not set, try ioctl to find out. */
		ioctl(1, TIOCGWINSZ, &window_size);
		Lines = (int) window_size.ws_row;
		(void) sprintf(lines_env, "LINES=%d", Lines);
		putenv(lines_env) ;
	}
	else
		Lines = atoi(lines_value);

	Slp_gl_init(Columns);

	/* Join signal handler chain for window resize. */
	resize_handler(0);

	/* Create command line input buffer */
	Tcl_DStringInit(&slptclfe.buffer);

	clearerr(stdin);

	if ((prompt = Slp_GetPrompt()) == NULL)
		prompt = DEF_PROMPT;
	Slp_SetPrompt(prompt);

	return(TCL_OK);
}
