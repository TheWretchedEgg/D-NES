#pragma once

#include <map>

constexpr int ERROR_LOAD_GAME_AFTER_START = 1;
constexpr int ERROR_LOAD_GAME_FAILED = 2;
constexpr int ERROR_SET_WINDOW_HANDLE_AFTER_START = 3;
constexpr int ERROR_SET_CALLBACK_AFTER_START = 4;
constexpr int ERROR_FAILED_TO_OPEN_CPU_LOG_FILE = 5;
constexpr int ERROR_UNIMPLEMENTED = 6;
constexpr int ERROR_START_WITHOUT_GAME_LOADED = 7;
constexpr int ERROR_START_ALREADY_STARTED = 8;
constexpr int ERROR_START_AFTER_STOP = 9;
constexpr int ERROR_START_AFTER_ERROR = 13;
constexpr int ERROR_STOP_ALREADY_STOPPED = 11;
constexpr int ERROR_STOP_NOT_STARTED = 12;
constexpr int ERROR_STOP_AFTER_ERROR = 13;
constexpr int ERROR_RUNTIME_ERROR = 14;
constexpr int ERROR_STATE_SAVE_NOT_RUNNING = 15;
constexpr int ERROR_STATE_LOAD_NOT_RUNNING = 16;
constexpr int ERROR_STATE_SAVE_LOAD_FILE_ERROR = 17;
constexpr int ERROR_OPEN_LOG_FILE_FAILED = 18;
constexpr int ERROR_SET_LOG_FILE_AFTER_START = 19;
constexpr int ERROR_SET_LOG_CALLBACK_AFTER_START = 20;
constexpr int ERROR_UNSUPPORTED_MAPPER = 21;
constexpr int ERROR_INITIALIZE_OPENGL_FAILED = 22;
constexpr int ERROR_FAILED_TO_SAVE_NV_RAM = 23;
constexpr int ERROR_CPU_EXECUTED_STP = 24;
constexpr int ERROR_INITIALIZE_ALSA_FAILED = 25;
constexpr int ERROR_STATE_LOAD_FAILED = 26;
constexpr int ERROR_FAILED_TO_OPEN_ROM_FILE = 27;
constexpr int ERROR_FAILED_TO_READ_ROM_FILE = 28;
constexpr int ERROR_INVALID_INES_HEADER = 29;
constexpr int ERROR_INVALID_NAME_TABLE_INDEX = 30;
constexpr int ERROR_INVALID_PATTERN_TABLE_INDEX = 31;
constexpr int ERROR_INVALID_PALETTE_INDEX = 32;
constexpr int ERROR_INVALID_SPRITE_INDEX = 33;

static const std::map<int, std::string> ERROR_CODE_TO_MESSAGE_MAP
{
    {ERROR_LOAD_GAME_AFTER_START, "Cannot load a game after the emulator has started"},
    {ERROR_LOAD_GAME_FAILED, "Failed to load ROM file"},
    {ERROR_SET_WINDOW_HANDLE_AFTER_START, "Cannot set the window handle after the emulator has started"},
    {ERROR_SET_CALLBACK_AFTER_START, "Cannot set the callback object after the emulator has started"},
    {ERROR_FAILED_TO_OPEN_CPU_LOG_FILE, "Could not open CPU log file"},
    {ERROR_UNIMPLEMENTED, "The functionality is not yet implementsd"},
    {ERROR_START_WITHOUT_GAME_LOADED, "The emulator cannot be started until a game is loaded"},
    {ERROR_START_ALREADY_STARTED, "The emulator has already been started"},
    {ERROR_START_AFTER_STOP, "The emulator cannot be restarted once it has been stopped"},
    {ERROR_START_AFTER_ERROR, "The emulator is in an error state and cannot be started"},
    {ERROR_STOP_ALREADY_STOPPED, "The emulator has already been stopped"},
    {ERROR_STOP_NOT_STARTED, "The emulator has not been started"},
    {ERROR_STOP_AFTER_ERROR, "The emulator is in an error state and is already stopped"},
    {ERROR_RUNTIME_ERROR, "The emulator encountered a fatal error while running"},
    {ERROR_STATE_SAVE_NOT_RUNNING, "Cannot save a state while the emulator is not running"},
    {ERROR_STATE_LOAD_NOT_RUNNING, "Cannot load a state while the emulator is not running"},
    {ERROR_STATE_SAVE_LOAD_FILE_ERROR, "Failed to open save state file"},
    {ERROR_OPEN_LOG_FILE_FAILED, "Failed to open the requested log file"},
    {ERROR_SET_LOG_FILE_AFTER_START, "Cannot set the log file after the emulator has started"},
    {ERROR_SET_LOG_CALLBACK_AFTER_START, "Cannot set the log callback after the emulator has started"},
    {ERROR_UNSUPPORTED_MAPPER, "Mapper specified in ROM file is unsupported"},
    {ERROR_INITIALIZE_OPENGL_FAILED, "Failed to initialize OpenGL"},
    {ERROR_FAILED_TO_SAVE_NV_RAM, "Failed to write save file"},
    {ERROR_CPU_EXECUTED_STP, "STP instruction executed, emulation stopped"},
    {ERROR_INITIALIZE_ALSA_FAILED, "Failed to initialize ALSA"},
    {ERROR_STATE_LOAD_FAILED, "Failed to load state save"},
    {ERROR_FAILED_TO_OPEN_ROM_FILE, "Failed to open the specified ROM file"},
    {ERROR_FAILED_TO_READ_ROM_FILE, "An error occured while reading the ROM file"},
    {ERROR_INVALID_INES_HEADER, "The ROM file header is invalid"},
    {ERROR_INVALID_NAME_TABLE_INDEX, "The requested name table index is out of range"},
    {ERROR_INVALID_PATTERN_TABLE_INDEX, "The requested pattern table index is out of range"},
    {ERROR_INVALID_PALETTE_INDEX, "The requested palette index is out of range"},
    {ERROR_INVALID_SPRITE_INDEX, "The requestd sprite index is out of range"}
};

static inline const char* GetErrorMessageFromCode(int code)
{
    auto it = ERROR_CODE_TO_MESSAGE_MAP.find(code);

    if (it == ERROR_CODE_TO_MESSAGE_MAP.end())
    {
        return "Unrecognized error code";
    }

    return it->second.c_str();
}

class NesException : public std::exception
{
public:
    explicit NesException(int errorCode): _errorCode(errorCode) {} 

    const char* what() const noexcept override
    {
        return ::GetErrorMessageFromCode(_errorCode);
    }

    int errorCode() const noexcept { return _errorCode; };

private:
    int _errorCode;
};