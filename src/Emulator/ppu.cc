/*
 * ppu.cc
 *
 *  Created on: Oct 9, 2014
 *      Author: Dale
 */

#include <cstring>
#include "ppu.h"
#include "cpu.h"
#include "apu.h"
#include "video/video_backend.h"

static constexpr uint32_t RgbLookupTable[64] =
{
    0x545454, 0x001E74, 0x081090, 0x300088, 0x440064, 0x5C0030, 0x540400, 0x3C1800, 0x202A00, 0x083A00, 0x004000, 0x003C00, 0x00323C, 0x000000, 0x000000, 0x000000,
    0x989698, 0x084CC4, 0x3032EC, 0x5C1EE4, 0x8814B0, 0xA01464, 0x982220, 0x783C00, 0x545A00, 0x287200, 0x087C00, 0x007628, 0x006678, 0x000000, 0x000000, 0x000000,
    0xECEEEC, 0x4C9AEC, 0x787CEC, 0xB062EC, 0xE454EC, 0xEC58B4, 0xEC6A64, 0xD48820, 0xA0AA00, 0x74C400, 0x4CD020, 0x38CC6C, 0x38B4CC, 0x3C3C3C, 0x000000, 0x000000,
    0xECEEEC, 0xA8CCEC, 0xBCBCEC, 0xD4B2EC, 0xECAEEC, 0xECAED4, 0xECB4B0, 0xE4C490, 0xCCD278, 0xB4DE78, 0xA8E290, 0x98E2B4, 0xA0D6E4, 0xA0A2A0, 0x000000, 0x000000
};

static constexpr uint32_t ResetDelay = 88974;

PPU::PPU(VideoBackend* vout, NESCallback* callback)
    : Cpu(nullptr)
    , Cartridge(nullptr)
    , VideoOut(vout)
    , Callback(callback)
    , FpsCounter(0)
    , CurrentFps(0)
    , FrameCountStart(std::chrono::steady_clock::now())
    , RequestTurboMode(false)
    , TurboModeEnabled(false)
    , TurboFrameSkip(0)
    , Clock(0)
    , Dot(1)
    , Line(241)
    , Even(true)
    , SuppressNmi(false)
    , InterruptActive(false)
    , PpuAddressIncrement(false)
    , BaseSpriteTableAddress(0)
    , BaseBackgroundTableAddress(0)
    , SpriteSizeSwitch(false)
    , NmiEnabled(false)
    , GrayScaleEnabled(false)
    , ShowBackgroundLeft(0)
    , ShowSpritesLeft(0)
    , ShowBackground(0)
    , ShowSprites(0)
    , IntenseRed(0)
    , IntenseGreen(0)
    , IntenseBlue(0)
    , LowerBits(0x06)
    , SpriteOverflowFlag(false)
    , SpriteZeroHitFlag(false)
    , NmiOccuredFlag(false)
    , SpriteZeroSecondaryOamFlag(false)
    , OamAddress(0)
    , PpuAddress(0)
    , PpuTempAddress(0)
    , FineXScroll(0)
    , AddressLatch(false)
    , RenderingEnabled(false)
    , RenderStateDelaySlot(false)
    , DataBuffer(0)
    , NameTableByte(0)
    , AttributeByte(0)
    , TileBitmapLow(0)
    , TileBitmapHigh(0)
    , BackgroundShift0(0)
    , BackgroundShift1(0)
    , BackgroundAttributeShift0(0)
    , BackgroundAttributeShift1(0)
    , BackgroundAttribute(0)
    , SpriteCount(0)
	, FrameBufferIndex(0)
    , NtscMode(false)
{
    memset(NameTable0, 0, sizeof(uint8_t) * 0x400);
    memset(NameTable1, 0, sizeof(uint8_t) * 0x400);
    memset(PrimaryOam, 0, sizeof(uint8_t) * 0x100);
    memset(SecondaryOam, 0, sizeof(uint8_t) * 0x20);
    memset(PaletteTable, 0, sizeof(uint8_t) * 0x20);
    memset(SpriteShift0, 0, sizeof(uint8_t) * 8);
    memset(SpriteShift1, 0, sizeof(uint8_t) * 8);
    memset(SpriteAttribute, 0, sizeof(uint8_t) * 8);
    memset(SpriteCounter, 0, sizeof(uint8_t) * 8);
}

PPU::~PPU() {}

void PPU::AttachCPU(CPU* cpu)
{
    Cpu = cpu;
}

void PPU::AttachAPU(APU* apu)
{
    Apu = apu;
}

void PPU::AttachCart(Cart* cart)
{
    Cartridge = cart;
}

int32_t PPU::GetCurrentDot()
{
    // Adjust the value of Dot to match Nintedulator's logs
    if (Dot == 0)
    {
        return 340;
    }
    else
    {
        return Dot - 1;
    }
}

int32_t PPU::GetCurrentScanline()
{
    // Adjust the value of Line to match Nintedulator's logs
    if (Dot == 0)
    {
        if (Line == 0)
        {
            return -1;
        }
        else
        {
            return Line - 1;
        }
    }
    else
    {
        if (Line == 261)
        {
            return -1;
        }
        else 
        {
            return Line - 1;
        }
    }
}

uint64_t PPU::GetClock()
{
    return Clock;
}

