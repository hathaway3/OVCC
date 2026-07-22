#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <agar/core.h>
#include <agar/gui.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef __MINGW32__
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include "becker.h"
#include "../CoCo/fileops.h"
#include "../CoCo/iniman.h"

#define MAX_PATH 260
#define EXTROMSIZE 8192

#ifndef __MINGW32__
typedef int boolean;
#endif
typedef int BOOL;

static char moduleName[17] = { "HDBDOS/DW/Becker" };

// socket
static int dwSocket = 0;

// vcc stuff
typedef void (*SETCART)(unsigned char);
typedef void (*SETCARTPOINTER)(SETCART);
static void (*PakSetCart)(unsigned char)=NULL;
static char IniFile[MAX_PATH]="";
static unsigned char HDBRom[EXTROMSIZE];
static bool DWTCPEnabled = false;
static char *PakRomAddr = NULL;

// are we retrying tcp conn
static bool retry = false;

// Guards dwSocket, retry, and the InBuffer/InReadPos/InWritePos/InBufferCount
// ring buffer below, all shared between the CPU thread (dw_status/dw_read/
// dw_write, invoked via PackPortRead/PackPortWrite) and the DriveWire TCP
// thread (DWTCPThread and the functions it calls). Previously unsynchronized.
static AG_Mutex BufferLock = AG_MUTEX_INITIALIZER;

// Tracks whether hDWTCPThread currently refers to a live thread, so
// killDWTCPThread() knows whether it's valid to join -- dw_setaddr()/
// dw_setport() can reach killDWTCPThread() during initial config load,
// before SetDWTCPConnectionEnable(1) has ever started a thread.
static bool threadRunning = false;

// circular buffer for socket io
static char InBuffer[BUFFER_SIZE];
// InBufferCount (bytes currently buffered) disambiguates a full buffer from
// an empty one -- comparing InReadPos==InWritePos alone can't tell them
// apart, which silently dropped/froze data once the buffer filled completely.
static int InBufferCount = 0;
static int InReadPos = 0;
static int InWritePos = 0;

// statistics
static int BytesWrittenSince = 0;
static int BytesReadSince = 0;
static long LastStats = 0;
static float ReadSpeed = 0;
static float WriteSpeed = 0;

// hostname and port

static char tmpdwaddress[MAX_PATH];
static char dwaddress[MAX_PATH];
static unsigned short dwsport = 65504;
static char curaddress[MAX_PATH];
static unsigned short curport = 65504;
static char serverPort[16];


//thread handle
static AG_Thread hDWTCPThread;

// scratchpad for msgs
char msg[MAX_PATH];

// log lots of stuff...
static boolean logging = false;

static void (*DynamicMenuCallback)( char *,int, int)=NULL;
unsigned char LoadExtRom(char *);
void SetDWTCPConnectionEnable(unsigned int enable);
int dw_setaddr(char *bufdwaddr);
int dw_setport(char *bufdwport);
void WriteLog(char *Message,unsigned char Type);
void BuildDynaMenu(void);
void LoadConfig(void);
void SaveConfig(void);

AG_MenuItem *menuAnchor = NULL;
AG_MenuItem *itemConfig = NULL;
AG_MenuItem *itemSeperator = NULL;

INIman *iniman = NULL;


void __attribute__ ((constructor)) initLibrary(void) {
 //
 // Function that is called when the library is loaded
 //
 //   printf("becker is initialized\n"); 
}

void __attribute__ ((destructor)) cleanUpLibrary(void) {
 //
 // Function that is called when the library is »closed«.
 //
 //   printf("becker is exited\n"); 
}

// coco checks for data
unsigned char dw_status( void )
{
	// check for input data waiting

	unsigned char result;

	AG_MutexLock(&BufferLock);
	result = (retry || dwSocket == 0 || InBufferCount == 0) ? 0 : 1;
	AG_MutexUnlock(&BufferLock);

	return result;
}

// coco reads a byte
unsigned char dw_read( void )
{
	// increment buffer read pos, return next byte
	unsigned char dwdata;

	AG_MutexLock(&BufferLock);
	dwdata = InBuffer[InReadPos];

	InReadPos++;

	if (InReadPos == BUFFER_SIZE)
		InReadPos = 0;

	if (InBufferCount > 0)
		InBufferCount--;
	AG_MutexUnlock(&BufferLock);

	BytesReadSince++;

	return(dwdata);
}

