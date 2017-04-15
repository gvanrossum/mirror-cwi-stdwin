/* TERMCAP STDWIN -- BASIC ROUTINES. */

#include "alfa.h"

#ifdef HAVE_SIGNAL_H
#include <sys/types.h>
#include <signal.h>
static RETSIGTYPE handler(); /* Forward */
#endif

int lines, columns;

/*ARGSUSED*/
void
winitargs(pargc, pargv)
	int *pargc;
	char ***pargv;
{
	wargs(pargc, pargv);
	winit();
}

/*ARGSUSED*/
void
wargs(pargc, pargv)
	int *pargc;
	char ***pargv;
{
}

/* Initialization call.
   Should be called only once, before any others.
   Will exit when the initialization fails. */

void
winit()
{
	int flags;
	int err;
	
#ifdef HAVE_SIGNAL_H
	(void) signal(SIGINT, handler);
#ifdef NDEBUG
	(void) signal(SIGQUIT, handler);
#endif
	(void) signal(SIGILL, handler);
	(void) signal(SIGIOT, handler);
#ifdef	SIGEMT
	(void) signal(SIGEMT, handler);
#endif
	(void) signal(SIGFPE, handler);
	(void) signal(SIGBUS, handler);
	(void) signal(SIGSEGV, handler);
#ifdef	SIGSYS
	(void) signal(SIGSYS, handler);
#endif
	(void) signal(SIGTERM, handler);
#endif /* HAVE_SIGNAL_H */
	
	getttykeydefs(0);	/* File descriptor 0 */
	err= trmstart(&lines, &columns, &flags);
	if (err != 0) {
		fprintf(stderr, "STDWIN: trmstart error %d\n", err);
		exit(2);
	}
	gettckeydefs();
	if (lines > MAXLINES)
		lines= MAXLINES;
	trmputdata(0, lines-1, 0, "", (char *)0);
	initsyswin();
	_winitmenus();
}

/* Termination call.
   Should be called before the program exits
   (else the terminal will be left in almost raw mode). */

void
wdone()
{
	if (lines > 0) {
		/* Move cursor to last screen line. */
		trmputdata(lines-1, lines-1, 0, "", (char *)0);
		trmsync(lines-1, 0);
	}
	lines= 0;
	trmend();
}

#ifdef HAVE_SIGNAL_H

/* Signal handler.
   Print a message and exit. */

static RETSIGTYPE
handler(sig)
	int sig;
{
	wdone();
	signal(sig, SIG_DFL);
	kill(0, sig);		/* Kill all others in our process group */
	kill(getpid(), sig);	/* Commit suicide */
	/* (On BSD, the signal is delivered to us only after we return.) */
}

#endif /* HAVE_SIGNAL_H */

/* Essential data structures. */

WINDOW winlist[MAXWINDOWS] /* = {FALSE, ...}, ... */;

WINDOW *wasfront;	/* What the application thinks is active */
WINDOW *front;		/* The window that is really active */
	/* If these are not equal, an activate or deactivate event
	   will be generated by wevent before anything else. */

char uptodate[MAXLINES] /* = FALSE, ... */;

/* Open a window. */

WINDOW *
wopen(title, drawproc)
	char *title;
	void (*drawproc)();
{
	int id;
	WINDOW *win;
	
	for (win= &winlist[0]; win < &winlist[MAXWINDOWS]; ++win) {
		if (!win->open)
			break;
	}
	id= win - &winlist[0];
	if (id >= MAXWINDOWS)
		return NULL;	/* Too many windows open */
	win->open= TRUE;
	_wreshuffle();
	win->resized = FALSE;	/* Don't begin with a redraw event */
	win->tag= 0;
	win->drawproc= drawproc;
	win->title= strdup(title);
	win->attr= wattr;
	
	win->offset= -win->top;
	win->curh= win->curv= -1;
	win->timer= 0;
	
	initmenubar(&win->mbar);
	
	front= win;
	return win;
}

/* Divide the available lines over the available windows.
   Line 0 is for window 0, the system window, and is different:
   it has no title, and is always one line high, except when it's
   the only window. */

