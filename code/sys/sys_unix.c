/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#define _FILE_OFFSET_BITS 64

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"

#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <pwd.h>
#include <libgen.h>
#include <fcntl.h>
#include <fenv.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/resource.h>

qboolean stdinIsATTY;

static char execBuffer[ 1024 ];
static char *execBufferPointer;
static char *execArgv[ 16 ];
static int execArgc;

/*
==============
Sys_ClearExecBuffer
==============
*/
static void Sys_ClearExecBuffer( void )
{
	execBufferPointer = execBuffer;
	Com_Memset( execArgv, 0, sizeof( execArgv ) );
	execArgc = 0;
}

/*
==============
Sys_AppendToExecBuffer
==============
*/
static void Sys_AppendToExecBuffer( const char *text )
{
	size_t size = sizeof( execBuffer ) - ( execBufferPointer - execBuffer );
	int length = strlen( text ) + 1;

	if( length > size || execArgc >= ARRAY_LEN( execArgv ) )
		return;

	Q_strncpyz( execBufferPointer, text, size );
	execArgv[ execArgc++ ] = execBufferPointer;

	execBufferPointer += length;
}

/*
==============
Sys_Exec
==============
*/
static int Sys_Exec( void )
{
	pid_t pid = fork( );

	if( pid < 0 )
		return -1;

	if( pid )
	{
		// Parent
		int exitCode;

		wait( &exitCode );

		return WEXITSTATUS( exitCode );
	}
	else
	{
		// Child
		execvp( execArgv[ 0 ], execArgv );

		// Failed to execute
		exit( -1 );

		return -1;
	}
}

#ifdef __APPLE__

/*
==================
Sys_DefaultHomePath
==================
*/
static char *Sys_DefaultHomePath(void)
{
	static char homePath[ MAX_OSPATH ] = { 0 };
	char *p;

	if( !*homePath )
	{
		if( ( p = getenv( "HOME" ) ) != NULL )
		{
			Com_sprintf( homePath, sizeof(homePath), "%s%c%s",
				p, PATH_SEP, "Library/Application Support/" );

			if( com_homepath && com_homepath->string[0] )
				Q_strcat(homePath, sizeof(homePath), com_homepath->string);
			else
				Q_strcat(homePath, sizeof(homePath), HOMEPATH_NAME);
		}
	}

	return homePath;
}

char *Sys_DefaultHomeConfigPath(void) { return Sys_DefaultHomePath(); }
char *Sys_DefaultHomeDataPath(void)   { return Sys_DefaultHomePath(); }
char *Sys_DefaultHomeStatePath(void)  { return Sys_DefaultHomePath(); }

#else // __APPLE__

/*
==================
Sys_HomeConfigPath
==================
*/
char *Sys_HomeConfigPath(void)
{
	static char homeConfigPath[ MAX_OSPATH ] = { 0 };
	char *p;

	if( !*homeConfigPath )
	{
		if( ( p = getenv( "XDG_CONFIG_HOME" ) ) != NULL && *p != '\0' )
			Com_sprintf(homeConfigPath, sizeof(homeConfigPath), "%s%c", p, PATH_SEP);
		else if( ( p = getenv( "HOME" ) ) != NULL && *p != '\0' )
			Com_sprintf(homeConfigPath, sizeof(homeConfigPath), "%s%c.config%c", p, PATH_SEP, PATH_SEP);

		if( *homeConfigPath )
		{
			if( com_homepath && com_homepath->string[0] )
				Q_strcat(homeConfigPath, sizeof(homeConfigPath), com_homepath->string);
			else
				Q_strcat(homeConfigPath, sizeof(homeConfigPath), HOMEPATH_NAME);
		}
	}

	return homeConfigPath;
}

/*
==================
Sys_HomeDataPath
==================
*/
char *Sys_HomeDataPath(void)
{
	static char homeDataPath[ MAX_OSPATH ] = { 0 };
	char *p;

	if( !*homeDataPath )
	{
		if( ( p = getenv( "XDG_DATA_HOME" ) ) != NULL && *p != '\0' )
			Com_sprintf(homeDataPath, sizeof(homeDataPath), "%s%c", p, PATH_SEP);
		else if( ( p = getenv( "HOME" ) ) != NULL && *p != '\0' )
			Com_sprintf(homeDataPath, sizeof(homeDataPath), "%s%c.local%cshare%c", p, PATH_SEP, PATH_SEP, PATH_SEP);

		if( *homeDataPath )
		{
			if( com_homepath && com_homepath->string[0] )
				Q_strcat(homeDataPath, sizeof(homeDataPath), com_homepath->string);
			else
				Q_strcat(homeDataPath, sizeof(homeDataPath), HOMEPATH_NAME);
		}
	}

	return homeDataPath;
}