void PPU::Step()
{
    // Pre-render line
    if (Line == 261)
    {
        if (Dot == 1)
        {
            NmiOccuredFlag = false;
            InterruptActive = false;
            SpriteZeroHitFlag = false;
        }

        if (RenderingEnabled)
        {
            if ((Dot >= 2 && Dot <= 257) || (Dot >= 322 && Dot <= 337))
            {
                BackgroundShift0 <<= 1;
                BackgroundShift1 <<= 1;
                BackgroundAttributeShift0 <<= 1;
                BackgroundAttributeShift1 <<= 1;

                if ((Dot - 1) % 8 == 0)
                {
                    LoadBackgroundShiftRegisters();
                }
            }

            if (Dot == 257)
            {
                PpuAddress = (PpuAddress & 0x7BE0) | (PpuTempAddress & 0x041F);
            }

            if ((Dot >= 1 && Dot <= 256) || (Dot >= 321 && Dot <= 336))
            {
                uint8_t cycle = (Dot - 1) % 8;
                switch (cycle)
                {
                case 0:
                    SetNameTableAddress();
                    break;
                case 1:
                    DoNameTableFetch();
                    break;
                case 2:
                    SetBackgroundAttributeAddress();
                    break;
                case 3:
                    DoBackgroundAttributeFetch();
                    break;
                case 4:
                    SetBackgroundLowByteAddress();
                    break;
                case 5:
                    DoBackgroundLowByteFetch();
                    break;
                case 6:
                    SetBackgroundHighByteAddress();
                    break;
                case 7:
                    DoBackgroundHighByteFetch();
                    IncrementXScroll();

                    if (Dot == 256)
                    {
                        IncrementYScroll();
                    }

                    break;
                }
            }
            else if (Dot >= 257 && Dot <= 320)
            {
                uint8_t cycle = (Dot - 1) % 8;
                switch (cycle)
                {
                case 0:
                    SetNameTableAddress();
                    break;
                case 1:
                    DoNameTableFetch();
                    break;
                case 2:
                    SetBackgroundAttributeAddress();
                    DoSpriteAttributeFetch();
                    break;
                case 3:
                    DoBackgroundAttributeFetch();
                    DoSpriteXCoordinateFetch();
                    break;
                case 4:
                    SetSpriteLowByteAddress();
                    break;
                case 5:
                    DoSpriteLowByteFetch();
                    break;
                case 6:
                    SetSpriteHighByteAddress();
                    break;
                case 7:
                    DoSpriteHighByteFetch();
                    break;
                }
            }
            else if (Dot >= 337 && Dot <= 340)
            {
                uint8_t cycle = (Dot - 1) % 4;
                switch (cycle)
                {
                    case 0: case 2:
                        SetNameTableAddress();
                        break;
                    case 1: case 3:
                        DoNameTableFetch();
                        break;
                }
            }
            // From dot 280 to 304 of the pre-render line copy all vertical position bits to ppuAddress from ppuTempAddress
            if (Dot >= 280 && Dot <= 304)
            {
                PpuAddress = (PpuAddress & 0x041F) | (PpuTempAddress & 0x7BE0);
            }

            if (Dot == 339 && !Even)
            {
                Dot = Line = 0;
            }
            else if (Dot == 340)
            {
                Dot = Line = 0;
            }
            else
            {
                ++Dot;
            }
        }
        else
        {
            if (Dot == 340)
            {
                Dot = Line = 0;
            }
            else
            {
                ++Dot;
            }
        }
    }
    // Visible Lines
    else if (Line >= 0 && Line <= 239)
    {
        if (RenderingEnabled)
        {
            if ((Dot >= 2 && Dot <= 257) || (Dot >= 322 && Dot <= 337))
            {
                BackgroundShift0 <<= 1;
                BackgroundShift1 <<= 1;
                BackgroundAttributeShift0 <<= 1;
                BackgroundAttributeShift1 <<= 1;

                if ((Dot - 1) % 8 == 0)
                {
                    LoadBackgroundShiftRegisters();
                }
            }

            if (Dot == 257)
            {
                SpriteEvaluation();
                PpuAddress = (PpuAddress & 0x7BE0) | (PpuTempAddress & 0x041F);
            }

            if ((Dot >= 1 && Dot <= 256) || (Dot >= 321 && Dot <= 336))
            {
                uint8_t cycle = (Dot - 1) % 8;
                switch (cycle)
                {
                case 0:
                    SetNameTableAddress();
                    break;
                case 1:
                    DoNameTableFetch();
                    break;
                case 2:
                    SetBackgroundAttributeAddress();
                    break;
                case 3:
                    DoBackgroundAttributeFetch();
                    break;
                case 4:
                    SetBackgroundLowByteAddress();
                    break;
                case 5:
                    DoBackgroundLowByteFetch();
                    break;
                case 6:
                    SetBackgroundHighByteAddress();
                    break;
                case 7:
                    DoBackgroundHighByteFetch();
                    IncrementXScroll();

                    if (Dot == 256)
                    {
                        IncrementYScroll();
                    }

                    break;
                }
            }
            else if (Dot >= 257 && Dot <= 320)
            {
                uint8_t cycle = (Dot - 1) % 8;
                switch (cycle)
                {
                case 0:
                    SetNameTableAddress();
                    break;
                case 1:
                    DoNameTableFetch();
                    break;
                case 2:
                    SetBackgroundAttributeAddress();
                    DoSpriteAttributeFetch();
                    break;
                case 3:
                    DoBackgroundAttributeFetch();
                    DoSpriteXCoordinateFetch();
                    break;
                case 4:
                    SetSpriteLowByteAddress();
                    break;
                case 5:
                    DoSpriteLowByteFetch();
                    break;
                case 6:
                    SetSpriteHighByteAddress();
                    break;
                case 7:
                    DoSpriteHighByteFetch();
                    break;
                }
            }
            else if (Dot >= 337 && Dot <= 340)
            {
                uint8_t cycle = (Dot - 1) % 4;
                switch (cycle)
                {
                    case 0: case 2:
                        SetNameTableAddress();
                        break;
                    case 1: case 3:
                        DoNameTableFetch();
                        break;
                }
            }

            if (Dot >= 1 && Dot <= 256)
            {
                //if (TurboFrameSkip == 0)
                {
                    RenderPixel();
                }
            }
        }
        else
        {
            if (Dot >= 1 && Dot <= 256)
            {
                //if (TurboFrameSkip == 0)
                {
                    RenderPixelIdle();
                }
            }
        }

        // End of Visible Frame
        if (Dot == 256 && Line == 239)
        {
            // Toggle even flag
            Even = !Even;

            UpdateFrameSkipCounters();

            // Update frame rate counter
            UpdateFrameRate();

            // Check if a change in rendering mode or turbo mode has been requested
            MaybeChangeModes();

            if (TurboFrameSkip == 0) {
                VideoOut->SubmitFrame(reinterpret_cast<uint8_t*>(FrameBuffer));
                FrameBufferIndex = 0;

                if (Callback != nullptr) {
                    Callback->OnFrameComplete();
                }
            }
        }

        if (Dot == 340)
        {
            Dot = 0;
            ++Line;
        }
        else
        {
            ++Dot;
        }
    }
    // Post-render line
    else if (Line == 240 || (Line == 241 && Dot == 0))
    {
        if (Dot == 340)
        {
            Dot = 0;
            ++Line;
        }
        else
        {
            ++Dot;
        }
    }
    // VBlank Lines
    else if (Line >= 241 && Line <= 260)
    {
        if (Dot == 1 && Line == 241 && Clock > ResetDelay)
        {
            if (!SuppressNmi)
            {
                NmiOccuredFlag = true;
            }

            SuppressNmi = false;
        }

        InterruptActive = NmiOccuredFlag && NmiEnabled;

        if (Dot == 340)
        {
            Dot = 0;
            ++Line;
        }
        else
        {
            ++Dot;
        }
    }

    RenderingEnabled = RenderStateDelaySlot;
    RenderStateDelaySlot = ShowBackground || ShowSprites;
    
    ++Clock;
}