// coco writes a byte
int dw_write( char dwdata)
{
	// send the byte if we're connected; dwSocket/retry are checked and the
	// send() itself done under BufferLock so a concurrent close from the
	// DriveWire TCP thread can't invalidate the fd out from under this send.
	AG_MutexLock(&BufferLock);

	if ((dwSocket != 0) & (!retry))
	{
		int res = send(dwSocket, &dwdata, 1, 0);
		if (res != 1)
		{
			sprintf(msg,"dw_write: socket error\n");
			fprintf(stderr, "%s", msg);
#ifdef __MINGW32__
			closesocket(dwSocket);
#else
			close(dwSocket);
#endif
			dwSocket = 0;
		}
		else
		{
			BytesWrittenSince++;
		}
	}
		else
	{
		sprintf(msg,"coco write but null socket\n");
		fprintf(stderr, "%s", msg);
	}

	AG_MutexUnlock(&BufferLock);

	return(0);
}

void killDWTCPThread(void)
{
	// Close the socket immediately (instead of sleeping and hoping the
	// thread notices) so any recv()/connect() the TCP thread is blocked in
	// returns right away.
	AG_MutexLock(&BufferLock);
	DWTCPEnabled = false;
	if (dwSocket != 0)
	{
#ifdef __MINGW32__
		closesocket(dwSocket);
#else
		close(dwSocket);
#endif
		dwSocket = 0;
	}
	AG_MutexUnlock(&BufferLock);

	// Wait for the thread to actually exit before returning. The caller may
	// be about to have this pak dlclose()'d (see mpi.c's UnloadModule); if
	// the thread were still running when that happens, it would crash.
	if (threadRunning)
	{
		AG_ThreadJoin(hDWTCPThread, NULL);
		threadRunning = false;
	}

	AG_MutexLock(&BufferLock);
	InReadPos = 0;
	InWritePos = 0;
	InBufferCount = 0;
	AG_MutexUnlock(&BufferLock);
}

// set our hostname, called from config.c
int dw_setaddr(char *bufdwaddr)
{
	strcpy(dwaddress,bufdwaddr);
	return(0);
}


// set our port, called from config.c
int dw_setport(char *bufdwport)
{
	dwsport = (unsigned short)atoi(bufdwport);

	if ((dwsport != curport) || (strcmp(dwaddress,curaddress) != 0))
	{
		// host or port has changed, kill open connection
		killDWTCPThread();
	}

	return(0);
}

// try to connect with DW server
// Only ever runs on the DriveWire TCP thread. retry/dwSocket are also read
// (and, for dwSocket, written) from the CPU thread's dw_status/dw_read/
// dw_write, so every assignment to them here goes through BufferLock; the
// blocking calls themselves (gethostbyname/socket/connect) are deliberately
// left outside the lock so they don't stall the CPU thread's buffer access.
void attemptDWConnection( void )
{

	AG_MutexLock(&BufferLock);
	retry = true;
	AG_MutexUnlock(&BufferLock);

	BOOL bOptValTrue = true;
	int iOptValTrue = 1;

	strcpy(curaddress, dwaddress);
	curport= dwsport;

	sprintf(msg,"Connecting to %s:%d... \n",dwaddress,dwsport);
	fprintf(stderr, "%s", msg);

	// resolve hostname
	struct hostent *dwSrvHost = gethostbyname(dwaddress);

	if (dwSrvHost == NULL || dwSrvHost->h_addr_list[0] == NULL)
	{
		// invalid hostname/no dns
		AG_MutexLock(&BufferLock);
		retry = false;
		AG_MutexUnlock(&BufferLock);
//              WriteLog("failed to resolve hostname.\n",TOCONS);
		return;
	}

	// allocate socket
	int newSocket;
#ifdef __MINGW32__
	newSocket = socket (AF_INET,SOCK_STREAM,IPPROTO_TCP);
#else
	newSocket = socket (AF_INET,SOCK_STREAM,0);
#endif
	if (newSocket == -1)
	{
		// no deal
		AG_MutexLock(&BufferLock);
		dwSocket = 0;
		retry = false;
		AG_MutexUnlock(&BufferLock);
		fprintf(stderr,"invalid socket.\n");
		return;
	}

	// set options
#ifdef __MINGW32__
	setsockopt(newSocket,IPPROTO_TCP,SO_REUSEADDR,(char *)&bOptValTrue,sizeof(bOptValTrue));
	setsockopt(newSocket,IPPROTO_TCP,TCP_NODELAY,(char *)&iOptValTrue,sizeof(iOptValTrue));
#else
	setsockopt(newSocket,SOL_SOCKET,SO_REUSEADDR,(char *)&bOptValTrue,sizeof(bOptValTrue));
	//setsockopt(newSocket,SOL_SOCKET,TCP_NODELAY,(char *)&iOptValTrue,sizeof(iOptValTrue));
#endif
	// build server address

#ifdef __MINGW32__
	SOCKADDR_IN dwSrvAddress;
#else
	struct sockaddr_in dwSrvAddress;
#endif

	dwSrvAddress.sin_family = AF_INET;
	dwSrvAddress.sin_addr = *((struct in_addr*)*dwSrvHost->h_addr_list);
	dwSrvAddress.sin_port = htons(dwsport);

	// try to connect...

#ifdef __MINGW32__
	int rc = connect(newSocket, (LPSOCKADDR)&dwSrvAddress, sizeof(dwSrvAddress));
#else
	int rc = connect(newSocket, &dwSrvAddress, sizeof(dwSrvAddress));
#endif

#ifdef __MINGW32__
	if (rc==SOCKET_ERROR)
#else
	if (rc==-1)
#endif
	{
		// no deal
//              WriteLog("failed to connect.\n",TOCONS);
#ifdef __MINGW32__
		closesocket(newSocket);
#else
		close(newSocket);
#endif
		AG_MutexLock(&BufferLock);
		dwSocket = 0;
		retry = false;
		AG_MutexUnlock(&BufferLock);
	}
	else
	{
		AG_MutexLock(&BufferLock);
		dwSocket = newSocket;
		retry = false;
		AG_MutexUnlock(&BufferLock);
	}
}

