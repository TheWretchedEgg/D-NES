#include <exception>
#include <cstring>
#include <cmath>

#include "apu.h"
#include "cpu.h"
#include "nes_exception.h"
#include "video/video_backend.h"
#include "audio/audio_backend.h"

namespace
{

template<typename T>
T clamp(T val, T low, T hi)
{
    return (val < low) ? low : ((val > hi) ? hi : val);
}

};

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
        throw NesException("APU::PulseUnit", "Tried to write to non-existant register " + std::to_string(reg));
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

uint8_t APU::PulseUnit::GetLevel()
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

void APU::PulseUnit::SaveState(StateSave::Ptr& state)
{
    state->StoreValue(Timer);
    state->StoreValue(TimerPeriod);
    state->StoreValue(SequenceCount);
    state->StoreValue(LengthCounter);
    state->StoreValue(DutyCycle);
    state->StoreValue(EnvelopeDividerVolume);
    state->StoreValue(EnvelopeDividerCounter);
    state->StoreValue(EnvelopeCounter);
    state->StoreValue(SweepShiftCount);
    state->StoreValue(SweepDivider);
    state->StoreValue(SweepDividerCounter);

    state->StorePackedValues(
        LengthHaltEnvelopeLoopFlag,
        ConstantVolumeFlag,
        SweepEnableFlag,
        SweepReloadFlag,
        SweepNegateFlag,
        EnvelopeStartFlag,
        EnabledFlag
    );
}

void APU::PulseUnit::LoadState(const StateSave::Ptr& state)
{
    state->ExtractValue(Timer);
    state->ExtractValue(TimerPeriod);
    state->ExtractValue(SequenceCount);
    state->ExtractValue(LengthCounter);
    state->ExtractValue(DutyCycle);
    state->ExtractValue(EnvelopeDividerVolume);
    state->ExtractValue(EnvelopeDividerCounter);
    state->ExtractValue(EnvelopeCounter);
    state->ExtractValue(SweepShiftCount);
    state->ExtractValue(SweepDivider);
    state->ExtractValue(SweepDividerCounter);

    state->ExtractPackedValues(
        LengthHaltEnvelopeLoopFlag,
        ConstantVolumeFlag,
        SweepEnableFlag,
        SweepReloadFlag,
        SweepNegateFlag,
        EnvelopeStartFlag,
        EnabledFlag
    );
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
        throw NesException("APU::TriangleUnit",  "Tried to write to non-existant register " + std::to_string(reg));
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

uint8_t APU::TriangleUnit::GetLevel()
{
	return Sequence[SequenceCount];
}

void APU::TriangleUnit::SaveState(StateSave::Ptr& state)
{
    state->StoreValue(Timer);
    state->StoreValue(TimerPeriod);
    state->StoreValue(SequenceCount);
    state->StoreValue(LinearCounter);
    state->StoreValue(LinearCounterPeriod);
    state->StoreValue(LengthCounter);
    
    state->StorePackedValues(
        LengthHaltControlFlag,
        LinearCounterReloadFlag,
        EnabledFlag
    );
}

void APU::TriangleUnit::LoadState(const StateSave::Ptr& state)
{
    state->ExtractValue(Timer);
    state->ExtractValue(TimerPeriod);
    state->ExtractValue(SequenceCount);
    state->ExtractValue(LinearCounter);
    state->ExtractValue(LinearCounterPeriod);
    state->ExtractValue(LengthCounter);
    
    state->ExtractPackedValues(
        LengthHaltControlFlag,
        LinearCounterReloadFlag,
        EnabledFlag
    );
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
        throw NesException("APU::NoiseUnit", "Tried to write to non-existant register " + std::to_string(reg));
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

uint8_t APU::NoiseUnit::GetLevel()
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

void APU::NoiseUnit::SaveState(StateSave::Ptr& state)
{
    state->StoreValue(Timer);
    state->StoreValue(TimerPeriodIndex);
    state->StoreValue(LinearFeedbackShiftRegister);
    state->StoreValue(LengthCounter);
    state->StoreValue(EnvelopeDividerVolume);
    state->StoreValue(EnvelopeDividerCounter);
    state->StoreValue(EnvelopeCounter);

    state->StorePackedValues(
        LengthHaltEnvelopeLoopFlag,
        ConstantVolumeFlag,
        EnvelopeStartFlag,
        ModeFlag,
        EnabledFlag
    );
}

void APU::NoiseUnit::LoadState(const StateSave::Ptr& state)
{
    state->ExtractValue(Timer);
    state->ExtractValue(TimerPeriodIndex);
    state->ExtractValue(LinearFeedbackShiftRegister);
    state->ExtractValue(LengthCounter);
    state->ExtractValue(EnvelopeDividerVolume);
    state->ExtractValue(EnvelopeDividerCounter);
    state->ExtractValue(EnvelopeCounter);

    state->ExtractPackedValues(
        LengthHaltEnvelopeLoopFlag,
        ConstantVolumeFlag,
        EnvelopeStartFlag,
        ModeFlag,
        EnabledFlag
    );
}

//**********************************************************************
// APU DMC Unit
//**********************************************************************

const uint16_t APU::DmcUnit::TimerPeriods[16] =
{
    214, 190, 170, 160, 143, 127, 113, 107, 95, 80, 71, 64, 53, 42, 36, 27
};

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
    , InterruptFlag(false)
    , InterruptEnabledFlag(false)
    , SampleLoopFlag(false)
    , SampleBufferEmptyFlag(true)
    , DmaRequest(false)
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

        if (SampleBufferEmptyFlag)
        {
            DmaRequest = true;
        }	
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
        throw NesException("APU::DmcUnit", "Tried to write to non-existant register " + std::to_string(reg));
    }
}