bool PPU::GetNMIActive()
{
    return InterruptActive;
}

void PPU::SetTurboModeEnabled(bool enabled)
{
    RequestTurboMode = enabled;
}

void PPU::SetNtscDecodingEnabled(bool enabled)
{
    RequestNtscMode = enabled;
}

uint8_t PPU::ReadPPUStatus()
{
    uint8_t vB = static_cast<uint8_t>(NmiOccuredFlag);
    uint8_t sp0 = static_cast<uint8_t>(SpriteZeroHitFlag);
    uint8_t spOv = static_cast<uint8_t>(SpriteOverflowFlag);

    if (Line == 241 && Dot == 1)
    {
        SuppressNmi = true; // Suppress interrupt
    }

    if (Line == 241 && Dot >= 2 && Dot <= 3)
    {
        InterruptActive = false;
    }

    NmiOccuredFlag = false;
    AddressLatch = false;

    return  (vB << 7) | (sp0 << 6) | (spOv << 5) | LowerBits;
}

uint8_t PPU::ReadOAMData()
{
    return PrimaryOam[OamAddress];
}

uint8_t PPU::ReadPPUData()
{
    uint8_t value = DataBuffer;
    DataBuffer = Read();
    PpuAddressIncrement ? PpuAddress = (PpuAddress + 32) & 0x7FFF : PpuAddress = (PpuAddress + 1) & 0x7FFF;
    SetBusAddress(PpuAddress);

    return value;
}

void PPU::WritePPUCTRL(uint8_t M)
{
    if (Cpu->GetClock() > ResetDelay)
    {
        PpuTempAddress = (PpuTempAddress & 0x73FF) | ((0x3 & static_cast<uint16_t>(M)) << 10); // High bits of NameTable address
        PpuAddressIncrement = ((0x4 & M) >> 2) != 0;
        BaseSpriteTableAddress = 0x1000 * ((0x8 & M) >> 3);
        BaseBackgroundTableAddress = 0x1000 * ((0x10 & M) >> 4);
        SpriteSizeSwitch = ((0x20 & M) >> 5) != 0;
        NmiEnabled = ((0x80 & M) >> 7) != 0;

        if (NmiEnabled == false && Line == 241 && (Dot - 1) == 1)
        {
            InterruptActive = false;
        }
    }

    LowerBits = (0x1F & M);
}

void PPU::WritePPUMASK(uint8_t M)
{
    if (Cpu->GetClock() > ResetDelay)
    {
        GrayScaleEnabled = (0x1 & M);
        ShowBackgroundLeft = ((0x2 & M) >> 1) != 0;
        ShowSpritesLeft = ((0x4 & M) >> 2) != 0;
        ShowBackground = ((0x8 & M) >> 3) != 0;
        ShowSprites = ((0x10 & M) >> 4) != 0;
        IntenseRed = ((0x20 & M) >> 5) != 0;
        IntenseGreen = ((0x40 & M) >> 6) != 0;
        IntenseBlue = ((0x80 & M) >> 7) != 0;
    }

    LowerBits = (0x1F & M);
}

void PPU::WriteOAMADDR(uint8_t M)
{
    OamAddress = M;
    LowerBits = (0x1F & OamAddress);
}

void PPU::WriteOAMDATA(uint8_t M)
{
    PrimaryOam[OamAddress++] = M;
    LowerBits = (0x1F & M);
}

void PPU::WritePPUSCROLL(uint8_t M)
{
    if (Cpu->GetClock() > ResetDelay)
    {
        if (AddressLatch)
        {
            PpuTempAddress = (PpuTempAddress & 0x0FFF) | ((0x7 & M) << 12);
            PpuTempAddress = (PpuTempAddress & 0x7C1F) | ((0xF8 & M) << 2);
        }
        else
        {
            FineXScroll = static_cast<uint8_t>(0x7 & M);
            PpuTempAddress = (PpuTempAddress & 0x7FE0) | ((0xF8 & M) >> 3);
        }

        AddressLatch = !AddressLatch;
    }

    LowerBits = static_cast<uint8_t>(0x1F & M);
}

void PPU::WritePPUADDR(uint8_t M)
{
    if (Cpu->GetClock() > ResetDelay)
    {      
        if (AddressLatch)
        {
            PpuTempAddress = (PpuTempAddress & 0x7F00) | M;
            PpuAddress = PpuTempAddress;
            SetBusAddress(PpuAddress);
        }
        else
        {
            PpuTempAddress = (PpuTempAddress & 0x00FF) | ((0x3F & M) << 8);
        }
        
        AddressLatch = !AddressLatch;
    }

    LowerBits = static_cast<uint8_t>(0x1F & M);
}

void PPU::WritePPUDATA(uint8_t M)
{
    Write(M);
    PpuAddressIncrement ? PpuAddress = (PpuAddress + 32) & 0x7FFF : PpuAddress = (PpuAddress + 1) & 0x7FFF;
    SetBusAddress(PpuAddress);

    LowerBits = (0x1F & M);
}

int PPU::GetFrameRate()
{
    return CurrentFps;
}

