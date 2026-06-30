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

#include <agar/core.h>
#include <SDL2/SDL.h>
//#include <sys/timerfd.h>
#include <sys/time.h>
#include <time.h>
#include <sched.h>
#include <mach/mach_time.h>
#include "throttle.h"
#include "audio.h"
#include "defines.h"
#include "vcc.h"

static unsigned long /*long*/ StartTime,EndTime,OneFrame,CurrentTime,SleepRes,TargetTime,OneMs;
static unsigned long /*long*/ LagTime = 0, LagCnt = 0;
static unsigned long /*long*/ MasterClock,Now;
static unsigned char FrameSkip=0;
static float fMasterClock=0;
static int timerfd;
static float cfps=0;

void CalibrateThrottle(void)
{
    struct sched_param schedparm;

	MasterClock = SDL_GetPerformanceFrequency();
	OneFrame = MasterClock / (TARGETFRAMERATE);
	OneMs = MasterClock / 1000;
	fMasterClock=(float)MasterClock;
	LagTime = 0;
	LagCnt = 0;


	// timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
	// memset(&schedparm, 0, sizeof(schedparm));
	// schedparm.sched_priority = 1; // lowest rt priority
	// pthread_setschedparam(0, SCHED_FIFO, &schedparm);

	StartTime = TargetTime = SDL_GetPerformanceCounter();
	//printf("CalibrateThrottle : MC %lld fMC %f 1ms %lld 1Frame %lld ST %lld TT %lld\n", MasterClock, fMasterClock, OneMs, OneFrame, StartTime, TargetTime);
}


void StartRender(void)
{
	StartTime = TargetTime; //SDL_GetPerformanceCounter();
	//fprintf(stderr, "<");
	return;
}

void EndRender(unsigned char Skip)
{
	FrameSkip = Skip;
	//if (abs(LagTime) > OneFrame) LagTime = 0;

	//fprintf(stderr, "<%lld>", LagTime);
	TargetTime = ( StartTime + (OneFrame * FrameSkip));
	//fprintf(stderr, ">");
	return;
}

// Sleep the thread until 'targetPerf' (SDL_GetPerformanceCounter units),
// returning idle time to the OS instead of busy-spinning the whole frame.
// Coarse sleep with mach_wait_until() for the bulk, then a short fine-spin
// (SpinMarginNs) for frame-accurate timing. Correct on Intel and Apple Silicon.
static void WaitUntil(unsigned long targetPerf)
{
	static const unsigned long SpinMarginNs = 500000; // 0.5ms fine-spin guard
	static mach_timebase_info_data_t tb = {0, 0};
	if (tb.denom == 0) mach_timebase_info(&tb);

	unsigned long now = SDL_GetPerformanceCounter();
	if (now >= targetPerf) return;

	// Remaining perf ticks -> nanoseconds (fMasterClock = perf frequency).
	double remNs = (double)(targetPerf - now) * 1.0e9 / fMasterClock;
	if (remNs > (double)SpinMarginNs)
	{
		uint64_t sleepNs = (uint64_t)(remNs - (double)SpinMarginNs);
		uint64_t sleepAbs = sleepNs * tb.denom / tb.numer; // ns -> mach absolute
		mach_wait_until(mach_absolute_time() + sleepAbs);
	}

	// Fine-spin the remaining margin (or any sleep overshoot exits immediately).
	while (SDL_GetPerformanceCounter() < targetPerf) {}
}

void FrameWait(void)
{
	CurrentTime = SDL_GetPerformanceCounter();

	if (CurrentTime > TargetTime)
	{
		extern void CPUConfigSpeedDec(void);  // ran out of time so reduce the CPU frequency
		if (cfps < (TARGETFRAMERATE-0.25)) CPUConfigSpeedDec();
		LagTime += CurrentTime - TargetTime;
		LagCnt++;
		//fprintf(stderr, "<%lld>", LagTime);
		return;
	}

	if (CurrentTime == TargetTime)
	{
		//LagTime = 0;
		//fprintf(stderr, "-");
		return;
	}

	extern void CPUConfigSpeedInc(void); // had time left over so increase the CPU frequency
	CPUConfigSpeedInc();
	// fprintf(stderr, "^");

	// Sleep (not spin) until the frame deadline, returning time to the OS.
	WaitUntil(TargetTime);

	CurrentTime = SDL_GetPerformanceCounter();
	LagTime += CurrentTime - TargetTime;
	LagCnt++;
	//fprintf(stderr, "(%ld,%ld)", LagTime);

	return;
}

// float timems()
// {
// 	static struct timeval tval_before, tval_after, tval_result;
// 	static int firsttime = 1;
// 	float secs, fsecs;

// 	if (firsttime)
// 	{
// 		gettimeofday(&tval_before, NULL);
// 		firsttime = 0;
// 		return 0.0;
// 	}

// 	gettimeofday(&tval_after, NULL);
// 	timersub(&tval_after, &tval_before, &tval_result);
// 	memcpy(&tval_before, &tval_after, sizeof(tval_after));

// 	secs = tval_result.tv_sec;
// 	fsecs = (float)tval_result.tv_usec / 1000000.0;

// 	return secs + fsecs;
// }

float CalculateFPS(void) //Done at end of render;
{

	static unsigned short FrameCount=0;
	static float fps=0,fNow=0,fLast=0;

	if (++FrameCount!=FRAMEINTERVAL)
		return(fps);

	Now = SDL_GetPerformanceCounter();
	fNow=(float)Now;
	fps=(fNow-fLast)/fMasterClock;
	fps= FRAMEINTERVAL/fps;
	fps*=FrameSkip;
	//printf("%d %2.2f %f %f %f\n", FrameCount, fps, fNow, fLast, fNow-fLast) ;
	fLast=fNow;
	FrameCount=0;
	cfps = fps;
	return(fps);
}

float GetCurrentFPS()
{
	return cfps;
}

