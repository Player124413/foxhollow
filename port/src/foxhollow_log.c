#include "foxhollow_log.h"
#if defined(__ANDROID__) || defined(FOXHOLLOW_LOG_TEST)
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
static int logFd = -1;
static int redirectStderr;
static char logPath[4096];
static char previousPath[4104];

static int open_log(void) {
    int fd = open(logPath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0 && redirectStderr) dup2(fd, STDERR_FILENO);
    return fd;
}

static void write_all(const char* text, size_t length) {
    while (length) {
        ssize_t count = write(logFd, text, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return;
        text += count;
        length -= (size_t)count;
    }
}

void fhInitFileLog(const char* path, int redirectErrors) {
    pthread_mutex_lock(&logMutex);
    if (logFd >= 0) close(logFd);
    logFd = -1;
    if (path && strlen(path) < sizeof(logPath)) {
        strcpy(logPath, path);
        snprintf(previousPath, sizeof(previousPath), "%s.prev", path);
        redirectStderr = redirectErrors;
        rename(logPath, previousPath);
        logFd = open_log();
    }
    pthread_mutex_unlock(&logMutex);
}

void fhWriteFileLog(const char* level, const char* module, const char* message, unsigned int length) {
    pthread_mutex_lock(&logMutex);
    if (logFd < 0) { pthread_mutex_unlock(&logMutex); return; }
    struct stat state;
    if (fstat(logFd, &state) == 0 && state.st_size >= 512 * 1024) {
        if (rename(logPath, previousPath) != 0) { pthread_mutex_unlock(&logMutex); return; }
        close(logFd);
        logFd = open_log();
        if (logFd < 0) { pthread_mutex_unlock(&logMutex); return; }
    }
    struct tm utc;
    time_t now = time(NULL);
    gmtime_r(&now, &utc);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    char header[256];
    int count = snprintf(header, sizeof(header), "%s [%.16s] %.128s: ", timestamp,
                         level ? level : "INFO", module ? module : "Foxhollow");
    if (count > 0) write_all(header, (size_t)count < sizeof(header) ? (size_t)count : sizeof(header) - 1);
    if (message) write_all(message, length > 16384 ? 16384 : length);
    if (length > 16384) write_all(" [truncated]", 12);
    write_all("\n", 1);
    pthread_mutex_unlock(&logMutex);
}
#else
void fhInitFileLog(const char* path, int redirectErrors) { (void)path; (void)redirectErrors; }
void fhWriteFileLog(const char* level, const char* module, const char* message, unsigned int length) {
    (void)level; (void)module; (void)message; (void)length;
}
#endif