void
_wreshuffle()
{
	int nwins= 0;
	int nlines= lines;
	int top= 0;
	WINDOW *win;
	
	/* Count open windows. */
	for (win= winlist; win < &winlist[MAXWINDOWS]; ++win) {
		if (win->open)
			++nwins;
	}
	/* Assign each open window its share of the screen. */
	for (win= winlist; win < &winlist[MAXWINDOWS]; ++win) {
		if (win->open) {
			int i= nlines/nwins;	/* This window's share */
			int id= win - winlist;
#ifdef LM_S_PROB
			int oldtop= win->top, oldbot= win->bottom;
			int dv, scrbot;
#endif
			/* Cause redraw event for old title position: */
			if (win->top > 0)
				uptodate[win->top-1]= FALSE;
			if (id == 0) {	/* System window */
				win->top= top;
				if (nwins > 1)
					i=1;
			}
			else
				win->top= top+1;
			win->bottom= top + i;
			nlines -= i;
			--nwins;
#ifndef LM_S_PROB
			/* This is overkill;
			   we should try not cause a redraw of all lines! */
			for (i= top; i < win->bottom; ++i) {
				uptodate[i]= FALSE;
			}
#else
			/* LM's probeersel */
			dv= oldtop-win->top;
			scrbot=win->bottom;
			if (oldbot-dv < scrbot) scrbot=oldbot-dv;
			scrollupdate(win->top, scrbot, dv);
			trmscrollup(win->top, scrbot-1, dv);
			for (i= top; i < win->bottom; ++i)
				if (!(win->top <= i && i < scrbot ||
				      win->top <= i+dv && i+dv < scrbot))
				uptodate[i]= FALSE;
#endif
			top= win->bottom;
			/* Cause redraw event for new title position: */
			if (win->top > 0)
				uptodate[win->top-1]= FALSE;
			/* Cause resize event: */
			win->resized= TRUE;
			/* Scroll back if negative line numbers visible: */
			if (win->top + win->offset < 0)
				wshow(win, 0, 0,
					columns, win->bottom - win->top);
#if 0 /* We don't really want to for showing the caret... */
			/* Show caret: */
			if (win->curv >= 0)
				wshow(win, win->curh, win->curv,
					win->curh, win->curv);
#endif
		}
	}
}

/* Close a window. */

void
wclose(win)
	WINDOW *win;
{
	int id= win - winlist;
	
	if (id < 0 || id >= MAXWINDOWS || !win->open)
		return;
	killmenubar(&win->mbar);
	if (win == wasfront)
		wasfront= NULL;
	if (win == front)
		front= NULL;
	win->open= FALSE;
	if (win->title != NULL) {
		free(win->title);
		win->title= NULL;
	}
	_wreshuffle();
	/* Select another window. */
	if (front == NULL) {
		/* Search below the window we just closed. */
		for (; win < &winlist[MAXWINDOWS]; ++win) {
			if (win->open) {
				front= win;
				break; /* First hit is OK */
			}
		}
		if (front == NULL) {
			/* Try searching above it. */
			for (win= winlist+1; win < &winlist[MAXWINDOWS];
								++win) {
				if (win->open)
					front= win;
				/* NO break -- we need the last hit */
			}
		}
		if (front == NULL) {
			/* Exasperation time. */
			front= &winlist[0]; /* The system window */
		}
		_wnewtitle(front); /* Make sure the title is redrawn */
	}
}

/* Dummies for functions not supported by this version. */

/*ARGSUSED*/
void
wsetdefwinpos(h, v)
	int h, v;
{
}

/*ARGSUSED*/
void
wsetdefwinsize(width, height)
	int width, height;
{
}

/*ARGSUSED*/
void
wsetmaxwinsize(width, height)
	int width, height;
{
}

/*ARGSUSED*/
void
wsetdefscrollbars(width, height)
	int width, height;
{
}

void
wgetdefwinpos(ph, pv)
	int *ph, *pv;
{
	*ph = *pv = 0;
}

void
wgetdefwinsize(ph, pv)
	int *ph, *pv;
{
	*ph = *pv = 0;
}

void
wgetdefscrollbars(ph, pv)
	int *ph, *pv;
{
	*ph = *pv = 0;
}

/* Make a window the active window. */

void
wsetactive(win)
	WINDOW *win;
{
	int id= win - winlist;
	
	if (id < 0 || id >= MAXWINDOWS || !win->open)
		return;
	_wnewtitle(front);
	front= win;
	_wnewtitle(front);
}

/* Select next, previous window.
   Note: these will never select the system window unless it is already
   selected (which can only occur if there are no other windows open). */

