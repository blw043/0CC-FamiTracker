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

#include "PatternCompiler.h"
#include "SeqInstrument.h"		// // //
#include "Instrument2A03.h"		// // //
#include "InstrumentFDS.h"		// // //
#include "TrackerChannel.h"
#include "Compiler.h"
#include "ft0cc/doc/groove.hpp"		// // //
#include "APU/Types.h"		// // //
#include "FamiTrackerModule.h"		// // //
#include "InstrumentManager.h"		// // //
#include "SongData.h"		// // //
#include "NumConv.h"		// // //
#include <algorithm>		// // //
#include "FamiTrackerEnv.h"		// // //
#include "SoundChipService.h"		// // //

/**
 * CPatternCompiler - Compress patterns to strings for the NSF code
 *
 */

/*

 Pattern byte layout:

 00h - 7Fh : Notes, where 00h = rest, 7Fh = Note cut
 80h - DFh : Commands, defined in the command table
 E0h - EFh : Quick instrument switches, E0h = instrument 0, EFh = instrument 15
 F0h - FFh : Volume changes, F0h = volume 0, FFh = volume 15

 Each row entry is ended by a note and the duration of the note,
 if fixed duration is enabled then duration is omitted.

*/

// Optimize note durations when possible (default on)
#define OPTIMIZE_DURATIONS

// Use single-byte instrument commands for instrument 0-15 (default on)
#define PACKED_INST_CHANGE

// Command table
enum command_t {
	CMD_INSTRUMENT,
	CMD_HOLD,		// // // 050B
	CMD_SET_DURATION,
	CMD_RESET_DURATION,
	CMD_EFF_SPEED,
	CMD_EFF_TEMPO,
	CMD_EFF_JUMP,
	CMD_EFF_SKIP,
	CMD_EFF_HALT,
	CMD_EFF_VOLUME,
	CMD_EFF_CLEAR,
	CMD_EFF_PORTAUP,
	CMD_EFF_PORTADOWN,
	CMD_EFF_PORTAMENTO,
	CMD_EFF_ARPEGGIO,
	CMD_EFF_VIBRATO,
	CMD_EFF_TREMOLO,
	CMD_EFF_PITCH,
	CMD_EFF_RESET_PITCH,
	CMD_EFF_DUTY,
	CMD_EFF_DELAY,
	CMD_EFF_SWEEP,
	CMD_EFF_DAC,
	CMD_EFF_OFFSET,
	CMD_EFF_SLIDE_UP,
	CMD_EFF_SLIDE_DOWN,
	CMD_EFF_VOL_SLIDE,
	CMD_EFF_NOTE_CUT,
	CMD_EFF_RETRIGGER,
	CMD_EFF_DPCM_PITCH,
	CMD_EFF_NOTE_RELEASE,		// // //
	CMD_EFF_LINEAR_COUNTER,		// // //
	CMD_EFF_GROOVE,				// // //
	CMD_EFF_DELAYED_VOLUME,		// // //
	CMD_EFF_TRANSPOSE,			// // //

	CMD_EFF_VRC7_PATCH,			// // // 050B
	CMD_EFF_VRC7_PORT,			// // // 050B
	CMD_EFF_VRC7_WRITE,			// // // 050B

	CMD_EFF_FDS_MOD_DEPTH,
	CMD_EFF_FDS_MOD_RATE_HI,
	CMD_EFF_FDS_MOD_RATE_LO,
	CMD_EFF_FDS_VOLUME,			// // //
	CMD_EFF_FDS_MOD_BIAS,		// // //

	CMD_EFF_N163_WAVE_BUFFER,	// // //

	CMD_EFF_S5B_ENV_TYPE,		// // //
	CMD_EFF_S5B_ENV_RATE_HI,	// // //
	CMD_EFF_S5B_ENV_RATE_LO,	// // //
	CMD_EFF_S5B_NOISE,			// // // 050B

