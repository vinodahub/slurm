/*****************************************************************************\
 *  xstring.c - heap-oriented string manipulation functions with "safe" 
 *	string expansion as needed.
 ******************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, Jim Garlick <garlick@llnl.gov>, 
 *  et. al.
 *  UCRL-CODE-2002-040.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#if 	HAVE_STRERROR_R && !HAVE_DECL_STRERROR_R
//char *strerror_r(int, char *, int);
#endif
#include <errno.h>
#if 	HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <pthread.h>
#include <xmalloc.h>
#include <xstring.h>
#include <strlcpy.h>
#include <xassert.h>

#include <src/common/slurm_errno.h>

#define XFGETS_CHUNKSIZE 64


/*
 * Ensure that a string has enough space to add 'needed' characters.
 * If the string is uninitialized, it should be NULL.
 */
static void makespace(char **str, int needed)
{
	int used;

	if (*str == NULL)
		*str = xmalloc(needed + 1);
	else {
		used = strlen(*str) + 1;
		while (used + needed > xsize(*str)) {
			int newsize = xsize(*str) + XFGETS_CHUNKSIZE;
			int actualsize;

			xrealloc(*str, newsize);
			actualsize = xsize(*str);

			xassert(actualsize == newsize);
		}
	}
}

/* 
 * Concatenate str2 onto str1, expanding str1 as needed.
 *   str1 (IN/OUT)	target string (pointer to in case of expansion)
 *   size (IN/OUT)	size of str1 (pointer to in case of expansion)
 *   str2 (IN)		source string
 */
void _xstrcat(char **str1, const char *str2)
{
	if (str2 == NULL) 
		str2 = "(null)";

	makespace(str1, strlen(str2));
	strcat(*str1, str2);
}

/* 
 * append one char to str and null terminate
 */
static void strcatchar(char *str, char c)
{
	int len = strlen(str);

	str[len++] = c;
	str[len] = '\0';
}

/* 
 * Add a character to str, expanding str1 as needed.
 *   str1 (IN/OUT)	target string (pointer to in case of expansion)
 *   size (IN/OUT)	size of str1 (pointer to in case of expansion)
 *   c (IN)		character to add
 */
void _xstrcatchar(char **str, char c)
{
	makespace(str, 1);
	strcatchar(*str, c);
}


/*
 * concatenate slurm_strerror(errno) onto string in buf, expand buf as needed
 *
 */
void _xslurm_strerrorcat(char **buf)
{

	char *err = slurm_strerror(errno);

	xstrcat(*buf, err);
}

/* 
 * append strftime of fmt to buffer buf, expand buf as needed 
 *
 */
void _xstrftimecat(char **buf, const char *fmt)
{
	char p[256];		/* output truncated to 256 chars */
	time_t t;
	struct tm *tm_ptr = NULL;
	static pthread_mutex_t localtime_lock = PTHREAD_MUTEX_INITIALIZER;

	const char default_fmt[] = "%m/%d/%Y %H:%M:%S %Z";

	if (fmt == NULL)
		fmt = default_fmt;

	if (time(&t) == (time_t) -1) 
		fprintf(stderr, "time() failed\n");

	pthread_mutex_lock(&localtime_lock);
	if (!(tm_ptr = localtime(&t)))
		fprintf(stderr, "localtime() failed\n");

	strftime(p, sizeof(p), fmt, tm_ptr);

	pthread_mutex_unlock(&localtime_lock);

	_xstrcat(buf, p);
}


/* 
 * Replacement for libc basename
 *   path (IN)		path possibly containing '/' characters
 *   RETURN		last component of path
 */
char * xbasename(char *path)
{
	char *p;

	p = strrchr(path , '/');
	return (p ? (p + 1) : path);
}	

/*
 * Duplicate a string.
 *   str (IN)		string to duplicate
 *   RETURN		copy of string
 */
char * xstrdup(const char *str)
{
	size_t siz,
	       rsiz;
	char   *result;

	if (str == NULL)
		return NULL;

	siz = strlen(str) + 1;
	result = (char *)xmalloc(siz);

	rsiz = strlcpy(result, str, siz);

	xassert(rsiz == siz-1);

	return result;
}
