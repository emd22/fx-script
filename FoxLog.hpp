#pragma once

#include "FoxDefines.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <string>

enum class FoxLogChannel
{
    None,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};


#define FX_LOG_CHANNEL_LABEL_DEBUG "[DEBUG] "
#define FX_LOG_CHANNEL_LABEL_INFO "[INFO]  "
#define FX_LOG_CHANNEL_LABEL_WARN "[WARN]  "
#define FX_LOG_CHANNEL_LABEL_ERROR "[ERROR] "
#define FX_LOG_CHANNEL_LABEL_FATAL "[FATAL] "

#define FX_LOG_STYLE_RESET "\x1b[0m"

std::ofstream& FoxLogGetFile(bool* can_write);
void FoxLogCreateFile(const std::string& path);

template <FoxLogChannel TLogChannel, bool TAllowColors = true>
constexpr std::string FoxLogGetChannelText()
{
    if constexpr (TLogChannel == FoxLogChannel::None) {
        return "";
    }

#ifdef FOX_LOG_ENABLE_COLORS
    constexpr bool AllowColors = TAllowColors;
#else
    // Colours are disabled globally on compilation, never show them
    constexpr bool AllowColors = false;
#endif

    if (AllowColors) {
        if constexpr (TLogChannel == FoxLogChannel::Debug) {
            return ("\x1b[92m" FX_LOG_CHANNEL_LABEL_DEBUG FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FoxLogChannel::Info) {
            return ("\x1b[94m" FX_LOG_CHANNEL_LABEL_INFO FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FoxLogChannel::Warning) {
            return ("\x1b[93m" FX_LOG_CHANNEL_LABEL_WARN FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FoxLogChannel::Error) {
            return ("\x1b[91m" FX_LOG_CHANNEL_LABEL_ERROR FX_LOG_STYLE_RESET);
        }
        else if constexpr (TLogChannel == FoxLogChannel::Fatal) {
            return ("\x1b[1;91m" FX_LOG_CHANNEL_LABEL_FATAL FX_LOG_STYLE_RESET);
        }
    }
    else {
        if constexpr (TLogChannel == FoxLogChannel::Debug) {
            return FX_LOG_CHANNEL_LABEL_DEBUG;
        }
        else if constexpr (TLogChannel == FoxLogChannel::Info) {
            return FX_LOG_CHANNEL_LABEL_INFO;
        }
        else if constexpr (TLogChannel == FoxLogChannel::Warning) {
            return FX_LOG_CHANNEL_LABEL_WARN;
        }
        else if constexpr (TLogChannel == FoxLogChannel::Error) {
            return FX_LOG_CHANNEL_LABEL_ERROR;
        }
        else if constexpr (TLogChannel == FoxLogChannel::Fatal) {
            return FX_LOG_CHANNEL_LABEL_FATAL;
        }
    }

    return "";
}


template <typename... TTypes>
void FoxLogDirectToStdout(std::string_view fmt, TTypes&&... args)
{
    auto msg = std::vformat(fmt, std::make_format_args(args...));
    std::cout << msg;
}

template <FoxLogChannel TChannel, typename... TTypes>
void FoxLogToStdout(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == FoxLogChannel::Debug) {
        return;
    }
#endif

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    // If the channel is set to `None`, do not print the channel name
    if constexpr (TChannel == FoxLogChannel::None) {
        std::cout << msg << '\n';
        return;
    }

    auto channel = FoxLogGetChannelText<TChannel>();

    std::cout << channel << msg << '\n';
}


template <typename... TTypes>
void FoxLogDirectToFile(std::string_view fmt, TTypes&&... args)
{
    bool can_write = false;
    std::ofstream& stream = FoxLogGetFile(&can_write);

    if (!can_write) {
        return;
    }

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    // If the channel is set to `None`, do not print the channel name
    stream << msg << '\n';
}


template <FoxLogChannel TChannel, typename... TTypes>
void FoxLogToFile(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == FoxLogChannel::Debug) {
        return;
    }
#endif

    bool can_write = false;
    std::ofstream& stream = FoxLogGetFile(&can_write);

    if (!can_write) {
        return;
    }

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    // If the channel is set to `None`, do not print the channel name
    if constexpr (TChannel == FoxLogChannel::None) {
        stream << msg << '\n';
        return;
    }

    auto channel = FoxLogGetChannelText<TChannel, false>();

    stream << channel << msg << '\n';
}

/**
 * @brief Logs a message to a channel from `FoxLogChannel`.
 */
template <FoxLogChannel TChannel, typename... TTypes>
void FoxLog(std::string_view fmt, TTypes&&... args)
{
    // Disregard debug logs when building for release
#ifdef FX_BUILD_RELEASE
    if constexpr (TChannel == FoxLogChannel::Debug) {
        return;
    }
#endif

#ifdef FX_LOG_OUTPUT_TO_STDOUT
    FoxLogToStdout<TChannel>(fmt, std::forward<TTypes>(args)...);
#endif

#ifdef FX_LOG_OUTPUT_TO_FILE
    FoxLogToFile<TChannel>(fmt, std::forward<TTypes>(args)...);
#endif
}

/**
 * @brief Logs a message to a channel from `FoxLogChannel`.
 */
template <typename... TTypes>
void FoxLogDirect(std::string_view fmt, TTypes&&... args)
{
#ifdef FX_LOG_OUTPUT_TO_STDOUT
    FoxLogDirectToStdout(fmt, std::forward<TTypes>(args)...);
#endif

#ifdef FX_LOG_OUTPUT_TO_FILE
    FoxLogDirectToFile(fmt, std::forward<TTypes>(args)...);
#endif
}

template <typename... TTypes>
void FoxLogInfo(std::string_view fmt, TTypes&&... args)
{
    FoxLog<FoxLogChannel::Info>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void FoxLogDebug(std::string_view fmt, TTypes&&... args)
{
    FoxLog<FoxLogChannel::Debug>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void FoxLogWarning(std::string_view fmt, TTypes&&... args)
{
    FoxLog<FoxLogChannel::Warning>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void FoxLogError(std::string_view fmt, TTypes&&... args)
{
    FoxLog<FoxLogChannel::Error>(fmt, std::forward<TTypes>(args)...);
}

template <typename... TTypes>
void FoxLogFatal(std::string_view fmt, TTypes&&... args)
{
    FoxLog<FoxLogChannel::Fatal>(fmt, std::forward<TTypes>(args)...);
}


template <FoxLogChannel TChannel>
constexpr void FoxLogChannelText()
{
#ifdef FX_LOG_OUTPUT_TO_STDOUT
    FoxLogDirectToStdout("{}", FoxLogGetChannelText<TChannel>());
#endif
#ifdef FX_LOG_OUTPUT_TO_FILE
    FoxLogDirectToFile("{}", FoxLogGetChannelText<TChannel, false>());
#endif
}


/////////////////////////////////
// Compiler output functions
/////////////////////////////////

std::ofstream& FoxAsmGetFile(bool* can_write);
void FoxAsmCreateFile(const std::string& path);

template <typename... TTypes>
void FoxAsmToFile(std::string_view fmt, TTypes&&... args)
{
    bool can_write = false;
    std::ofstream& stream = FoxAsmGetFile(&can_write);

    if (!can_write) {
        return;
    }

    auto msg = std::vformat(fmt, std::make_format_args(args...));

    stream << msg << '\n';
}


template <typename... TTypes>
void FoxAsmToStdout(std::string_view fmt, TTypes&&... args)
{
    auto msg = std::vformat(fmt, std::make_format_args(args...));

    std::cout << msg << '\n';
}


/**
 * @brief Logs a message to a channel from `FoxLogChannel`.
 */
template <typename... TTypes>
void FoxAsm(std::string_view fmt, TTypes&&... args)
{
#ifdef FOX_ASM_OUTPUT_TO_STDOUT
    FoxAsmToStdout(fmt, std::forward<TTypes>(args)...);
#endif

#ifdef FOX_ASM_OUTPUT_TO_FILE
    FoxAsmToFile(fmt, std::forward<TTypes>(args)...);
#endif
}
