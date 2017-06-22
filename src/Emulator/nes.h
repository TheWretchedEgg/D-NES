/*
 * nes.h
 *
 *  Created on: Aug 8, 2014
 *      Author: Dale
 */

#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <iostream>
#include <functional>

struct NesParams
{
    std::string RomPath;
    std::string SavePath;
    bool CpuLogEnabled;

    // PPU Settings
    bool FrameLimitEnabled;
    bool NtscDecoderEnabled;

    // APU Settings
    bool AudioEnabled;
    bool FiltersEnabled;
    float MasterVolume;
    float PulseOneVolume;
    float PulseTwoVolume;
    float TriangleVolume;
    float NoiseVolume;
    float DmcVolume;

    NesParams()
        : RomPath("")
        , SavePath("")
        , CpuLogEnabled(false)
        , FrameLimitEnabled(true)
        , NtscDecoderEnabled(false)
        , AudioEnabled(true)
        , FiltersEnabled(false)
        , MasterVolume(1.0f)
        , PulseOneVolume(1.0f)
        , PulseTwoVolume(1.0f)
        , TriangleVolume(1.0f)
        , NoiseVolume(1.0f)
        , DmcVolume(1.0f)
    {}
};

class CPU;
class APU;
class PPU;
class Cart;

class NES
{
public:
    NES(const NesParams& params);
    ~NES();

    const std::string& GetGameName();

	void BindFrameCompleteCallback(const std::function<void(uint8_t*)>& fn);
	void BindErrorCallback(const std::function<void(std::string)>& fn);

    template<class T>
    void BindFrameCompleteCallback(void(T::*fn)(uint8_t*), T* obj)
    {
        BindFrameCompleteCallback(std::bind(fn, obj, std::placeholders::_1));
    }

    template<class T>
    void BindErrorCallback(void(T::*fn)(std::string), T* obj)
    {
        BindErrorCallback(std::bind(fn, obj, std::placeholders::_1));
    }

    bool IsStopped();
    bool IsPaused();

    void SetControllerOneState(uint8_t state);
    uint8_t GetControllerOneState();

    void CpuSetLogEnabled(bool enabled);
    void SetNativeSaveDirectory(const std::string& saveDir);

    int GetFrameRate();
    void GetNameTable(int table, uint8_t* pixels);
    void GetPatternTable(int table, int palette, uint8_t* pixels);
    void GetPalette(int palette, uint8_t* pixels);
    void GetPrimarySprite(int sprite, uint8_t* pixels);

    void PpuSetFrameLimitEnabled(bool enabled);
    void PpuSetNtscDecoderEnabled(bool enabled);

    void ApuSetAudioEnabled(bool enabled);
    void ApuSetFiltersEnabled(bool enabled);
    void ApuSetMasterVolume(float volume);
    void ApuSetPulseOneVolume(float volume);
    float ApuGetPulseOneVolume();
    void ApuSetPulseTwoVolume(float volume);
    float ApuGetPulseTwoVolume();
    void ApuSetTriangleVolume(float volume);
    float ApuGetTriangleVolume();
    void ApuSetNoiseVolume(float volume);
    float ApuGetNoiseVolume();
    void ApuSetDmcVolume(float volume);
    float ApuGetDmcVolume();

    // Launch the emulator on a new thread.
    // This function returns immediately.
    void Start();

    // Instructs the emulator to stop and then blocks until it does.
    // Once this function returns this object may be safely deleted.
    void Stop();

    void Resume();
    void Pause();
    void Reset();

private:
    // Main run function, launched in a new thread by NES::Start
    void Run();

    std::thread NesThread;

    APU* Apu;
    CPU* Cpu;
    PPU* Ppu;
    Cart* Cartridge;

    std::function<void(std::string)> OnError;
};
