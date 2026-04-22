#include "FoxLog.hpp"

#include <fstream>

static std::ofstream sCurrentLogFile;

std::ofstream& FoxLogGetFile(bool* can_write)
{
    if (!sCurrentLogFile.is_open()) {
        (*can_write) = false;
    }
    else {
        (*can_write) = true;
    }

    return sCurrentLogFile;
}

void FoxLogCreateFile(const std::string& path)
{
    sCurrentLogFile.open(path.c_str());
}


/////////////////////////////////
// Compiler output functions
/////////////////////////////////

static std::ofstream sCurrentAsmFile;

static int sCurrentIndent = 0;

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

void FoxAsmOutputIndent()
{
    for (int i = 0; i < sCurrentIndent; i++) {
        FoxAsmDirect("\t");
    }
}


void FoxAsmIncreaseIndent()
{
    ++sCurrentIndent;
}

void FoxAsmDecreaseIndent()
{
    --sCurrentIndent;

    if (sCurrentIndent < 0) {
        sCurrentIndent = 0;
    }
}
