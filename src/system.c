/*
 *
 * s y s t e m . c                              -- System relative primitives
 *
 * Copyright © 1994-2020 Erick Gallesio - I3S-CNRS/ESSI <eg@unice.fr>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 *           Author: Erick Gallesio [eg@kaolin.unice.fr]
 *    Creation date: 29-Mar-1994 10:57
 * Last file update: 19-Sep-2020 19:04 (eg)
 */

#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <locale.h>
#include "stklos.h"
#include "struct.h"

#ifndef MAXBUFF
#  define MAXBUFF 1024
#endif

#ifdef WIN32
#  define TIME_DIV_CONST 10000
#else
#  define TIME_DIV_CONST 1000
#endif


static SCM exit_procs = STk_nil; /* atexit functions */
static SCM date_type, time_type;

/******************************************************************************
 *
 * Utilities
 *
 ******************************************************************************/
static void error_bad_path(SCM path)
{
  STk_error("~S is a bad pathname", path);
}

static void error_bad_string(SCM path)
{
  STk_error("~S is a bad string", path);
}

static void error_bad_int_or_out_of_bounds(SCM val)
{
  STk_error("bad integer ~S (or out of range)", val);
}

static void error_win32_primitive(void)
{
  STk_error("Win32 primitive not available on this system");
}

static void error_posix(SCM obj1, SCM obj2)
{
  if (obj1 && obj2)
    STk_error("%s: ~A ~A", strerror(errno), obj1, obj2);
  else
    if (obj1)
      STk_error("%s: ~A", strerror(errno), obj1);
    else
      STk_error("%s", strerror(errno));
}

static SCM my_access(SCM path, int mode)
{
  if (!STRINGP(path)) error_bad_path(path);
  return MAKE_BOOLEAN(access(STk_expand_file_name(STRING_CHARS(path)), mode) == 0);
}

static int my_stat(SCM path, struct stat *s)
{
  if (!STRINGP(path)) error_bad_path(path);
  return stat(STk_expand_file_name(STRING_CHARS(path)), s);
}

static int my_lstat(SCM path, struct stat *s)
{
  if (!STRINGP(path)) error_bad_path(path);
  return lstat(STk_expand_file_name(STRING_CHARS(path)), s);
}



int STk_dirp(const char *path)
{
  struct stat buf;

  if (stat(path, &buf) >= 0)
    return S_ISDIR(buf.st_mode);
  return FALSE;
}



#ifdef FIXME
//EG: void STk_whence(char *exec, char *path)
//EG: {
//EG:   char *p, *q, dir[MAX_PATH_LENGTH];
//EG:   struct stat buf;
//EG:
//EG:   if (ISABSOLUTE(exec)) {
//EG:     strncpy(path, exec, MAX_PATH_LENGTH);
//EG:     return;
//EG:   }
//EG:
//EG:   /* the executable path may be specified by relative path from the cwd. */
//EG:   /* Patch suggested by Shiro Kawai <shiro@squareusa.com> */
//EG:   if (strchr(exec, DIRSEP) != NULL) {
//EG:     getcwd(dir, MAX_PATH_LENGTH);
//EG:     sprintf(dir + strlen(dir), "%c%s", DIRSEP, exec);
//EG:     absolute(dir, path);
//EG:     return;
//EG:   }
//EG:
//EG: #ifdef FREEBSD
//EG:   /* I don't understand why this is needed */
//EG:   if (access(path, X_OK) == 0) {
//EG:     stat(path, &buf);
//EG:     if (!S_ISDIR(buf.st_mode)) return;
//EG:   }
//EG: #endif
//EG:
//EG:   p = getenv("PATH");
//EG:   if (p == NULL) {
//EG:     p = "/bin:/usr/bin";
//EG:   }
//EG:
//EG:   while (*p) {
//EG:     /* Copy the stuck of path in dir */
//EG:     for (q = dir; *p && *p != PATHSEP; p++, q++) *q = *p;
//EG:     *q = '\000';
//EG:
//EG:     if (!*dir) {
//EG:       /* patch suggested by Erik Ostrom <eostrom@vesuvius.ccs.neu.edu> */
//EG:       getcwd(path, MAX_PATH_LENGTH);
//EG:       sprintf(path + strlen(path), "%c%s", DIRSEP, exec);
//EG:     }
//EG:     else
//EG:       sprintf(path, "%s%c%s", dir, DIRSEP, exec);
//EG:
//EG:     sprintf(path, "%s%c%s", dir, DIRSEP, exec);
//EG:     if (access(path, X_OK) == 0) {
//EG:       stat(path, &buf);
//EG:       if (!S_ISDIR(buf.st_mode)) return;
//EG:     }
//EG:
//EG:     /* Try next path */
//EG:     if (*p) p++;
//EG:   }
//EG:   /* Not found. Set path to "" */
//EG:   path[0] = '\0';
//EG: }
#endif

/******************************************************************************
 *
 * Primitives
 *
 ******************************************************************************/

