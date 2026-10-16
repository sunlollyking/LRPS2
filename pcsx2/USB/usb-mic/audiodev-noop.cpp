// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "USB/usb-mic/audiodev.h"

std::unique_ptr<AudioDevice> AudioDevice::CreateDevice(AudioDir dir, u32 channels, std::string devname, s32 latency)
{
	return nullptr;
}