	CMD_EFF_VRC7_FIRST = CMD_EFF_VRC7_PATCH,
	CMD_EFF_VRC7_LAST  = CMD_EFF_VRC7_WRITE,
	CMD_EFF_FDS_FIRST  = CMD_EFF_FDS_MOD_DEPTH,
	CMD_EFF_FDS_LAST   = CMD_EFF_FDS_MOD_BIAS,
	CMD_EFF_N163_FIRST = CMD_EFF_N163_WAVE_BUFFER,
	CMD_EFF_N163_LAST  = CMD_EFF_N163_WAVE_BUFFER,
};

const unsigned char CMD_LOOP_POINT = 26;	// Currently unused

CPatternCompiler::CPatternCompiler(const CFamiTrackerModule &ModFile, const std::vector<unsigned> &InstList, const DPCM_List_t *pDPCMList, std::shared_ptr<CCompilerLog> pLogger) :		// // //
	m_iInstrumentList(InstList),
	m_pDPCMList(pDPCMList),
	modfile_(ModFile),
	m_pLogger(std::move(pLogger))
{
}

CPatternCompiler::~CPatternCompiler()
{
}

void CPatternCompiler::CompileData(int Track, int Pattern, stChannelID Channel) {
	const auto *pSong = modfile_.GetSong(Track);		// // //
	if (!pSong)
		return;
	const auto *pInstManager = modfile_.GetInstrumentManager();

	int EffColumns = pSong->GetEffectColumnCount(Channel);

	// Global init
	m_iHash = 0;
	m_iDuration = 0;
	m_iCurrentDefaultDuration = 0xFF;

	m_vData.clear();
	m_vCompressedData.clear();

	// Local init
	unsigned int iPatternLen = pSong->GetPatternLength();
	unsigned char LastInstrument = MAX_INSTRUMENTS + 1;
	unsigned char DPCMInst = 0;

	for (unsigned int i = 0; i < iPatternLen; ++i) {
		stChanNote ChanNote = pSong->GetPattern(Channel, Pattern).GetNoteOn(i);		// // //

		const note_t Note = ChanNote.Note;
		const unsigned char Octave = ChanNote.Octave;
		const unsigned char Instrument = FindInstrument(ChanNote.Instrument);
		const unsigned char Volume = ChanNote.Vol;

		bool Action = false;

		if (ChanNote.Instrument != MAX_INSTRUMENTS && ChanNote.Instrument != HOLD_INSTRUMENT && (is_note(Note) || Note == note_t::echo))		// // //
			if (!IsInstrumentCompatible(Channel.Chip, pInstManager->GetInstrumentType(ChanNote.Instrument)))		// // //
				Print("Error: Missing or incompatible instrument (on row " + conv::from_uint(i) +
					", channel " + std::string {FTEnv.GetSoundChipService()->GetChannelFullName(Channel)} + ", pattern " + conv::from_uint(Pattern) + ")\n");

		// Check for delays, must come first
		for (int j = 0; j < EffColumns; ++j) {
			effect_t Effect = ChanNote.Effects[j].fx;
			unsigned char EffParam = ChanNote.Effects[j].param;
			if (Effect == effect_t::DELAY && EffParam > 0) {
				WriteDuration();
				for (int k = 0; k < EffColumns; ++k) {
					// Clear skip and jump commands on delayed rows
					if (ChanNote.Effects[k].fx == effect_t::SKIP) {
						WriteData(Command(CMD_EFF_SKIP));
						WriteData(ChanNote.Effects[k].param + 1);
						ChanNote.Effects[k] = { };
					}
					else if (ChanNote.Effects[k].fx == effect_t::JUMP) {
						WriteData(Command(CMD_EFF_JUMP));
						WriteData(ChanNote.Effects[k].param + 1);
						ChanNote.Effects[k] = { };
					}
				}
				Action = true;
				WriteData(Command(CMD_EFF_DELAY));
				WriteData(EffParam);
			}
		}

#ifdef OPTIMIZE_DURATIONS

		// Determine length of space between notes
		stSpacingInfo SpaceInfo = ScanNoteLengths(Track, i, Pattern, Channel);		// // //

		if (SpaceInfo.SpaceCount > 2) {
			if (SpaceInfo.SpaceSize != m_iCurrentDefaultDuration && SpaceInfo.SpaceCount != 0xFF) {
				// Enable compressed durations
				WriteDuration();
				m_iCurrentDefaultDuration = SpaceInfo.SpaceSize;
				WriteData(Command(CMD_SET_DURATION));
				WriteData(m_iCurrentDefaultDuration);
			}
		}
		else {
			if (m_iCurrentDefaultDuration != 0xFF && m_iCurrentDefaultDuration != SpaceInfo.SpaceSize) {
				// Disable compressed durations
				WriteDuration();
				m_iCurrentDefaultDuration = 0xFF;
				WriteData(Command(CMD_RESET_DURATION));
			}
		}

#endif /* OPTIMIZE_DURATIONS */
/*
		if (SpaceInfo.SpaceCount > 2 && SpaceInfo.SpaceSize != CurrentDefaultDuration) {
			CurrentDefaultDuration = SpaceInfo.SpaceSize;
			WriteData(CMD_SET_DURATION);
			WriteData(CurrentDefaultDuration);
		}
		else if (SpaceInfo.SpaceCount < 2 && SpaceInfo.SpaceSize == CurrentDefaultDuration) {
		}
		else
*/
		if (Note != note_t::halt && Note != note_t::release) {		// // //
			if (Instrument != LastInstrument && Instrument < MAX_INSTRUMENTS) {
				LastInstrument = Instrument;
				// Write instrument change command
				//if (Channel < InstrChannels) {
				if (!IsDPCM(Channel)) {		// Skip DPCM
					WriteDuration();
#ifdef PACKED_INST_CHANGE
					if (Instrument < 0x10)
						WriteData(0xE0 | Instrument);
					else {
						WriteData(Command(CMD_INSTRUMENT));
						WriteData(Instrument << 1);
					}
#else
					WriteData(Command(CMD_INSTRUMENT));
					WriteData(Instrument << 1);
#endif /* PACKED_INST_CHANGE */
					Action = true;
				}
				else {
					DPCMInst = ChanNote.Instrument;
				}
			}
			if (Instrument == HOLD_INSTRUMENT && !IsDPCM(Channel)) {		// // // 050B
				WriteDuration();
				WriteData(Command(CMD_HOLD));
				Action = true;
			}
#ifdef OPTIMIZE_DURATIONS
			if (Instrument == LastInstrument && Instrument < MAX_INSTRUMENTS) {		// // //
				if (!IsDPCM(Channel)) {
					WriteDuration();
					Action = true;
				}
			}
#endif /* OPTIMIZE_DURATIONS */
		}

		const auto NESNote = static_cast<unsigned char>([&] {
			switch (Note) {
			case note_t::none:    return 0xFF;
			case note_t::halt:    return 0x7F - 1;
			case note_t::release: return 0x7F - 2;
			case note_t::echo:    return 0x6F + Octave;
			default:
				int NESNote = ChanNote.ToMidiNote();		// // //
				if (IsDPCM(Channel)) {
					// 2A03 DPCM
					int LookUp = FindSample(DPCMInst, NESNote);
					if (LookUp <= 0) { // Invalid sample, skip
						Print("Error: Missing DPCM sample (on row " + conv::from_uint(i) +
							", channel " + std::string {FTEnv.GetSoundChipService()->GetChannelFullName(Channel)} + ", pattern " + conv::from_uint(Pattern) + ")\n");
						return 0xFF;
					}

					// TODO: Print errors if incompatible or non-existing instrument is found
					if (auto pInstrument = std::dynamic_pointer_cast<CInstrument2A03>(pInstManager->GetInstrument(DPCMInst)))
						m_bDSamplesAccessed[pInstrument->GetSampleIndex(NESNote)] = true;
					return LookUp - 1;
				}
				else {
					if (IsAPUNoise(Channel))
						return (NESNote & 0x0F) | 0x10;
					return NESNote; // All other channels
				}
			}
		}());

		for (int j = 0; j < EffColumns; ++j) {
			effect_t Effect = ChanNote.Effects[j].fx;
			unsigned char EffParam = ChanNote.Effects[j].param;

			if (Effect != effect_t::none) {
				WriteDuration();
				Action = true;
			}

			switch (Effect) {
				case effect_t::SPEED:
					if (EffParam >= modfile_.GetSpeedSplitPoint() && pSong->GetSongTempo())		// // //
						WriteData(Command(CMD_EFF_TEMPO));
					else
						WriteData(Command(CMD_EFF_SPEED));
					WriteData(EffParam ? EffParam : 1); // NSF halts if 0 is exported
					break;
				case effect_t::JUMP:
					WriteData(Command(CMD_EFF_JUMP));
					WriteData(EffParam + 1);
					break;
				case effect_t::SKIP:
					WriteData(Command(CMD_EFF_SKIP));
					WriteData(EffParam + 1);
					break;
				case effect_t::HALT:
					WriteData(Command(CMD_EFF_HALT));
					WriteData(EffParam);
					break;
				case effect_t::VOLUME:		// // //
					if ((Channel.Chip == sound_chip_t::APU && !IsDPCM(Channel)) || Channel.Chip == sound_chip_t::MMC5) {
						WriteData(Command(CMD_EFF_VOLUME));
						if ((EffParam <= 0x1F) || (EffParam >= 0xE0 && EffParam <= 0xE3))
							WriteData(EffParam & 0x9F);
					}
					break;
				case effect_t::PORTAMENTO:
					if (!IsDPCM(Channel)) {
						if (EffParam == 0)
							WriteData(Command(CMD_EFF_CLEAR));
						else {
							WriteData(Command(CMD_EFF_PORTAMENTO));
							WriteData(EffParam);
						}
					}
					break;
				case effect_t::PORTA_UP:
					if (!IsDPCM(Channel)) {
						if (EffParam == 0)
							WriteData(Command(CMD_EFF_CLEAR));
						else {
							switch (Channel.Chip) {		// // //
							case sound_chip_t::APU: case sound_chip_t::VRC6: case sound_chip_t::MMC5: case sound_chip_t::S5B:
								if (!modfile_.GetLinearPitch()) {
									WriteData(Command(CMD_EFF_PORTAUP));
									break;
								}
								[[fallthrough]];
							default:
								WriteData(Command(CMD_EFF_PORTADOWN));
								break;
							}
							WriteData(EffParam);
						}
					}
					break;
				case effect_t::PORTA_DOWN:
					if (!IsDPCM(Channel)) {
						if (EffParam == 0)
							WriteData(Command(CMD_EFF_CLEAR));
						else {
							switch (Channel.Chip) {		// // //
							case sound_chip_t::APU: case sound_chip_t::VRC6: case sound_chip_t::MMC5: case sound_chip_t::S5B:
								if (!modfile_.GetLinearPitch()) {
									WriteData(Command(CMD_EFF_PORTADOWN));
									break;
								}
								[[fallthrough]];
							default:
								WriteData(Command(CMD_EFF_PORTAUP));
								break;
							}
							WriteData(EffParam);
						}
					}
					break;
					/*
				case effect_t::PORTAOFF:
					if (Channel.Chip == sound_chip_t::APU) {
						WriteData(CMD_EFF_PORTAOFF);
						//WriteData(EffParam);
					}
					break;*/
				case effect_t::SWEEPUP:
					if (IsAPUPulse(Channel)) {
						WriteData(Command(CMD_EFF_SWEEP));
						WriteData(0x88 | (EffParam & 0x77));	// Calculate sweep
					}
					break;
				case effect_t::SWEEPDOWN:
					if (IsAPUPulse(Channel)) {
						WriteData(Command(CMD_EFF_SWEEP));
						WriteData(0x80 | (EffParam & 0x77));	// Calculate sweep
					}
					break;
				case effect_t::ARPEGGIO:
					if (!IsDPCM(Channel)) {
						if (EffParam == 0)
							WriteData(Command(CMD_EFF_CLEAR));
						else {
							WriteData(Command(CMD_EFF_ARPEGGIO));
							WriteData(EffParam);
						}
					}
					break;
				case effect_t::VIBRATO:
					if (!IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_VIBRATO));
						//WriteData(EffParam);
						WriteData((EffParam & 0xF) << 4 | (EffParam >> 4));
					}
					break;
				case effect_t::TREMOLO:
					if (!IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_TREMOLO));
//						WriteData(EffParam & 0xF7);
						WriteData((EffParam & 0xF) << 4 | (EffParam >> 4));
					}
					break;
				case effect_t::PITCH:
					if (!IsDPCM(Channel)) {
						if (EffParam == 0x80)
							WriteData(Command(CMD_EFF_RESET_PITCH));
						else {
							switch (Channel.Chip) {
							case sound_chip_t::APU: case sound_chip_t::VRC6: case sound_chip_t::MMC5: case sound_chip_t::S5B:		// // //
								if (!modfile_.GetLinearPitch())
									break;
								[[fallthrough]];
							default:
								EffParam = (char)(256 - (int)EffParam);
								if (EffParam == 0)
									EffParam = 0xFF;
								break;
							}
							WriteData(Command(CMD_EFF_PITCH));
							WriteData(EffParam);
						}
					}
					break;
				case effect_t::DAC:
					if (IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_DAC));
						WriteData(EffParam & 0x7F);
					}
					break;
				case effect_t::DUTY_CYCLE:
					if (Channel.Chip == sound_chip_t::VRC7) {		// // // 050B
						WriteData(Command(CMD_EFF_VRC7_PATCH));
						WriteData(EffParam << 4);
					}
					else if (Channel.Chip == sound_chip_t::S5B) {
						WriteData(Command(CMD_EFF_DUTY));
						WriteData((EffParam << 6) | ((EffParam & 0x04) << 3));
					}
					else if (!IsAPUTriangle(Channel) && !IsDPCM(Channel)) {	// Not triangle and dpcm
						WriteData(Command(CMD_EFF_DUTY));
						WriteData(EffParam);
					}
					break;
				case effect_t::SAMPLE_OFFSET:
					if (IsDPCM(Channel)) {	// DPCM
						WriteData(Command(CMD_EFF_OFFSET));
						WriteData(EffParam);
					}
					break;
				case effect_t::SLIDE_UP:
					if (!IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_SLIDE_UP));
						WriteData(EffParam);
					}
					break;
				case effect_t::SLIDE_DOWN:
					if (!IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_SLIDE_DOWN));
						WriteData(EffParam);
					}
					break;
				case effect_t::VOLUME_SLIDE:
					if (!IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_VOL_SLIDE));
						WriteData(EffParam);
					}
					break;
				case effect_t::NOTE_CUT:
					if (EffParam >= 0x80 && IsAPUTriangle(Channel)) {		// // //
						WriteData(Command(CMD_EFF_LINEAR_COUNTER));
						WriteData(EffParam - 0x80);
					}
					else if (EffParam < 0x80) {
						WriteData(Command(CMD_EFF_NOTE_CUT));
						WriteData(EffParam);
					}
					break;
				case effect_t::RETRIGGER:
					if (IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_RETRIGGER));
						WriteData(EffParam + 1);
					}
					break;
				case effect_t::DPCM_PITCH:
					if (IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_DPCM_PITCH));
						WriteData(EffParam);
					}
					break;
				case effect_t::NOTE_RELEASE:		// // //
					if (EffParam < 0x80) {
						WriteData(Command(CMD_EFF_NOTE_RELEASE));
						WriteData(EffParam);
					}
					break;
				case effect_t::GROOVE:		// // //
					if (EffParam < MAX_GROOVE) {
						WriteData(Command(CMD_EFF_GROOVE));

						int Pos = 1;
						for (int k = 0; k < EffParam; ++k)
							if (const auto pGroove = modfile_.GetGroove(k))
								Pos += pGroove->compiled_size(); // TODO: use groove manager instead
						WriteData(Pos);
					}
					break;
				case effect_t::DELAYED_VOLUME:		// // //
					if (!IsDPCM(Channel) && (EffParam >> 4) && (EffParam & 0x0F)) {
						WriteData(Command(CMD_EFF_DELAYED_VOLUME));
						WriteData(EffParam);
					}
					break;
				case effect_t::TRANSPOSE:			// // //
					if (!IsDPCM(Channel)) {
						WriteData(Command(CMD_EFF_TRANSPOSE));
						WriteData(EffParam);
					}
					break;
				// // // VRC7
				case effect_t::VRC7_PORT:
					if (Channel.Chip == sound_chip_t::VRC7) {
						WriteData(Command(CMD_EFF_VRC7_PORT));
						WriteData(EffParam & 0x07);
					}
					break;
				case effect_t::VRC7_WRITE:
					if (Channel.Chip == sound_chip_t::VRC7) {
						WriteData(Command(CMD_EFF_VRC7_WRITE));
						WriteData(EffParam);
					}
					break;
				// FDS
				case effect_t::FDS_MOD_DEPTH:
					if (Channel.Chip == sound_chip_t::FDS) {
						WriteData(Command(CMD_EFF_FDS_MOD_DEPTH));
						WriteData(EffParam);
					}
					break;
				case effect_t::FDS_MOD_SPEED_HI:
					if (Channel.Chip == sound_chip_t::FDS) {
						WriteData(Command(CMD_EFF_FDS_MOD_RATE_HI));
						WriteData(EffParam);		// // //
					}
					break;
				case effect_t::FDS_MOD_SPEED_LO:
					if (Channel.Chip == sound_chip_t::FDS) {
						WriteData(Command(CMD_EFF_FDS_MOD_RATE_LO));
						WriteData(EffParam);
					}
					break;
				case effect_t::FDS_MOD_BIAS:		// // //
					if (Channel.Chip == sound_chip_t::FDS) {
						WriteData(Command(CMD_EFF_FDS_MOD_BIAS));
						WriteData(EffParam);
					}
					break;
				case effect_t::FDS_VOLUME:		// // //
					if (Channel.Chip == sound_chip_t::FDS) {
						WriteData(Command(CMD_EFF_FDS_VOLUME));
						WriteData(EffParam == 0xE0 ? 0x80 : (EffParam ^ 0x40));
					}
					break;
				// // // Sunsoft 5B
				case effect_t::SUNSOFT_ENV_TYPE:
					if (Channel.Chip == sound_chip_t::S5B) {
						WriteData(Command(CMD_EFF_S5B_ENV_TYPE));
						WriteData(EffParam);
					}
					break;
				case effect_t::SUNSOFT_ENV_HI:
					if (Channel.Chip == sound_chip_t::S5B) {
						WriteData(Command(CMD_EFF_S5B_ENV_RATE_HI));
						WriteData(EffParam);
					}
					break;
				case effect_t::SUNSOFT_ENV_LO:
					if (Channel.Chip == sound_chip_t::S5B) {
						WriteData(Command(CMD_EFF_S5B_ENV_RATE_LO));
						WriteData(EffParam);
					}
					break;
				case effect_t::SUNSOFT_NOISE:		// // // 050B
					if (Channel.Chip == sound_chip_t::S5B) {
						WriteData(Command(CMD_EFF_S5B_NOISE));
						WriteData(EffParam & 0x1F);
					}
					break;
				// // // N163
				case effect_t::N163_WAVE_BUFFER:
					if (Channel.Chip == sound_chip_t::N163 && EffParam <= 0x7F) {
						WriteData(Command(CMD_EFF_N163_WAVE_BUFFER));
						WriteData(EffParam == 0x7F ? 0x80 : EffParam);
					}
					break;
			}
		}

		// Volume command
		if (Volume < 0x10) {
			WriteDuration();
			WriteData(0xF0 | Volume);
			Action = true;			// Terminate command
		}

		if (NESNote == 0xFF) {
			if (Action) {
				// A instrument/effect command was issued but no new note, write rest command
				WriteData(0);
			}
			AccumulateDuration();
		}
		else {
			// Write note command
			WriteDuration();
			WriteData(NESNote + 1);
			AccumulateDuration();
		}
	}

	WriteDuration();