/*
<doc EXT winify-file-name
 * (winify-file-name fn)
 *
 * On Win32 system, when compiled with the Cygwin environment,
 * file names are internally represented in a POSIX-like internal form.
 * |Winify-file-bame| permits to obtain back the Win32 name of an interned
 * file name
 * @lisp
 * (winify-file-name "/tmp")
 *     => "C:\\\\cygwin\\\\tmp"
 * (list (getcwd) (winify-file-name (getcwd)))
 *     => ("//saxo/homes/eg/Projects/STklos"
 *         "\\\\\\\\saxo\\\\homes\\\\eg\\\\Projects\\\\STklos")
 * @end lisp
doc>
*/
DEFINE_PRIMITIVE("winify-file-name", winify_filename, subr1, (SCM _UNUSED(f)))
{
#ifdef WIN32
  char expanded[2 * MAX_PATH_LENGTH];

  if (!STRINGP(f)) error_bad_string(f);

  cygwin_conv_to_win32_path(STRING_CHARS(f), expanded);
  return STk_Cstring2string(expanded);
#else
  error_win32_primitive();
  return STk_void;
#endif
}


/*
<doc EXT posixify-file-name
 * (posixify-file-name fn)
 *
 * On Win32 system, when compiled with the Cygwin environment,
 * file names are internally represented in a POSIX-like internal form.
 * |posixify-file-bame| permits to obtain the interned file name from
 * its external form.
 * file name
 * @lisp
 * (posixify-file-name "C:\\\\cygwin\\\\tmp")
 *        => "/tmp"
 * @end lisp
doc>
*/
DEFINE_PRIMITIVE("posixify-file-name", posixify_filename, subr1, (SCM _UNUSED(f)))
{
#ifdef WIN32
  char expanded[2 * MAX_PATH_LENGTH];

  if (!STRINGP(f)) error_bad_string(f);

  cygwin_conv_to_posix_path(STRING_CHARS(f), expanded);
  return STk_Cstring2string(expanded);
#else
  error_win32_primitive();
  return STk_void;
#endif
}


/*
<doc EXT expand-file-name
 * (expand-file-name path)
 *
 * |Expand-file-name| expands the filename given in |path| to
 * an absolute path.
 * @lisp
 *   ;; Current directory is ~eg/stklos (i.e. /users/eg/stklos)
 *   (expand-file-name "..")            => "/users/eg"
 *   (expand-file-name "~eg/../eg/bin") => "/users/eg/bin"
 *   (expand-file-name "~/stklos)"      => "/users/eg/stk"
 * @end lisp
doc>
*/

DEFINE_PRIMITIVE("expand-file-name", expand_fn, subr1, (SCM s))
{
  if (!STRINGP(s)) error_bad_string(s);
  return STk_Cstring2string(STk_expand_file_name(STRING_CHARS(s)));
}


/*
<doc EXT canonical-file-name
 * (canonical-file-name path)
 *
 * Expands all symbolic links in |path| and returns its canonicalized
 * absolute path name. The resulting path does not have symbolic links.
 * If |path| doesn't designate a valid path name, |canonical-file-name|
 * returns |#f|.
doc>
*/
DEFINE_PRIMITIVE("canonical-file-name", canonical_path, subr1, (SCM str))
{
  if (!STRINGP(str)) error_bad_string(str);
  return STk_resolve_link(STRING_CHARS(str), 0);
}

/*
<doc EXT getcwd
 * (getcwd)
 *
 * Returns a string containing the current working directory.
doc>
*/
DEFINE_PRIMITIVE("getcwd", getcwd, subr0, (void))
{
  char buf[MAX_PATH_LENGTH], *s;
  SCM z;

  s = getcwd(buf, MAX_PATH_LENGTH);
  if (!s)
    error_posix(NULL, NULL);
  z = STk_Cstring2string(buf);

  return z;
}


/*
<doc EXT chdir
 * (chdir dir)
 *
 * Changes the current directory to the directory given in string |dir|.
doc>
*/
DEFINE_PRIMITIVE("chdir", chdir, subr1, (SCM s))
{
  if (!STRINGP(s)) error_bad_path(s);

  if (chdir(STk_expand_file_name(STRING_CHARS(s))) != 0)
    error_posix(s, NULL);

  return STk_void;
}

/*
<doc EXT make-directory
 * (make-directory dir)
 *
 * Create a directory with name |dir|.
doc>
*/
DEFINE_PRIMITIVE("make-directory", make_directory, subr1, (SCM path))
{
  mode_t mask;

  if (!STRINGP(path)) error_bad_path(path);
  umask(mask = umask(0));
  if (mkdir(STRING_CHARS(path), 0777) != 0)
    error_posix(path, NULL);
  return STk_void;
}

/*
<doc EXT delete-directory remove-directory
 * (remove-directory dir)
 * (delete-directory dir)
 *
 * Delete the directory with name |dir|.
 * @l
 * ,(bold "Note:") The name |remove-directory| is kept for compatibility.
doc>
*/
DEFINE_PRIMITIVE("remove-directory", remove_directory, subr1, (SCM path))
{
  if (!STRINGP(path)) error_bad_path(path);
  if (rmdir(STRING_CHARS(path)) != 0)
    error_posix(path, NULL);
  return STk_void;
}

/*
<doc EXT getpid
 * (getpid)
 *
 * Returns the system process number of the current program (i.e. the
 * Unix ,(emph "pid")) as an integer.
doc>
*/
DEFINE_PRIMITIVE("getpid", getpid, subr0, (void))
{
  return (MAKE_INT((int) getpid()));
}