void PPU::GetNameTable(int table, uint8_t* pixels)
{
    uint16_t tableIndex;
    if (table == 0)
    {
        tableIndex = 0x000;
    }
    else if (table == 1)
    {
        tableIndex = 0x400;
    }
    else if (table == 2)
    {
        tableIndex = 0x800;
    }
    else
    {
        tableIndex = 0xC00;
    }

    for (uint32_t i = 0; i < 30; ++i)
    {
        for (uint32_t f = 0; f < 32; ++f)
        {
            uint16_t address = (tableIndex | (i << 5) | f);
            uint8_t ntByte = Peek(0x2000 | address);
            uint8_t atShift = (((address & 0x0002) >> 1) | ((address & 0x0040) >> 5)) * 2;
            uint8_t atByte = Peek(0x23C0 | (address & 0x0C00) | ((address >> 4) & 0x38) | ((address >> 2) & 0x07));
            atByte = (atByte >> atShift) & 0x3;
            uint16_t patternAddress = static_cast<uint16_t>(ntByte) * 16;

            for (uint32_t h = 0; h < 8; ++h)
            {
                uint8_t tileLow = Peek(BaseBackgroundTableAddress + patternAddress + h);
                uint8_t tileHigh = Peek(BaseBackgroundTableAddress + patternAddress + h + 8);

                for (uint32_t g = 0; g < 8; ++g)
                {
                    uint16_t pixel = 0x0003 & ((((tileLow << g) & 0x80) >> 7) | (((tileHigh << g) & 0x80) >> 6));
                    uint16_t paletteIndex = 0x3F00 | (atByte << 2) | pixel;
                    if ((paletteIndex & 0x3) == 0) paletteIndex = 0x3F00;
                    uint32_t rgb = RgbLookupTable[Peek(paletteIndex)];
                    uint8_t red = (rgb & 0xFF0000) >> 16;
                    uint8_t green = (rgb & 0x00FF00) >> 8;
                    uint8_t blue = (rgb & 0x0000FF);

                    uint32_t index = ((24 * f) + (g * 3)) + ((6144 * i) + (h * 768));

                    pixels[index] = red;
                    pixels[index + 1] = green;
                    pixels[index + 2] = blue;
                }
            }
        }
    }
}

void PPU::GetPatternTable(int table, int palette, uint8_t* pixels)
{
    uint16_t tableIndex;
    if (table == 0)
    {
        tableIndex = 0x0000;
    }
    else
    {
        tableIndex = 0x1000;
    }

    for (uint32_t i = 0; i < 16; ++i)
    {
        for (uint32_t f = 0; f < 16; ++f)
        {
            uint32_t patternIndex = (16 * f) + (256 * i);

            for (uint32_t g = 0; g < 8; ++g)
            {
                uint8_t tileLow = Peek(tableIndex + patternIndex + g);
                uint8_t tileHigh = Peek(tableIndex + patternIndex + g + 8);

                for (uint32_t h = 0; h < 8; ++h)
                {
                    uint16_t pixel = 0x0003 & ((((tileLow << h) & 0x80) >> 7) | (((tileHigh << h) & 0x80) >> 6));
                    uint16_t paletteIndex = (0x3F00 + (4 * (palette % 8))) | pixel;
                    uint32_t rgb = RgbLookupTable[Peek(paletteIndex)];
                    uint8_t red = (rgb & 0xFF0000) >> 16;
                    uint8_t green = (rgb & 0x00FF00) >> 8;
                    uint8_t blue = (rgb & 0x0000FF);

                    uint32_t index = ((24 * f) + (h * 3)) + ((3072 * i) + (g * 384));

                    pixels[index] = red;
                    pixels[index + 1] = green;
                    pixels[index + 2] = blue;
                }
            }
        }
    }
}

void PPU::GetPalette(int palette, uint8_t* pixels)
{
    uint16_t baseAddress;
    if (palette == 0)
    {
        baseAddress = 0x3F00;
    }
    else if (palette == 1)
    {
        baseAddress = 0x3F04;
    }
    else if (palette == 2)
    {
        baseAddress = 0x3F08;
    }
    else if (palette == 3)
    {
        baseAddress = 0x3F0C;
    }
    else if (palette == 4)
    {
        baseAddress = 0x3F10;
    }
    else if (palette == 5)
    {
        baseAddress = 0x3F14;
    }
    else if (palette == 6)
    {
        baseAddress = 0x3F18;
    }
    else
    {
        baseAddress = 0x3F1C;
    }

    for (uint32_t i = 0; i < 4; ++i)
    {
        uint16_t paletteAddress = baseAddress + i;

        for (uint32_t g = 0; g < 16; ++g)
        {
            for (uint32_t h = 0; h < 16; ++h)
            {
                uint32_t rgb = RgbLookupTable[Peek(paletteAddress)];
                uint8_t red = (rgb & 0xFF0000) >> 16;
                uint8_t green = (rgb & 0x00FF00) >> 8;
                uint8_t blue = (rgb & 0x0000FF);

                uint32_t index = ((48 * i) + (h * 3)) + (192 * g);

                pixels[index] = red;
                pixels[index + 1] = green;
                pixels[index + 2] = blue;
            }
        }
    }
}

void PPU::GetPrimaryOAM(int sprite, uint8_t* pixels)
{
    uint8_t byteOne = PrimaryOam[(0x4 * sprite) + 1];
    uint8_t byteTwo = PrimaryOam[(0x4 * sprite) + 2];

    uint16_t tableIndex = BaseSpriteTableAddress;
    uint16_t patternIndex = byteOne * 16;
    uint8_t palette = (byteTwo & 0x03) + 4;

    for (uint32_t i = 0; i < 8; ++i)
    {
        uint8_t tileLow = Peek(tableIndex + patternIndex + i);
        uint8_t tileHigh = Peek(tableIndex + patternIndex + i + 8);

        for (uint32_t f = 0; f < 8; ++f)
        {
            uint16_t pixel = 0x0003 & ((((tileLow << f) & 0x80) >> 7) | (((tileHigh << f) & 0x80) >> 6));
            uint16_t paletteIndex = (0x3F00 + (4 * (palette % 8))) | pixel;
            uint32_t rgb = RgbLookupTable[Peek(paletteIndex)];
            uint8_t red = (rgb & 0xFF0000) >> 16;
            uint8_t green = (rgb & 0x00FF00) >> 8;
            uint8_t blue = (rgb & 0x0000FF);

            uint32_t index = (24 * i) + (f * 3);

            pixels[index] = red;
            pixels[index + 1] = green;
            pixels[index + 2] = blue;
        }
    }
}

