#include "foxhollow_quit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int output;

static void destructor(void) {
  const char text[] = "destructor";
  assert(write(output, text, sizeof(text)) == (ssize_t)sizeof(text));
}

int main(void) {
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  const pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    close(pipefd[0]);
    output = pipefd[1];
    assert(atexit(destructor) == 0);
    FILE* stream = fdopen(output, "w");
    assert(stream != NULL);
    assert(fputs("flushed", stream) >= 0);
    fhExitGameProcess();
    abort();
  }
  close(pipefd[1]);
  char text[64] = {0};
  size_t size = 0;
  ssize_t received;
  while ((received = read(pipefd[0], text + size, sizeof(text) - size)) > 0) {
    size += (size_t)received;
    assert(size < sizeof(text));
  }
  assert(received == 0);
  close(pipefd[0]);
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status));
  assert(WEXITSTATUS(status) == EXIT_SUCCESS);
  assert(size == strlen("flushed"));
  assert(strcmp(text, "flushed") == 0);
}