//	OptimizeString();
}

unsigned char CPatternCompiler::Command(int cmd) const {
	CSoundChipSet Chip = modfile_.GetSoundChipSet();		// // //

	if (!Chip.IsMultiChip()) {		// // // truncate values if some chips do not exist
		if (!Chip.ContainsChip(sound_chip_t::N163) && cmd > CMD_EFF_N163_LAST)
			cmd -= CMD_EFF_N163_LAST - CMD_EFF_N163_FIRST + 1;
		// MMC5
		if (!Chip.ContainsChip(sound_chip_t::FDS) && cmd > CMD_EFF_FDS_LAST)
			cmd -= CMD_EFF_FDS_LAST - CMD_EFF_FDS_FIRST + 1;
		if (!Chip.ContainsChip(sound_chip_t::VRC7) && cmd > CMD_EFF_VRC7_LAST)
			cmd -= CMD_EFF_VRC7_LAST - CMD_EFF_VRC7_FIRST + 1;
		// VRC6
	}

	return (cmd << 1) | 0x80;
}

unsigned int CPatternCompiler::FindInstrument(int Instrument) const
{
	if (Instrument == MAX_INSTRUMENTS)
		return MAX_INSTRUMENTS;
	if (Instrument == HOLD_INSTRUMENT)		// // // 050B
		return HOLD_INSTRUMENT;

	for (std::size_t i = 0; i < m_iInstrumentList.size(); ++i)
		if (m_iInstrumentList[i] == Instrument)
			return i;

	return 0;	// Could not find the instrument
}

