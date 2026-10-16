/*  USBnull
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include "Common.h"
#include "USB.h"
#include "libretro-usb/USBinternal.h"
#include "usb-eyetoy/usb-eyetoy-webcam.h"

USBcallback USBirq;

s8 *ram;

/* OHCI host-controller clock-timing globals, set by ohci_create(). */
s64 g_usb_frame_time = 0;
s64 g_usb_bit_time   = 0;
s64 g_usb_last_cycle = 0;

static OHCIState* s_qemu_ohci     = nullptr;
static s64        s_usb_clocks    = 0;
static s64        s_usb_remaining = 0;

static int        s_port_device[USB_NUM_PORTS] = { USB_DEV_NONE, USB_DEV_NONE };
static USBDevice* s_usb_device[USB_NUM_PORTS]  = { nullptr, nullptr };

void USBSetPortDevice(unsigned port, int device)
{
	if (port < USB_NUM_PORTS)
		s_port_device[port] = device;
}

static void USBCreateDevice(u32 port)
{
	USBDevice* dev = nullptr;
	switch (s_port_device[port])
	{
		case USB_DEV_EYETOY:
			dev = usb_eyetoy::eyetoy_create(usb_eyetoy::TYPE_EYETOY, "");
			break;
		case USB_DEV_CAPTURE_EYE:
			dev = usb_eyetoy::eyetoy_create(usb_eyetoy::TYPE_OV511P, "");
			break;
		default:
			return;
	}
	if (!dev)
		return;

	OHCIPort& rhport = s_qemu_ohci->rhport[port];
	rhport.port.dev = dev;
	dev->attached = true;
	usb_attach(&rhport.port);
	s_usb_device[port] = dev;
}

static void USBDestroyDevice(u32 port)
{
	USBDevice* dev = s_usb_device[port];
	if (!dev)
		return;
	if (dev->klass.unrealize)
		dev->klass.unrealize(dev);
	s_qemu_ohci->rhport[port].port.dev = nullptr;
	s_usb_device[port] = nullptr;
}

int usb_get_ticks_per_second(void)
{
	return PSXCLK;
}

s64 usb_get_clock(void)
{
	return s_usb_clocks;
}

void USBconfigure()
{
}

s32 USBinit()
{
	s_qemu_ohci = ohci_create(0x1f801600, USB_NUM_PORTS);
	if (!s_qemu_ohci)
		return -1;

	s_usb_clocks     = 0;
	s_usb_remaining  = 0;
	g_usb_last_cycle = 0;
	return 0;
}

void USBshutdown()
{
	if (!s_qemu_ohci)
		return;
	for (u32 port = 0; port < USB_NUM_PORTS; port++)
		USBDestroyDevice(port);
	free(s_qemu_ohci);
	s_qemu_ohci = nullptr;
}

/* Devices stay attached across suspend/resume; the guest's USB driver
 * would otherwise see them unplugged. */
s32 USBopen()
{
	if (!s_qemu_ohci)
		return -1;
	for (u32 port = 0; port < USB_NUM_PORTS; port++)
	{
		if (!s_usb_device[port])
			USBCreateDevice(port);
	}
	return 0;
}

void USBclose()
{
}

u8 USBread8(u32 addr)
{
	return 0;
}

u16 USBread16(u32 addr)
{
	return 0;
}

u32 USBread32(u32 addr)
{
	if (!s_qemu_ohci)
		return 0;
	return ohci_mem_read(s_qemu_ohci, addr);
}

void USBwrite8(u32 addr, u8 value)
{
}

void USBwrite16(u32 addr, u16 value)
{
}

void USBwrite32(u32 addr, u32 value)
{
	if (s_qemu_ohci)
		ohci_mem_write(s_qemu_ohci, addr, value);
}

void USBirqCallback(USBcallback callback)
{
	USBirq = callback;
}

int _USBirqHandler(void)
{
	return 1;
}

USBhandler USBirqHandler(void)
{
	// Pass our handler to pcsx2.
	return (USBhandler)_USBirqHandler;
}

void USBsetRAM(void *mem)
{
	ram = (s8 *)mem;
}

s32 USBfreeze(int mode, freezeData *data)
{
	return 0;
}

void USBasync(u32 cycles)
{
	if (!s_qemu_ohci)
		return;

	s_usb_remaining += cycles;
	s_usb_clocks    += s_usb_remaining;
	if (s_qemu_ohci->eof_timer > 0)
	{
		while ((u64)s_usb_remaining >= s_qemu_ohci->eof_timer)
		{
			s_usb_remaining -= s_qemu_ohci->eof_timer;
			s_qemu_ohci->eof_timer = 0;
			ohci_frame_boundary(s_qemu_ohci);

			/* Break out of the loop if the bus was stopped. */
			if (!s_qemu_ohci->eof_timer)
				break;
		}
		if ((s_usb_remaining > 0) && (s_qemu_ohci->eof_timer > 0))
		{
			s64 m = s_qemu_ohci->eof_timer;
			if (s_usb_remaining < m)
				m = s_usb_remaining;
			s_qemu_ohci->eof_timer -= m;
			s_usb_remaining        -= m;
		}
	}
}
