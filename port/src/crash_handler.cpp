#include <borealis/crash.hpp>
#include "foxhollow_log.h"
#include <cstdlib>

extern "C" void fhInstallCrashHandler(void) {
#ifdef __ANDROID__
    fhInitFileLog(std::getenv("FOXHOLLOW_LOG_FILE"), 1);
#endif
    borealis::crash::install();
}