/*
<doc EXT system
 * (system string)
 *
 * Sends the given |string| to the system shell |/bin/sh|. The result of
 * |system| is the integer status code the shell returns.
doc>
*/
DEFINE_PRIMITIVE("system", system, subr1, (SCM com))
{
  if (!STRINGP(com)) error_bad_string(com);
  return MAKE_INT(system(STRING_CHARS(com)));
}

/*
<doc EXT file-is-directory? file-is-regular? file-is-writable? file-is-readable? file-is-executable?
 * (file-is-directory?  string)
 * (file-is-regular?    string)
 * (file-is-readable?   string)
 * (file-is-writable?   string)
 * (file-is-executable? string)
 *
 * Returns |#t| if the predicate is true for the path name given in
 * |string|; returns |#f| otherwise (or if |string| denotes a file
 * which does not exist).
doc>
 */
DEFINE_PRIMITIVE("file-is-directory?", file_is_directoryp, subr1, (SCM f))
{
  struct stat info;

  if (my_stat(f, &info) != 0) return STk_false;
  return MAKE_BOOLEAN((S_ISDIR(info.st_mode)));
}


DEFINE_PRIMITIVE("file-is-regular?", file_is_regularp, subr1, (SCM f))
{
  struct stat info;

  if (my_stat(f, &info) != 0) return STk_false;
  return MAKE_BOOLEAN((S_ISREG(info.st_mode)));
}


DEFINE_PRIMITIVE("file-is-readable?", file_is_readablep, subr1, (SCM f))
{
  return my_access(f, R_OK);
}


DEFINE_PRIMITIVE("file-is-writable?", file_is_writablep, subr1, (SCM f))
{
  return my_access(f, W_OK);
}


DEFINE_PRIMITIVE("file-is-executable?", file_is_executablep, subr1, (SCM f))
{
  return my_access(f, X_OK);
}



/*
<doc R7RS file-exists?
 * (file-exists? string)
 *
 * Returns |#t| if the path name given in |string| denotes an existing file;
 * returns |#f| otherwise.
doc>
 */
DEFINE_PRIMITIVE("file-exists?", file_existsp, subr1, (SCM f))
{
  struct stat info;

  return MAKE_BOOLEAN(my_lstat(f, &info) == 0);
}

/*
<doc EXT file-size
 * (file-size string)
 *
 * Returns the size of the file whose path name is given in
 * |string|.If |string| denotes a file which does not exist,
 * |file-size| returns |#f|.
doc>
 */

DEFINE_PRIMITIVE("file-size", file_size, subr1, (SCM f))
{
  struct stat info;

  if (my_stat(f, &info) != 0) return STk_false;
  return STk_long2integer((long) info.st_size);
}

/*
<doc EXT glob
 * (glob pattern ...)
 *
 * |Glob| performs file name ``globbing'' in a fashion similar to the
 * csh shell. |Glob| returns a list of the filenames that match at least
 * one of |pattern| arguments.  The |pattern| arguments may contain
 * the following special characters:
 * ,(itemize
 * (item [|?| Matches any single character.])
 * (item [|*| Matches any sequence of zero or more characters.])
 * (item [|\[chars\]| Matches any single character in |chars|.
 * If chars contains a sequence of the form |a-b| then any character
 * between |a| and |b| (inclusive) will match.])
 * (item [|\\x| Matches the character |x|.])
 * (item [|{a,b,...}| Matches any of the strings |a|, |b|, etc.])
 * )
 *
 * As with csh, a '.' at the beginning of a file's name or just after
 * a '/ must be matched explicitly or with a |@{@}| construct.
 * In addition, all '/' characters must be matched explicitly.
 * @l
 * If the first character in a pattern is '~' then it refers to
 * the home directory of the user whose name follows the '~'.
 * If the '~' is followed immediately by `/' then the value of
 * the environment variable HOME is used.
 * @l
 * |Glob| differs from csh globbing in two ways.  First, it does not
 * sort its result list (use the |sort| procedure if you want the list
 * sorted).
 * Second, |glob| only returns the names of files that actually exist;
 * in csh no check for existence is made unless a pattern contains a
 * |?|, |*|, or |\[\]| construct.
doc>
*/
DEFINE_PRIMITIVE("glob", glob, vsubr, (int argc, SCM *argv))
{
  return STk_do_glob(argc, argv);
}


/*
<doc R7RS delete-file remove-file
 * (delete-file string)
 *
 * Removes the file whose path name is given in |string|.
 * The result of |delete-file| is ,(emph "void").
 * @l
 * This function is also called |remove-file| for compatibility
 * reasons. ,(index "remove-file")
doc>
*/
#define do_remove(filename)                             \
  if (!STRINGP(filename)) error_bad_string(filename);   \
  if (remove(STRING_CHARS(filename)) != 0)              \
    error_posix(filename, NULL);                        \
  return STk_void;                                      \

DEFINE_PRIMITIVE("delete-file", delete_file, subr1, (SCM filename))
{
  do_remove(filename);
}

DEFINE_PRIMITIVE("remove-file", remove_file, subr1, (SCM filename))
{
  do_remove(filename);
}


/*
<doc EXT rename-file
 * (rename-file string1 string2)
 *
 * Renames the file whose path-name is |string1| to a file whose path-name is
 * |string2|. The result of |rename-file| is ,(emph "void").
doc>
*/
DEFINE_PRIMITIVE("rename-file", rename_file, subr2, (SCM filename1, SCM filename2))
{
  if (!STRINGP(filename1)) error_bad_string(filename1);
  if (!STRINGP(filename2)) error_bad_string(filename2);
  if (rename(STRING_CHARS(filename1), STRING_CHARS(filename2)) != 0)
    error_posix(filename1, filename2);
  return STk_void;
}