unsigned int CPatternCompiler::FindSample(int Instrument, int MidiNote) const		// // //
{
	return (*m_pDPCMList)[Instrument][MidiNote];
}

CPatternCompiler::stSpacingInfo CPatternCompiler::ScanNoteLengths(int Track, unsigned int StartRow, int Pattern, stChannelID Channel) {		// // //
	const auto *pSong = modfile_.GetSong(Track);		// // //
	if (!pSong)
		return { };

	int StartSpace = -1, Space = 0, SpaceCount = 0;

	for (unsigned i = StartRow; i < pSong->GetPatternLength(); ++i) {
		const auto &NoteData = pSong->GetPattern(Channel, Pattern).GetNoteOn(i);		// // //
		bool NoteUsed = false;

		if (NoteData.Note != note_t::none)
			NoteUsed = true;
		else if (NoteData.Instrument < MAX_INSTRUMENTS || NoteData.Instrument == HOLD_INSTRUMENT)		// // //
			NoteUsed = true;
		else if (NoteData.Vol < MAX_VOLUME)
			NoteUsed = true;
		else for (unsigned j = 0, Count = pSong->GetEffectColumnCount(Channel); j < Count; ++j)
			if (NoteData.Effects[j].fx != effect_t::none)
				NoteUsed = true;

		if (i == StartRow && !NoteUsed)
			return {0xFF, StartSpace};

		if (i > StartRow) {
			if (NoteUsed) {
				if (StartSpace == -1)
					StartSpace = Space;
				else if (StartSpace == Space)
					++SpaceCount;
				else
					return {SpaceCount, StartSpace};
				Space = 0;
			}
			else
				++Space;
		}
	}

	if (StartSpace == Space)
		++SpaceCount;

	return {SpaceCount, StartSpace};
}

