#include "foxhollow_log.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void* write_lines(void* ignored) {
    (void)ignored;
    for (int i = 0; i < 100; ++i) fhWriteFileLog("INFO", "thread", "parallel", 8);
    return NULL;
}

int main(void) {
    char directory[] = "/tmp/foxhollow-log-XXXXXX";
    assert(mkdtemp(directory));
    char path[256], previous[264];
    snprintf(path, sizeof(path), "%s/native.log", directory);
    snprintf(previous, sizeof(previous), "%s.prev", path);
    fhInitFileLog(path, 0);
    fhWriteFileLog("ERROR", "gpu", "driver failure", 14);
    FILE* input = fopen(path, "r");
    assert(input);
    char line[1024];
    assert(fgets(line, sizeof(line), input));
    assert(strstr(line, "[ERROR] gpu: driver failure"));
    fclose(input);
    pthread_t one, two;
    assert(!pthread_create(&one, NULL, write_lines, NULL));
    assert(!pthread_create(&two, NULL, write_lines, NULL));
    pthread_join(one, NULL); pthread_join(two, NULL);
    fhInitFileLog(path, 0);
    assert(access(previous, F_OK) == 0);
    char message[20000];
    memset(message, 'X', sizeof(message));
    for (int i = 0; i < 100; ++i) fhWriteFileLog("INFO", "bounded", message, sizeof(message));
    fhInitFileLog(NULL, 0);
    struct stat state;
    assert(stat(path, &state) == 0 && state.st_size < 512 * 1024 + 17000);
    assert(stat(previous, &state) == 0 && state.st_size < 512 * 1024 + 17000);
    input = fopen(path, "r"); assert(input);
    int found = 0;
    while (fgets(line, sizeof(line), input)) found |= strstr(line, "[truncated]") != NULL;
    fclose(input);
    assert(found);
    fhInitFileLog("/missing/foxhollow/log", 0);
    fhWriteFileLog("INFO", NULL, NULL, 0);
    fhInitFileLog(NULL, 0);
    unlink(path); unlink(previous); rmdir(directory);
}