/*
==================
Sys_HomeStatePath
==================
*/
char *Sys_HomeStatePath(void)
{
	static char homeStatePath[ MAX_OSPATH ] = { 0 };
	char *p;

	if( !*homeStatePath )
	{
		if( ( p = getenv( "XDG_STATE_HOME" ) ) != NULL && *p != '\0' )
			Com_sprintf(homeStatePath, sizeof(homeStatePath), "%s%c", p, PATH_SEP);
		else if( ( p = getenv( "HOME" ) ) != NULL && *p != '\0' )
			Com_sprintf(homeStatePath, sizeof(homeStatePath), "%s%c.local%cstate%c", p, PATH_SEP, PATH_SEP, PATH_SEP);

		if( *homeStatePath )
		{
			if( com_homepath && com_homepath->string[0] )
				Q_strcat(homeStatePath, sizeof(homeStatePath), com_homepath->string);
			else
				Q_strcat(homeStatePath, sizeof(homeStatePath), HOMEPATH_NAME);
		}
	}

	return homeStatePath;
}

/*
==================
Sys_LegacyHomePath
==================
*/
static char *Sys_LegacyHomePath(void)
{
	static char homePath[ MAX_OSPATH ] = { 0 };
	char *p;

	if( ( p = getenv( "FLATPAK_ID" ) ) != NULL && *p != '\0' )
	{
		// Flatpaks always use XDG
		return "";
	}

	if( !*homePath )
	{
		if( ( p = getenv( "HOME" ) ) != NULL && *p != '\0' )
		{
			Com_sprintf(homePath, sizeof(homePath), "%s%c%s",
				p, PATH_SEP, HOMEPATH_NAME_UNIX_LEGACY);
		}
	}

	return homePath;
}

/*
==================
Sys_MigrateToXDG
==================
*/
qboolean Sys_MigrateToXDG(void)
{
	const char *scriptTemplate =
		"#!/bin/sh\n"

		"set -eu\n"

		"legacy_home=\"%s\"\n"
		"xdg_config_home=\"%s\"\n"
		"xdg_data_home=\"%s\"\n"
		"xdg_state_home=\"%s\"\n"

		"xdg_config_pattern=\"*.cfg\"\n"
		"xdg_data_pattern=\"demos/*.dm_* *.log *.pk3 *.txt \\\n"
		"    screenshots/*.jpg screenshots/*.tga videos/*.avi\"\n"
		"xdg_state_pattern=\"*.dat q3history q3key\"\n"

		"glob_copy() {\n"
		"    game_dir=${1:+$1/}\n"
		"    dst=\"$2\"\n"
		"    shift 2\n"
		"    for pattern in \"$@\"; do\n"
		"        subdir=$(dirname \"$pattern\")\n"
		"        [ \"$subdir\" = \".\" ] && subdir=\"\"\n"
		"        find \"$legacy_home/$game_dir\" \\\n"
		"            -path \"$legacy_home/$game_dir$pattern\" -type f \\\n"
		"            -exec mkdir -p \"$dst/$game_dir$subdir\" \\; \\\n"
		"            -exec cp -av {} \"$dst/$game_dir$subdir\" \\;\n"
		"    done\n"
		"}\n"

		"unmatched_copy() {\n"
		"    game_dir=${1:+$1/}\n"
		"    shift\n"
		"    find_args=\"\"\n"
		"    for pattern in \"$@\"; do\n"
		"        find_args=\"$find_args \\\n"
		"            -not -path \\\"$legacy_home/$game_dir$pattern\\\"\"\n"
		"    done\n"
		"    eval \"find '$legacy_home/$game_dir' -type f $find_args\" | \\\n"
		"    while IFS= read -r file; do\n"
		"        dst=\"$xdg_data_home${file#$legacy_home}\"\n"
		"        dst_dir=$(dirname \"$dst\")\n"
		"        mkdir -p \"$dst_dir\"\n"
		"        cp -av \"$file\" \"$dst\"\n"
		"    done\n"
		"}\n"

		"echo \"Starting XDG migration...\"\n"

		"glob_copy \"\" \"$xdg_state_home\" \"qkey\"\n"

		"for game_dir in \"$legacy_home\"/*; do\n"
		"    [ -d \"$game_dir\" ] || continue\n"
		"    game=$(basename \"$game_dir\")\n"
		"    glob_copy \"$game\" \"$xdg_config_home\" $xdg_config_pattern\n"
		"    glob_copy \"$game\" \"$xdg_data_home\" $xdg_data_pattern\n"
		"    glob_copy \"$game\" \"$xdg_state_home\" $xdg_state_pattern\n"
		"    unmatched_copy \"$game\" \\\n"
		"        $xdg_config_pattern \\\n"
		"        $xdg_data_pattern \\\n"
		"        $xdg_state_pattern\n"
		"done\n"

		"echo \"XDG migration complete!\"\n";

	char scriptBuffer[2048];
	int len = Com_sprintf( scriptBuffer, sizeof( scriptBuffer ), scriptTemplate,
		Sys_LegacyHomePath( ), Sys_HomeConfigPath( ),
		Sys_HomeDataPath( ), Sys_HomeStatePath( ) );

	if( len < 0 || len >= (int)sizeof( scriptBuffer ) )
	{
		Com_Printf( "XDG migration error: substitution failed.\n" );
		return qfalse;
	}

	char scriptPath[] = "/tmp/xdgmigrationXXXXXX";
	int fd = mkstemp( scriptPath );
	if( fd == -1 )
	{
		Com_Printf( "XDG migration error: script creation failed.\n" );
		return qfalse;
	}

	if( write( fd, scriptBuffer, len ) != len )
	{
		close( fd );
		unlink( scriptPath );
		Com_Printf( "XDG migration error: script write failed.\n" );
		return qfalse;
	}
	close( fd );

	if( chmod( scriptPath, 0700 ) == -1 )
	{
		unlink( scriptPath );
		Com_Printf( "XDG migration error: script chmod failed.\n" );
		return qfalse;
	}

	Sys_ClearExecBuffer( );
	Sys_AppendToExecBuffer( scriptPath );
	int result = Sys_Exec( );
	unlink( scriptPath );

	return result == 0;
}

