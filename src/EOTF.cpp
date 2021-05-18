/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <stdafx.h>

#include "EOTF.h"


const TCHAR* ToString(const EOTF eotf)
{
	switch (eotf)
	{
	case EOTF::UNKNOWN:
		return TEXT("UNKNOWN");

	case EOTF::SDR:
		return TEXT("SDR");

	case EOTF::HDR:
		return TEXT("HDR");

	case EOTF::PQ:
		return TEXT("PQ (ST2084)");

	case EOTF::HLG:
		return TEXT("HLG");
	}

	throw std::runtime_error("UNSPECIFIED EOTF");
}