void CPatternCompiler::WriteData(unsigned char Value)
{
	m_vData.push_back(Value);
	m_iHash += Value;				// Simple CRC-hash
	m_iHash += (m_iHash << 10);
	m_iHash ^= (m_iHash >> 6);
}

void CPatternCompiler::AccumulateDuration()
{
	++m_iDuration;
}

void CPatternCompiler::WriteDuration()
{
	if (m_iCurrentDefaultDuration == 0xFF) {
		if (!m_vData.size() && m_iDuration > 0)
			WriteData(0x00);
		if (m_iDuration > 0)
			WriteData(m_iDuration - 1);
	}

	m_iDuration = 0;
}

// Returns the size of the block at 'position' in the data array. A block is terminated by a note
int CPatternCompiler::GetBlockSize(int Position)
{
	unsigned int Pos = Position;

	int iDuration = 1;

	// Find if note duration optimization is on
	for (int i = 0; i < Position; ++i) {
		if (m_vData[i] == Command(CMD_SET_DURATION))
			iDuration = 0;
		else if (m_vData[i] == Command(CMD_RESET_DURATION))
			iDuration = 1;
	}

	for (; Pos < m_vData.size(); ++Pos) {
		unsigned char data = m_vData[Pos];
		if (data < 0x80) {		// Note
			//int size = (Pos + 1 + iDuration) - Position;
			int size = (Pos - Position);
//			if (size > 1)
//				return size - 1;

			return size + 1 + iDuration;// (Pos + 1 + iDuration) - Position;
		}
		else if (data == Command(CMD_SET_DURATION))
			iDuration = 0;
		else if (data == Command(CMD_RESET_DURATION))
			iDuration = 1;
		else {
			if (data < 0xE0 || data > 0xEF)
				++Pos;				// Command, skip parameter
		}
	//	++Pos;
	}

	// Error
	return 1;
}