/*
==================
Sys_ShouldUseLegacyHomePath
==================
*/
static qboolean Sys_ShouldUseLegacyHomePath(void)
{
	if( access( Sys_HomeConfigPath( ), F_OK ) == 0 )
	{
		// If the XDG config directory exists, prefer XDG layout, regardless
		return qfalse;
	}

	if( ( com_homepath && com_homepath->string[0] ) ||
		Cvar_VariableString( "fs_homepath" )[0] )
	{
		// If a custom homepath has been explicity set then
		// that strongly implies that migration isn't desired
		return qfalse;
	}

	const char *legacyHomePath = Sys_LegacyHomePath();

	if( !*legacyHomePath || access( legacyHomePath, F_OK ) != 0 )
	{
		// The legacy home path doesn't exist
		return qfalse;
	}

	char migrationRefusedPath[ MAX_OSPATH ];
	Com_sprintf( migrationRefusedPath, sizeof( migrationRefusedPath ),
		"%s/.xdgMigrationRefused", legacyHomePath );

	// If the user hasn't already refused, ask if they want to migrate
	if( access( migrationRefusedPath, F_OK ) != 0 )
	{
		dialogResult_t result = Sys_Dialog( DT_YES_NO, va(
			"Modern games and applications store files in "
			"directories according to the Free Desktop standard. "
			"Here's what that would look like for %s:\n\n"
			"Configuration files:\n  %s\n\n"
			"Data files; pk3s, screenshots, logs, demos, etc.:\n  %s\n\n"
			"Internal runtime files:\n  %s\n\n"
			"At the moment all of these files are found here:\n  %s\n\n"
			"Do you want to copy your files to these new directories?",
			PRODUCT_NAME,
			Sys_HomeConfigPath( ), Sys_HomeDataPath( ), Sys_HomeStatePath( ),
			legacyHomePath ),
			"Home Directory Files Upgrade" );

		if( result == DR_YES )
			return !Sys_MigrateToXDG( );

		// Guard against asking again in future
		fclose( fopen( migrationRefusedPath, "w" ) );
	}

	return qtrue;
}

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_DefaultHomeConfigPath
==================
*/
char *Sys_DefaultHomeConfigPath(void)
{
	if( Sys_ShouldUseLegacyHomePath( ) )
		return Sys_LegacyHomePath( );

	return Sys_HomeConfigPath( );
}