bool APU::DmcUnit::CheckIRQ()
{
    return InterruptFlag;
}

void APU::DmcUnit::ClockTimer()
{
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

            if (SampleBytesRemaining != 0)
            {
                DmaRequest = true;
            }
        }
    }

    --Timer;
}

bool APU::DmcUnit::CheckDmaRequest()
{
    bool request = DmaRequest;
    DmaRequest = false;

    return request;
}

uint16_t APU::DmcUnit::GetDmaAddress()
{
    return CurrentAddress;
}

void APU::DmcUnit::WriteDmaByte(uint8_t byte)
{
    SampleBuffer = byte;
    SampleBufferEmptyFlag = false;

    if (CurrentAddress == 0xFFFF)
    {
        CurrentAddress = 0x8000;
    }
    else
    {
        ++CurrentAddress;
    }

    --SampleBytesRemaining;

    if (SampleBytesRemaining == 0)
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
}

uint8_t APU::DmcUnit::GetLevel()
{
	return OutputLevel;
}

void APU::DmcUnit::SaveState(StateSave::Ptr& state)
{
    state->StoreValue(Timer);
    state->StoreValue(TimerPeriodIndex);
    state->StoreValue(OutputLevel);
    state->StoreValue(SampleAddress);
    state->StoreValue(CurrentAddress);
    state->StoreValue(SampleLength);
    state->StoreValue(SampleBytesRemaining);
    state->StoreValue(SampleBuffer);
    state->StoreValue(SampleShiftRegister);
    state->StoreValue(SampleBitsRemaining);

    state->StorePackedValues(
        InterruptFlag,
        InterruptEnabledFlag,
        SampleLoopFlag,
        SampleBufferEmptyFlag,
        DmaRequest,
        SilenceFlag
    );
}

void APU::DmcUnit::LoadState(const StateSave::Ptr& state)
{
    state->ExtractValue(Timer);
    state->ExtractValue(TimerPeriodIndex);
    state->ExtractValue(OutputLevel);
    state->ExtractValue(SampleAddress);
    state->ExtractValue(CurrentAddress);
    state->ExtractValue(SampleLength);
    state->ExtractValue(SampleBytesRemaining);
    state->ExtractValue(SampleBuffer);
    state->ExtractValue(SampleShiftRegister);
    state->ExtractValue(SampleBitsRemaining);

    state->ExtractPackedValues(
        InterruptFlag,
        InterruptEnabledFlag,
        SampleLoopFlag,
        SampleBufferEmptyFlag,
        DmaRequest,
        SilenceFlag
    );
}

//**********************************************************************
// APU Mixer
//**********************************************************************

APU::MixerUnit::MixerUnit(APU& apu)
	: Apu(apu)
	, TurboModeEnabled(false)
	, AudioEnabled(true)
	, PulseOneAccumulator(0)
	, PulseTwoAccumulator(0)
	, TriangleAccumulator(0)
	, NoiseAccumulator(0)
	, DmcAccumulator(0)
	, CyclesPerSample(0)
	, CycleRemainder(0)
	, CycleCount(0)
	, ExtraCycle(0)
	, ExtraCount(0)
	, TargetFramePeriod(0)
	, TargetCpuFrequency(0)
	, SamplesPerFrame(0)
	, FrameSampleCount(0)
{
	SetTargetFrameRate(60);
}

