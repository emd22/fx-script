#include "FoxLog.hpp"

#include <fstream>

static std::ofstream sCurrentLogFile;

std::ofstream& FxLogGetFile(bool* can_write)
{
    if (!sCurrentLogFile.is_open()) {
        FoxLogToStdout<FoxLogChannel::Error>("Attempting to write to log file that has not been opened");
        (*can_write) = false;
    }
    else {
        (*can_write) = true;
    }

    return sCurrentLogFile;
}

void FxLogCreateFile(const std::string& path)
{
    sCurrentLogFile.open(path.c_str());
}


/////////////////////////////////
// Compiler output functions
/////////////////////////////////

static std::ofstream sCurrentAsmFile;

std::ofstream& FoxAsmGetFile(bool* can_write)
{
    if (!sCurrentAsmFile.is_open()) {
        FoxLogToStdout<FoxLogChannel::Error>("Attempting to write ASM to a file that has not been opened");
        (*can_write) = false;
    }
    else {
        (*can_write) = true;
    }

    return sCurrentAsmFile;
}

void FoxAsmCreateFile(const std::string& path)
{
    sCurrentAsmFile.open(path.c_str());
}
