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

// First-run Startup Wizard: lets a new user pick a CPU/RAM profile and which
// peripherals they want (single pak, or several sharing an MPI) before OVCC
// ever boots, instead of silently landing on a bare CoCo 3. Also reachable
// any time from Configuration -> "Setup Wizard..." to swap the running setup.
//
// The window itself is a disposable (non-singleton) AGAR dialog, same as the
// MPI config dialog (mpi/mpi.c) or the cartridge file picker (vccgui.c) --
// it's rebuilt from scratch each time it's opened and destroyed on close.

#include <stdio.h>
#include <string.h>
#include "defines.h"
#include "vcc.h"
#include "config.h"
#include "pakinterface.h"
#include "fileops.h"
#include "iniman.h"
#include "wizard.h"

#define MAX_WIZ_PAKS 32
#define MAX_MPI_SLOTS 4

extern STRConfig CurrentConfig;

static SystemState2 *wizState = NULL;
static int wizFirstRun = 1;

static int wizCpuType = 0;
static int wizRamSize = 1;

static PakInfo wizPaks[MAX_WIZ_PAKS];
static int wizPakCount = 0;
static int wizChecked[MAX_WIZ_PAKS];
static AG_Checkbox *wizCheckboxes[MAX_WIZ_PAKS];

static char wizSummary[1024] = "";

static void UpdateWizardSummary(void)
{
	static const char *ramLabels[4] = { "128KB", "512KB", "2048KB", "8096KB" };
	char line[128];
	int i, selected = 0;

	strcpy(wizSummary, (wizCpuType == 1) ? "CPU: Hitachi HD6309\n" : "CPU: Motorola MC6809\n");

	snprintf(line, sizeof(line), "RAM: %s\n", ramLabels[wizRamSize & 3]);
	strcat(wizSummary, line);

	strcat(wizSummary, "Peripherals: ");
	for (i = 0; i < wizPakCount; i++)
	{
		if (wizChecked[i])
		{
			if (selected) strcat(wizSummary, ", ");
			strcat(wizSummary, wizPaks[i].Name);
			selected++;
		}
	}

	if (selected == 0)
		strcat(wizSummary, "(none -- bare CoCo 3)");
	else if (selected > 1)
		strcat(wizSummary, "\n(these will share a Multi-Pak Interface)");

	strcat(wizSummary, "\n\nClick Launch! to start, or Skip to use defaults.");
}

// MPI only has 4 slots -- once 4 peripherals are checked, disable the rest
// rather than silently dropping a 5th pick later.
static void RefreshPeripheralEnable(void)
{
	int i, selected = 0;

	for (i = 0; i < wizPakCount; i++)
		if (wizChecked[i]) selected++;

	for (i = 0; i < wizPakCount; i++)
	{
		if (wizChecked[i] || selected < MAX_MPI_SLOTS)
			AG_WidgetEnable(wizCheckboxes[i]);
		else
			AG_WidgetDisable(wizCheckboxes[i]);
	}

	UpdateWizardSummary();
}

static void PeripheralToggled(AG_Event *event)
{
	RefreshPeripheralEnable();
}

static void CpuTypeChanged(AG_Event *event)
{
	UpdateWizardSummary();
}

static void RamSizeChanged(AG_Event *event)
{
	UpdateWizardSummary();
}

static int FindPakByName(const char *name)
{
	int i;

	for (i = 0; i < wizPakCount; i++)
		if (strcmp(wizPaks[i].Name, name) == 0)
			return i;

	return -1;
}

static void ApplyPreset(int idx0, int idx1)
{
	int i;

	for (i = 0; i < wizPakCount; i++)
		wizChecked[i] = 0;

	if (idx0 >= 0) wizChecked[idx0] = 1;
	if (idx1 >= 0) wizChecked[idx1] = 1;

	RefreshPeripheralEnable();
}

static void PresetBare(AG_Event *event)
{
	wizCpuType = 0;
	wizRamSize = 1;
	ApplyPreset(-1, -1);
}

static void PresetFloppy(AG_Event *event)
{
	wizCpuType = 0;
	wizRamSize = 1;
	// Name must match FD502/fd502.c's own ModuleName() string; if that pak
	// isn't installed this just quietly leaves nothing checked.
	ApplyPreset(FindPakByName("FD502 26-133"), -1);
}