/*
==================
Sys_DefaultHomeDataPath
==================
*/
char *Sys_DefaultHomeDataPath(void)
{
	if( Sys_ShouldUseLegacyHomePath( ) )
		return Sys_LegacyHomePath( );

	return Sys_HomeDataPath( );
}

/*
==================
Sys_DefaultHomeStatePath
==================
*/
char *Sys_DefaultHomeStatePath(void)
{
	if( Sys_ShouldUseLegacyHomePath( ) )
		return Sys_LegacyHomePath( );

	return Sys_HomeStatePath( );
}
#endif

#endif

#ifndef GODOT_GDEXTENSION
/*
================
Sys_SteamPath
================
*/
char *Sys_SteamPath( void )
{
	// Steam doesn't let you install Quake 3 on Mac/Linux
	return "";
}

/*
================
Sys_GogPath
================
*/
char *Sys_GogPath( void )
{
	// GOG doesn't let you install Quake 3 on Mac/Linux
	return "";
}

/*
================
Sys_MicrosoftStorePath
================
*/
char* Sys_MicrosoftStorePath(void)
{
	// Microsoft Store doesn't exist on Mac/Linux
	return "";
}
#endif


#ifndef GODOT_GDEXTENSION
/*
================
Sys_Milliseconds
================
*/
/* base time in seconds, that's our origin
   timeval:tv_sec is an int:
   assuming this wraps every 0x7fffffff - ~68 years since the Epoch (1970) - we're safe till 2038 */
unsigned long sys_timeBase = 0;
/* current time in ms, using sys_timeBase as origin
   NOTE: sys_timeBase*1000 + curtime -> ms since the Epoch
     0x7fffffff ms - ~24 days
   although timeval:tv_usec is an int, I'm not sure whether it is actually used as an unsigned int
     (which would affect the wrap period) */