/*
<doc EXT directory-files
 * (directory-files path)
 *
 * Returns the list of the files in the directory |path|. Directories ,(q ".")
 * and ,(q "..") don't appear in the result.
doc>
*/
DEFINE_PRIMITIVE("directory-files", directory_files, subr1, (SCM dirname))
{
  char *path;
  DIR* dir;
  SCM res = STk_nil;
  struct dirent *d;

  if (!STRINGP(dirname)) error_bad_string(dirname);
  path = STk_expand_file_name(STRING_CHARS(dirname));
  dir  = opendir(path);
  if (!dir) error_posix(dirname, NULL);

  for (d = readdir(dir); d ; d = readdir(dir)) {
    if (d->d_name[0] == '.')
      if ((d->d_name[1] == '\0') || (d->d_name[1] == '.' && d->d_name[2] == '\0'))
        continue;
    res = STk_cons(STk_Cstring2string(d->d_name), res);
  }
  closedir(dir);
  return STk_dreverse(res);
}



/*
<doc EXT copy-file
 * (copy-file string1 string2)
 *
 * Copies the file whose path-name is |string1| to a file whose path-name is
 * |string2|. If the file |string2| already exists, its content prior
 * the call to |copy-file| is lost. The result of |copy-file| is ,(emph "void").
doc>
*/
DEFINE_PRIMITIVE("copy-file", copy_file, subr2, (SCM filename1, SCM filename2))
{
  char buff[1024];
  int f1, f2, n;

  /* Should I use sendfile on Linux here? */
  if (!STRINGP(filename1)) error_bad_string(filename1);
  if (!STRINGP(filename2)) error_bad_string(filename2);

  f1 = open(STRING_CHARS(filename1), O_RDONLY);
  if (f1 == -1) error_posix(filename1, NULL);
  f2 = open(STRING_CHARS(filename2), O_WRONLY|O_CREAT|O_TRUNC, 0666);
  if (f2 == -1) error_posix(filename2, NULL);

  while ((n = read(f1, buff, MAXBUFF)) > 0) {
    if ((n < 0) || (write(f2, buff, n) < n)) {
      close(f1); close(f2);
      error_posix(filename1, filename2);
    }
  }

  close(f1); close(f2);
  return STk_void;
}


/*
<doc EXT temporary-file-name
 * (temporary-file-name)
 *
 * Generates a unique temporary file name. The value returned by
 * |temporary-file-name| is the newly generated name of |#f|
 * if a unique name cannot be generated.
doc>
*/
DEFINE_PRIMITIVE("temporary-file-name", tmp_file, subr0, (void))
{
#ifdef WIN32
  char buff[MAX_PATH_LENGTH], *s;

  s = tmpnam(buff);
  return s ? STk_Cstring2string(s) : STk_false;
#else
  static int cpt=0, pid;
  static char *tmpdir;
  char buff[MAX_PATH_LENGTH];
  MUT_DECL(tmpnam_mutex);

  MUT_LOCK(tmpnam_mutex);
  if (cpt == 0) {
    /* First call: initialize tmpdir and pid */
    char *tmp = getenv("TMPDIR");

    tmpdir = (tmp) ? tmp : "/tmp";
    pid    = (int) getpid();
  }

  for ( ; ; ) {

    sprintf(buff, "%s/stklos%05d-%05x", tmpdir, pid, cpt++);
    if (cpt > 100000)           /* arbitrary limit to avoid infinite search */
      return STk_false;
    if (access(buff, F_OK) == -1) break;
  }
  MUT_UNLOCK(tmpnam_mutex);

  return STk_Cstring2string(buff);
#endif
}


/*
<doc EXT register-exit-function!
 * (register-exit-function! proc)
 *
 * This function registers |proc| as an exit function. This function will
 * be called when the program exits. When called, |proc| will be passed one
 * parmater which is the status given to the |exit| function (or 0 if the
 * programe terminates normally). The result of  |register-exit-function!|
 * is undefined.
 * @lisp
 * (let* ((tmp (temporary-file-name))
 *        (out (open-output-file tmp)))
 *   (register-exit-function! (lambda (n)
 *                              (when (zero? n)
 *                                (delete-file tmp))))
 *   out)
 * @end lisp
doc>
*/
MUT_DECL(at_exit_mutex)         /* The exit mutex */

DEFINE_PRIMITIVE("register-exit-function!", at_exit, subr1, (SCM proc))
{
  if (STk_procedurep(proc) == STk_false) STk_error("bad procedure ~S", proc);

  MUT_LOCK(at_exit_mutex);
  exit_procs = STk_cons(proc, exit_procs);
  MUT_UNLOCK(at_exit_mutex);
  return STk_void;
}


DEFINE_PRIMITIVE("%pre-exit", pre_exit, subr1, (SCM retcode))
{
  /* Execute the at-exit handlers */
  MUT_LOCK(at_exit_mutex);
  for (  ; !NULLP(exit_procs); exit_procs = CDR(exit_procs))
    STk_C_apply(CAR(exit_procs), 1, retcode);
  MUT_UNLOCK(at_exit_mutex);

  /* Flush all bufers */
  STk_close_all_ports();

  return STk_void;
}