static void PresetMultiPak(AG_Event *event)
{
	wizCpuType = 0;
	wizRamSize = 2;
	// Names must match FD502/fd502.c and HardDisk/harddisk.c's own
	// ModuleName() strings.
	ApplyPreset(FindPakByName("FD502 26-133"), FindPakByName("Hard Drive + Cloud9 RTC"));
}

// Writes the wizard's picks into CurrentConfig/Vcc.ini and reloads, exactly
// the same way a normal boot does -- see config.c LoadConfig()/InsertModule().
static void ApplyWizardSelections(void)
{
	int i, selectedIdx[MAX_MPI_SLOTS];
	int selectedCount = 0;
	int mpiIdx;

	CurrentConfig.CpuType = (unsigned char)wizCpuType;
	CurrentConfig.RamSize = (unsigned char)wizRamSize;

	for (i = 0; i < wizPakCount && selectedCount < MAX_MPI_SLOTS; i++)
		if (wizChecked[i])
			selectedIdx[selectedCount++] = i;

	if (selectedCount == 0)
	{
		UpdateOnBoot("");
	}
	else if (selectedCount == 1)
	{
		UpdateOnBoot(wizPaks[selectedIdx[0]].Path);
	}
	else
	{
		mpiIdx = FindPakByName("MPI");

		if (mpiIdx < 0)
		{
			// No MPI pak installed -- fall back to the first pick alone so
			// the user still ends up with a working single-device system.
			UpdateOnBoot(wizPaks[selectedIdx[0]].Path);
		}
		else
		{
			char iniPath[MAX_PATH];

			GetIniFilePath(iniPath);
			UpdateOnBoot(wizPaks[mpiIdx].Path);

			// Same [MPI] SWPOSITION/SLOTn schema mpi.c's own WriteConfig()
			// writes (mpi/mpi.c:674-686) -- MPI will read these back itself
			// via SetIniPath()->LoadConfig() when InsertModule() mounts it.
			WritePrivateProfileInt("MPI", "SWPOSITION", 3, iniPath);
			for (i = 0; i < MAX_MPI_SLOTS; i++)
			{
				char slotKey[8];
				sprintf(slotKey, "SLOT%d", i + 1);
				WritePrivateProfileString("MPI", slotKey,
					(i < selectedCount) ? wizPaks[selectedIdx[i]].Path : "", iniPath);
			}
		}
	}

	WriteIniFile();
	LoadConfig(wizState); // Re-reads what we just wrote and calls InsertModule() with it.
}

static void FinishWizard(AG_Event *event)
{
	ApplyWizardSelections();

	if (wizFirstRun)
	{
		extern void FinishBoot(SystemState2 *);
		FinishBoot(wizState);
	}
	else
	{
		wizState->ResetPending = 2;
		wizState->EmulationRunning = TRUE;
	}

	AG_CloseFocusedWindow();
}

static void SkipWizard(AG_Event *event)
{
	extern void FinishBoot(SystemState2 *);

	LoadConfig(wizState);
	FinishBoot(wizState);

	AG_CloseFocusedWindow();
}

