/*
 * spooling_filter - buffer hpzjs output before the CUPS USB backend
 *
 * Copyright (c) 2026 Druk Duplex contributors
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef RENDERER_PATH
#define RENDERER_PATH "/Library/Printers/hpzjs/filter/rastertozjs"
#endif

#define LIVE_OUTPUT_ENV "HPZJS_LIVE_FD"

static volatile sig_atomic_t child_pid = 0;
static volatile sig_atomic_t canceled = 0;

static void forward_signal(int sig) {
  pid_t pid = (pid_t)child_pid;
  canceled = 1;
  if (pid > 0) kill(pid, sig);
}

static int copy_fd(int src, int dst) {
  char buf[65536];
  for (;;) {
    if (canceled) return 0;
    ssize_t n = read(src, buf, sizeof(buf));
    if (n == 0) return 0;
    if (n < 0) {
      if (canceled) return 0;
      if (errno == EINTR) continue;
      return -1;
    }
    char *p = buf;
    while (n > 0) {
      if (canceled) return 0;
      ssize_t written = write(dst, p, (size_t)n);
      if (written < 0) {
        if (canceled) return 0;
        if (errno == EINTR) continue;
        return -1;
      }
      p += written;
      n -= written;
    }
  }
}

int main(int argc, char **argv) {
  (void)argc;

  const char *tmpdir = getenv("TMPDIR");
  if (tmpdir == NULL || tmpdir[0] == '\0') {
    fputs("hpzjs-spool: TMPDIR is not set\n", stderr);
    return 1;
  }

  size_t tmpdir_len = strlen(tmpdir);
  const char suffix[] = "hpzjs-spool.XXXXXX";
  size_t tmpl_size = tmpdir_len + sizeof(suffix) + 1;
  char *tmpl = malloc(tmpl_size);
  if (tmpl == NULL) {
    perror("hpzjs-spool: malloc");
    return 1;
  }
  int written = snprintf(tmpl, tmpl_size, "%s%shpzjs-spool.XXXXXX", tmpdir,
                         tmpdir[tmpdir_len - 1] == '/' ? "" : "/");
  if (written < 0 || (size_t)written >= tmpl_size) {
    fputs("hpzjs-spool: invalid TMPDIR\n", stderr);
    free(tmpl);
    return 1;
  }

  int tmp = mkstemp(tmpl);
  if (tmp < 0) {
    perror("hpzjs-spool: mkstemp");
    free(tmpl);
    return 1;
  }
  if (fchmod(tmp, 0600) < 0 || unlink(tmpl) < 0) {
    perror("hpzjs-spool: secure temporary file");
    close(tmp);
    free(tmpl);
    return 1;
  }
  free(tmpl);

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = forward_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGHUP, &action, NULL);

  sigset_t blocked;
  sigset_t previous;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGTERM);
  sigaddset(&blocked, SIGINT);
  sigaddset(&blocked, SIGHUP);
  if (sigprocmask(SIG_BLOCK, &blocked, &previous) < 0) {
    perror("hpzjs-spool: sigprocmask");
    close(tmp);
    return 1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("hpzjs-spool: fork");
    sigprocmask(SIG_SETMASK, &previous, NULL);
    close(tmp);
    return 1;
  }
  if (pid == 0) {
    char live_fd_text[16];
    if (sigprocmask(SIG_SETMASK, &previous, NULL) < 0) _exit(126);
    int live_fd = fcntl(STDOUT_FILENO, F_DUPFD, 10);
    if (live_fd < 0) _exit(126);
    snprintf(live_fd_text, sizeof(live_fd_text), "%d", live_fd);
    if (setenv(LIVE_OUTPUT_ENV, live_fd_text, 1) < 0) _exit(126);
    if (dup2(tmp, STDOUT_FILENO) < 0) _exit(126);
    close(tmp);
    execv(RENDERER_PATH, argv);
    perror("hpzjs-spool: exec");
    _exit(127);
  }

  child_pid = pid;
  if (sigprocmask(SIG_SETMASK, &previous, NULL) < 0) {
    perror("hpzjs-spool: sigprocmask");
    kill(pid, SIGTERM);
    close(tmp);
    return 1;
  }
  if (canceled) kill(pid, SIGTERM);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      perror("hpzjs-spool: waitpid");
      close(tmp);
      return 1;
    }
  }
  child_pid = 0;

  if (canceled) {
    close(tmp);
    return 0;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    close(tmp);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  }

  if (lseek(tmp, 0, SEEK_SET) < 0) {
    perror("hpzjs-spool: lseek");
    close(tmp);
    return 1;
  }
  int result = copy_fd(tmp, STDOUT_FILENO) < 0 ? 1 : 0;
  if (result) perror("hpzjs-spool: output");
  close(tmp);
  return result;
}