StateSave::Ptr PPU::SaveState()
{
    StateSave::Ptr state = StateSave::New();

    state->StoreValue(Clock);
    state->StoreValue(Dot);
    state->StoreValue(Line);
    state->StoreValue(BaseSpriteTableAddress);
    state->StoreValue(BaseBackgroundTableAddress);
    state->StoreValue(LowerBits);
    state->StoreValue(OamAddress);
    state->StoreValue(PpuAddress);
    state->StoreValue(PpuTempAddress);
    state->StoreValue(FineXScroll);
    state->StoreValue(DataBuffer);
    state->StoreValue(PrimaryOam);
    state->StoreValue(SecondaryOam);
    state->StoreValue(NameTable0);
    state->StoreValue(NameTable1);
    state->StoreValue(PaletteTable);
    state->StoreValue(NameTableByte);
    state->StoreValue(AttributeByte);
    state->StoreValue(TileBitmapLow);
    state->StoreValue(TileBitmapHigh);
    state->StoreValue(BackgroundShift0);
    state->StoreValue(BackgroundShift1);
    state->StoreValue(BackgroundAttributeShift0);
    state->StoreValue(BackgroundAttributeShift1);
    state->StoreValue(BackgroundAttribute);
    state->StoreValue(SpriteCount);
    state->StoreValue(SpriteShift0);
    state->StoreValue(SpriteShift1);
    state->StoreValue(SpriteAttribute);
    state->StoreValue(SpriteCounter);
    state->StoreValue(FrameBufferIndex);

    state->StorePackedValues(
        Even, 
        SuppressNmi,
        InterruptActive,
        PpuAddressIncrement,
        SpriteSizeSwitch,
        NmiEnabled,
        GrayScaleEnabled,
        ShowBackgroundLeft
    );

    state->StorePackedValues(
        ShowSpritesLeft,
        ShowBackground,
        ShowSprites,
        IntenseRed,
        IntenseGreen,
        IntenseBlue,
        SpriteOverflowFlag,
        SpriteZeroHitFlag
    );

    state->StorePackedValues(
        NmiOccuredFlag,
        SpriteZeroSecondaryOamFlag,
        AddressLatch,
        RenderingEnabled,
        RenderStateDelaySlot
    );

    return state;
}

void PPU::LoadState(const StateSave::Ptr& state)
{
    state->ExtractValue(Clock);
    state->ExtractValue(Dot);
    state->ExtractValue(Line);
    state->ExtractValue(BaseSpriteTableAddress);
    state->ExtractValue(BaseBackgroundTableAddress);
    state->ExtractValue(LowerBits);
    state->ExtractValue(OamAddress);
    state->ExtractValue(PpuAddress);
    state->ExtractValue(PpuTempAddress);
    state->ExtractValue(FineXScroll);
    state->ExtractValue(DataBuffer);
    state->ExtractValue(PrimaryOam);
    state->ExtractValue(SecondaryOam);
    state->ExtractValue(NameTable0);
    state->ExtractValue(NameTable1);
    state->ExtractValue(PaletteTable);
    state->ExtractValue(NameTableByte);
    state->ExtractValue(AttributeByte);
    state->ExtractValue(TileBitmapLow);
    state->ExtractValue(TileBitmapHigh);
    state->ExtractValue(BackgroundShift0);
    state->ExtractValue(BackgroundShift1);
    state->ExtractValue(BackgroundAttributeShift0);
    state->ExtractValue(BackgroundAttributeShift1);
    state->ExtractValue(BackgroundAttribute);
    state->ExtractValue(SpriteCount);
    state->ExtractValue(SpriteShift0);
    state->ExtractValue(SpriteShift1);
    state->ExtractValue(SpriteAttribute);
    state->ExtractValue(SpriteCounter);
    state->ExtractValue(FrameBufferIndex);

    state->ExtractPackedValues(
        Even, 
        SuppressNmi,
        InterruptActive,
        PpuAddressIncrement,
        SpriteSizeSwitch,
        NmiEnabled,
        GrayScaleEnabled,
        ShowBackgroundLeft
    );

    state->ExtractPackedValues(
        ShowSpritesLeft,
        ShowBackground,
        ShowSprites,
        IntenseRed,
        IntenseGreen,
        IntenseBlue,
        SpriteOverflowFlag,
        SpriteZeroHitFlag
    );

    state->ExtractPackedValues(
        NmiOccuredFlag,
        SpriteZeroSecondaryOamFlag,
        AddressLatch,
        RenderingEnabled,
        RenderStateDelaySlot
    );
}

uint8_t PPU::ReadNameTable0(uint16_t address)
{
    return NameTable0[address];
}

uint8_t PPU::ReadNameTable1(uint16_t address)
{
    return NameTable1[address];
}

void PPU::WriteNameTable0(uint8_t M, uint16_t address)
{
    NameTable0[address] = M;
}

void PPU::WriteNameTable1(uint8_t M, uint16_t address)
{
    NameTable1[address] = M;
}

void PPU::RenderNtscPixel(int pixel)
{
    static constexpr float levels[16] =
    {
        // Normal Levels
          -0.116f / 12.f, 0.000f / 12.f, 0.307f / 12.f, 0.714f / 12.f,
           0.399f / 12.f, 0.684f / 12.f, 1.000f / 12.f, 1.000f / 12.f,
           // Attenuated Levels
          -0.087f / 12.f, 0.000f / 12.f, 0.229f / 12.f, 0.532f / 12.f,
           0.298f / 12.f, 0.510f / 12.f, 0.746f / 12.f, 0.746f / 12.f
    };

    auto SignalLevel = [=](int phase)
    {
        // Decode the NES color.
        int color = (pixel & 0x0F);    // 0..15 "cccc"
        int level = (pixel >> 4) & 3;  // 0..3  "ll"
        int emphasis = (pixel >> 6);   // 0..7  "eee"
        if (color > 13) { level = 1; } // For colors 14..15, level 1 is forced.

                                       // The square wave for this color alternates between these two voltages:
        int low = level;
        int high = level + 4;

        if (color == 0) { low = high; } // For color 0, only high level is emitted
        if (color > 12) { high = low; } // For colors 13..15, only low level is emitted

         // Generate the square wave
        auto InColorPhase = [=](int color) { return (color + phase) % 12 < 6; }; // Inline function
        int index = InColorPhase(color) ? high : low;

        // When de-emphasis bits are set, some parts of the signal are attenuated:
        if (((emphasis & 1) && InColorPhase(0)) || ((emphasis & 2) && InColorPhase(4)) || ((emphasis & 4) && InColorPhase(8)))
        {
            return levels[index + 8];
        }
        else
        {
            return levels[index];
        }
    };

    int phase = (Clock << 3) % 12;
    for (int p = 0; p < 8; ++p) // Each pixel produces distinct 8 samples of NTSC signal.
    {
        int index = ((Dot - 1) << 3) + p;
        SignalLevels[index] = SignalLevel((phase + p) % 12);
    }
}

