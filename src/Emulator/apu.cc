#include <exception>
#include <cstring>
#include <cmath>
#include <iostream>

#include "apu.h"
#include "cpu.h"

namespace {
    template<typename T>
    T clamp(T val, T low, T hi)
    {
        return (val < low) ? low : ((val > hi) ? hi : val);
    }
};

const uint32_t APU::CpuFrequency = 1789773;

const uint8_t APU::LengthCounterLookupTable[32] =
{
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

//**********************************************************************
// APU Pulse Unit
//**********************************************************************

const uint8_t APU::PulseUnit::Sequences[4] = { 0x02, 0x06, 0x1E, 0xF9 };

APU::PulseUnit::PulseUnit(bool IsPulseUnitOne)
    : Timer(0)
    , TimerPeriod(0)
    , SequenceCount(0)
    , LengthCounter(0)
    , EnvelopeDividerVolume(0)
    , EnvelopeDividerCounter(0)
    , EnvelopeCounter(0xF)
    , SweepShiftCount(0)
    , SweepDivider(0)
    , SweepDividerCounter(0)
    , LengthHaltEnvelopeLoopFlag(false)
    , ConstantVolumeFlag(false)
    , SweepEnableFlag(false)
    , SweepReloadFlag(false)
    , SweepNegateFlag(false)
    , EnvelopeStartFlag(false)
    , EnabledFlag(false)
    , PulseOneFlag(IsPulseUnitOne)
{}

void APU::PulseUnit::SetEnabled(bool enabled)
{
    EnabledFlag = enabled;
    if (!EnabledFlag)
    {
        LengthCounter = 0;
    }
}

bool APU::PulseUnit::GetEnabled()
{
    return EnabledFlag;
}

uint8_t APU::PulseUnit::GetLengthCounter()
{
    return LengthCounter;
}

void APU::PulseUnit::WriteRegister(uint8_t reg, uint8_t value)
{
    switch (reg)
    {
    case 0:
        DutyCycle = value >> 6;
        LengthHaltEnvelopeLoopFlag = !!(value & 0x20);
        ConstantVolumeFlag = !!(value & 0x10);
        EnvelopeDividerVolume = (value & 0x0F);
        break;
    case 1:
        SweepEnableFlag = !!(value & 0x80);;
        SweepDivider = value >> 4;
        SweepNegateFlag = !!(value & 0x08);
        SweepShiftCount = (value & 0x07);
        SweepReloadFlag = true;
        break;
    case 2:
        TimerPeriod = (TimerPeriod & 0xFF00) | value;
        break;
    case 3:
        TimerPeriod = (TimerPeriod & 0x00FF) | (static_cast<uint16_t>(value & 0x07) << 8);
        EnvelopeStartFlag = true;
        SequenceCount = 0;

        if (EnabledFlag)
        {
            LengthCounter = LengthCounterLookupTable[value >> 3];
        }

        break;
    default:
        throw std::runtime_error("APU::PulseUnit tried to write to non-existant register");
    }
}

void APU::PulseUnit::ClockTimer()
{
    if (Timer == 0)
    {
        Timer = TimerPeriod;
        SequenceCount = (SequenceCount + 1) % 8;
    }
    else
    {
        --Timer;
    }
}

void APU::PulseUnit::ClockSweep()
{
    if (SweepDividerCounter == 0 && SweepEnableFlag && SweepShiftCount != 0)
    {
        if (SweepNegateFlag)
        {
            if (TimerPeriod >= 8)
            {
                // The first pulse unit has a hardware glitch that causes it to be subtracted by the shift value minus 1
                if (PulseOneFlag)
                {
                    TimerPeriod -= (TimerPeriod >> SweepShiftCount) - 1;
                }
                else
                {
                    TimerPeriod -= (TimerPeriod >> SweepShiftCount);
                }
            }
        }
        else
        {
            uint16_t TargetPeriod = TimerPeriod + (TimerPeriod >> SweepShiftCount);
            if (TargetPeriod <= 0x7FF && TimerPeriod >= 8)
            {
                TimerPeriod = TargetPeriod;
            }
        }
    }

    if (SweepReloadFlag)
    {
        SweepDividerCounter = SweepDivider;
        SweepReloadFlag = false;
    }
    else if (SweepDividerCounter == 0)
    {
        SweepDividerCounter = SweepDivider;
    }
    else
    {
        --SweepDividerCounter;
    }
}

void APU::PulseUnit::ClockEnvelope()
{
    if (EnvelopeStartFlag)
    {
        EnvelopeStartFlag = false;
        EnvelopeDividerCounter = EnvelopeDividerVolume;
        EnvelopeCounter = 0xF;
    }
    else
    {
        if (EnvelopeDividerCounter == 0)
        {
            EnvelopeDividerCounter = EnvelopeDividerVolume;

            if (EnvelopeCounter == 0)
            {
                if (LengthHaltEnvelopeLoopFlag)
                {
                    EnvelopeCounter = 0xF;
                }
            }
            else
            {
                --EnvelopeCounter;
            }
        }
        else
        {
            --EnvelopeDividerCounter;
        }
    }
}

void APU::PulseUnit::ClockLengthCounter()
{
    if (LengthCounter != 0 && !LengthHaltEnvelopeLoopFlag)
    {
        --LengthCounter;
    }
}

APU::PulseUnit::operator uint8_t ()
{
    uint8_t SequenceValue = (Sequences[DutyCycle] >> SequenceCount) & 0x1;

    // Target period only matters in add mode
    uint16_t TargetPeriod = SweepNegateFlag ? 0 : TimerPeriod + (TimerPeriod >> SweepShiftCount);

    if (SequenceValue == 0 || TargetPeriod > 0x7FF || LengthCounter == 0 || TimerPeriod < 8)
    {
        return 0;
    }
    else
    {
        if (ConstantVolumeFlag)
        {
            return EnvelopeDividerVolume;
        }
        else
        {
            return EnvelopeCounter;
        }
    }
}

//**********************************************************************
// APU Triangle Unit
//**********************************************************************

const uint8_t APU::TriangleUnit::Sequence[32] =
{
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

APU::TriangleUnit::TriangleUnit()
    : Timer(0)
    , TimerPeriod(0)
    , SequenceCount(0)
    , LinearCounter(0)
    , LinearCounterPeriod(0)
    , LengthCounter(0)
    , LengthHaltControlFlag(false)
    , LinearCounterReloadFlag(false)
    , EnabledFlag(false)
{}

void APU::TriangleUnit::SetEnabled(bool enabled)
{
    EnabledFlag = enabled;
    if (!EnabledFlag)
    {
        LengthCounter = 0;
    }
}

bool APU::TriangleUnit::GetEnabled()
{
    return EnabledFlag;
}

uint8_t APU::TriangleUnit::GetLengthCounter()
{
    return LengthCounter;
}

void APU::TriangleUnit::WriteRegister(uint8_t reg, uint8_t value)
{
    switch (reg)
    {
    case 0:
        LengthHaltControlFlag = !!(value & 0x80);
        LinearCounterPeriod = value & 0x7F;
        break;
    case 1:
        TimerPeriod = (TimerPeriod & 0xFF00) | value;
        break;
    case 2:
        TimerPeriod = (TimerPeriod & 0x00FF) | (static_cast<uint16_t>(value & 0x7) << 8);
        LinearCounterReloadFlag = true;

        if (EnabledFlag)
        {
            LengthCounter = LengthCounterLookupTable[value >> 3];
        }

        break;
    default:
        throw std::runtime_error("APU::TriangleUnit tried to write to non-existant register");
    }
}

void APU::TriangleUnit::ClockTimer()
{
    if (Timer != 0)
    {
        --Timer;
    }
    else
    {
        if (LinearCounter != 0 && LengthCounter != 0)
        {
            SequenceCount = (SequenceCount + 1) % 32;
        }

        Timer = TimerPeriod;
    }
}

void APU::TriangleUnit::ClockLinearCounter()
{
    if (LinearCounterReloadFlag)
    {
        LinearCounter = LinearCounterPeriod;
    }
    else if (LinearCounter != 0)
    {
        --LinearCounter;
    }

    if (!LengthHaltControlFlag)
    {
        LinearCounterReloadFlag = false;
    }
}

void APU::TriangleUnit::ClockLengthCounter()
{
    if (LengthCounter != 0 && !LengthHaltControlFlag)
    {
        --LengthCounter;
    }
}

APU::TriangleUnit::operator uint8_t ()
{
    return Sequence[SequenceCount];
}

//**********************************************************************
// APU Noise Unit
//**********************************************************************

const uint16_t APU::NoiseUnit::TimerPeriods[16] =
{
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

APU::NoiseUnit::NoiseUnit()
    : Timer(TimerPeriods[0])
    , TimerPeriodIndex(0)
    , LinearFeedbackShiftRegister(1)
    , LengthCounter(0)
    , EnvelopeDividerVolume(0)
    , EnvelopeDividerCounter(0)
    , EnvelopeCounter(0xF)
    , LengthHaltEnvelopeLoopFlag(false)
    , ConstantVolumeFlag(false)
    , EnvelopeStartFlag(false)
    , ModeFlag(false)
    , EnabledFlag(false)
{}

void APU::NoiseUnit::SetEnabled(bool enabled)
{
    EnabledFlag = enabled;
    if (!EnabledFlag)
    {
        LengthCounter = 0;
    }
}

bool APU::NoiseUnit::GetEnabled()
{
    return EnabledFlag;
}

uint8_t APU::NoiseUnit::GetLengthCounter()
{
    return LengthCounter;
}

void APU::NoiseUnit::WriteRegister(uint8_t reg, uint8_t value)
{
    switch (reg)
    {
    case 0:
        LengthHaltEnvelopeLoopFlag = !!(value & 0x20);
        ConstantVolumeFlag = !!(value & 0x10);
        EnvelopeDividerVolume = value & 0x0F;
        break;
    case 1:
        ModeFlag = !!(value & 0x80);
        TimerPeriodIndex = value & 0x0F;
        break;
    case 2:
        if (EnabledFlag)
        {
            LengthCounter = LengthCounterLookupTable[value >> 3];
        }
        EnvelopeStartFlag = true;
        break;
    default:
        throw std::runtime_error("APU::NoiseUnit tried to write to non-existant register");
    }
}

void APU::NoiseUnit::ClockTimer()
{
    if (Timer == 0)
    {
        Timer = TimerPeriods[TimerPeriodIndex];

        uint16_t Feedback;
        if (ModeFlag)
        {
            Feedback = (((LinearFeedbackShiftRegister << 6) & 0x0040) ^ (LinearFeedbackShiftRegister & 0x0040)) << 8;
        }
        else
        {
            Feedback = (((LinearFeedbackShiftRegister << 1) & 0x0002) ^ (LinearFeedbackShiftRegister & 0x0002)) << 13;
        }

        LinearFeedbackShiftRegister = (LinearFeedbackShiftRegister >> 1) | Feedback;
    }
    else
    {
        --Timer;
    }
}

void APU::NoiseUnit::ClockEnvelope()
{
    if (EnvelopeStartFlag)
    {
        EnvelopeStartFlag = false;
        EnvelopeDividerCounter = EnvelopeDividerVolume;
        EnvelopeCounter = 0xF;
    }
    else
    {
        if (EnvelopeDividerCounter == 0)
        {
            EnvelopeDividerCounter = EnvelopeDividerVolume;

            if (EnvelopeCounter == 0)
            {
                if (LengthHaltEnvelopeLoopFlag)
                {
                    EnvelopeCounter = 0xF;
                }
            }
            else
            {
                --EnvelopeCounter;
            }
        }
        else
        {
            --EnvelopeDividerCounter;
        }
    }
}

void APU::NoiseUnit::ClockLengthCounter()
{
    if (LengthCounter != 0 && !LengthHaltEnvelopeLoopFlag)
    {
        --LengthCounter;
    }
}

APU::NoiseUnit::operator uint8_t ()
{
    if (LengthCounter != 0 && !(LinearFeedbackShiftRegister & 0x0001))
    {
        if (ConstantVolumeFlag)
        {
            return EnvelopeDividerVolume;
        }
        else
        {
            return EnvelopeCounter;
        }
    }
    else
    {
        return 0;
    }
}

const uint16_t APU::DmcUnit::TimerPeriods[16] =
{
    214, 190, 170, 160, 143, 127, 113, 107, 95, 80, 71, 64, 53,  42,  36,  27
};

//**********************************************************************
// APU DMC Unit
//**********************************************************************

APU::DmcUnit::DmcUnit(APU& apu)
    : Apu(apu)
    , Timer(TimerPeriods[0])
    , TimerPeriodIndex(0)
    , OutputLevel(0)
    , SampleAddress(0xC000)
    , CurrentAddress(0xC000)
    , SampleLength(1)
    , SampleBytesRemaining(0)
    , SampleBuffer(0)
    , SampleShiftRegister(0)
    , SampleBitsRemaining(8)
    , MemoryStallCountdown(0)
    , InterruptFlag(false)
    , InterruptEnabledFlag(false)
    , SampleLoopFlag(false)
    , SampleBufferEmptyFlag(true)
    , InMemoryStall(false)
    , SilenceFlag(true)
{}

void APU::DmcUnit::SetEnabled(bool enabled)
{
    if (!enabled)
    {
        SampleBytesRemaining = 0;
    }
    else if (SampleBytesRemaining == 0)
    {
        CurrentAddress = SampleAddress;
        SampleBytesRemaining = SampleLength;
    }
}

bool APU::DmcUnit::GetEnabled()
{
    return SampleBytesRemaining > 0;
}

uint16_t APU::DmcUnit::GetSampleBytesRemaining()
{
    return SampleBytesRemaining;
}

void APU::DmcUnit::ClearInterrupt()
{
    InterruptFlag = false;
}

void APU::DmcUnit::WriteRegister(uint8_t reg, uint8_t value)
{
    switch (reg)
    {
    case 0:
        InterruptEnabledFlag = !!(value & 0x80);
        SampleLoopFlag = !!(value & 0x40);
        TimerPeriodIndex = value & 0x0F;

        if (!InterruptEnabledFlag)
        {
            InterruptFlag = false;
        }
        break;
    case 1:
        OutputLevel = value & 0x7F;
        break;
    case 2:
        SampleAddress = 0xC000 | (static_cast<uint16_t>(value) << 6);
        break;
    case 3:
        SampleLength = 0x0001 | (static_cast<uint16_t>(value) << 4);
        break;
    default:
        throw std::runtime_error("APU::DmcUnit tried to write to non-existant register");
    }
}

bool APU::DmcUnit::CheckIRQ()
{
    return InterruptFlag;
}

void APU::DmcUnit::ClockTimer()
{
    // Memory Reader
    if (InMemoryStall)
    {
        if (--MemoryStallCountdown == 0)
        {
            SampleBuffer = Apu.Cartridge->PrgRead(CurrentAddress - 0x6000);
            SampleBufferEmptyFlag = false;

            if (--SampleBytesRemaining == 0)
            {
                if (SampleLoopFlag)
                {
                    CurrentAddress = SampleAddress;
                    SampleBytesRemaining = SampleLength;
                }
                else if (InterruptEnabledFlag)
                {
                    InterruptFlag = true;
                }
            }
            else
            {
                if (CurrentAddress == 0xFFFF)
                {
                    CurrentAddress = 0x8000;
                }
                else
                {
                    ++CurrentAddress;
                }
            }

            InMemoryStall = false;
            Apu.Cpu->SetStalled(false);
        }
    }
    else if (SampleBufferEmptyFlag && SampleBytesRemaining > 0)
    {
        InMemoryStall = true;
        MemoryStallCountdown = 2;
        Apu.Cpu->SetStalled(true);
    }

    if (Timer == 0)
    {
        Timer = TimerPeriods[TimerPeriodIndex];

        // Output Unit
        if (!SilenceFlag)
        {
            if (!!(SampleShiftRegister & 0x1))
            {
                if (OutputLevel <= 125)
                {
                    OutputLevel += 2;
                }
            }
            else
            {
                if (OutputLevel >= 2)
                {
                    OutputLevel -= 2;
                }
            }
        }

        SampleShiftRegister >>= 1;
        --SampleBitsRemaining;

        if (SampleBitsRemaining == 0)
        {
            SampleBitsRemaining = 8;

            if (SampleBufferEmptyFlag)
            {
                SilenceFlag = true;
            }
            else
            {
                SilenceFlag = false;
                SampleShiftRegister = SampleBuffer;
                SampleBufferEmptyFlag = true;
            }
        }
    }
    else
    {
        --Timer;
    }
}

APU::DmcUnit::operator uint8_t ()
{
    return OutputLevel;
}

//**********************************************************************
// APU Main Unit
//**********************************************************************

APU::APU()
    : Cpu(nullptr)
    , Cartridge(nullptr)
    , PulseOne(true)
    , PulseTwo(false)
    , Dmc(*this)
    , Clock(0)
    , SequenceCount(0)
    , SequenceMode(false)
    , InterruptInhibit(true)
    , FrameInterruptFlag(false)
    , FrameResetFlag(false)
    , FrameResetCountdown(0)
    , CurrentFrameLength(16667)
    , CyclesToNextSample(0)
    , EffectiveCpuFrequency(CpuFrequency)
    , ExtraCount(0.0f)
    , Fraction(0.0f)
    , MasterVolume(1.0f)
    , PulseOneVolume(1.0f)
    , PulseTwoVolume(1.0f)
    , TriangleVolume(1.0f)
    , NoiseVolume(1.0f)
    , DmcVolume(1.0f)
{
    double whole, frequency;
    frequency = CpuFrequency;
    Fraction = modf(frequency / AudioBackend::SampleRate, &whole) * 0.89;
}

APU::~APU()
{
}

void APU::AttachCPU(CPU* cpu)
{
    Cpu = cpu;
}

void APU::AttachCart(Cart* cart)
{
    this->Cartridge = cart;
}

void APU::SetFrameLength(int32_t length)
{
    int32_t delta = length - CurrentFrameLength;

    // Only update the frame length if the change is
    // greater than 100 microsecods in either direction
    if (delta > 100 || delta < -100)
    {
        double whole, frequency;
        if (length >= 16567 && length <= 16767)
        {
            frequency = CpuFrequency;
            EffectiveCpuFrequency = CpuFrequency;
        }
        else
        {
            frequency = (16667.f / length) * CpuFrequency;
            EffectiveCpuFrequency = static_cast<int32_t>(frequency);
        }

        // Fraction represents the fractional part of the CPU frequency divided by the sample rate
        // It is used to add extra cycles between samples now and then to keep the rate of sample
        // generation close to 44100 samples per second. Multiplying by 0.89 also gives a margin
        // for error so that the emulator doesn't fall behind in generating audio if there are
        // any hiccups in the speed of emulation
        Fraction = modf(frequency / AudioBackend::SampleRate, &whole) * 0.89;
        CurrentFrameLength = length;
    }
}

void APU::Step()
{
    ++Clock;

    Triangle.ClockTimer();
    if (Clock % 2 == 0)
    {
        PulseOne.ClockTimer();
        PulseTwo.ClockTimer();
        Noise.ClockTimer();
        Dmc.ClockTimer();

        // Made need to move this to after the sequence count
        if (FrameResetFlag)
        {
            if (--FrameResetCountdown == 0)
            {
                Clock = 0;
                FrameResetFlag = false;
            }
        }
    }

    if (SequenceMode)
    {
        if (Clock == 7457 || Clock == 22371 || Clock == 14913 || Clock == 37281)
        {
            PulseOne.ClockEnvelope();
            PulseTwo.ClockEnvelope();
            Triangle.ClockLinearCounter();
            Noise.ClockEnvelope();

            if (Clock == 14913 || Clock == 37281)
            {
                PulseOne.ClockLengthCounter();
                PulseOne.ClockSweep();
                PulseTwo.ClockLengthCounter();
                PulseTwo.ClockSweep();
                Triangle.ClockLengthCounter();
                Noise.ClockLengthCounter();
            }
        }

        if (Clock == 37282)
        {
            Clock = 0;
        }
    }
    else
    {
        if (Clock == 7457 || Clock == 22371 || Clock == 14913 || Clock == 29829)
        {
            PulseOne.ClockEnvelope();
            PulseTwo.ClockEnvelope();
            Triangle.ClockLinearCounter();
            Noise.ClockEnvelope();

            if (Clock == 14913 || Clock == 29829)
            {
                PulseOne.ClockLengthCounter();
                PulseOne.ClockSweep();
                PulseTwo.ClockLengthCounter();
                PulseTwo.ClockSweep();
                Triangle.ClockLengthCounter();
                Noise.ClockLengthCounter();
            }
        }

        if (Clock >= 29828 && Clock <= 29830 && !InterruptInhibit)
        {
            FrameInterruptFlag = true;
        }

        if (Clock == 29830)
        {
            Clock = 0;
        }
    }

    GenerateSample();
}

void APU::GenerateSample()
{
    if (CyclesToNextSample == 0)
    {
        if (ExtraCount >= 1.0f)
        {
            double whole;
            CyclesToNextSample = (EffectiveCpuFrequency / AudioBackend::SampleRate) + 1;
            ExtraCount = modf(ExtraCount, &whole);
        }
        else
        {
            CyclesToNextSample = EffectiveCpuFrequency / AudioBackend::SampleRate;
        }

        // Decided to use the exact calculation rather than the lookup tables for this
        float pulse1 = PulseOne * PulseOneVolume;
        float pulse2 = PulseTwo * PulseTwoVolume;
        float triangle = Triangle * TriangleVolume;
        float noise = Noise * NoiseVolume;
        float dmc = Dmc * DmcVolume;

        float PulseOut = 0.0f;
        float TndOut = 0.0f;

        if (pulse1 != 0.0f || pulse2 != 0.0f)
        {
            PulseOut = 95.88f / ((8128.0f / (pulse1 + pulse2)) + 100.0f);
        }

        if (triangle != 0.0f || noise != 0.0f || dmc != 0.0f)
        {
            TndOut = 159.79f / ((1.0f / ((triangle / 8227.0f) + (noise / 12241.0f) + (dmc / 22638.0f))) + 100.0f);
        }

        Backend << (PulseOut + TndOut) * MasterVolume;

        ExtraCount += Fraction;
    }

    --CyclesToNextSample;
}

bool APU::CheckIRQ()
{
    return FrameInterruptFlag || Dmc.CheckIRQ();
}

void APU::WritePulseOneRegister(uint8_t reg, uint8_t value)
{
    PulseOne.WriteRegister(reg, value);
}

void APU::WritePulseTwoRegister(uint8_t reg, uint8_t value)
{
    PulseTwo.WriteRegister(reg, value);
}

void APU::WriteTriangleRegister(uint8_t reg, uint8_t value)
{
    Triangle.WriteRegister(reg, value);
}

void APU::WriteNoiseRegister(uint8_t reg, uint8_t value)
{
    Noise.WriteRegister(reg, value);
}

void APU::WriteDmcRegister(uint8_t reg, uint8_t value)
{
    Dmc.WriteRegister(reg, value);
}

void APU::WriteAPUStatus(uint8_t value)
{
    PulseOne.SetEnabled(!!(value & 0x1));
    PulseTwo.SetEnabled(!!(value & 0x2));
    Triangle.SetEnabled(!!(value & 0x4));
    Noise.SetEnabled(!!(value & 0x8));
    Dmc.SetEnabled(!!(value & 0x10));

    Dmc.ClearInterrupt();
}

uint8_t APU::ReadAPUStatus()
{
    uint8_t value = 0;
    value |= Dmc.CheckIRQ() << 7;
    value |= FrameInterruptFlag << 6;
    value |= (Dmc.GetSampleBytesRemaining() != 0) << 4;
    value |= (Noise.GetLengthCounter() != 0) << 3;
    value |= (Triangle.GetLengthCounter() != 0) << 2;
    value |= (PulseTwo.GetLengthCounter() != 0) << 1;
    value |= (PulseOne.GetLengthCounter() != 0) << 0;

    // TODO: Add check for case where flag was set this cycle
    FrameInterruptFlag = false;

    return value;
}

void APU::WriteAPUFrameCounter(uint8_t value)
{
    SequenceMode = !!(value & 0x80);
    InterruptInhibit = !!(value & 0x40);

    if (InterruptInhibit)
    {
        FrameInterruptFlag = false;
    }

    if (SequenceMode)
    {
        PulseOne.ClockEnvelope();
        PulseTwo.ClockEnvelope();
        Triangle.ClockLinearCounter();
        Noise.ClockEnvelope();

        PulseOne.ClockLengthCounter();
        PulseOne.ClockSweep();
        PulseTwo.ClockLengthCounter();
        PulseTwo.ClockSweep();
        Triangle.ClockLengthCounter();
        Noise.ClockLengthCounter();
    }

    FrameResetFlag = true;
    FrameResetCountdown = 2;
}

void APU::SetAudioEnabled(bool enabled)
{
    Backend.SetEnabled(enabled);
}

void APU::SetMasterVolume(float volume)
{
    MasterVolume = clamp(volume, 0.0f, 1.0f);
}

float APU::GetMasterVolume()
{
    return MasterVolume;
}

void APU::SetFiltersEnabled(bool enabled)
{
    Backend.SetFiltersEnabled(enabled);
}

void APU::SetPulseOneVolume(float volume)
{
    PulseOneVolume = clamp(volume, 0.0f, 1.0f);
}

float APU::GetPulseOneVolume()
{
    return PulseOneVolume;
}

void APU::SetPulseTwoVolume(float volume)
{
    PulseTwoVolume = clamp(volume, 0.0f, 1.0f);
}

float APU::GetPulseTwoVolume()
{
    return PulseTwoVolume;
}

void APU::SetTriangleVolume(float volume)
{
    TriangleVolume = clamp(volume, 0.0f, 1.0f);
}

float APU::GetTriangleVolume()
{
    return TriangleVolume;
}

void APU::SetNoiseVolume(float volume)
{
    NoiseVolume = clamp(volume, 0.0f, 1.0f);
}

float APU::GetNoiseVolume()
{
    return NoiseVolume;
}

void APU::SetDmcVolume(float volume)
{
    DmcVolume = clamp(volume, 0.0f, 1.0f);
}

float APU::GetDmcVolume()
{
    return DmcVolume;
}