// TCP connection thread
void *DWTCPThread(void *p)
{
#ifdef __MINGW32__
	WSADATA wsaData;
#endif

	int sz;
	int res;

		// Request Winsock version 2.2

#ifdef __MINGW32__
	if ((WSAStartup(0x202, &wsaData)) != 0)
	{
		fprintf(stderr, "WSAStartup() failed, DWTCPConnection thread exiting\n");
		WSACleanup();
		return(p);
	}
#endif

	while(DWTCPEnabled)
	{
		// get connected
		attemptDWConnection();

		// keep trying...
		while ((dwSocket == 0) & DWTCPEnabled)
		{
			attemptDWConnection();

			// after 2 tries, sleep between attempts
			usleep(TCP_RETRY_DELAY*1000);
		}
		
		while ((dwSocket != 0) & DWTCPEnabled)
		{
			// we have a connection, lets chew through some i/o

			// Read as much as the free space in the ring buffer allows,
			// capped to the contiguous run to end-of-buffer (a second recv()
			// next iteration picks up after the wrap). InBufferCount (not
			// InReadPos vs InWritePos) is the source of truth for how much
			// room is free, since read==write position is ambiguous between
			// "empty" and "completely full".
			int freeSpace;
			int writePos;

			AG_MutexLock(&BufferLock);
			freeSpace = BUFFER_SIZE - InBufferCount;
			writePos = InWritePos;
			if (writePos + freeSpace > BUFFER_SIZE)
				sz = BUFFER_SIZE - writePos;
			else
				sz = freeSpace;
			AG_MutexUnlock(&BufferLock);

			if (sz == 0)
			{
				// Buffer is completely full; back off briefly instead of
				// busy-looping until the CPU thread drains some of it via
				// dw_read().
				usleep(1000);
				continue;
			}

			// read the data
			res = recv(dwSocket,(char *)InBuffer + writePos, sz, 0);

			if (res < 1)
			{
				// no good, bail out
				AG_MutexLock(&BufferLock);
#ifdef __MINGW32__
				closesocket(dwSocket);
#else
				close(dwSocket);
#endif
				dwSocket = 0;
				AG_MutexUnlock(&BufferLock);
			}
			else
			{
				// good recv, inc ptr
				AG_MutexLock(&BufferLock);
				InWritePos += res;
				if (InWritePos == BUFFER_SIZE)
					InWritePos = 0;
				InBufferCount += res;
				AG_MutexUnlock(&BufferLock);
			}

		}

	}

	// close socket if necessary
	AG_MutexLock(&BufferLock);
	if (dwSocket != 0)
	{
#ifdef __MINGW32__
		closesocket(dwSocket);
#else
		close(dwSocket);
#endif
		dwSocket = 0;
	}
	AG_MutexUnlock(&BufferLock);

	sprintf(msg,"DWTCPConnection thread terminated\n");
	fprintf(stderr, "%s", msg);

	return(p);
}