void PPU::RenderNtscLine()
{
    static constexpr int width = 256;
    static constexpr float sineTable[12] =
    {
        0.89101f,  0.54464f,  0.05234f, -0.45399f, -0.83867f, -0.99863f,
       -0.89101f, -0.54464f, -0.05234f,  0.45399f,  0.83867f,  0.99863f
    };

    int phase = ((Clock - width + 1) << 3) % 12;

    for (unsigned int x = 0; x < width; ++x)
    {
        // Determine the region of scanline signal to sample. Take 12 samples.
        int center = ((x * width * 8) / width) + 4;
        int begin = center - 6; if (begin < 0) begin = 0;
        int end = center + 6; if (end > (width << 3)) end = (width << 3);
        float y = 0.f, i = 0.f, q = 0.f; // Calculate the color in YIQ.
        for (int p = begin; p < end; ++p) // Collect and accumulate samples
        {
            float level = SignalLevels[p];
            y = y + level;
            i = i + level * sineTable[(phase + p + 3) % 12];
            q = q + level * sineTable[(phase + p) % 12];
        }

        auto clamp = [](float v) { return (v > 255.0f) ? 255.0f : ((v < 0.0f) ? 0.0f : v); };

        int red = static_cast<int>(clamp(255.95f * (y + 0.946882f*i + 0.623557f*q)));
        int green = static_cast<int>(clamp(255.95f * (y + -0.274788f*i + -0.635691f*q)));
        int blue = static_cast<int>(clamp(255.95f * (y + -1.108545f*i + 1.709007f*q)));

        if (VideoOut != nullptr)
        {
            uint32_t pixel = (red << 16) | (green << 8) | blue | 0xFF000000;
			FrameBuffer[FrameBufferIndex++] = pixel;
        }
        
    }
}

// This is sort of a bastardized version of the PPU sprite evaluation
// It is not cycle accurate unfortunately, but I don't suspect that will
// cause any issues (but what do I know really?)
void PPU::SpriteEvaluation()
{
    uint8_t glitchCount = 0;
    SpriteCount = 0;
    SpriteZeroSecondaryOamFlag = false;

    for (int i = 0; i < 8; ++i)
    {
        uint8_t index = i * 4;
        SecondaryOam[index] = 0xFF;
        SecondaryOam[index + 1] = 0xFF;
        SecondaryOam[index + 2] = 0xFF;
        SecondaryOam[index + 3] = 0xFF;
    }

    for (int i = 0; i < 64; ++i)
    {
        uint8_t size = SpriteSizeSwitch ? 16 : 8;
        uint8_t index = i * 4; // Index of one of 64 sprites in Primary OAM
        uint8_t spriteY = PrimaryOam[index + glitchCount]; // The sprites Y coordinate, glitchCount is used to replicate a hardware glitch

        if (SpriteCount < 8) // If fewer than 8 sprites have been found
        {
            // If a sprite is in range, copy it to the next spot in secondary OAM
            if (spriteY <= Line && spriteY + size > Line && Line != 239)
            {
                if (index == 0)
                {
                    SpriteZeroSecondaryOamFlag = true;
                }

                SecondaryOam[SpriteCount * 4] = PrimaryOam[index];
                SecondaryOam[(SpriteCount * 4) + 1] = PrimaryOam[index + 1];
                SecondaryOam[(SpriteCount * 4) + 2] = PrimaryOam[index + 2];
                SecondaryOam[(SpriteCount * 4) + 3] = PrimaryOam[index + 3];
                SpriteCount++;
            }
        }
        else // 8 sprites have already been found
        {
            // If the sprite is in range, set the sprite overflow, due to glitch Count this may actually be incorrect
            if (spriteY <= Line && spriteY + size > Line && !SpriteOverflowFlag)
            {
                SpriteOverflowFlag = true;
            }
            else if (!SpriteOverflowFlag) // If no sprite in range the increment glitchCount
            {
                glitchCount++;
            }
        }
    }
}

void PPU::RenderPixel()
{
	uint16_t bgPixel = 0;
    uint16_t bgPaletteIndex = 0x3F00;

    if (ShowBackground && (ShowBackgroundLeft || Dot > 8))
	{
        uint16_t bgAttribute = (((BackgroundAttributeShift0 << FineXScroll) & 0x8000) >> 13) | (((BackgroundAttributeShift1 << FineXScroll) & 0x8000) >> 12);
		bgPixel = (((BackgroundShift0 << FineXScroll) & 0x8000) >> 15) | (((BackgroundShift1 << FineXScroll) & 0x8000) >> 14);
		bgPaletteIndex |= bgAttribute | bgPixel;
	}

    uint16_t spPixel = 0;
    uint16_t spPaletteIndex = 0x3F10;
    bool spPriority = true;
    bool spriteFound = false;

    for (int i = 0; i < 8; ++i)
    {
        if (SpriteCounter[i] <= 0 && SpriteCounter[i] >= -7)
        {
            uint8_t spShift0 = SpriteShift0[i];
            uint8_t spShift1 = SpriteShift1[i];
            uint8_t spAttribute = SpriteAttribute[i];

            uint16_t pixel;
            if (spAttribute & 0x40)
            {
                pixel = (spShift0 & 0x1) | ((spShift1 & 0x1) << 1);
                SpriteShift0[i] = spShift0 >> 1;
                SpriteShift1[i] = spShift1 >> 1;
            }
            else
            {
                pixel = ((spShift0 & 0x80) >> 7) | ((spShift1 & 0x80) >> 6);
                SpriteShift0[i] = spShift0 << 1;
                SpriteShift1[i] = spShift1 << 1;
            }

            if (ShowSprites && (ShowSpritesLeft || Dot > 8))
            {
                if (pixel != 0 && !spriteFound)
                {
                    spPixel = pixel;
                    spPriority = !!(spAttribute & 0x20);
                    spPaletteIndex |= ((spAttribute & 0x03) << 2) | spPixel;
                    spriteFound = true;

                    if (i == 0 && SpriteZeroSecondaryOamFlag && !SpriteZeroHitFlag)
                    {
                        SpriteZeroHitFlag = Dot > 1 && Dot != 256 && spPixel != 0 && bgPixel != 0;
                    }
                }
            }
        }

        --SpriteCounter[i];
    }

    uint16_t colour;
    uint16_t paletteIndex;

    if (bgPixel == 0 && spPixel == 0)
    {
        paletteIndex = 0x3F00;
    }
    else if (bgPixel == 0 && spPixel != 0)
    {
        paletteIndex = spPaletteIndex;
    }
    else if (bgPixel != 0 && spPixel == 0)
    {
        paletteIndex = bgPaletteIndex;
    }
    else if (!spPriority)
    {
        paletteIndex = spPaletteIndex;
    }
    else
    {
        paletteIndex = bgPaletteIndex;
    }

    if ((paletteIndex & 0x3) == 0)
    {
        paletteIndex = 0x3F00;
    }

    colour = ReadPalette(paletteIndex);

    DecodePixel(colour);
}

