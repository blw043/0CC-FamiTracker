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


#pragma once

#include <string_view>
#include <string>
#ifdef _WINDOWS
#include "stdafx.h" // windows-specific
#endif

namespace conv {

template <typename CharT>
std::basic_string_view<CharT> to_sv(const CharT *str) {
	return std::basic_string_view<CharT>(str);
}

template <typename CharT, typename TraitsT, typename AllocT>
std::basic_string_view<CharT> to_sv(const std::basic_string<CharT, TraitsT, AllocT> &str) {
	return std::basic_string_view<CharT>(str);
}
template <typename CharT, typename TraitsT, typename AllocT>
std::basic_string_view<CharT> to_sv(std::basic_string<CharT, TraitsT, AllocT> &&) = delete;

template <typename CharT>
std::basic_string_view<CharT> to_sv(std::basic_string_view<CharT> str) {
	return str;
}

#ifdef _WINDOWS
template <typename CharT, typename TraitsT>
std::basic_string_view<CharT> to_sv(const ATL::CStringT<CharT, TraitsT> &str) {
	return std::basic_string_view<CharT>(str, str.GetLength());
}
template <typename CharT, typename TraitsT>
std::basic_string_view<CharT> to_sv(ATL::CStringT<CharT, TraitsT> &&str) = delete;
#endif

} // namespace conv