// called from config.c/UpdateConfig
void SetDWTCPConnectionEnable(unsigned int enable)
{
	// turning us on?
	if ((enable == 1) & (!DWTCPEnabled))
	{
		DWTCPEnabled = true;

		// WriteLog("DWTCPConnection has been enabled.\n",TOCONS);

		// reset buffer pointers
		AG_MutexLock(&BufferLock);
		InReadPos = 0;
		InWritePos = 0;
		InBufferCount = 0;
		AG_MutexUnlock(&BufferLock);

		// start it up...

		if (AG_ThreadTryCreate(&hDWTCPThread, DWTCPThread, NULL)!=0)
		{
			fprintf(stderr, "Cannot start DWTCPConnection thread!\n");
			// Roll back: no thread actually started, so a later
			// SetDWTCPConnectionEnable(1) call must be able to retry --
			// leaving DWTCPEnabled true here would permanently block that.
			DWTCPEnabled = false;
			return;
		}
		threadRunning = true;

		sprintf(msg,"DWTCPConnection thread started\n");
		fprintf(stderr, "%s", msg);

	}
	else if ((enable != 1) & DWTCPEnabled)
	{
		// we were running but have been turned off
		DWTCPEnabled = false;

		killDWTCPThread();

		// WriteLog("DWTCPConnection has been disabled.\n",TOCONS);
	
	}

}

// dll exported functions
void BuildMenu(void);
void ADDCALL ModuleName(char *ModName, AG_MenuItem *Temp)
{

	menuAnchor = Temp;
	strcpy(ModName, moduleName);

	if (menuAnchor != NULL) 
	{
		BuildMenu();
	}

	return ;
}

void ADDCALL PackPortWrite(unsigned char Port,unsigned char Data)
{
	switch (Port)
	{
		// write data 
		case 0x42:
			dw_write(Data);
			break;
	}
	return;
}

unsigned char ADDCALL PackPortRead(unsigned char Port)
{
	switch (Port)
	{
		// read status
		case 0x41:
			if (dw_status() != 0)
				return(2);
			else
				return(0);
			break;
		// read data 
		case 0x42:
			return(dw_read());
			break;
	}

	return 0;
}
/*
	__declspec(dllexport) unsigned char ModuleReset(void)
	{
		if (PakSetCart!=NULL)
			PakSetCart(1);
		return(0);
	}
*/
unsigned char ADDCALL SetCart(SETCART Pointer)
{
	
	PakSetCart=Pointer;
	return(0);
}

unsigned char ADDCALL PakMemRead8(unsigned short Address)
{
	//sprintf(msg,"PalMemRead8: addr %d  val %d\n",(Address & 8191), Rom[Address & 8191]);
	//WriteLog(msg,TOCONS);
	return(HDBRom[Address & 8191]);

}

void ADDCALL HeartBeat(void)
{
	// flush write buffer in the future..?
	return;
}

void ADDCALL ModuleStatus(char *DWStatus)
{
	// calculate speed
	struct timespec now;

	#define CLOCK_MONOTONIC 1 // Not picking up define from time.h for some reason?

	clock_gettime(CLOCK_MONOTONIC, &now);

	long sinceCalc = ((now.tv_sec * 1000) + (now.tv_nsec / 1000000)) - LastStats;
	
	if (sinceCalc > STATS_PERIOD_MS)
	{
		LastStats += sinceCalc;
		
		// kbps = bits / elapsed-ms; the 1000ms/1000bits-per-kbit factors cancel,
		// leaving bytes*8/sinceCalc directly (sinceCalc is in milliseconds).
		ReadSpeed = 8.0f * (BytesReadSince / (float)sinceCalc);
		WriteSpeed = 8.0f * (BytesWrittenSince / (float)sinceCalc);

		BytesReadSince = 0;
		BytesWrittenSince = 0;
	}
        
	if (DWTCPEnabled)
	{
		if (retry)
		{
				sprintf(DWStatus,"DW: Try %s", curaddress);
		}
		else if (dwSocket == 0)
		{
				sprintf(DWStatus,"DW: ConError");
		}
		else
		{
			sprintf(DWStatus,"DW: ConOK  R:%04.1f W:%04.1f", ReadSpeed , WriteSpeed);
		}
	}
	else
	{
		sprintf(DWStatus,"");
	}
	return;
}

void OKCallback(AG_Event *event)
{
	dw_setaddr(tmpdwaddress);
	dw_setport(serverPort);
	SaveConfig();
	AG_CloseFocusedWindow();
}