void APU::MixerUnit::Clock()
{
	PulseOneAccumulator += Apu.PulseOne.GetLevel();
	PulseTwoAccumulator += Apu.PulseTwo.GetLevel();
	TriangleAccumulator += Apu.Triangle.GetLevel();
	NoiseAccumulator += Apu.Noise.GetLevel();
	DmcAccumulator += Apu.Dmc.GetLevel();

	CycleCount++;

	if (CycleCount == CyclesPerSample)
	{
		ExtraCount += CycleRemainder;

		if (ExtraCount > Apu.AudioOut->GetSampleRate())
		{
			ExtraCount = ExtraCount - Apu.AudioOut->GetSampleRate();
		}
		else
		{
			if (AudioEnabled && !TurboModeEnabled)
			{
				GenerateSample();
			}

			CycleCount = 0;

			FrameSampleCount++;
			if (FrameSampleCount >= SamplesPerFrame)
			{
				UpdateMode();
				FrameSampleCount = 0;
			}
		}
	}
	else if (CycleCount > CyclesPerSample)
	{
		if (AudioEnabled && !TurboModeEnabled)
		{
			GenerateSample();
		}

		CycleCount = 0;

		FrameSampleCount++;
		if (FrameSampleCount >= SamplesPerFrame)
		{
			UpdateMode();
			FrameSampleCount = 0;
		}
	}
}

void APU::MixerUnit::GenerateSample()
{
	float pulseOneLevel = static_cast<float>(PulseOneAccumulator) / CycleCount;
	float pulseTwoLevel = static_cast<float>(PulseTwoAccumulator) / CycleCount;
	float triangleLevel = static_cast<float>(TriangleAccumulator) / CycleCount;
	float noiseLevel = static_cast<float>(NoiseAccumulator) / CycleCount;
	float dmcLevel = static_cast<float>(DmcAccumulator) / CycleCount;

	pulseOneLevel *= Apu.PulseOneVolume;
	pulseTwoLevel *= Apu.PulseTwoVolume;
	triangleLevel *= Apu.TriangleVolume;
	noiseLevel *= Apu.NoiseVolume;
	dmcLevel *= Apu.DmcVolume;

	PulseOneAccumulator = 0;
	PulseTwoAccumulator = 0;
	TriangleAccumulator = 0;
	NoiseAccumulator = 0;
	DmcAccumulator = 0;

	float pulse = 0.0f;
	float tndOut = 0.0f;

	if (pulseOneLevel != 0.0f || pulseTwoLevel != 0.0f)
	{
		pulse = 95.88f / ((8128.0f / (pulseOneLevel + pulseTwoLevel)) + 100.0f);
	}

	if (triangleLevel != 0.0f || noiseLevel != 0.0f || dmcLevel != 0.0f)
	{
		tndOut = 159.79f / ((1.0f / ((triangleLevel / 8227.0f) + (noiseLevel / 12241.0f) + (dmcLevel / 22638.0f))) + 100.0f);
	}

	// Send final sample to the backend
	float finalSample = (((pulse + tndOut) * Apu.MasterVolume) * 2.0f) - 1.0f;
	Apu.AudioOut->SubmitSample(finalSample);
}

void APU::MixerUnit::Reset()
{
	CycleCount = 0;
	ExtraCount = 0;
	FrameSampleCount = 0;
	PulseOneAccumulator = 0;
	PulseTwoAccumulator = 0;
	TriangleAccumulator = 0;
	NoiseAccumulator = 0;
	DmcAccumulator = 0;

    Apu.AudioOut->Reset();
}