void
_wselnext()
{
	WINDOW *win;
	
	for (win= front+1; win < &winlist[MAXWINDOWS]; ++win) {
		if (win->open) {
			wsetactive(win);
			return;
		}
	}
	for (win= winlist+1; win < front; ++win) {
		if (win->open) {
			wsetactive(win);
			return;
		}
	}
}

void
_wselprev()
{
	WINDOW *win;
	
	for (win= front-1; win > winlist; --win) {
		if (win->open) {
			wsetactive(win);
			return;
		}
	}
	for (win= &winlist[MAXWINDOWS-1]; win > front; --win) {
		if (win->open) {
			wsetactive(win);
			return;
		}
	}
}

/* Force a redraw of the entire screen.
   (This routine only sets 'invalid' bits for all lines;
   the actual redraw is done later in wgetevent or wupdate.) */

void
_wredraw()
{
	int i;
	
	for (i= 0; i < lines; ++i)
		uptodate[i]= FALSE;
	_wreshuffle();
	trmundefined();
	trmputdata(0, lines-1, 0, "", (char *)0);
}

/* Temporarily restore cooked tty mode. */

_wcooked()
{
	trmputdata(lines-1, lines-1, 0, "", (char *)0);
	trmsync(lines-1, 0);
	trmend();
}

/* Back to window mode.
   If 'wait' flag is set, wait until a character is typed before
   continuing. clearing the screen. */

_wstdwin(wait)
	bool wait;
{
	int flags;
	
	if (wait) {
		printf("\nCR to continue... ");
		fflush(stdout);
	}
	(void) trmstart(&lines, &columns, &flags);
	if (wait)
		(void) trminput();
	_wredraw();
	wmessage((char *)NULL); /* Reset menu bar */
}

/* Suspend the process (BSD Unix only). */

void
_wsuspend()
{
	_wcooked();
	trmsuspend();
	_wstdwin(FALSE);
}

/* Execute a shell command, if possible and necessary outside the window
   manager.  If the 'wait' parameter is set, the window manager pauses
   until a character is typed before continuing. */

int
wsystem(cmd, wait)
	char *cmd;
	bool wait;
{
#ifdef HAVE_SYSTEM
	int status;
	_wcooked();
	status= system(cmd);
	_wstdwin(wait);
	return status;
#else
	return -1;
#endif
}

/* Return active window. */

WINDOW *
wgetactive()
{
	return front;
}

/* Change a window's title. */

void
wsettitle(win, title)
	WINDOW *win;
	char *title;
{
	if (win->title != NULL)
		free(win->title);
	win->title= strdup(title);
	_wnewtitle(win);
}

/* Change the mouse cursor -- dummy */

/*ARGSUSED*/
void
wsetwincursor(win, cursor)
	WINDOW *win;
	CURSOR *cursor;
{
}

/* Fetch a mouse cursor -- always return NULL */

/*ARGSUSED*/
CURSOR *
wfetchcursor(name)
	char *name;
{
	return NULL;
}

/* Set a window's extent (document size). Not implemented here. */

/*ARGSUSED*/
void
wsetdocsize(win, width, height)
	WINDOW *win;
	int width, height;
{
}

/*ARGSUSED*/
void
wgetdocsize(win, pwidth, pheight)
	WINDOW *win;
	int *pwidth, *pheight;
{
	*pwidth = *pheight = 0;
}

/* Get a window's origin with respect to document. */

void
wgetorigin(win, ph, pv)
	WINDOW *win;
	int *ph, *pv;
{
	*ph= 0;
	*pv= win->top + win->offset;
}

/* Get a window's origin with respect to screen. */

void
wgetwinpos(win, ph, pv)
	WINDOW *win;
	int *ph, *pv;
{
	*ph= 0;
	*pv= win->top;
}

/* Get a window's window size. */

void
wgetwinsize(win, pwidth, pheight)
	WINDOW *win;
	int *pwidth, *pheight;
{
	*pwidth= columns;
	*pheight= win->bottom - win->top;
}

/* Get the screen size in pixels. */

void
wgetscrsize(ph, pv)
	int *ph, *pv;
{
	*ph= columns;
	*pv= lines;
}

/* Get the screen size in mm.
   Of course we don't know it; we pretend that the average character
   is 3 mm wide and 6 mm high, which is a reasonable approximation
   of reality on my terminal (an HP2621). */

void
wgetscrmm(ph, pv)
	int *ph, *pv;
{
	*ph= columns * 3;
	*pv= lines * 6;
}

void
wfleep()
{
	trmbell();
}