void RunStartupWizard(SystemState2 *state, int firstRun)
{
	AG_Window *win;
	AG_Notebook *nb;
	AG_NotebookTab *tab;
	AG_Box *hb, *vbox;
	int i;

	wizState = state;
	wizFirstRun = firstRun;

	if (firstRun)
		ReadIniFile(); // Nothing has been read yet on a true first run; this seeds
		                // CurrentConfig with config.c's real defaults (and iniman/Vcc.ini)
		                // instead of the wizard reading raw zeroed-out globals.

	wizCpuType = (int)CurrentConfig.CpuType;
	wizRamSize = (int)CurrentConfig.RamSize;

	wizPakCount = EnumeratePaks(wizPaks, MAX_WIZ_PAKS);
	for (i = 0; i < wizPakCount; i++)
		wizChecked[i] = 0;

	win = AG_WindowNew(0);
	if (win == NULL)
		return;

	AG_WindowSetGeometryAligned(win, AG_WINDOW_ALIGNMENT_NONE, 520, 420);
	AG_WindowSetCaptionS(win, firstRun ? "Welcome to OVCC!" : "OVCC Setup Wizard");
	AG_WindowSetCloseAction(win, AG_WINDOW_DETACH);

	nb = AG_NotebookNew(win, AG_NOTEBOOK_EXPAND);

	tab = AG_NotebookAdd(nb, "Welcome", AG_BOX_VERT);
	{
		AG_LabelNew(tab, 0, "Let's set up your virtual Color Computer 3!");
		AG_LabelNew(tab, 0, "Pick a quick-start below, or use the tabs to customize.");
		AG_SeparatorNew(tab, AG_SEPARATOR_HORIZ);

		vbox = AG_BoxNewVert(tab, AG_VBOX_HFILL);
		AG_ButtonNewFn(vbox, 0, "Bare CoCo 3 (no cartridge)", PresetBare, NULL);
		AG_ButtonNewFn(vbox, 0, "Floppy Disk System (FD-502)", PresetFloppy, NULL);
		AG_ButtonNewFn(vbox, 0, "Classic Multi-Pak (MPI + Floppy + Hard Disk)", PresetMultiPak, NULL);

		AG_SeparatorNew(tab, AG_SEPARATOR_HORIZ);
		AG_LabelNew(tab, 0, "Want full control? Visit the System and Peripherals tabs.");
	}

	tab = AG_NotebookAdd(nb, "System", AG_BOX_VERT);
	{
		AG_Box *hbox2, *vbox2;
		AG_Radio *radio;

		hbox2 = AG_BoxNewHoriz(tab, AG_HBOX_EXPAND);

		vbox2 = AG_BoxNewVert(hbox2, AG_VBOX_VFILL | AG_BOX_FRAME);
		AG_LabelNew(vbox2, 0, "CPU");
		{
			const char *radioItems[] = { "Motorola MC6809", "Hitachi HD6309", NULL };
			radio = AG_RadioNewFn(vbox2, AG_RADIO_VFILL, radioItems, CpuTypeChanged, NULL);
			AG_BindInt(radio, "value", &wizCpuType);
		}

		vbox2 = AG_BoxNewVert(hbox2, AG_VBOX_VFILL | AG_BOX_FRAME);
		AG_LabelNew(vbox2, 0, "Memory Size");
		{
			const char *radioItems[] = { "128KB", "512KB", "2048KB", "8096KB", NULL };
			radio = AG_RadioNewFn(vbox2, AG_RADIO_VFILL, radioItems, RamSizeChanged, NULL);
			AG_BindInt(radio, "value", &wizRamSize);
		}
	}

	tab = AG_NotebookAdd(nb, "Peripherals", AG_BOX_VERT);
	{
		if (wizPakCount == 0)
		{
			AG_LabelNew(tab, 0, "No pak modules were found -- OVCC will boot a bare CoCo 3.");
		}
		else
		{
			AG_LabelNew(tab, 0, "Choose up to 4 devices (they'll share a Multi-Pak Interface):");
			for (i = 0; i < wizPakCount; i++)
			{
				AG_Checkbox *cb = AG_CheckboxNewFn(tab, 0, wizPaks[i].Name, PeripheralToggled, NULL);
				AG_BindInt(cb, "state", &wizChecked[i]);
				wizCheckboxes[i] = cb;
			}
		}
	}

	tab = AG_NotebookAdd(nb, "Finish", AG_BOX_VERT);
	{
		UpdateWizardSummary();
		AG_LabelNewPolled(tab, AG_LABEL_HFILL | AG_LABEL_FRAME, "%s", wizSummary);

		AG_SeparatorNew(tab, AG_SEPARATOR_HORIZ);

		hb = AG_BoxNewHoriz(tab, AG_BOX_HOMOGENOUS | AG_BOX_HFILL);
		AG_ButtonNewFn(hb, 0, "Launch!", FinishWizard, NULL);
		if (firstRun)
			AG_ButtonNewFn(hb, 0, "Skip / Use Defaults", SkipWizard, NULL);
	}

	AG_WindowShow(win);
}