void PPU::RenderPixelIdle()
{
    if (TurboFrameSkip == 0)
    {
        uint16_t colour;

        if (PpuAddress >= 0x3F00 && PpuAddress <= 0x3FFF)
        {
            colour = ReadPalette(PpuAddress);
        }
        else
        {
            colour = ReadPalette(0x3F00);
        }

        DecodePixel(colour);
    }
}

void PPU::DecodePixel(uint16_t colour)
{
    if (TurboFrameSkip == 0)
    {
        // NTSC rendering is not supported in turbo mode
        if (!TurboModeEnabled && NtscMode)
        {
            uint16_t pixel = colour & 0x3F;
            pixel = pixel | (IntenseRed << 6);
            pixel = pixel | (IntenseGreen << 7);
            pixel = pixel | (IntenseBlue << 8);

            RenderNtscPixel(pixel);
            if (Dot == 256) RenderNtscLine();
        }
        else
        {
            if (VideoOut != nullptr)
            {
                FrameBuffer[FrameBufferIndex++] = RgbLookupTable[colour];
            }
        }
    }
}

void PPU::MaybeChangeModes()
{
    if (TurboModeEnabled != RequestTurboMode)
    {
        TurboModeEnabled = RequestTurboMode;
        if (!TurboModeEnabled)
        {
            TurboFrameSkip = 0;
        }
    }
    else if (NtscMode != RequestNtscMode)
    {
        NtscMode = RequestNtscMode;
    }
}

void PPU::IncrementXScroll()
{
    if ((PpuAddress & 0x001F) == 31) // Reach end of Name Table
    {
        PpuAddress &= 0xFFE0; // Set Coarse X to 0
        PpuAddress ^= 0x400; // Switch horizontal Name Table
    }
    else
    {
        PpuAddress++; // Increment Coarse X
    }
}

void PPU::IncrementYScroll()
{
    if ((PpuAddress & 0x7000) != 0x7000) // if the fine Y < 7
    {
        PpuAddress += 0x1000; // increment fine Y
    }
    else
    {
        PpuAddress &= 0x8FFF; // Set fine Y to 0
        uint16_t coarseY = (PpuAddress & 0x03E0) >> 5; // get coarse Y

        if (coarseY == 29) // End of name table
        {
            coarseY = 0; // set coarse Y to 0
            PpuAddress ^= 0x0800; // Switch Name Table
        }
        else if (coarseY == 31) // End of attribute table
        {
            coarseY = 0; // set coarse Y to 0
        }
        else
        {
            coarseY++; // Increment coarse Y
        }

        PpuAddress = (PpuAddress & 0xFC1F) | (coarseY << 5); // Combine values into new address
    }
}

void PPU::IncrementClock()
{
    if (Line == 261 && !Even && (ShowSprites || ShowBackground) && Dot == 339)
    {
        Dot = Line = 0;
    }
    else if (Line == 261 && Dot == 340)
    {
        Dot = Line = 0;
    }
    else if (Dot == 340)
    {
        Dot = 0;
        ++Line;
    }
    else
    {
        ++Dot;
    }

    ++Clock;
}

void PPU::LoadBackgroundShiftRegisters()
{
    BackgroundShift0 = BackgroundShift0 | TileBitmapLow;
    BackgroundShift1 = BackgroundShift1 | TileBitmapHigh;
    BackgroundAttributeShift0 = BackgroundAttributeShift0 | ((AttributeByte & 0x1) ? 0xFF : 0x00);
    BackgroundAttributeShift1 = BackgroundAttributeShift1 | ((AttributeByte & 0x2) ? 0xFF : 0x00);
}

void PPU::SetNameTableAddress()
{
    SetBusAddress(0x2000 | (PpuAddress & 0x0FFF));
}

void PPU::DoNameTableFetch()
{
    NameTableByte = Read();
}

void PPU::SetBackgroundAttributeAddress()
{
    SetBusAddress(0x23C0 | (PpuAddress & 0x0C00) | ((PpuAddress >> 4) & 0x38) | ((PpuAddress >> 2) & 0x07));
}

void PPU::DoBackgroundAttributeFetch()
{
    // Get the attribute byte, determines the palette to use when rendering
    AttributeByte = Read();
    uint8_t attributeShift = (((PpuAddress & 0x0002) >> 1) | ((PpuAddress & 0x0040) >> 5)) << 1;
    AttributeByte = (AttributeByte >> attributeShift) & 0x3;
}

void PPU::SetBackgroundLowByteAddress()
{
    uint8_t fineY = PpuAddress >> 12; // Get fine y scroll bits from address
    uint16_t patternAddress = static_cast<uint16_t>(NameTableByte) << 4; // Get pattern address, independent of the table

    SetBusAddress(BaseBackgroundTableAddress + patternAddress + fineY);
}

void PPU::DoBackgroundLowByteFetch()
{
    TileBitmapLow = Read();
}

void PPU::SetBackgroundHighByteAddress()
{
    uint8_t fineY = PpuAddress >> 12; // Get fine y scroll
    uint16_t patternAddress = static_cast<uint16_t>(NameTableByte) << 4; // Get pattern address, independent of the table

    SetBusAddress(BaseBackgroundTableAddress + patternAddress + fineY + 8);
}

