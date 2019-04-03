#pragma once

#include "mapper_base.h"

class UXROM : public MapperBase
{
public:
    UXROM(iNesFile& file);
    virtual ~UXROM();

    void SaveState(StateSave::Ptr& state) override;
    void LoadState(const StateSave::Ptr& state) override;

    uint8_t PrgRead(uint16_t address) override;
    void PrgWrite(uint8_t M, uint16_t address) override;

    uint8_t ChrRead(uint16_t address) override;
    void ChrWrite(uint8_t M, uint16_t address) override;
private:
    uint8_t _register;
};