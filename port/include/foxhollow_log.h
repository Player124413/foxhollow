#ifndef FOXHOLLOW_LOG_H
#define FOXHOLLOW_LOG_H
#ifdef __cplusplus
extern "C" {
#endif
void fhInitFileLog(const char* path, int redirectErrors);
void fhWriteFileLog(const char* level, const char* module, const char* message, unsigned int length);
#ifdef __cplusplus
}
#endif
#endif