void PPU::DoBackgroundHighByteFetch()
{
    TileBitmapHigh = Read();
}

void PPU::DoSpriteAttributeFetch()
{
    uint8_t sprite = (Dot - 257) / 8;
    SpriteAttribute[sprite] = SecondaryOam[(sprite * 4) + 2];
}

void PPU::DoSpriteXCoordinateFetch()
{
    uint8_t sprite = (Dot - 257) / 8;
    SpriteCounter[sprite] = SecondaryOam[(sprite * 4) + 3];
}

void PPU::SetSpriteLowByteAddress()
{
    uint8_t sprite = (Dot - 257) / 8;
    if (sprite < SpriteCount)
    {
        bool flipVertical = !!(0x80 & SpriteAttribute[sprite]);
        uint8_t spriteY = SecondaryOam[sprite * 4];

        if (SpriteSizeSwitch) // if spriteSize is 8x16
        {
            uint16_t base = (0x1 & SecondaryOam[(sprite * 4) + 1]) ? 0x1000 : 0; // Get base pattern table from bit 0 of pattern address
            uint16_t patternIndex = base + ((SecondaryOam[(sprite * 4) + 1] >> 1) << 5); // Index of the beginning of the pattern
            uint16_t offset = flipVertical ? 15 - (Line - spriteY) : (Line - spriteY); // Offset from base index
            if (offset >= 8) offset += 8;

            SetBusAddress(patternIndex + offset);
        }
        else
        {
            uint16_t patternIndex = BaseSpriteTableAddress + (SecondaryOam[(sprite * 4) + 1] << 4); // Index of the beginning of the pattern
            uint16_t offset = flipVertical ? 7 - (Line - spriteY) : (Line - spriteY); // Offset from base index

            SetBusAddress(patternIndex + offset);
        }
    }
    else
    {
        if (SpriteSizeSwitch)
        {
            SetBusAddress(0x1FF0);
        }
        else
        {
            SetBusAddress(BaseSpriteTableAddress + 0xFF0);
        }
    }
}

void PPU::DoSpriteLowByteFetch()
{
    uint8_t sprite = (Dot - 257) / 8;
    uint8_t bitmap = Read();

    SpriteShift0[sprite] = sprite < SpriteCount ? bitmap : 0x00; 
}

void PPU::SetSpriteHighByteAddress()
{
    uint8_t sprite = (Dot - 257) / 8;
    if (sprite < SpriteCount)
    {
        bool flipVertical = !!(0x80 & SpriteAttribute[sprite]);
        uint8_t spriteY = SecondaryOam[sprite * 4];

        if (SpriteSizeSwitch) // if spriteSize is 8x16
        {
            uint16_t base = (0x1 & SecondaryOam[(sprite * 4) + 1]) ? 0x1000 : 0; // Get base pattern table from bit 0 of pattern address
            uint16_t patternIndex = base + ((SecondaryOam[(sprite * 4) + 1] >> 1) << 5); // Index of the beginning of the pattern
            uint16_t offset = flipVertical ? 15 - (Line - spriteY) : (Line - spriteY); // Offset from base index
            if (offset >= 8) offset += 8;

            SetBusAddress(patternIndex + offset + 8);
        }
        else
        {
            uint16_t patternIndex = BaseSpriteTableAddress + (SecondaryOam[(sprite * 4) + 1] << 4); // Index of the beginning of the pattern
            uint16_t offset = flipVertical ? 7 - (Line - spriteY) : (Line - spriteY); // Offset from base index

            SetBusAddress(patternIndex + offset + 8);
        }
    }
    else
    {
        if (SpriteSizeSwitch)
        {
            SetBusAddress(0x1FF0);
        }
        else
        {
            SetBusAddress(BaseSpriteTableAddress + 0xFF0);
        }
    }
}

void PPU::DoSpriteHighByteFetch()
{
    uint8_t sprite = (Dot - 257) / 8;
    uint8_t bitmap = Read();

    SpriteShift1[sprite] = sprite < SpriteCount ? bitmap : 0x00; 
}


void PPU::SetBusAddress(uint16_t address)
{
    PpuBusAddress = address & 0x3FFF;

    if (PpuBusAddress < 0x3F00)
    {
        Cartridge->SetPpuAddress(PpuBusAddress);
    }
}

uint8_t PPU::Read()
{
    if (PpuBusAddress < 0x3F00)
    {
        return Cartridge->PpuRead();
    }
    else
    {
        return ReadPalette(PpuBusAddress);
    }
}

void PPU::Write(uint8_t value)
{
    if (PpuBusAddress < 0x3F00)
    {
        Cartridge->PpuWrite(value);
    }
    else
    {
        WritePalette(value, PpuBusAddress);
    }
}

uint8_t PPU::ReadPalette(uint16_t address)
{
    // Ignore second nibble if addr is a multiple of 4
    if (address % 4 == 0)
    {
        address &= 0xFF0F;
    }

    return PaletteTable[address % 0x20];
}

void PPU::WritePalette(uint8_t value, uint16_t address)
{
    // Ignore second nibble if addr is a multiple of 4
    if (address % 4 == 0)
    {
        address &= 0xFF0F;
    }

    PaletteTable[address % 0x20] = value & 0x3F; // Ignore bits 6 and 7
}

uint8_t PPU::Peek(uint16_t address)
{
    if (address < 0x3F00)
    {
        return Cartridge->PpuPeek(address);
    }
    else
    {
        return ReadPalette(address);
    }
}

void PPU::UpdateFrameRate()
{
    using namespace std::chrono;

    steady_clock::time_point now = steady_clock::now();
    microseconds time_span = duration_cast<microseconds>(now - FrameCountStart);
    if (time_span.count() >= 1000000)
    {
        CurrentFps = FpsCounter;
        FpsCounter = 0;
        FrameCountStart = steady_clock::now();

        if (VideoOut != nullptr)
        {
            VideoOut->SetFps(CurrentFps);
        }
    }
    else
    {
        FpsCounter++;
    }
}

void PPU::UpdateFrameSkipCounters()
{
    if (TurboModeEnabled)
    {
        if (TurboFrameSkip == 0)
        {
            TurboFrameSkip = 20;
        }
        else
        {
            TurboFrameSkip--;
        }
    }
}
