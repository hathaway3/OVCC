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
#include <stdio.h>
#include <stdbool.h>
#include "defines.h"
#include "fileops.h"

#ifdef DARWIN
#include <mach-o/dyld.h>
#include <limits.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#elif !defined(__MINGW32__)
#include <unistd.h>
#endif

static char ExecFolder[MAX_PATH];

static void InitExecFolder(void) {
	if (ExecFolder[0] != 0) return;
#ifdef DARWIN
	char exec_path[1024];
	uint32_t size = sizeof(exec_path);
	if (_NSGetExecutablePath(exec_path, &size) == 0) {
		char real_exec_path[1024];
		if (realpath(exec_path, real_exec_path) == NULL) {
			strncpy(real_exec_path, exec_path, sizeof(real_exec_path));
		}
		char temp_path[1024];
		strncpy(temp_path, real_exec_path, sizeof(temp_path));
		char *exec_dir = dirname(temp_path);
		strncpy(ExecFolder, exec_dir, sizeof(ExecFolder));
	} else {
		getcwd(ExecFolder, sizeof(ExecFolder));
	}
#else
	getcwd(ExecFolder, sizeof(ExecFolder));
#endif
}

void ValidatePath(char *Path)
{
	char TempPath[MAX_PATH]="";
	int tpl;

	InitExecFolder();

	strcpy(TempPath,Path);			
	PathRemoveFileSpec(TempPath);		//Get path to Incomming file
	tpl = strlen(ExecFolder);

	if (!strncmp(TempPath, ExecFolder, tpl))	// If they match remove the Path
	{
		strcpy(Path, &(Path[++tpl]));
		//PathStripPath(Path);
	}
	return;
}

int CheckPath( char *Path)	//Return 1 on Error
{
#ifdef DARWIN
	char TempPath[1024];
	if ((strlen(Path)==0) || (strlen(Path) > 1024))
		return(1);
	if (ResolvePlatformPath(Path, TempPath, sizeof(TempPath))) {
		strcpy(Path, TempPath);
		return 0;
	}
	return 1;
#else
	char TempPath[MAX_PATH]="";

	InitExecFolder();

	if ((strlen(Path)==0) | (strlen(Path) > MAX_PATH))
		return(1);
	
	if (!AG_FileExists(Path)) //File Doesn't exist
	{
		strcpy(TempPath, ExecFolder);

		if ( (strlen(TempPath)) + (strlen(Path)) > MAX_PATH)	//Resulting path is to large Bail.
			return(1);

		strcat(TempPath, Path);
		
		if (!AG_FileExists(TempPath))
			return(1);

		strcpy(Path,TempPath);
	}

	return(0);
#endif
}

char GetPathDelim()
{
	static char dirdelim = 0;

	if (dirdelim == 0)
	{
		char *platform = SDL_GetPlatform();

		if (!strcmp(platform, "Windows"))
		{
			dirdelim = '\\';
		}
		else 
		{
			dirdelim = '/';
		}
	}

	return dirdelim;
}

char *GetPathDelimStr()
{
	static char PathDelimString[2] = " ";

	PathDelimString[0] = GetPathDelim();

	return PathDelimString;
}

// These are here to remove dependance on shlwapi.dll. ASCII only
void PathStripPath (char *TextBuffer)
{
	char dirdelim = GetPathDelim();

	char TempBuffer[MAX_PATH] = "";
	short Index = (short)strlen(TextBuffer);

	if ((Index > MAX_PATH) | (Index==0))	//Test for overflow
		return;

	for (; Index >= 0; Index--)
		if (TextBuffer[Index] == dirdelim)
			break;
	
	if (Index < 0)	//delimiter not found
		return;
	strcpy(TempBuffer, &TextBuffer[Index + 1]);
	strcpy(TextBuffer, TempBuffer);
}

BOOL PathRemoveFileSpec(char *Path)
{
	char dirdelim = GetPathDelim();
	int len = (int)strlen(Path);
	if (len == 0 || len > MAX_PATH)
		return false;

	int index = len - 1;
	// Find the last delimiter
	while (index >= 0 && Path[index] != dirdelim) {
		index--;
	}

	if (index < 0) {
		// No directory delimiter found, so no folder path to keep.
		// Leave the path unchanged.
		return false;
	}

	// Remove trailing delimiters
	while (index > 0 && Path[index] == dirdelim) {
		index--;
	}

	Path[index + 1] = '\0';
	return true;
}		

