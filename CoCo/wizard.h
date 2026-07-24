#ifndef __WIZARD_H__
#define __WIZARD_H__
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

#ifdef __cplusplus
extern "C" {
#endif

// Shows the Startup Wizard window (CPU/RAM/peripheral picker).
//
// firstRun = 1: boot-time flow -- the wizard's own Finish/Skip buttons write
//   Vcc.ini and then call FinishBoot() themselves (see vcc.c), since nothing
//   has booted yet.
// firstRun = 0: menu-triggered flow (Configuration -> "Setup Wizard...") --
//   Finish hot-swaps the already-running config instead of booting.
void RunStartupWizard(SystemState2 *state, int firstRun);

#ifdef __cplusplus
}
#endif

#endif
