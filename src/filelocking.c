/*
This file is part of mfaktc (mfakto).
Copyright (C) 2009 - 2014  Oliver Weihe (o.weihe@t-online.de)
                           Bertram Franz (bertramf@gmx.net)

mfaktc (mfakto) is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

mfaktc (mfakto) is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with mfaktc (mfakto).  If not, see <http://www.gnu.org/licenses/>.
*/

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#if defined _MSC_VER || defined __MINGW32__
  #include <Windows.h>
  #include <io.h>
  #include <direct.h>
  #include <process.h>
  #undef open
  #undef close
  #define open _open
  #define close _close
  #define getcwd _getcwd
  #define chdir _chdir
  #define getdrive _getdrive
  #define chdrive _chdrive
  #define getpid _getpid
  #define fdopen _fdopen
  #define fdclose _fdclose
  #define MODE _S_IREAD | _S_IWRITE
  #define O_RDONLY _O_RDONLY
#else
  #include <unistd.h>
  #include <sched.h>
  #define MODE S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
  static void Sleep(unsigned int ms)
  {
    struct timespec ts;
    ts.tv_sec  = (time_t) ms/1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
  }
  #define getdrive() 0
  #define chdrive(x) 0
#endif

#define MAX_LOCKED_FILES 5

typedef struct _lockinfo
{
  int       lockfd;
  FILE *    lockfile;
  FILE *    open_file;
  char      lock_filename[256];
} lockinfo;

static unsigned int num_locked_files = 0;
static lockinfo     locked_files[MAX_LOCKED_FILES];
static char* current_dir = NULL;
static int   current_drive = 0;

/* See if the given file exists */

int file_exists (char	*filename)
{
	int fd;
  if (current_dir == NULL) {
      current_dir = getcwd(NULL, 0);
      current_drive = getdrive();
  }
  if (chdrive(current_drive) || current_dir == NULL || chdir(current_dir)) {
      fprintf(stderr, "\nWarning: Current directory \"%s\" is not available.\n", current_dir);
  }
  fd = open(filename, O_RDONLY);
//  printf ("file_exists(%s)\n", filename);
	if (fd < 0) return 0;
	close(fd);
	return 1;
}

/* Allocates a slot in the locked_files array */
int _just_lock (const char *path, int retries)
{
  unsigned int i, t;
  int lockfd;
  int idx = num_locked_files;
  int pid = (int)getpid();
  FILE *f;

  if (strlen(path) > 250)
  {
    fprintf(stderr, "Cannot lock %.250s: Name too long.\n", path);
    return -1;
  }

  if (num_locked_files >= MAX_LOCKED_FILES)
  {
    fprintf(stderr, "Cannot lock %.250s: Too many locked files.\n", path);
    return -1;
  }

  sprintf(locked_files[idx].lock_filename, "%.250s.lck", path);

  if (current_dir == NULL) {
      current_dir = getcwd(NULL, 0);
      current_drive = getdrive();
  }
  if (chdrive(current_drive) || current_dir == NULL || chdir(current_dir)) {
      fprintf(stderr, "\nWarning: Current directory \"%s\" is not available.\n", current_dir);
  }
//  printf("fopen_and_lock(%s)\n", path);
  for(i = 0, t = 30; i <= retries; i++)
  {
    if ((lockfd = open(locked_files[idx].lock_filename, O_EXCL | O_CREAT, MODE)) < 0)
    {
      if (errno == EEXIST)
      {
        if (i==50) fprintf(stderr, "%.250s already exists, waiting ...\n", locked_files[idx].lock_filename);
        if (t<1000) t *= 2; // slowly increase sleep time up to 1 sec
        Sleep(t);
        continue;
      }
      else
      {
        perror("Cannot open lockfile");
        break;
      }
    }
    break;
  }

  locked_files[idx].lockfd = lockfd;

  if (lockfd < 0)
  {
    num_locked_files--;
    return -1;
  }

  if (i > 0)
  {
    printf("Locked %.250s\n", path);
  }

  // Write our PID for debugging. Not used by any functionality yet, so no
  // error checks.
  // A future version can read the existing lock and complain about the exact
  // PID holding it.
  f = fdopen(lockfd, "w");
  if (f)
  {
    fprintf(f, "%d\n", pid);
    fflush(f);
    locked_files[idx].lockfile = f;
  }

  return ++num_locked_files;
}

void _just_unlock(int idx)
{
  if (close(locked_files[idx].lockfd) != 0) perror("Failed to close lockfile");
  if (remove(locked_files[idx].lock_filename)!= 0) perror("Failed to delete lockfile");

  memmove(&locked_files[idx], &locked_files[idx+1],
          (num_locked_files - idx - 1) * sizeof(lockinfo));
  num_locked_files--;

  // Fill the rest with zeros to avoid garbage
  memset(&locked_files[num_locked_files], 0, sizeof(lockinfo) * (MAX_LOCKED_FILES - num_locked_files));
}

FILE *fopen_and_lock(const char *path, const char *mode)
{
  int idx;
  FILE *f;

  if ((idx = _just_lock(path, INT_MAX)) < 0)
  {
    return NULL;
  }

  f=fopen(path, mode);
  if (f)
  {
    locked_files[idx].open_file = f;
  }
  else
  {
    _just_unlock(idx);
  }

  return f;
}

int unlock_and_fclose(FILE *f)
{
  unsigned int i;
  int found = 0;
  int ret;

  if (f == NULL) return -1;

  if (current_dir == NULL) {
      current_dir = getcwd(NULL, 0);
      current_drive = getdrive();
  }
  if (chdrive(current_drive) || current_dir == NULL || chdir(current_dir)) {
      fprintf(stderr, "\nWarning: Current directory \"%s\" is not available.\n", current_dir);
  }

  for (i=0; i<num_locked_files; i++)
  {
    if (locked_files[i].open_file == f)
    {
      found = 1;
      break;
    }
  }

  if (!found)
  {
    fprintf(stderr, "unlock_and_fclose: File not found in locked files list.\n");
    if (f)
    {
      ret = fclose(f);
    }
    return ret;
  }

  _just_unlock(i);
  if (f)
  {
    ret = fclose(f);
  }
  return ret;
}

const char* just_lock(const char *path, int retries)
{
  int idx;

  if ((idx = _just_lock(path, retries)) < 0)
  {
    return NULL;
  }

  return locked_files[idx-1].lock_filename;
}

int just_unlock(const char *lock_filename)
{
  unsigned int i;
  int found = 0;

  for (i=0; i<num_locked_files; i++)
  {
    if (strcmp(locked_files[i].lock_filename, lock_filename) == 0)
    {
      found = 1;
      break;
    }
  }

  if (!found)
  {
    fprintf(stderr, "just_unlock: Lockfile not found in locked files list.\n");
    return -1;
  }

  if (locked_files[i].open_file)
  {
    fprintf(stderr, "just_unlock: Warning: Lockfile %.250s has an open file associated.\n", lock_filename);
  }
  _just_unlock(i);
  return 0;
}
