/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** 0CC-FamiTracker is (C) 2014-2018 HertzDevil
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

#include "APU/2A03.h"
#include "Common.h"
#include <algorithm>
#include "APU/Mixer.h"
#include "ft0cc/doc/dpcm_sample.hpp"		// // //
#include "RegisterState.h"		// // //

// // // 2A03 sound chip class

C2A03::C2A03(CMixer &Mixer, std::uint8_t nInstance) :
	CSoundChip(Mixer, nInstance),
	m_Square1(Mixer, nInstance, sound_chip_t::APU, value_cast(apu_subindex_t::pulse1)),
	m_Square2(Mixer, nInstance, sound_chip_t::APU, value_cast(apu_subindex_t::pulse2)),
	m_Triangle(Mixer, nInstance),
	m_Noise(Mixer, nInstance),
	m_DPCM(Mixer, nInstance)		// // //
{
	m_pRegisterLogger->AddRegisterRange(0x4000, 0x4017);		// // //
}

sound_chip_t C2A03::GetID() const {		// // //
	return sound_chip_t::APU;
}

void C2A03::Reset()
{
	m_iFrameSequence	= 0;
	m_iFrameMode		= 0;

	m_Square1.Reset();
	m_Square2.Reset();
	m_Triangle.Reset();
	m_Noise.Reset();
	m_DPCM.Reset();
}

void C2A03::Process(uint32_t Time)
{
	RunAPU1(Time);
	RunAPU2(Time);
}

void C2A03::EndFrame()
{
	m_Square1.EndFrame();
	m_Square2.EndFrame();
	m_Triangle.EndFrame();
	m_Noise.EndFrame();
	m_DPCM.EndFrame();
}

void C2A03::Write(uint16_t Address, uint8_t Value)
{
	if (Address < 0x4000U || Address > 0x401FU) return;
	switch (Address) {
	case 0x4015:
		m_Square1.WriteControl(Value);
		m_Square2.WriteControl(Value >> 1);
		m_Triangle.WriteControl(Value >> 2);
		m_Noise.WriteControl(Value >> 3);
		m_DPCM.WriteControl(Value >> 4);
		return;
	case 0x4017:
		m_iFrameSequence = 0;
		if (Value & 0x80) {
			m_iFrameMode = 1;
			Clock_240Hz();
			Clock_120Hz();
			Clock_60Hz();
		}
		else
			m_iFrameMode = 0;
		return;
	}

	switch (Address & 0x1C) {
		case 0x00: m_Square1.Write(Address & 0x03, Value); break;
		case 0x04: m_Square2.Write(Address & 0x03, Value); break;
		case 0x08: m_Triangle.Write(Address & 0x03, Value); break;
		case 0x0C: m_Noise.Write(Address & 0x03, Value); break;
		case 0x10: m_DPCM.Write(Address & 0x03, Value); break;
	}
}

uint8_t C2A03::Read(uint16_t Address, bool &Mapped)
{
	switch (Address) {
	case 0x4015:
	{
		uint8_t RetVal;

		RetVal = m_Square1.ReadControl();
		RetVal |= m_Square2.ReadControl() << 1;
		RetVal |= m_Triangle.ReadControl() << 2;
		RetVal |= m_Noise.ReadControl() << 3;
		RetVal |= m_DPCM.ReadControl() << 4;
		RetVal |= m_DPCM.DidIRQ() << 7;

		Mapped = true;
		return RetVal;
	}
	}
	return 0U;
}

double C2A03::GetFreq(int Channel) const		// // //
{
	switch (Channel) {
	case 0: return m_Square1.GetFrequency();
	case 1: return m_Square2.GetFrequency();
	case 2: return m_Triangle.GetFrequency();
	case 3: return m_Noise.GetFrequency();
	case 4: return m_DPCM.GetFrequency();
	}
	return 0.0;
}