int curtime;
int Sys_Milliseconds (void)
{
	struct timeval tp;

	gettimeofday(&tp, NULL);

	if (!sys_timeBase)
	{
		sys_timeBase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	curtime = (tp.tv_sec - sys_timeBase)*1000 + tp.tv_usec/1000;

	return curtime;
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_RandomBytes
==================
*/
qboolean Sys_RandomBytes( byte *string, int len )
{
	FILE *fp;

	fp = fopen( "/dev/urandom", "r" );
	if( !fp )
		return qfalse;

	setvbuf( fp, NULL, _IONBF, 0 ); // don't buffer reads from /dev/urandom

	if( fread( string, sizeof( byte ), len, fp ) != len )
	{
		fclose( fp );
		return qfalse;
	}

	fclose( fp );
	return qtrue;
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_GetCurrentUser
==================
*/
char *Sys_GetCurrentUser( void )
{
	struct passwd *p;

	if ( (p = getpwuid( getuid() )) == NULL ) {
		return "player";
	}
	return p->pw_name;
}
#endif

#define MEM_THRESHOLD 96*1024*1024

/*
==================
Sys_LowPhysicalMemory

TODO
==================
*/
qboolean Sys_LowPhysicalMemory( void )
{
	return qfalse;
}

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_Basename
==================
*/
const char *Sys_Basename( char *path )
{
	return basename( path );
}

/*
==================
Sys_Dirname
==================
*/
const char *Sys_Dirname( char *path )
{
	return dirname( path );
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==============
Sys_FOpen
==============
*/
FILE *Sys_FOpen( const char *ospath, const char *mode ) {
	struct stat buf;

	// check if path exists and is a directory
	if ( !stat( ospath, &buf ) && S_ISDIR( buf.st_mode ) )
		return NULL;

	return fopen( ospath, mode );
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_Mkdir
==================
*/
qboolean Sys_Mkdir( const char *path )
{
	int result = mkdir( path, 0750 );

	if( result != 0 )
		return errno == EEXIST;

	return qtrue;
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_Mkfifo
==================
*/
FILE *Sys_Mkfifo( const char *ospath )
{
	FILE	*fifo;
	int	result;
	int	fn;
	struct	stat buf;

	// if file already exists AND is a pipefile, remove it
	if( !stat( ospath, &buf ) && S_ISFIFO( buf.st_mode ) )
		FS_Remove( ospath );

	result = mkfifo( ospath, 0600 );
	if( result != 0 )
		return NULL;

	fifo = fopen( ospath, "w+" );
	if( fifo )
	{
		fn = fileno( fifo );
		fcntl( fn, F_SETFL, O_NONBLOCK );
	}

	return fifo;
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_Cwd
==================
*/
char *Sys_Cwd( void )
{
	static char cwd[MAX_OSPATH];

	char *result = getcwd( cwd, sizeof( cwd ) - 1 );
	if( result != cwd )
		return NULL;

	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_ListFiles
==================
*/
char **Sys_ListFiles(const char *directory, const char *extension, const char *filter, int *numfiles, qboolean wantsubs)
...
    return listCopy;
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==================
Sys_FreeFileList
==================
*/
void Sys_FreeFileList( char **list )
{
	int i;

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==============
Sys_GLimpInit

Unix specific GL implementation initialisation
==============
*/
void Sys_GLimpInit( void )
{
	// NOP
}
#endif

void Sys_SetFloatEnv(void)
{
	// rounding toward nearest
	fesetround(FE_TONEAREST);
}

#ifndef GODOT_GDEXTENSION
/*
==============
Sys_PlatformInit

Unix specific initialisation
==============
*/
void Sys_PlatformInit( void )
{
	const char* term = getenv( "TERM" );

	signal( SIGHUP, Sys_SigHandler );
	signal( SIGQUIT, Sys_SigHandler );
	signal( SIGTRAP, Sys_SigHandler );
	signal( SIGABRT, Sys_SigHandler );
	signal( SIGBUS, Sys_SigHandler );

	Sys_SetFloatEnv();

	stdinIsATTY = isatty( STDIN_FILENO ) &&
		!( term && ( !strcmp( term, "raw" ) || !strcmp( term, "dumb" ) ) );
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==============
Sys_SetEnv

set/unset environment variables (empty value removes it)
==============
*/

void Sys_SetEnv(const char *name, const char *value)
{
	if(value && *value)
		setenv(name, value, 1);
	else
		unsetenv(name);
}
#endif

#ifndef GODOT_GDEXTENSION
/*
==============
Sys_PID
==============
*/
int Sys_PID( void )
{
	return getpid( );
}
#endif

/*
==============
Sys_PIDIsRunning
==============
*/
qboolean Sys_PIDIsRunning( int pid )
{
	return kill( pid, 0 ) == 0;
}

#ifndef GODOT_GDEXTENSION
/*
=================
Sys_DllExtension

Check if filename should be allowed to be loaded as a DLL.
=================
*/
qboolean Sys_DllExtension( const char *name ) {
...
	return qfalse;
}
#endif


/*
==============
Sys_OpenFolderInPlatformFileManager
==============
*/
qboolean Sys_OpenFolderInPlatformFileManager( const char *path )
{
	Sys_ClearExecBuffer( );

#ifdef __APPLE__
	Sys_AppendToExecBuffer( "open" );
#else
	Sys_AppendToExecBuffer( "xdg-open" );
#endif

	Sys_AppendToExecBuffer( path );

	return Sys_Exec( ) == 0;
}

/*
=================
Sys_SetMaxFileLimit
=================
*/
qboolean Sys_SetMaxFileLimit( void )
{
#ifdef RLIMIT_NOFILE
	struct rlimit limit;

	// Get the current open file limit
	if( getrlimit( RLIMIT_NOFILE, &limit ) == 0 )
	{
		// Set the file limit to the maximum
		limit.rlim_cur = limit.rlim_max;
		if( setrlimit( RLIMIT_NOFILE, &limit ) == 0 )
			return qtrue;
		else
			Com_DPrintf( S_COLOR_YELLOW "WARNING: setrlimit (rlim_max) failed\n" );

#ifdef OPEN_MAX
		// On older macOS versions an error can happen trying to set a file limit above
		// OPEN_MAX. If we see an error, then try again with OPEN_MAX as the limit.
		limit.rlim_cur = OPEN_MAX;
		if( setrlimit( RLIMIT_NOFILE, &limit ) == 0 )
			return qtrue;
		else
			Com_DPrintf( S_COLOR_YELLOW "WARNING: setrlimit (OPEN_MAX) failed\n" );
#endif
	}
	else
		Com_DPrintf( S_COLOR_YELLOW "WARNING: getrlimit failed\n" );

#endif // RLIMIT_NOFILE

	return qfalse;
}