void APU::MixerUnit::SetTargetFrameRate(uint32_t rate)
{
	// Minimum framerate is 20 fps
	rate = clamp(rate, 20U, 240U);
	TargetFramePeriod = 1000000 / rate;

	// Special case for 60 fps to avoid any floating point weirdness
	if (rate == 60)
	{
		TargetCpuFrequency = CPU::NTSC_FREQUENCY;
		CyclesPerSample = CPU::NTSC_FREQUENCY / Apu.AudioOut->GetSampleRate();
		CycleRemainder = CPU::NTSC_FREQUENCY % Apu.AudioOut->GetSampleRate();
		SamplesPerFrame = Apu.AudioOut->GetSampleRate() / rate;
	}
	else
	{
		TargetCpuFrequency = static_cast<uint32_t>(static_cast<double>(CPU::NTSC_FREQUENCY) * (static_cast<double>(rate) / 60.0));
		CyclesPerSample = TargetCpuFrequency / Apu.AudioOut->GetSampleRate();
		CycleRemainder = TargetCpuFrequency % Apu.AudioOut->GetSampleRate();
		SamplesPerFrame = Apu.AudioOut->GetSampleRate() / rate;
	}

	Reset();
}

void APU::MixerUnit::UpdateMode()
{
	if (Apu.TurboModeEnabled != TurboModeEnabled)
	{
		TurboModeEnabled = Apu.TurboModeEnabled;

		if (!TurboModeEnabled)
		{
			Reset();
		}
	}

	if (Apu.AudioEnabled != AudioEnabled)
	{
		AudioEnabled = Apu.AudioEnabled;

		if (AudioEnabled && !TurboModeEnabled)
		{
			Reset();
		}
	}
}

//**********************************************************************
// APU Main Unit
//**********************************************************************

APU::APU(AudioBackend* aout)
	: Cpu(nullptr)
	, Cartridge(nullptr)
	, AudioOut(aout)
	, PulseOne(true)
	, PulseTwo(false)
	, Dmc(*this)
	, Mixer(*this)
    , Clock(6)
    , SequenceCount(0)
    , LongSequenceFlag(false)
    , InterruptInhibit(false)
    , FrameInterruptFlag(false)
    , FrameResetFlag(false)
    , FrameResetCountdown(0)
    , TurboModeEnabled(false)
	, AudioEnabled(true)
    , MasterVolume(1.0f)
    , PulseOneVolume(1.0f)
    , PulseTwoVolume(1.0f)
    , TriangleVolume(1.0f)
    , NoiseVolume(1.0f)
    , DmcVolume(1.0f)
{
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

void APU::SetTargetFrameRate(uint32_t rate)
{
	Mixer.SetTargetFrameRate(rate);
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

    if (LongSequenceFlag)
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

	Mixer.Clock();
}

bool APU::CheckIRQ()
{
    return FrameInterruptFlag || Dmc.CheckIRQ();
}

bool APU::CheckDmaRequest()
{
    return Dmc.CheckDmaRequest();
}

uint16_t APU::GetDmaAddress()
{
    return Dmc.GetDmaAddress();
}

void APU::WriteDmaByte(uint8_t byte)
{
    Dmc.WriteDmaByte(byte);
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
    LongSequenceFlag = !!(value & 0x80);
    InterruptInhibit = !!(value & 0x40);

    if (InterruptInhibit)
    {
        FrameInterruptFlag = false;
    }

    if (LongSequenceFlag)
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

void APU::SetTurboModeEnabled(bool enabled)
{
    TurboModeEnabled = enabled;
}

void APU::SetAudioEnabled(bool enabled)
{
	AudioEnabled = enabled;
}

void APU::SetMasterVolume(float volume)
{
    MasterVolume = clamp(volume, 0.0f, 1.0f);
}

float APU::GetMasterVolume()
{
    return MasterVolume;
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

StateSave::Ptr APU::SaveState()
{
    StateSave::Ptr subState;
    StateSave::Ptr state = StateSave::New();

    PulseOne.SaveState(state);
    PulseTwo.SaveState(state);
    Triangle.SaveState(state);
    Noise.SaveState(state);
    Dmc.SaveState(state);

    state->StoreValue(Clock);
    state->StoreValue(SequenceCount);
    state->StoreValue(FrameResetCountdown);

    state->StorePackedValues(
        LongSequenceFlag,
        InterruptInhibit,
        FrameInterruptFlag,
        FrameResetFlag
    );

    return state;
}

void APU::LoadState(const StateSave::Ptr& state)
{
    PulseOne.LoadState(state);
    PulseTwo.LoadState(state);
    Triangle.LoadState(state);
    Noise.LoadState(state);
    Dmc.LoadState(state);

    state->ExtractValue(Clock);
    state->ExtractValue(SequenceCount);
    state->ExtractValue(FrameResetCountdown);

    state->ExtractPackedValues(
        LongSequenceFlag,
        InterruptInhibit,
        FrameInterruptFlag,
        FrameResetFlag
    );
}