/*
<doc EXT exit
 * (exit)
 * (exit ret-code)
 *
 * Exits the program with the specified integer return code. If |ret-code|
 * is omitted, the program terminates with a return code of 0.
 * If  program has registered exit functions with |register-exit-function!|,
 * they are called (in an order which is the reverse of their call order).
 * @l
 * ,(bold "Note:") The ,(stklos) |exit| primitive accepts also an
 * integer value as parameter (,(rseven) accepts only a boolean).
doc>
*/
DEFINE_PRIMITIVE("exit", exit, subr01, (SCM retcode))
{
  long ret = 0;
  SCM cond;

  if (retcode) {
    if (BOOLEANP(retcode)) {
      ret = (retcode != STk_true);
    } else {
      ret = STk_integer_value(retcode);
      if (ret == LONG_MIN) STk_error("bad return code ~S", retcode);
    }
  }

  /* Raise a &exit-r7rs condition  with the numeric value of the exit code*/
  cond = STk_make_C_cond(STk_exit_condition, 1, MAKE_INT(ret));
  STk_raise(cond);

  return STk_void; /* never reached */
}

/*
<doc EXT emergency-exit
 * (emergency-exit)
 * (emergency-exit ret-code)
 *
 * Terminates the program without running any outstanding
 * dynamic-wind ,(emph "after") procedures and communicates an exit
 * value to the operating system in the same manner as |exit|.
 * @l
 * ,(bold "Note:") The ,(stklos) |emergency-exit| primitive accepts also an
 * integer value as parameter (,(rseven) accepts only a boolean).
doc>
*/
DEFINE_PRIMITIVE("emergency-exit", emergency_exit, subr01, (SCM retcode))
{
  long ret = 0;

  if (retcode) {
    if (BOOLEANP(retcode)) {
      ret = (retcode != STk_true);
    } else {
      ret = STk_integer_value(retcode);
      if (ret == LONG_MIN) STk_error("bad return code ~S", retcode);
    }
  }
  _exit(ret);

  return STk_void; /* never reached */
}


/*
<doc EXT machine-type
 * (machine-type)
 *
 * Returns a string identifying the kind of machine which is running the
 * program. The result string is of the form
 * |[os-name]-[os-version]-[cpu-architecture]|.
doc>
*/
DEFINE_PRIMITIVE("machine-type", machine_type, subr0, (void))
{
  return STk_Cstring2string(BUILD_MACHINE);
}


#ifdef FIXME
//EG: #ifndef HZ
//EG: #define HZ 60.0
//EG: #endif
//EG:
//EG: #ifdef CLOCKS_PER_SEC
//EG: #  define TIC CLOCKS_PER_SEC
//EG: #else
//EG: #  define TIC HZ
//EG: #endif
//EG:
//EG: PRIMITIVE STk_get_internal_info(void)
//EG: {
//EG:   SCM z = STk_makevect(7, STk_nil);
//EG:   long allocated, used, calls;
//EG:
//EG:   /* The result is a vector which contains
//EG:    *      0 The total cpu used in ms
//EG:    *      1 The number of cells currently in use.
//EG:    *    2 Total number of allocated cells
//EG:    *      3 The number of cells used since the last call to get-internal-info
//EG:    *      4 Number of gc calls
//EG:    *    5 Total time used in the gc
//EG:    *      6 A boolean indicating if Tk is initialized
//EG:    */
//EG:
//EG:   STk_gc_count_cells(&allocated, &used, &calls);
//EG:
//EG:   VECT(z)[0] = STk_makenumber(STk_my_time());
//EG:   VECT(z)[1] = STk_makeinteger(used);
//EG:   VECT(z)[2] = STk_makeinteger(allocated);
//EG:   VECT(z)[3] = STk_makenumber((double) STk_alloc_cells);
//EG:   VECT(z)[4] = STk_makeinteger(calls);
//EG:   VECT(z)[5] = STk_makenumber((double) STk_total_gc_time);
//EG: #ifdef USE_TK
//EG:   VECT(z)[6] = Tk_initialized ? STk_true: STk_false;
//EG: #else
//EG:   VECT(z)[6] = STk_false;
//EG: #endif
//EG:
//EG:   STk_alloc_cells = 0;
//EG:   return z;
//EG: }
#endif


/*
<doc EXT date
 * (date)
 *
 * Returns the current date in a string
doc>
*/
DEFINE_PRIMITIVE("date", date, subr0, (void))
{
  time_t t   = time(NULL);
  char * res;

  res = ctime(&t);
  res[strlen(res)-1] = '\0'; /* to "delete" the newline added by asctime */
  return STk_Cstring2string(res);
}


/*
<doc EXT clock
 * (clock)
 *
 * Returns an approximation of processor time, in milliseconds, used so far by the
 * program.
doc>
 */
DEFINE_PRIMITIVE("clock", clock, subr0, (void))
{
  return STk_double2real((double) clock() /
                         CLOCKS_PER_SEC * (double) TIME_DIV_CONST);
}


/*
<doc EXT current-seconds
 * (current-seconds)
 *
 * Returns the time since the Epoch (that is 00:00:00 UTC, January 1, 1970),
 * measured in seconds.
 * @l
 * @bold("Note"): This ,(stklos) function should not be confused with
 * the ,(rseven)  primitive |current-second| which returns an inexact number
 * and whose result is expressed using  the International Atomic Time
 * instead of UTC.
 *
doc>
 */
