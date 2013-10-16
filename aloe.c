/*
 * Copyright 2013 Clemson University
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <fnmatch.h>
#include <utmpx.h>
#ifdef __linux__
# include <linux/version.h>
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13)
#  define USE_INOTIFY 1
# endif
#endif
#ifdef USE_INOTIFY
# include <sys/inotify.h>
static int inotify_fd, inotify_wd;
#endif

#define VERSION "0.1.2"

static void usage(FILE *file, const char *arg0) {
  fprintf(file,
    "Usage: %s [options] command ...\n"
    "Options:\n"
    "  -h          Print this message\n"
    "  -v          Print version\n"
    "  -f          Run in the foreground\n"
    "  -u pattern  Trigger when all users matching pattern are logged out\n"
    "  -t pattern  Trigger when all ttys matching pattern are logged out\n"
    , arg0);
}

static void wait_for_event(void) {
#ifdef USE_INOTIFY
  ssize_t n;
  struct inotify_event in;
  while((n = read(inotify_fd, &in, sizeof(in))) == sizeof(in)) {
    if(in.wd == inotify_wd) return;
  }
  if(n == -1) {
    perror("read() on inotify fd");
    exit(1);
  }
#else
  sleep(5);
#endif
}

int main(int argc, char *argv[]) {
  int opt, n, cmd_len, ready, daemonize = 1;
  pid_t pid;
  struct utmpx *ut;
  const char *user_pattern = NULL;
  const char *tty_pattern = NULL;
  char **command;

  while((opt = getopt(argc, argv, "+hvfu:t:")) != -1) {
    switch(opt) {
    case 'h':
      usage(stdout, argv[0]);
      exit(0);
    case 'v':
      printf("aloe " VERSION "\n");
      exit(0);
    case 'f':
      daemonize = 0;
      break;
    case 'u':
      user_pattern = optarg;
      break;
    case 't':
      tty_pattern = optarg;
      break;
    default:
      usage(stderr, argv[0]);
      exit(1);
    }
  }

  if(optind == argc) {
    fprintf(stderr, "%s: missing command\n", argv[0]);
    usage(stderr, argv[0]);
    exit(1);
  }
  cmd_len = argc - optind;
  command = calloc(cmd_len + 1, sizeof(char*));
  for(n = 0; n < cmd_len; ++n) {
    command[n] = argv[optind + n];
  }

  if(daemonize) {
    pid_t sid;
    pid = fork();
    if(pid == -1) {
      perror("fork()");
      exit(2);
    } else if(pid != 0) {
      exit(0);
    }
    sid = setsid();
    if(sid == -1) {
      perror("setsid()");
      exit(2);
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

#ifdef USE_INOTIFY
  inotify_fd = inotify_init();
  if(inotify_fd == -1) {
    perror("inotify_init()");
    exit(2);
  }
  inotify_wd = inotify_add_watch(inotify_fd, _PATH_UTMPX, IN_CLOSE_WRITE);
  if(inotify_fd == -1) {
    perror("inotify_add_watch()");
    exit(2);
  }
#endif

  do {
    ready = 1;
    setutxent();
    while((ut = getutxent())) {
#ifdef __linux__
      if (ut->ut_type != USER_PROCESS)
        continue;
#endif
      if (user_pattern && fnmatch(user_pattern, ut->ut_user, 0) != 0)
        continue;
      if (tty_pattern && fnmatch(tty_pattern, ut->ut_line, 0) != 0)
        continue;
      ready = 0;
      break;
    }
    if(n == -1) {
      perror("getutxent()");
      exit(2);
    }
    if(ready) break;
    wait_for_event();
  } while(1);
  endutxent();
#ifdef USE_INOTIFY
  close(inotify_fd);
#endif

  pid = fork();
  if(pid == -1) {
    perror("fork()");
    exit(2);
  } else if(pid == 0) {
    execvp(command[0], command);
    perror(command[0]);
    exit(1);
  } else {
    int rc, status;
    rc = waitpid(pid, &status, 0);
    if(rc == -1) {
      perror("waitpid()");
      exit(2);
    } else if(rc == pid) {
      exit(status);
    } else {
      exit(2);
    }
  }
}
