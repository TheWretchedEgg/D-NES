#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "nes.h"

class CPU;
class APU;
class PPU;
class Cart;
class VideoBackend;
class AudioBackend;

class NESImpl : public NES
{
public:
    NESImpl();
    ~NESImpl();

    bool Initialize(const char* path, void* handle = nullptr);

    void SetCallback(NESCallback* callback);

    const char* GetGameName();

    State GetState();

    void SetControllerOneState(uint8_t state);
    uint8_t GetControllerOneState();

    void SetCpuLogEnabled(bool enabled);
    void SetNativeSaveDirectory(const char* saveDir);
    void SetStateSaveDirectory(const char* saveDir);

    void SetTargetFrameRate(uint32_t rate);
    void SetTurboModeEnabled(bool enabled);

    int GetFrameRate();
    void GetNameTable(int table, uint8_t* pixels);
    void GetPatternTable(int table, int palette, uint8_t* pixels);
    void GetPalette(int palette, uint8_t* pixels);
    void GetSprite(int sprite, uint8_t* pixels);
    void SetNtscDecoderEnabled(bool enabled);
    void SetFpsDisplayEnabled(bool enabled);
    void SetOverscanEnabled(bool enabled);

    void ShowMessage(const char* message, uint32_t duration);

    void SetAudioEnabled(bool enabled);
    void SetMasterVolume(float volume);

    void SetPulseOneVolume(float volume);
    float GetPulseOneVolume();
    void SetPulseTwoVolume(float volume);
    float GetPulseTwoVolume();
    void SetTriangleVolume(float volume);
    float GetTriangleVolume();
    void SetNoiseVolume(float volume);
    float GetNoiseVolume();
    void SetDmcVolume(float volume);
    float GetDmcVolume();

    // Launch the emulator on a new thread.
    // This function returns immediately.
    void Start();

    // Instructs the emulator to stop and then blocks until it does.
    // Once this function returns this object may be safely deleted.
    void Stop();

    void Resume();
    void Pause();
    void Reset();

    const char* SaveState(int slot);
    const char* LoadState(int slot);

    const char* GetErrorMessage();

private:
    // Main run function, launched in a new thread by NES::Start
    void Run();

    std::atomic<State> CurrentState;

    std::mutex PauseMutex;
    std::condition_variable PauseVariable;

    std::thread NesThread;
    std::string StateSaveDirectory;
    std::string ErrorMessage;

    APU* Apu;
    CPU* Cpu;
    PPU* Ppu;
    Cart* Cartridge;
    VideoBackend* VideoOut;
    AudioBackend* AudioOut;

    NESCallback* Callback;
};