DEFINE_PRIMITIVE("current-seconds", current_seconds, subr0, (void))
{
  return STk_ulong2integer(time(NULL));
}

/*
<doc R7RS current-second
 * (current-second)
 *
 * Returns an inexact number representing the current time on the
 * International Atomic Time (TAI) scale.  The value 0.0 represents
 * midnight on January 1, 1970 TAI (equivalent to ten seconds before
 * midnight Universal Time) and the value 1.0 represents one TAI
 * second later.
doc>
 */

/* Offset: https://fr.wikipedia.org/wiki/Temps_atomique_international */
#define TAI_OFFSET +37.0L

DEFINE_PRIMITIVE("current-second", current_second, subr0, (void))
{
  /* R7RS states: Neither high accuracy nor high precision are
   * required; in particular, returning Coordinated Universal Time plus
   * a suitable constant might be the best an implementation can do.
   */
  struct timespec now;

  clock_gettime(CLOCK_REALTIME, &now);
  return STk_double2real(TAI_OFFSET +
                         (double) now.tv_sec +
                         1.0E-9 * (double) now.tv_nsec);
}


/*
<doc EXT current-time
 * (current-time)
 *
 * Returns a time object corresponding to the current time.
doc>
*/
DEFINE_PRIMITIVE("current-time", current_time, subr0, (void))
{
  struct timespec now;
  SCM argv[4];

  clock_gettime(CLOCK_REALTIME, &now);

  argv[3] = time_type;
  argv[2] = STk_intern("time-utc");
  argv[1] = STk_long2integer(now.tv_sec);
  argv[0] = STk_long2integer(now.tv_nsec);
  return STk_make_struct(4, &argv[3]);
}



/*
<doc EXT sleep
 * (sleep n)
 *
 * Suspend the execution of the program for at |ms| milliseconds. Note that due
 * to system clock resolution, the pause may be a little bit longer. If a
 * signal arrives during the pause, the execution may be resumed.
 *
doc>
*/
DEFINE_PRIMITIVE("sleep", sleep, subr1, (SCM ms))
{
  long n = STk_integer_value(ms);
  struct timespec ts;

  if (n == LONG_MIN)
    error_bad_int_or_out_of_bounds(ms);

  ts.tv_sec  = n / 1000;
  ts.tv_nsec = (n % 1000) * 1000000;

  nanosleep(&ts, NULL);
  return STk_void;
}


/*
<doc EXT seconds->date
 * (seconds->date n)
 *
 * Convert the date |n| expressed as a number of seconds since the Epoch
 * to a date.
doc>
*/
DEFINE_PRIMITIVE("%seconds->date", seconds2date, subr1, (SCM seconds))
{
  int overflow;
  SCM argv[11];
  struct tm *t;
  time_t tt;

  tt = (time_t) STk_integer2int32(seconds, &overflow);

  if (overflow) error_bad_int_or_out_of_bounds(seconds);

  t = localtime(&tt);
  argv[10]  = date_type;
  argv[9]  = MAKE_INT(t->tm_sec);
  argv[8]  = MAKE_INT(t->tm_min);
  argv[7]  = MAKE_INT(t->tm_hour);
  argv[6]  = MAKE_INT(t->tm_mday);
  argv[5]  = MAKE_INT(t->tm_mon + 1);
  argv[4]  = MAKE_INT(t->tm_year + 1900);
  argv[3]  = MAKE_INT(t->tm_wday);
  argv[2]  = MAKE_INT(t->tm_yday + 1);
  argv[1]  = MAKE_INT(t->tm_isdst);
#ifdef DARWIN
  argv[0]  = MAKE_INT(0);       /* Cannot figure how to find the timezone */
#else
  argv[0]  = STk_long2integer(timezone);
#endif
  return STk_make_struct(11, &argv[10]);
}


/*
<doc EXT date->seconds
 * (date->seconds d)
 *
 * Convert the date |d| to the number of seconds since the ,(emph "Epoch").
doc>
*/
DEFINE_PRIMITIVE("date->seconds", date2seconds, subr1, (SCM date))
{
  struct tm t;
  time_t n;
  SCM *p;

  if (!STRUCTP(date) || STRUCT_TYPE(date) != date_type)
    STk_error("bad date ~S", date);

  p = (SCM *) &(STRUCT_SLOTS(date));
  t.tm_sec   = STk_integer_value(*p++);
  t.tm_min   = STk_integer_value(*p++);
  t.tm_hour  = STk_integer_value(*p++);
  t.tm_mday  = STk_integer_value(*p++);
  t.tm_mon   = STk_integer_value(*p++) - 1;
  t.tm_year  = STk_integer_value(*p++) - 1900;
  t.tm_isdst = -1;                      /* to ignore DST */

  n = mktime(&t);
  if (n == (time_t)(-1)) STk_error("cannot convert date to seconds (~S)", date);

  return STk_double2real((double) n);
}



