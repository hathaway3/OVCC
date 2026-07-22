/*
Copyright 2015 by Joseph Forgione
This file is part of VCC (Virtual Color Computer).

    VCC (Virtual Color Computer) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    VCC (Virtual Color Computer) is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VCC (Virtual Color Computer).  If not, see <http://www.gnu.org/licenses/>.
*/

#include <time.h>
#include <stdbool.h>
#include "stdio.h"
#include "stdlib.h"
#include "distortc.h"

/* Table description:							   Bit3  Bit2  Bit1  Bit0
Write to $FF51 read from $FF50
	0x00		1-second digit register				 S8    S4    S2    S1
	0x01		10-second digit register			  x   S40   S20   S10
	0x02		1-minute digit register				Mi8   Mi4   Mi2   Mi1
	0x03		10-minute digit register			  x  Mi40  Mi20  Mi10
	0x04		1-hour digit register				 H8    H4    H2    H1
	0x05		PM/AM, 10-hour digit register		  x   P/A   H20   H10
	0x06		1-day digit register				 D8    D4    D2    D1
	0x07		10-day digit register				  x     x   D20   D10
	0x08		1-month digit register				Mo8   Mo4   Mo2   Mo1
	0x09		10-month digit register			   Mo80  Mo40  Mo20  Mo10
	0x0A		1-year digit register				 Y8    Y4    Y2    Y1
	0x0B		10-yead digit register				Y80   Y40   Y20   Y10
	0x0C		Week register						  x    W4    W2    W1

													 30
	0x0D		Control register D					Sec   IRQ  Busy  Hold
													adj  Flag
													           ITRP
	0x0E		Control register E					 T1    T0 /STND  Mask
													  
													           
	0x0F		Control register F				   Test 24/12  Stop  Rest
													  
	
	Note: Digits are BDC. Registers only four bits wide.
	X denotes 'not used'


*/
static unsigned char time_register=0;
static time_t rawtime;
static struct timespec tspec;
// localtime() returns a pointer into a static buffer shared process-wide;
// other pak modules with their own clocks (e.g. HardDisk/SuperIDE's
// cloud9.c) also call localtime(), so holding onto its pointer across
// multiple read_time() calls (needed for the Stop bit below, which freezes
// the displayed time) would risk another module's clock call clobbering it
// in between. Copy the result into our own storage instead.
static struct tm nowbuf;
static struct tm *now = NULL;
static unsigned char Hour12=0;
// Control register F (0x0F) bit 1: stop the clock, freezing whatever time
// was last read instead of advancing -- a real, unambiguous feature of the
// documented bit table (unlike 0x0D/0x0E's IRQ/busy/timer-rate bits, which
// aren't specified precisely enough here to implement without guessing at
// real chip behavior).
static bool ClockStopped=false;

unsigned char read_time(unsigned short port)
{
	unsigned char ret_val=0;
	if (port == 0x50)
	{
		if (!ClockStopped || now == NULL)
		{
			struct tm *tmp;
			clock_gettime(CLOCK_REALTIME, &tspec);
			rawtime = tspec.tv_sec;
			tmp = localtime(&rawtime);
			if (tmp != NULL)
			{
				nowbuf = *tmp;
				now = &nowbuf;
			}
		}
		if (now == NULL) return(0); // localtime() failed and never has before
		switch (time_register)
		{

		case 0:
		ret_val= now->tm_sec % 10;
		break;
		case 1:
		ret_val= now->tm_sec / 10;
		break;
		case 2:
		ret_val = now->tm_min % 10;
		break;
		case 3:
		ret_val = now->tm_min / 10;
		break;
		case 4:
		ret_val = now->tm_hour % 10;
		break;
		case 5:
		ret_val = now->tm_hour / 10;
		
		break;
		case 6:
		ret_val = now->tm_mday % 10;
		break;
		case 7:
		ret_val = now->tm_mday /10;
		break;
		case 8:
		ret_val = (now->tm_mon+1) % 10;
		break;
		case 9:
		ret_val = (now->tm_mon+1) /10 ;
		break;
		case 0xA:
		ret_val = now->tm_year%10;
		break;
		case 0xB:
		ret_val = (now->tm_year%100)/10;
		break;
		case 0xC:
		ret_val=(unsigned char)now->tm_wday; //May not be right
		break;
		case 0xD:
		// bit1 = Busy. Reads here are always a fresh, instantaneous
		// snapshot in this emulation (there's no real update-in-progress
		// window like a physical RTC has), so this is never busy.
		ret_val = 0;
		break;
		case 0xE:

		break;
		case 0xF:
		// bit2 = 24/12 format (Hour12, set via write_time); bit1 = Stop
		// readback.
		ret_val = (ClockStopped ? 2 : 0) | (Hour12 ? 0 : 4);
		break;
		default:
			ret_val=0;
		break;

		}
	}
return(ret_val);
}

void write_time(unsigned char data,unsigned char port)
{
	switch (port)
	{
		case 0x50:
			switch (time_register)
			{
				case 0x0F:
					Hour12=!((data & 4)>>2);
					ClockStopped = (data & 2) ? true : false;
				//	if (Hour12==0)
				//		MessageBox(0,"Setting format 0","Ok",0);
				//	else
				//		MessageBox(0,"Setting format 1","Ok",0);
				break;

				default:
				break;
			}

		break;

		case 0x51:
			time_register=(data & 0xF);
		break;
	}
	return;
}

