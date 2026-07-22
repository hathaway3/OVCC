#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "mpu.h"
#include "gpu.h"
#include "dma.h"
#include "gpuprimitives.h"
#include "linkedlists.h"

LinkedList ScreenList = { NULL, NULL, 0 };

// Guards ScreenList against the cross-thread race between NewScreen/GetScreen
// (called synchronously on the CPU thread from mpu.c's ExecuteCommand) and
// DestroyScreen (which only ever runs on the GPU thread, dispatched from the
// command queue in gpu.c) appending/removing+freeing nodes concurrently.
static pthread_mutex_t ScreenListLock = PTHREAD_MUTEX_INITIALIZER;

static unsigned short currentID;

void NewScreen(unsigned short idref, unsigned short address, unsigned short width, unsigned short height, unsigned short bitsperpixel)
{
    // fprintf(stderr, "NewScreen %x %d %d %d\n", address, width, height, bitsperpixel);

    // PixelsPerByte = 8/bitsperpixel below assumes bitsperpixel evenly
    // divides 8; anything else (e.g. 3, or >8) corrupts the pitch/shift math
    // used by every subsequent SetScreenPixel call for this screen.
    if (bitsperpixel != 1 && bitsperpixel != 2 && bitsperpixel != 4 && bitsperpixel != 8)
    {
        WriteCoCoInt(idref, 0xFFFF);
        return;
    }

    Screen *NewScreen = malloc(sizeof(Screen));
    if (NewScreen == NULL) { WriteCoCoInt(idref, 0xFFFF); return; }

    NewScreen->id = ++currentID;
    NewScreen->nextScreen = NULL;
    NewScreen->ScreenAddress = address;
    NewScreen->ScreenWidth  = width;
    NewScreen->ScreenHeight = height;
    NewScreen->BitsPerPixel = bitsperpixel;
    NewScreen->PixelsPerByte = (8 / NewScreen->BitsPerPixel);
    NewScreen->ScreenPitch = width / NewScreen->PixelsPerByte;
    NewScreen->ScreenEnd = NewScreen->ScreenAddress + (NewScreen->ScreenPitch * NewScreen->ScreenHeight);

    NewScreen->PPBshift = -1;
    for(unsigned short int PPB = NewScreen->PixelsPerByte ; PPB ; PPB=PPB>>1) { NewScreen->PPBshift++; }

    pthread_mutex_lock(&ScreenListLock);
    AppendListItem(&ScreenList, (LinkedListItem*)NewScreen);
    pthread_mutex_unlock(&ScreenListLock);

    // Interegate the mmu and record the current process taskmmubank map

    unsigned char tr = (MemRead(0xFF91) & 0x01)<<3; // Task register
    unsigned short addr = 0xFFA0 + tr;

    for(short i = 0 ; i <  8 ; i++)
    {
        NewScreen->taskmmubank[i] = MemRead(addr++);
        //fprintf(stderr, "%02x ", NewScreen->taskmmubank[i]);
    }
    // write(2, "\n",1);
    // fprintf(stderr, "NewScreen %d %d %d %d\n", NewScreen->PixelsPerByte, NewScreen->ScreenPitch, NewScreen->ScreenEnd, NewScreen->PPBshift);
    // ReportQueue();

    WriteCoCoInt(idref, (unsigned short)NewScreen->id);
}

void DestroyScreen(unsigned short int id)
{
    // fprintf(stderr, "DestroyScreen %d\n", id);

    pthread_mutex_lock(&ScreenListLock);
    Screen *screen = (Screen*)RemovelistItem(&ScreenList, (unsigned int)id);
    pthread_mutex_unlock(&ScreenListLock);

    if (screen == NULL) return;

    free(screen);
}

// Frees every remaining screen (used by ModuleReset). Callers must ensure
// the GPU thread isn't concurrently executing a command against any of
// these objects -- mpu.c's ModuleReset does this by stopping the GPU
// thread (which fully drains the command queue) before calling this.
void ResetScreens(void)
{
    pthread_mutex_lock(&ScreenListLock);
    LinkedListItem *item = ScreenList.ListHead;
    while (item != NULL)
    {
        LinkedListItem *next = item->nextItem;
        free(item);
        item = next;
    }
    ScreenList.ListHead = NULL;
    ScreenList.ListTail = NULL;
    ScreenList.itemCnt = 0;
    pthread_mutex_unlock(&ScreenListLock);
}

unsigned int ScreenCount(void)
{
    unsigned int count;
    pthread_mutex_lock(&ScreenListLock);
    count = ScreenList.itemCnt;
    pthread_mutex_unlock(&ScreenListLock);
    return count;
}

Screen *GetScreen(unsigned short int id)
{
    pthread_mutex_lock(&ScreenListLock);
    Screen *screen = (Screen*)FindListItem(&ScreenList, (unsigned int)id);
    pthread_mutex_unlock(&ScreenListLock);
    return screen;
}