BOOL PathRemoveExtension(char *Path)
{
	int len = (int)strlen(Path);
	if (len == 0 || len > MAX_PATH)
		return false;

	char dirdelim = GetPathDelim();
	int index = len - 1;

	// Search backwards for a dot, but stop if we hit a path delimiter
	while (index >= 0 && Path[index] != '.') {
		if (Path[index] == dirdelim) {
			break;
		}
		index--;
	}

	if (index >= 0 && Path[index] == '.') {
		Path[index] = '\0';
		return true;
	}

	return false;
}

char* PathFindExtension(char *Path)
{
	int len = (int)strlen(Path);
	if (len == 0 || len > MAX_PATH)
		return &Path[len];

	char dirdelim = GetPathDelim();
	int index = len - 1;

	while (index >= 0 && Path[index] != '.') {
		if (Path[index] == dirdelim) {
			break;
		}
		index--;
	}

	if (index >= 0 && Path[index] == '.') {
		return &Path[index];
	}

	return &Path[len]; // Return pointer to null terminator
}

#ifdef DARWIN
int ResolvePlatformPath(const char *filename, char *resolved, size_t max_len) {
    if (!filename || filename[0] == '\0') {
        resolved[0] = '\0';
        return 0;
    }
    // If absolute path
    if (filename[0] == '/') {
        strncpy(resolved, filename, max_len);
        return (access(resolved, F_OK) == 0);
    }

    // Determine the main executable's directory
    char exec_path[1024];
    uint32_t size = sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &size) != 0) {
        return 0;
    }
    
    char real_exec_path[1024];
    if (realpath(exec_path, real_exec_path) == NULL) {
        strncpy(real_exec_path, exec_path, sizeof(real_exec_path));
    }

    char temp_path[1024];
    strncpy(temp_path, real_exec_path, sizeof(temp_path));
    char *exec_dir = dirname(temp_path);

    char bundle_parent[1024] = "";
    char bundle_plugins[1024] = "";
    size_t exec_dir_len = strlen(exec_dir);
    
    if (exec_dir_len > 15 && strcmp(exec_dir + exec_dir_len - 15, "/Contents/MacOS") == 0) {
        strncpy(temp_path, exec_dir, exec_dir_len - 15);
        temp_path[exec_dir_len - 15] = '\0'; // /path/to/ovcc.app
        snprintf(bundle_plugins, sizeof(bundle_plugins), "%s/Contents/PlugIns", temp_path);
        
        char temp_path2[1024];
        strncpy(temp_path2, temp_path, sizeof(temp_path2));
        char *parent = dirname(temp_path2);
        strncpy(bundle_parent, parent, sizeof(bundle_parent));
    } else {
        strncpy(bundle_parent, exec_dir, sizeof(bundle_parent));
        strncpy(bundle_plugins, exec_dir, sizeof(bundle_plugins));
    }

    char app_support[1024] = "";
    char *home = getenv("HOME");
    if (home) {
        snprintf(app_support, sizeof(app_support), "%s/Library/Application Support/OVCC", home);
        struct stat st = {0};
        if (stat(app_support, &st) == -1) {
            mkdir(app_support, 0755);
        }
    } else {
        strncpy(app_support, exec_dir, sizeof(app_support));
    }

    const char *fn = filename;
    if (strncmp(fn, "modules/", 8) == 0) {
        fn += 8;
    } else if (strncmp(fn, "./modules/", 10) == 0) {
        fn += 10;
    } else if (strncmp(fn, "./", 2) == 0) {
        fn += 2;
    }

    // 1. App support directory
    snprintf(resolved, max_len, "%s/%s", app_support, fn);
    if (access(resolved, F_OK) == 0) return 1;

    // 2. Bundle PlugIns directory
    snprintf(resolved, max_len, "%s/%s", bundle_plugins, fn);
    if (access(resolved, F_OK) == 0) return 1;

    // 3. Bundle parent directory
    snprintf(resolved, max_len, "%s/%s", bundle_parent, fn);
    if (access(resolved, F_OK) == 0) return 1;

    // 4. Executable directory
    snprintf(resolved, max_len, "%s/%s", exec_dir, fn);
    if (access(resolved, F_OK) == 0) return 1;

    // 5. Current directory
    strncpy(resolved, fn, max_len);
    if (access(resolved, F_OK) == 0) return 1;

    // Fallback
    snprintf(resolved, max_len, "%s/%s", app_support, fn);
    return 0;
}
#else
int ResolvePlatformPath(const char *filename, char *resolved, size_t max_len) {
    if (!filename || filename[0] == '\0') {
        resolved[0] = '\0';
        return 0;
    }
    // Check if absolute path (Linux or Windows drive letter)
    if (filename[0] == '/' || (filename[0] != '\0' && filename[1] == ':')) {
        strncpy(resolved, filename, max_len);
        return (access(resolved, F_OK) == 0);
    }
    
    // Otherwise fallback to ExecFolder-relative or current directory
    if (ExecFolder[0] != '\0') {
        snprintf(resolved, max_len, "%s%c%s", ExecFolder, GetPathDelim(), filename);
        if (access(resolved, F_OK) == 0) return 1;
    }
    
    strncpy(resolved, filename, max_len);
    return (access(resolved, F_OK) == 0);
}
#endif

