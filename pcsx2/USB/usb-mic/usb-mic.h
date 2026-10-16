// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include "USB/libretro-usb/qusb.h"
#include <string>

namespace usb_mic
{
	enum MicrophoneType
	{
		MIC_SINGSTAR,
		MIC_LOGITECH,
		MIC_KONAMI,
		MIC_COUNT,
	};

	// An empty device_name picks the microphone automatically.
	USBDevice* mic_create(u32 subtype, int samplerate, std::string device_name);
} // namespace usb_mic