void CPatternCompiler::OptimizeString()
{
	// Try to optimize by finding repeating patterns and compress them into a loop (simple RLE)
	//

	//
	// Ok, just figured this won't work without using loads of NES RAM so I'll
	// probably put this on hold for a while
	//

	unsigned int i, j, k, l;
	int matches, best_length = 0, last_inst;
	bool matched;

	/*

	80 00 2E 00 2E 00 2E 00 2E 00 2E 00 2E 00 ->
	80 00 2E 00 FF 06 02

	*/

	// Always copy first 2 bytes
//	memcpy(m_pCompressedData, m_pData, 2);
//	m_iCompressedDataPointer += 2;

	if (m_vData[0] == 0x80)
		last_inst = m_vData[1];
	else
		last_inst = 0;

	// Loop from start
	for (i = 0; i < m_vData.size(); /*i += 2*/) {

		int best_matches = 0;

		// Instrument
		if (m_vData[i] == 0x80)
			last_inst = m_vData[i + 1];
		else if (m_vData[i] >= 0xE0 && m_vData[i] <= 0xEF)
			last_inst = m_vData[i & 0xF];

		// Start checking from the first tuple
		for (l = GetBlockSize(i); l < (m_vData.size() - i); /*l += 2*/) {
			matches = 0;
			// See how many following matches there are from this combination in a row
			for (j = i + l; j <= m_vData.size(); j += l) {
				matched = true;
				// Compare one word
				for (k = 0; k < l; ++k) {
					if (m_vData[i + k] != m_vData[j + k])
						matched = false;
				}
				if (!matched)
					break;
				++matches;
				/*
				if ((j + l) <= m_iDataPointer) {
					if (memcmp(m_pData + i, m_pData + j, l) == 0)
						++matches;
					else
						break;
				}
				*/
			}
			// Save
			if (matches > best_matches) {
				best_matches = matches;
				best_length = l;
			}

			l += GetBlockSize(i + l);
		}
		// Compress
		if ((best_matches > 1 && best_length > 4) || best_matches > 2 /*&& (best_length > 2 && best_matches > 1)*/) {
			// Include the first one
			++best_matches;
			int size = best_length * best_matches;
			//
			// Last known instrument must also be added
			//
			std::copy_n(m_vData.begin() + i, best_length, m_vCompressedData.end());		// // //
			// Define a loop point: 0xFF (number of loops) (number of bytes)
			m_vCompressedData.push_back(Command(CMD_LOOP_POINT));
			m_vCompressedData.push_back(best_matches - 1);	// the nsf code sees one less
			m_vCompressedData.push_back(best_length);
			i += size;
		}
		else {
			// No loop
			int size = GetBlockSize(i);
			std::copy_n(m_vData.begin() + i, size, m_vCompressedData.end());		// // //
			i += size;
		}
	}

	(void)last_inst;		// // //
}

unsigned int CPatternCompiler::GetHash() const
{
	return m_iHash;
}

void CPatternCompiler::Print(std::string_view text) const		// // //
{
	if (m_pLogger)
		m_pLogger->WriteLog(text);
}

bool CPatternCompiler::CompareData(const std::vector<unsigned char> &data) const		// // //
{
	return m_vData == data;
}

const std::vector<unsigned char> &CPatternCompiler::GetData() const		// // //
{
	return m_vData;
}

const std::vector<unsigned char> &CPatternCompiler::GetCompressedData() const		// // //
{
	return m_vCompressedData;
}

unsigned int CPatternCompiler::GetDataSize() const
{
	return m_vData.size();
}

unsigned int CPatternCompiler::GetCompressedDataSize() const
{
	return m_vCompressedData.size();
}
