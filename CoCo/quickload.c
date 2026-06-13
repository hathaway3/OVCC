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
#include <stdio.h>
#include "defines.h"
#include "pakinterface.h"
#include "vcc.h"
#include "coco3.h"
#include "tcc1014mmu.h"
#include "fileops.h"

static unsigned char FileType=0;
static unsigned short FileLenth=0;
static  short StartAddress=0;
static unsigned short XferAddress=0;
static unsigned char *MemImage=NULL;
FILE *BinImage=NULL;
static unsigned char Flag=1;
static int temp=255;
static char Extension[MAX_PATH]="";

unsigned char QuickLoad(char *BinFileName)
{
	unsigned int MemIndex=0;
	unsigned char result = 0;

	if (!AG_FileExists(BinFileName))
		return(1);				//File Not Found

	strcpy(Extension,PathFindExtension(BinFileName));
	SDL_strlwr(Extension);
	if ( (strcmp(Extension,".rom")==0) | (strcmp(Extension,".ccc")==0))
	{
		InsertModule (BinFileName);
		return(0);
	}

	BinImage=fopen(BinFileName,"rb");
	if (BinImage==NULL)
		return(2);				//Can't Open File
			
	MemImage=(unsigned char *)malloc(65535);
	if (MemImage==NULL)
	{
		_MessageBox("Can't alocate ram");
		fclose(BinImage);
		BinImage = NULL;
		return(3);				//Not enough memory
	}

	if ( strcmp(Extension,".bin")==0)
	{
		while (true)
		{
			temp=fread(MemImage,sizeof(char),5,BinImage);
			if (temp < 5)
			{
				_MessageBox(".Bin file is corrupt or truncated");
				result = 3;
				goto cleanup;
			}
			FileType=MemImage[0];
			FileLenth=(MemImage[1]<<8) + MemImage[2];
			StartAddress=(MemImage[3]<<8)+MemImage[4];

			switch (FileType)
			{
			case 0:
				temp=fread(&MemImage[0],sizeof(char),FileLenth,BinImage);
				if (temp < FileLenth)
				{
					_MessageBox(".Bin file is corrupt or truncated");
					result = 3;
					goto cleanup;
				}
				for (MemIndex=0;MemIndex<FileLenth;MemIndex++) //Kluge!!!
					MemWrite8(MemImage[MemIndex],StartAddress++);
				break;
			case 255:
				XferAddress=StartAddress;
				if ( (XferAddress==0) | (XferAddress >32767) |(FileLenth != 0) )
				{
					_MessageBox(".Bin file is corrupt or invalid Transfer Address");
					result = 3;
					goto cleanup;
				}
				CPUForcePC(XferAddress);
				result = 0;
				goto cleanup;
				break;
			default:
				_MessageBox(".Bin file is corrupt or invalid");
				result = 3;
				goto cleanup;
				break;
			} //End Switch
		} //End While
	} // End if
	else
	{
		result = 255; //Invalid File type
	}

cleanup:
	if (BinImage != NULL) {
		fclose(BinImage);
		BinImage = NULL;
	}
	if (MemImage != NULL) {
		free(MemImage);
		MemImage = NULL;
	}
	return result;
} //End Proc

unsigned short GetXferAddr(void)
{
	return(XferAddress);
}