unsigned char GetScreenMMUmemPagefromAddress(Screen *screen, unsigned short int addr)
{
    if (screen == NULL) return 0;
    unsigned short bankidx = addr>>13;
    return screen->taskmmubank[bankidx];
}

void SetColor(unsigned short screenid, unsigned short color)
{
    pthread_mutex_lock(&ScreenListLock);
    Screen *screen = (Screen*)FindListItem(&ScreenList,  (unsigned int)screenid);
    pthread_mutex_unlock(&ScreenListLock);

    if (screen == NULL) return;

    // fprintf(stderr, "SetColor %d\n", color);
    screen->Color = color;
}

void SetScreenColor(Screen *screen, unsigned short color)
{
    if (screen == NULL) { return; }

    // fprintf(stderr, "SetColor %d\n", color);
    screen->Color = color;
}

void SetPixel(unsigned short screenid, unsigned short x, unsigned short y)
{
    pthread_mutex_lock(&ScreenListLock);
    Screen *screen = (Screen*)FindListItem(&ScreenList,  (unsigned int)screenid);
    pthread_mutex_unlock(&ScreenListLock);

    if (screen == NULL) return;

    SetScreenPixel(screen, x, y);
}


void SetScreenPixel(Screen *screen, unsigned short x, unsigned short y)
{
    if (screen == NULL) { return; }

    // fprintf(stderr, "SetPixel %d %d\n", x, y);
    unsigned short pixaddr = screen->ScreenAddress + (y * screen->ScreenPitch) + (x>>screen->PPBshift);
    // ScreenEnd = ScreenAddress + pitch*height is one past the last valid
    // byte (exclusive), so pixaddr == ScreenEnd was a one-byte overwrite
    // past the screen into whatever CoCo memory follows it.
    if (pixaddr < screen->ScreenAddress || pixaddr >= screen->ScreenEnd)
    {
        // write(0, "?", 1);
        return;
    }
    unsigned short xmodPPB = x%screen->PixelsPerByte;
    unsigned char  pixmask = pixelmasks[screen->PPBshift][xmodPPB];
    unsigned char bankpage = GetScreenMMUmemPagefromAddress(screen, pixaddr);
    // unsigned char pixelbyte = MemRead(pixaddr) & (pixmask^0xff);
    unsigned char  pixelbyte = MmuRead(bankpage, pixaddr) & (pixmask^0xff);
    pixelbyte |= screen->Color<<(screen->BitsPerPixel*(screen->PixelsPerByte-xmodPPB-1));
    // MemWrite(pixelbyte, pixaddr);
    MmuWrite(pixelbyte, bankpage, pixaddr);
}

void DrawLine(unsigned short screenid, unsigned short x1, unsigned short y1, unsigned short x2, unsigned short y2)
{
    pthread_mutex_lock(&ScreenListLock);
    Screen *screen = (Screen*)FindListItem(&ScreenList,  (unsigned int)screenid);
    pthread_mutex_unlock(&ScreenListLock);

    if (screen == NULL) return;

    // fprintf(stderr, "DrawLine %x %d %d %d\n", x1, y1, x2, y2);
	int dx, dy;
	int inc1, inc2;
	int x, y, d;
	int xEnd, yEnd;
	int xDir, yDir;
	
	dx = abs(x2 - x1);
	dy = abs(y2 - y1);

	if (dy <= dx) 
    {
		d = dy*2 - dx;
		inc1 = dy*2;
		inc2 = (dy-dx)*2;
		if (x1 > x2) { x = x2; y = y2; yDir = -1; xEnd = x1; } 
        else { x = x1; y = y1; yDir = 1; xEnd = x2; }
		SetScreenPixel(screen, x, y);

		if (((y2-y1)*yDir) > 0) {
			while (x < xEnd) 
            {
				x++;
				if (d < 0) { d += inc1; } else { y++; d += inc2; }
				SetScreenPixel(screen, x, y);
			}
		} 
        else 
        {
			while (x < xEnd) 
            {
				x++;
				if (d < 0) { d += inc1; } else { y--; d += inc2; }
				SetScreenPixel(screen, x, y);
			}
		}		
	} 
    else 
    {
		d = dx*2 - dy;
		inc1 = dx*2;
		inc2 = (dx-dy)*2;
		if (y1 > y2) { y = y2; x = x2; yEnd = y1; xDir = -1; } 
        else { y = y1; x = x1; yEnd = y2; xDir = 1; }
		SetScreenPixel(screen, x, y);

		if (((x2-x1)*xDir) > 0) 
        {
			while (y < yEnd) 
            {
				y++;
				if (d < 0) { d += inc1; } else { x++; d += inc2; }
				SetScreenPixel(screen, x, y);
			}
		} 
        else 
        {
			while (y < yEnd) 
            {
				y++;
				if (d < 0) { d += inc1; } else { x--; d += inc2; }
				SetScreenPixel(screen, x, y);
			}
		}
	}
}