DEFINE_PRIMITIVE("%seconds->string", date2string, subr2, (SCM fmt, SCM seconds))
{
  char buffer[1024];
  struct tm *p;
  time_t tt;
  int len, overflow;

  tt = (time_t) STk_integer2int32(seconds, &overflow);

  if (!STRINGP(fmt)) error_bad_string(fmt);
  if (overflow)      error_bad_int_or_out_of_bounds(seconds);

  p   = localtime(&tt);
  len = strftime(buffer, 1023, STRING_CHARS(fmt), p);

  if (len > 0)
    return STk_Cstring2string(buffer);
  else
    STk_error("buffer too short!");

  return STk_void; /* never reached */
}


/*
<doc EXT running-os
 * (running-os)
 *
 * Returns the name of the underlying Operating System which is running
 * the program.
 * The value returned by |runnin-os| is a symbol. For now, this procedure
 * returns either |unix|, |android|, |windows|, or |cygwin-windows|.
doc>
*/
DEFINE_PRIMITIVE("running-os", running_os, subr0, (void))
{
#ifdef WIN32
# ifdef __CYGWIN32__
  return STk_intern("cygwin-windows");
# else
  return STk_intern("windows");
# endif
#else
#  ifdef __ANDROID__
  return STk_intern("android");
#  else
  return STk_intern("unix");
#endif
#endif
}



/*
<doc EXT getenv
 * (getenv str)
 * (getenv)
 *
 * Looks for the environment variable named |str| and returns its
 * value as a string, if it exists. Otherwise, |getenv| returns |#f|.
 * If |getenv| is called without parameter, it returns the list of
 * all the environment variables accessible from the program as an
 * A-list.
 * @lisp
 * (getenv "SHELL")
 *      => "/bin/zsh"
 * (getenv)
 *      => (("TERM" . "xterm") ("PATH" . "/bin:/usr/bin") ...)
 * @end lisp
doc>
 */
static SCM build_posix_environment(char **env)
{
  SCM l;

  for (l=STk_nil; *env; env++) {
    char *s, *p;

    s = *env; p = strchr(s, '=');
    if (p)
      l = STk_cons(STk_cons(STk_makestring(p-s, s), STk_Cstring2string(p+1)),l);
  }
  return l;
}


DEFINE_PRIMITIVE("getenv", getenv, subr01, (SCM str))
{
  char *tmp;

  if (str) {            /* One parameter: find the value of the given variable */
    if (!STRINGP(str)) error_bad_string(str);

    tmp = getenv(STRING_CHARS(str));
    return tmp ? STk_Cstring2string(tmp) : STk_false;
  } else {              /* No parameter: give the complete environment */
    extern char **environ;
    return build_posix_environment(environ);
  }
}

/*
<doc EXT setenv!
 * (setenv! var value)
 *
 * Sets the environment variable |var| to |value|. |Var| and
 * |value| must be strings. The result of |setenv!| is ,(emph "void").
doc>
 */
DEFINE_PRIMITIVE("setenv!", setenv, subr2, (SCM var, SCM value))
{
  char *s;

  if (!STRINGP(var))                  error_bad_string(var);
  if (strchr(STRING_CHARS(var), '=')) STk_error("variable ~S contains a '='", var);
  if (!STRINGP(value))                STk_error("value ~S is not a string", value);

  s = STk_must_malloc(strlen(STRING_CHARS(var))   +
                      strlen(STRING_CHARS(value)) + 2); /* 2 because of '=' & \0 */
  sprintf(s, "%s=%s", STRING_CHARS(var), STRING_CHARS(value));
  putenv(s);
  return STk_void;
}
/*
<doc EXT unsetenv!
 * (unsetenv! var)
 *
 * Unsets the environment variable |var|. |Var| must be a string.
 * The result of |unsetenv!| is ,(emph "void").
doc>
 */
DEFINE_PRIMITIVE("unsetenv!", unsetenv, subr1, (SCM var))
{
#ifndef SOLARIS
  if (!STRINGP(var)) error_bad_string(var);
  unsetenv(STRING_CHARS(var));
  return STk_void;
#else
  /* not exactly the same since getenv will not return #f after that */
  return STk_setenv(var, STk_Cstring2string(""));
#endif
}

/*
<doc EXT hostname
 * (hostname)
 *
 * Return the  host name of the current processor as a string.
doc>
*/
DEFINE_PRIMITIVE("hostname", hostname, subr0, (void))
{
  char buff[256];

  if (gethostname(buff, 256) < 0)
    buff[255] = '0';
  return STk_Cstring2string(buff);
}

/*
<doc EXT pause
 * (pause)
 *
doc>
*/
DEFINE_PRIMITIVE("pause", pause, subr0, (void))
{
  pause();
  return STk_void;
}





/*
 * Undocumented primitives
 *
 */

DEFINE_PRIMITIVE("%library-prefix", library_prefix, subr0, (void))
{
  return STk_Cstring2string(PREFIXDIR);
}

DEFINE_PRIMITIVE("%shared-suffix", shared_suffix, subr0, (void))
{
  return STk_Cstring2string(SHARED_SUFFIX);
}

DEFINE_PRIMITIVE("%shared-library-suffix", shared_library_suffix, subr0, (void))
{
  return STk_Cstring2string(SHARED_LIB_SUFFIX);
}

DEFINE_PRIMITIVE("%chmod", change_mode, subr2, (SCM file, SCM value))
{
  long mode = STk_integer_value(value);

  if (!STRINGP(file))   error_bad_path(file);
  if (mode < 0 || mode > 0777) error_bad_int_or_out_of_bounds(value);

  return MAKE_BOOLEAN(chmod(STRING_CHARS(file), mode) == 0);
}