void ConfigBecker(AG_Event *event)
{
	AG_Window *win;

    if ((win = AG_WindowNewNamedS(0, "DriveWire Server")) == NULL)
    {
        return;
    }

    AG_WindowSetGeometryAligned(win, AG_WINDOW_ALIGNMENT_NONE, 420, 150);
    AG_WindowSetCaptionS(win, "DriveWire Server");
    AG_WindowSetCloseAction(win, AG_WINDOW_DETACH);

	// Server Address

	AG_Textbox *tbx = AG_TextboxNewS(win, AG_TEXTBOX_HFILL, "Server Address:");
	strcpy(tmpdwaddress,dwaddress);
	AG_TextboxBindASCII(tbx, tmpdwaddress, sizeof(tmpdwaddress));
	AG_TextboxSizeHint(tbx, "127.0.0.1 or some long name");

	// Server Port

	sprintf(serverPort, "%d", dwsport);
	tbx = AG_TextboxNewS(win, AG_TEXTBOX_HFILL, "Server Port:");
	AG_TextboxBindASCII(tbx, serverPort, sizeof(serverPort));
	AG_TextboxSizeHint(tbx, "65504 or whatever");
	
	// OK & Cancel buttons

	AG_HBox* hbox = AG_BoxNewHoriz(win, 0);

	AG_ButtonNewFn(hbox, 0, "OK", OKCallback, NULL);
	AG_ButtonNewFn(hbox, 0, "Cancel", AGWINDETACH(win));

	AG_WindowShow(win);
}

void BuildMenu(void)
{
	if (itemConfig == NULL)
	{
        itemSeperator = AG_MenuSeparator(menuAnchor);
	    // itemConfig = AG_MenuNode(menuAnchor, "DriveWire Server", NULL);
		// AG_MenuAction(itemConfig, "Config", NULL, ConfigBecker, NULL);
		itemConfig = AG_MenuAction(menuAnchor, "Config DW Server", NULL, ConfigBecker, NULL);
	}
}

void ADDCALL ModuleConfig(unsigned char func)
{
	switch(func)
	{
	case 0: // Destroy Menus
	{
		killDWTCPThread();
		if (itemConfig)
			AG_MenuDel(itemConfig);
		itemConfig = NULL;
		if (itemSeperator)
			AG_MenuDel(itemSeperator);
		itemSeperator = NULL;
	}
	break;

	case 1: // Update ini file
		strcpy(IniFile, iniman->files[iniman->lastfile].name);

	break;

	default:
		break;
	}

	return;
}

//void ADDCALL SetIniPath(char *IniFilePath)
void ADDCALL SetIniPath(INIman *InimanP)
{
	//strcpy(IniFile,IniFilePath);
	strcpy(IniFile, InimanP->files[InimanP->lastfile].name);
	InitPrivateProfile(InimanP);
	iniman = InimanP;
	LoadConfig();
	SetDWTCPConnectionEnable(1);
	return;
}

unsigned char ADDCALL ModuleReset(void)
{
	fprintf(stderr, "Becker ModuleReset\n");
	if (PakRomAddr != NULL)
	{
		memcpy(PakRomAddr, HDBRom, EXTROMSIZE);
	}
	return(0);
}

void ADDCALL PakRomShare(char *pakromaddr)
{
	fprintf(stderr, "Becker PakRomShare\n");
	PakRomAddr = pakromaddr;
}

void LoadConfig(void)
{
	char saddr[MAX_PATH]="";
	char sport[MAX_PATH]="";
	char DiskRomPath[MAX_PATH];

	GetPrivateProfileString(moduleName,"DWServerAddr","",saddr,MAX_PATH,IniFile);
	GetPrivateProfileString(moduleName,"DWServerPort","",sport,MAX_PATH,IniFile);
	
	if (strlen(saddr) > 0)
		dw_setaddr(saddr);
	else
		dw_setaddr("127.0.0.1");

	if (strlen(sport) > 0)
		dw_setport(sport);
	else
		dw_setport("65504");
	
#ifdef DARWIN
	ResolvePlatformPath("hdbdwbck.rom", DiskRomPath, sizeof(DiskRomPath));
#else
    getcwd(DiskRomPath, MAX_PATH);
	strcat(DiskRomPath, "/hdbdwbck.rom");
#endif
	LoadExtRom(DiskRomPath);
}

void SaveConfig(void)
{
	WritePrivateProfileString(moduleName,"DWServerAddr",dwaddress,IniFile);
	sprintf(msg, "%d", dwsport);
	WritePrivateProfileString(moduleName,"DWServerPort",msg,IniFile);
	return;
}

unsigned char LoadExtRom(char *FilePath)	//Returns 1 on if loaded
{

	FILE *rom_handle = NULL;
	unsigned char RetVal = 0;

	rom_handle = fopen(FilePath, "rb");
	if (rom_handle == NULL)
		memset(HDBRom, 0xFF, EXTROMSIZE);
	else
	{
		fread(HDBRom, 1, EXTROMSIZE, rom_handle);
		RetVal = 1;
		fclose(rom_handle);
	}
	return(RetVal);
}
