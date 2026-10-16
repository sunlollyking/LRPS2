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

#ifndef __USB_H__
#define __USB_H__

#define USBdefs
#include "Pcsx2Defs.h"

typedef void (*USBcallback)(int cycles);
typedef int (*USBhandler)(void);

extern USBcallback USBirq;

extern s8 *ram;

#define USB_NUM_PORTS 2

enum UsbPortDevice
{
	USB_DEV_NONE = 0,
	USB_DEV_EYETOY,
	USB_DEV_CAPTURE_EYE,
};

/* Takes effect when the core thread next starts. */
void USBSetPortDevice(unsigned port, int device);

void USBconfigure();
s32 USBinit();
void USBshutdown();
s32 USBopen();
void USBclose();
u8 USBread8(u32 addr);
u16 USBread16(u32 addr);
u32 USBread32(u32 addr);
void USBwrite8(u32 addr, u8 value);
void USBwrite16(u32 addr, u16 value);
void USBwrite32(u32 addr, u32 value);
void USBirqCallback(USBcallback callback);
int _USBirqHandler(void);
USBhandler USBirqHandler(void);
void USBsetRAM(void *mem);
s32 USBfreeze(int mode, freezeData *data);
void USBasync(u32 cycles);

#endif