void C2A03::ClockSequence()
{
	if (m_iFrameMode == 0) {
		m_iFrameSequence = (m_iFrameSequence + 1) % 4;
		switch (m_iFrameSequence) {
			case 0: Clock_240Hz(); break;
			case 1: Clock_240Hz(); Clock_120Hz(); break;
			case 2: Clock_240Hz(); break;
			case 3: Clock_240Hz(); Clock_120Hz(); Clock_60Hz(); break;
		}
	}
	else {
		m_iFrameSequence = (m_iFrameSequence + 1) % 5;
		switch (m_iFrameSequence) {
			case 0: Clock_240Hz(); Clock_120Hz(); break;
			case 1: Clock_240Hz(); break;
			case 2: Clock_240Hz(); Clock_120Hz(); break;
			case 3: Clock_240Hz(); break;
			case 4: break;
		}
	}
}

void C2A03::ChangeMachine(machine_t Machine)
{
	switch (Machine) {
	case machine_t::NTSC:		// // //
		m_Square1.CPU_RATE = MASTER_CLOCK_NTSC;		// // //
		m_Square2.CPU_RATE = MASTER_CLOCK_NTSC;
		m_Triangle.CPU_RATE = MASTER_CLOCK_NTSC;
		m_Noise.PERIOD_TABLE = CNoise::NOISE_PERIODS_NTSC;
		m_DPCM.PERIOD_TABLE = CDPCM::DMC_PERIODS_NTSC;
		m_pMixer->SetClockRate(MASTER_CLOCK_NTSC);
		break;
	case machine_t::PAL:
		m_Square1.CPU_RATE = MASTER_CLOCK_PAL;		// // //
		m_Square2.CPU_RATE = MASTER_CLOCK_PAL;
		m_Triangle.CPU_RATE = MASTER_CLOCK_PAL;
		m_Noise.PERIOD_TABLE = CNoise::NOISE_PERIODS_PAL;
		m_DPCM.PERIOD_TABLE = CDPCM::DMC_PERIODS_PAL;
		m_pMixer->SetClockRate(MASTER_CLOCK_PAL);
		break;
	}
}

inline void C2A03::Clock_240Hz()
{
	m_Square1.EnvelopeUpdate();
	m_Square2.EnvelopeUpdate();
	m_Noise.EnvelopeUpdate();
	m_Triangle.LinearCounterUpdate();
}

inline void C2A03::Clock_120Hz()
{
	m_Square1.SweepUpdate(1);
	m_Square2.SweepUpdate(0);

	m_Square1.LengthCounterUpdate();
	m_Square2.LengthCounterUpdate();
	m_Triangle.LengthCounterUpdate();
	m_Noise.LengthCounterUpdate();
}

inline void C2A03::Clock_60Hz()
{
	// IRQ
}

inline void C2A03::RunAPU1(uint32_t Time)
{
	// APU pin 1
	while (Time > 0) {
		uint32_t Period = std::max((uint32_t)std::min(m_Square1.GetPeriod(), m_Square2.GetPeriod()), 7u);
		Period = std::min(Period, Time);
		m_Square1.Process(Period);
		m_Square2.Process(Period);
		Time -= Period;
	}
}

inline void C2A03::RunAPU2(uint32_t Time)
{
	// APU pin 2
	while (Time > 0) {
		uint32_t Period = std::max((uint32_t)std::min(std::min(m_Triangle.GetPeriod(), m_Noise.GetPeriod()), m_DPCM.GetPeriod()), 7u);
		Period = std::min(Period, Time);
		m_Triangle.Process(Period);
		m_Noise.Process(Period);
		m_DPCM.Process(Period);
		Time -= Period;
	}
}

void C2A03::WriteSample(std::shared_ptr<const ft0cc::doc::dpcm_sample> pSample) {		// // //
	// Sample may not be removed when used by the sample memory class!
	preview_sample_ = std::move(pSample);
	m_DPCM.GetSampleMemory().SetMem(*preview_sample_);
}

void C2A03::ClearSample() {		// // //
	m_DPCM.GetSampleMemory().Clear();
}

uint8_t C2A03::GetSamplePos() const
{
	return m_DPCM.GetSamplePos();
}

uint8_t C2A03::GetDeltaCounter() const
{
	return m_DPCM.GetDeltaCounter();
}

bool C2A03::DPCMPlaying() const
{
	return m_DPCM.IsPlaying();
}