DEFINE_PRIMITIVE("%big-endian?", big_endianp, subr0, (void))
{
  int i = 1;
  char *p = (char *)&i;

  return MAKE_BOOLEAN(p[0] != 1);
}


DEFINE_PRIMITIVE("%get-locale", get_locale, subr0, (void))
{
  char *str = setlocale(LC_ALL, NULL);

  return str? STk_Cstring2string(str) : STk_false;
}


DEFINE_PRIMITIVE("%uname", uname, subr0, (void))
{
  struct utsname name;
  SCM z;

  if (uname(&name) != 0) {
    error_posix(NULL, NULL);
  }
  z = STk_makevect(5, STk_void);
  VECTOR_DATA(z)[0] = STk_Cstring2string(name.sysname);
  VECTOR_DATA(z)[1] = STk_Cstring2string(name.nodename);
  VECTOR_DATA(z)[2] = STk_Cstring2string(name.release);
  VECTOR_DATA(z)[3] = STk_Cstring2string(name.version);
  VECTOR_DATA(z)[4] = STk_Cstring2string(name.machine);

  return z;
}


int STk_init_system(void)
{
  SCM current_module = STk_STklos_module;

  /* Create the system-date structure-type */
  date_type =  STk_make_struct_type(STk_intern("%date"),
                                    STk_false,
                                    LIST10(STk_intern("second"),
                                           STk_intern("minute"),
                                           STk_intern("hour"),
                                           STk_intern("day"),
                                           STk_intern("month"),
                                           STk_intern("year"),
                                           STk_intern("week-day"),
                                           STk_intern("year-day"),
                                           STk_intern("dst"),
                                           STk_intern("tz")));
  STk_define_variable(STk_intern("%date"), date_type, current_module);

  /* Create the time structure-type */
  time_type =  STk_make_struct_type(STk_intern("%time"),
                                    STk_false,
                                    LIST3(STk_intern("type"),
                                          STk_intern("second"),
                                          STk_intern("nanosecond")));
  STk_define_variable(STk_intern("%time"), time_type, current_module);

  /* for SRFI-19: */
  STk_define_variable(STk_intern("time-tai"),       STk_intern("time-tai"), current_module);
  STk_define_variable(STk_intern("time-utc"),       STk_intern("time-utc"), current_module);
  STk_define_variable(STk_intern("time-monotonic"), STk_intern("time-monotonic"), current_module);
  STk_define_variable(STk_intern("time-thread"),    STk_intern("time-thread"), current_module);
  STk_define_variable(STk_intern("time-process"),   STk_intern("time-process"), current_module);
  STk_define_variable(STk_intern("time-duration"),  STk_intern("time-duration"), current_module);
                      
  /* Declare primitives */
  ADD_PRIMITIVE(clock);
  ADD_PRIMITIVE(date);
  ADD_PRIMITIVE(current_seconds);
  ADD_PRIMITIVE(current_second);
  ADD_PRIMITIVE(current_time);
  ADD_PRIMITIVE(sleep);
  ADD_PRIMITIVE(seconds2date);
  ADD_PRIMITIVE(date2seconds);
  ADD_PRIMITIVE(date2string);
  ADD_PRIMITIVE(running_os);
  ADD_PRIMITIVE(getenv);
  ADD_PRIMITIVE(setenv);
  ADD_PRIMITIVE(unsetenv);
  ADD_PRIMITIVE(hostname);
  ADD_PRIMITIVE(library_prefix);
  ADD_PRIMITIVE(shared_suffix);
  ADD_PRIMITIVE(shared_library_suffix);
  ADD_PRIMITIVE(change_mode);

  ADD_PRIMITIVE(getcwd);
  ADD_PRIMITIVE(chdir);
  ADD_PRIMITIVE(make_directory);
  ADD_PRIMITIVE(remove_directory);
  ADD_PRIMITIVE(directory_files);
  ADD_PRIMITIVE(getpid);
  ADD_PRIMITIVE(system);

  ADD_PRIMITIVE(file_is_directoryp);
  ADD_PRIMITIVE(file_is_regularp);
  ADD_PRIMITIVE(file_is_readablep);
  ADD_PRIMITIVE(file_is_writablep);
  ADD_PRIMITIVE(file_is_executablep);
  ADD_PRIMITIVE(file_existsp);
  ADD_PRIMITIVE(file_size);
  ADD_PRIMITIVE(glob);
  ADD_PRIMITIVE(expand_fn);
  ADD_PRIMITIVE(canonical_path);

  ADD_PRIMITIVE(remove_file);
  ADD_PRIMITIVE(delete_file);
  ADD_PRIMITIVE(rename_file);
  ADD_PRIMITIVE(copy_file);
  ADD_PRIMITIVE(tmp_file);
  ADD_PRIMITIVE(pre_exit);
  ADD_PRIMITIVE(exit);
  ADD_PRIMITIVE(emergency_exit);
  ADD_PRIMITIVE(at_exit);
  ADD_PRIMITIVE(machine_type);

  ADD_PRIMITIVE(winify_filename);
  ADD_PRIMITIVE(posixify_filename);

  ADD_PRIMITIVE(pause);
  ADD_PRIMITIVE(big_endianp);
  ADD_PRIMITIVE(get_locale);
  ADD_PRIMITIVE(uname);
  return TRUE;
}
