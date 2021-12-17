/* EDITION AC10 (REL002), ITD ACST.174 (95/05/28 20:26:30) -- OPEN */
/* PLS */
/******************************************************************************\
********************************************************************************
**																			  **
**									S L P									  **
**																			  **
********************************************************************************
\******************************************************************************/
#ifndef _SLP_INCLUDED
#define _SLP_INCLUDED

/*+****************************************************************************
*
*	NAME
*	slp.h - Programmers' Tcl Plus header file
*
*	DESCRIPTION
*	This module defines things in the Programmers Tcl Plus (SLP) library.
*
*****************************************************************************-*/

/********************************** INCLUDES **********************************/
#include <tcl.h>

/********************************** DEFINES ***********************************/

/*********************************** MACROS ***********************************/
#undef ANSI_PROTO
#undef _VARARGS_
#undef EXTERN

#if defined(__STDC__) || defined (__cplusplus)
#	define ANSI_PROTO(s) s
#	ifdef __STDC__
#	  define _VARARGS_ , ...
#	else /* #elif not supported by K&R cpp */
#	  ifdef __cplusplus
#	    define _VARARGS_ ...
#	  endif
#	endif
#else
#	define ANSI_PROTO(s) ()
#	define _VARARGS_
#endif
#ifdef __cplusplus
#	define EXTERN extern "C"
#else
#	define EXTERN extern
#endif

/********************************** TYPEDEFS **********************************/
/* None */
/********************************** EXTERNS ***********************************/
EXTERN char			*Slp_GetPrompt ANSI_PROTO((void));
EXTERN Tcl_Interp	*Slp_GetTclInterp ANSI_PROTO((void));
EXTERN int			Slp_InitStdio ANSI_PROTO((void));
EXTERN char			*Slp_InitTclInterp ANSI_PROTO((Tcl_Interp *interp));
EXTERN void			Slp_OutputPrompt ANSI_PROTO((void));
EXTERN void			Slp_SetCleanupProc ANSI_PROTO((int (*proc)()));
EXTERN int			Slp_SetPrompt ANSI_PROTO((char *prompt));
EXTERN int			Slp_SetTclInterp ANSI_PROTO((Tcl_Interp *interp));
EXTERN void			Slp_StdinHandler ANSI_PROTO((void));
EXTERN char *		Slp_getline ANSI_PROTO((char *prompt));

EXTERN void			Slp_gl_init ANSI_PROTO ((int scrn_wdth));
							/* to initialize slp_getline */
EXTERN void			Slp_gl_cleanup ANSI_PROTO((void));
							/* to undo slp_gl_init */
EXTERN void			Slp_gl_redraw ANSI_PROTO((void));
							/* issue \n and redraw all */
EXTERN void			Slp_gl_replace ANSI_PROTO((void));
							/* toggle replace/insert mode */

EXTERN int			(*Slp_Printf)ANSI_PROTO((char *format_string,...));
											/* user-supplied printf which
											   may perform output paging,
											   defaults to printf() */

EXTERN char *		Slp_KeystrokesHelpText[];
EXTERN char *		Slp_Copyright[];
EXTERN char *		Slp_VersionInfo[];

EXTERN void (*Slp_CharEchoOff) ANSI_PROTO((void));
EXTERN void (*Slp_CharEchoOn) ANSI_PROTO((void));

/*
	printf not defined in /usr/include/stdio.h
	on Tadpole SPARCbook running SunOS 4.1.2
 */
#if sun || sun2 || sun3 || sun4
EXTERN int	printf ANSI_PROTO((char *format, ...));
#endif

/******************************************************************************/
#endif /* _SLP_INCLUDED */
