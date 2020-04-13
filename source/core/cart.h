/*
 * mapper.h
 *
 *  Created on: Jul 8, 2014
 *      Author: Dale
 */

#pragma once

#include <string>
#include <memory>

#include "nes.h"
#include "ines.h"
#include "state_save.h"

class CPU;
class PPU;
class MapperBase;

class Cart
{
public:
    Cart(NES& nes);
    ~Cart();

    int Initialize(iNesFile* file);

    void SaveNativeSave(const std::string& saveDir);
    void LoadNativeSave(const std::string& saveDir);

    uint8_t CpuRead(uint16_t address);
    void CpuWrite(uint8_t M, uint16_t address);

    void SetPpuAddress(uint16_t address);
    uint8_t PpuRead();
    void PpuWrite(uint8_t M);

    uint8_t PpuPeek(uint16_t address);

    StateSave::Ptr SaveState();
    void LoadState(const StateSave::Ptr& state);

    bool CheckIRQ();



private:
    NES& _nes;
    iNesFile* _iNesFile;
    std::unique_ptr<MapperBase> _mapper;
};