// Rewrites an absolute path in place to be relative to one of the same
// bundle-relative roots ResolvePlatformPath() searches (Application Support,
// Contents/PlugIns, executable directory), so a saved Module/OnBoot path
// keeps working after the .app bundle is moved or renamed. Paths outside all
// of those roots (e.g. a module the user picked from elsewhere on disk) are
// left untouched, since there's no bundle-relative form for them.
#ifdef DARWIN
void MakeModulePathPortable(char *Path)
{
    if (Path[0] != '/')
        return; // Already relative; nothing to do.

    char exec_path[1024];
    uint32_t size = sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &size) != 0)
        return;

    char real_exec_path[1024];
    if (realpath(exec_path, real_exec_path) == NULL) {
        strncpy(real_exec_path, exec_path, sizeof(real_exec_path));
    }

    char temp_path[1024];
    strncpy(temp_path, real_exec_path, sizeof(temp_path));
    char *exec_dir = dirname(temp_path);
    size_t exec_dir_len = strlen(exec_dir);

    char bundle_plugins[1024] = "";
    if (exec_dir_len > 15 && strcmp(exec_dir + exec_dir_len - 15, "/Contents/MacOS") == 0) {
        char bundle_path[1024];
        strncpy(bundle_path, exec_dir, exec_dir_len - 15);
        bundle_path[exec_dir_len - 15] = '\0';
        snprintf(bundle_plugins, sizeof(bundle_plugins), "%s/Contents/PlugIns", bundle_path);
    }

    char app_support[1024] = "";
    char *home = getenv("HOME");
    if (home) {
        snprintf(app_support, sizeof(app_support), "%s/Library/Application Support/OVCC", home);
    }

    const char *roots[] = { app_support, bundle_plugins, exec_dir };
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        size_t root_len = strlen(roots[i]);
        if (root_len == 0)
            continue;
        if (strncmp(Path, roots[i], root_len) == 0 && Path[root_len] == '/') {
            char remainder[MAX_PATH];
            strncpy(remainder, Path + root_len + 1, sizeof(remainder));
            strcpy(Path, remainder);
            return;
        }
    }
    // Not under any known bundle-relative root; leave it as an absolute path.
}
#else
void MakeModulePathPortable(char *Path)
{
    (void)Path;
}
#endif

// Where the Startup Wizard (wizard.c) looks for pak module shared libraries to
// offer as peripheral choices. Mirrors the directories each module's own
// Makefile installs into (see Makefile.common's LIBDIR and the Darwin module
// makefiles' "install" targets), not just an arbitrary guess.
#ifdef DARWIN
int GetPakSearchDir(char *out, size_t max_len)
{
    char exec_path[1024];
    uint32_t size = sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &size) != 0) {
        out[0] = '\0';
        return 0;
    }

    char real_exec_path[1024];
    if (realpath(exec_path, real_exec_path) == NULL) {
        strncpy(real_exec_path, exec_path, sizeof(real_exec_path));
    }

    char temp_path[1024];
    strncpy(temp_path, real_exec_path, sizeof(temp_path));
    char *exec_dir = dirname(temp_path);
    size_t exec_dir_len = strlen(exec_dir);

    if (exec_dir_len > 15 && strcmp(exec_dir + exec_dir_len - 15, "/Contents/MacOS") == 0) {
        char bundle_path[1024];
        strncpy(bundle_path, exec_dir, exec_dir_len - 15);
        bundle_path[exec_dir_len - 15] = '\0';
        snprintf(out, max_len, "%s/Contents/PlugIns", bundle_path);
    } else {
        // Not running from a .app bundle (e.g. a dev build); fall back to
        // wherever the executable itself lives.
        strncpy(out, exec_dir, max_len);
    }
    return 1;
}
#else
int GetPakSearchDir(char *out, size_t max_len)
{
    InitExecFolder();
#ifndef __MINGW32__
    // Linux: prefer the location `make install` actually populates
    // (Makefile.common: LIBDIR = $(prefix)/lib/ovcc) over a dev-build guess.
    if (access("/usr/local/lib/ovcc", F_OK) == 0) {
        strncpy(out, "/usr/local/lib/ovcc", max_len);
        return 1;
    }
#endif
    snprintf(out, max_len, "%s%c%s", ExecFolder, GetPathDelim(), "libs");
    return 1;
}
#endif