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

#include "ChannelMap.h"
#include "TrackerChannel.h"
#include "APU/Types.h"		// // //

CChannelMap::CChannelMap(CSoundChipSet chips, unsigned n163chs) :
	chips_(chips), n163chs_(n163chs)
{
}

const CChannelOrder &CChannelMap::GetChannelOrder() const {
	return order_;
}

void CChannelMap::RegisterChannel(CTrackerChannel &Channel) {		// // //
	// Adds a channel to the channel map
	if (order_.AddChannel(Channel.GetID()))
		m_pChannels.push_back(&Channel);
}

bool CChannelMap::SupportsChannel(const CTrackerChannel &ch) const {		// // //
	return HasExpansionChip(ch.GetChip()) && !(ch.GetChip() == sound_chip_t::N163 &&
		GetChannelSubIndex(ch.GetID()) >= GetChipChannelCount(sound_chip_t::N163));
}

CTrackerChannel &CChannelMap::GetChannel(int index) const {		// // //
	return *m_pChannels.at(index);
}

CTrackerChannel &CChannelMap::FindChannel(chan_id_t chan) const {		// // //
	return GetChannel(GetChannelIndex(chan));
}

int CChannelMap::GetChannelIndex(chan_id_t chan) const {		// // //
	// Translate channel ID to index, returns -1 if not found
	return order_.GetChannelIndex(chan);
}

bool CChannelMap::HasChannel(chan_id_t chan) const {		// // //
	return order_.HasChannel(chan);
}

int CChannelMap::GetChannelCount() const {		// // //
	return order_.GetChannelCount();
}

chan_id_t CChannelMap::GetChannelType(int index) const {		// // //
	return GetChannel(index).GetID();
}

sound_chip_t CChannelMap::GetChipType(int index) const {
	return GetChannel(index).GetChip();
}

const CSoundChipSet &CChannelMap::GetExpansionFlag() const noexcept {		// // //
	return chips_;
}

unsigned CChannelMap::GetChipChannelCount(sound_chip_t chip) const {
	if (chip == sound_chip_t::N163)
		return HasExpansionChip(chip) ? n163chs_ : 0;

	unsigned count = 0;
	for (auto pChan : m_pChannels)
		if (pChan->GetChip() == chip)
			++count;
	return count;
}

bool CChannelMap::HasExpansionChip(sound_chip_t chips) const noexcept {
	return chips_.ContainsChip(chips);
}
