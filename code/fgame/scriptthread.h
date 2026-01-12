/*
===========================================================================
Copyright (C) 2023 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#pragma once

#include "../corepp/class.h"
#include "../corepp/listener.h"
#include "scriptvm.h"

// Forward declaration
class ScriptMaster;

class ScriptThread : public Listener
{
    friend class ScriptVM;
    friend class ScriptMaster;
    friend class Flag;
    friend class EndOn;
    friend class Listener;

private:
    ScriptVM             *m_ScriptVM;
    SafePtr<ScriptThread> m_WaitingContext;

    bool CanScriptTracePrint(void);

    void ScriptExecute(ScriptVariable *data, int dataSize, ScriptVariable& returnValue);
    void ScriptExecuteInternal(ScriptVariable *data = NULL, int dataSize = 0);

protected:
    void Getcvar(Event *ev);
    void GetRandomFloat(Event *ev);
    void GetRandomInt(Event *ev);
    void GetAbs(Event *ev);
    void GetSelf(Event *ev);
    //////////
    // Added in 2.30
    void EventCos(Event *ev);
    void EventSin(Event *ev);
    void EventTan(Event *ev);
    void EventATan(Event *ev);
    void EventSqrt(Event *ev);
    //////////
    void Vector_Length(Event *ev);
    void Vector_Normalize(Event *ev);
    void Vector_Add(Event *ev);
    void Vector_Subtract(Event *ev);
    void Vector_Scale(Event *ev);
    void Vector_DotProduct(Event *ev);
    void Vector_CrossProduct(Event *ev);
    void Vector_ToAngles(Event *ev);
    void EventAIsCloserThanBToC(Event *ev);
    void EventPointsWithinDist(Event *ev);
    void Angles_ToForward(Event *ev);
    void Angles_ToLeft(Event *ev);
    void Angles_ToUp(Event *ev);
    void Angles_PointAt(Event *ev);
    void CreateThread(Event *ev);
    void CreateReturnThread(Event *ev);
    void ExecuteScript(Event *ev);
    void ExecuteReturnScript(Event *ev);
    void EventGoto(Event *ev);
    void EventThrow(Event *ev);
    void EventDelayThrow(Event *ev);
    void EventWait(Event *ev);
    void EventWaitFrame(Event *ev);
    void EventResume(Event *ev);
    void EventPause(Event *ev);
    void EventEnd(Event *ev);
    void Print(Event *ev);
    void Println(Event *ev);
    void DPrintln(Event *ev); // Added in 2.0
    void IPrintln(Event *ev);
    void IPrintlnBold(Event *ev);
    void IPrintln_NoLoc(Event *ev);
    void IPrintlnBold_NoLoc(Event *ev);
    void MPrint(Event *ev);
    void MPrintln(Event *ev);
    void Assert(Event *ev);
    void CastInt(Event *ev);
    void CastFloat(Event *ev);
    void CastString(Event *ev);
    void CastBoolean(Event *ev);
    void CastEntity(Event *ev);
    void TriggerEvent(Event *ev);
    void ServerEvent(Event *ev);
    void StuffCommand(Event *ev);
    void Spawn(Event *ev);
    void SpawnReturn(Event *ev);
    Listener *SpawnInternal(Event *ev);
    void MapEvent(Event *ev);
    void RegisterAliasAndCache(Event *ev);
    void CacheResourceEvent(Event *ev);
    void SetCvarEvent(Event *ev);
    void EventDebugLine(Event *ev); // Added in 2.0
    void CueCamera(Event *ev);
    void CuePlayer(Event *ev);
    void FreezePlayer(Event *ev);
    void ReleasePlayer(Event *ev);
    void FadeIn(Event *ev);
    void FadeOut(Event *ev);
    void FadeSound(Event *ev);
    void ClearFade(Event *ev);
    void Letterbox(Event *ev);
    void ClearLetterbox(Event *ev);
    void MusicEvent(Event *ev);
    void ForceMusicEvent(Event *ev);
    void MusicVolumeEvent(Event *ev);
    void RestoreMusicVolumeEvent(Event *ev);
    void SoundtrackEvent(Event *ev);
    void RestoreSoundtrackEvent(Event *ev);
    void CameraCommand(Event *ev);
    void SetCinematic(Event *ev);
    void SetNonCinematic(Event *ev);
    void SetAllAIOff(Event *ev);
    void SetAllAIOn(Event *ev);
    void KillEnt(Event *ev);
    void GetEntByEntnum(Event *ev); // Added in 2.30
    void RemoveEnt(Event *ev);
    void KillClass(Event *ev);
    void RemoveClass(Event *ev);
    void SetLightStyle(Event *ev);
    void CenterPrint(Event *ev);
    void LocationPrint(Event *ev);
    void MissionFailed(Event *ev);
    void EventIsAlive(Event *ev);
    void EventPopmenu(Event *ev);
    void EventShowmenu(Event *ev);
    void EventHidemenu(Event *ev);
    void EventPlayMovie(Event *ev); // Added in 2.0
    void EventPushmenu(Event *ev);
    void EventHideMouse(Event *ev);
    void EventCreateListener(Event *ev);
    void EventTrace(Event *ev);
    void EventSightTrace(Event *ev);
    void EventPrint3D(Event *ev);
    void EventHudDrawShader(Event *ev);
    void EventHudDrawAlign(Event *ev);
    void EventHudDrawRect(Event *ev);
    void EventHudDrawVirtualSize(Event *ev);
    void EventHudDrawColor(Event *ev);
    void EventHudDrawAlpha(Event *ev);
    void EventHudDrawString(Event *ev);
    void EventHudDrawFont(Event *ev);
    void EventError(Event *ev);
    void EventDebugInt3(Event *ev);
    void EventTimeout(Event *ev);
    void EventRadiusDamage(Event *ev);
    void EventLandmineDamage(Event *ev); // Added in 2.30
    void EventBspTransition(Event *ev);
    void EventLevelTransition(Event *ev);
    void EventMissionTransition(Event *ev);
    void EventEarthquake(Event *ev);
    void EventTeamWin(Event *ev);
    void EventStopTeamRespawn(Event *ev); // Added in 2.30
    void EventGetBoundKey1(Event *ev);
    void EventGetBoundKey2(Event *ev);
    void EventLocConvertString(Event *ev);
    void EventSetScoreboardToggle(Event *ev); // Added in 2.30
    void EventAddObjective(Event *ev);
    void EventSetCurrentObjective(Event *ev);
    void SetObjectiveLocation(Event *ev);
    void ClearObjectiveLocation(Event *ev);
    void EventDrawHud(Event *ev);
    void EventRegisterCommand(Event *ev);

    //
    // Objectives
    //
    static void AddObjective(int index, int status, str text, Vector location);
    static void SetCurrentObjective(int iObjective, int iTeam);
    void        SetObjectiveLocation(Vector vLocation);
    void        ClearObjectiveLocation(void);
    void        SendObjective();
    void        SendObjectives();
    void        ClearObjectives();

    void WaitSkip(Event *ev);
    void ThreadSkip(Event *ev);

    //
    // Openmohaa addition
    //
    void DelayExecute(Event& ev);
    void DelayExecute(Event *ev = NULL);

    int GetThreadState(void);

    void CancelWaiting(Event *ev);

    void RestoreSound(Event *ev);
    void EventHudDraw3d(Event *ev);
    void EventHudDrawTimer(Event *ev);
    void CanSwitchTeams(Event *ev);
    void RemoveArchivedClass(Event *ev);
    void Earthquake(Event *ev);
    void GetPlayerNetname(Event *ev);
    void GetPlayerIP(Event *ev);
    void ServerStufftext(Event *ev);
    void GetAreaEntities(Event *ev);
    void GetPlayerPing(Event *ev);
    void GetPlayerClientNum(Event *ev);
    void EventIHudDraw3d(Event *ev);
    void EventIHudDrawAlign(Event *ev);
    void EventIHudDrawAlpha(Event *ev);
    void EventIHudDrawColor(Event *ev);
    void EventIHudDrawFont(Event *ev);
    void EventIHudDrawRect(Event *ev);
    void EventIHudDrawShader(Event *ev);
    void EventIHudDrawString(Event *ev);
    void EventIHudDrawTimer(Event *ev);
    void EventIHudDrawVirtualSize(Event *ev);
    void EventInfo_ValueForKey(Event *ev);
    void EventIsArray(Event *ev);
    void EventIsDefined(Event *ev);
    void EventIsOnGround(Event *ev);
    void EventIsOutOfBounds(Event *ev);
    void FileOpen(Event *ev);
    void FileWrite(Event *ev);
    void FileRead(Event *ev);
    void FileClose(Event *ev);
    void FileEof(Event *ev);
    void FileSeek(Event *ev);
    void FileTell(Event *ev);
    void FileRewind(Event *ev);
    void FilePutc(Event *ev);
    void FilePuts(Event *ev);
    void FileGetc(Event *ev);
    void FileGets(Event *ev);
    void FileError(Event *ev);
    void FileFlush(Event *ev);
    void FlagClear(Event *ev);
    void FlagInit(Event *ev);
    void FlagSet(Event *ev);
    void FlagWait(Event *ev);
    void GetArrayKeys(Event *ev);
    void GetArrayValues(Event *ev);
    void GetEntArray(Event *ev);
    void GetTime(Event *ev);
    void GetTimeZone(Event *ev);
    void PregMatch(Event *ev);
    void GetDate(Event *ev);
    void RegisterEvent(Event *ev);
    void UnregisterEvent(Event *ev);
    void SubscribeEvent(Event *ev);
    void UnsubscribeEvent(Event *ev);
    void Conprintf(Event *ev);
    void CharToInt(Event *ev);
    void FileExists(Event *ev);
    void FileReadAll(Event *ev);
    void FileSaveAll(Event *ev);
    void FileRemove(Event *ev);
    void FileRename(Event *ev);
    void FileCopy(Event *ev);
    void FileReadPak(Event *ev);
    void FileList(Event *ev);
    void FileNewDirectory(Event *ev);
    void FileRemoveDirectory(Event *ev);
    void EventACos(Event *ev);
    void EventASin(Event *ev);
    void EventATan2(Event *ev);
    void EventCosH(Event *ev);
    void EventSinH(Event *ev);
    void EventTanH(Event *ev);
    void EventExp(Event *ev);
    void EventFrexp(Event *ev);
    void EventLdexp(Event *ev);
    void EventLog(Event *ev);
    void EventLog10(Event *ev);
    void EventModf(Event *ev);
    void EventPow(Event *ev);
    void EventCeil(Event *ev);
    void EventFloor(Event *ev);
    void EventFmod(Event *ev);
    void StringBytesCopy(Event *ev);
    void CreateHUD(Event *ev);
    void TraceDetails(Event *ev);
    void TypeOfVariable(Event *ev);
    void Md5String(Event *ev);
    void Md5File(Event *ev);
    void SetTimer(Event *ev);
    void TeamGetScore(Event *ev);
    void TeamSetScore(Event *ev);
    void VisionGetNaked(Event *ev);
    void VisionSetNaked(Event *ev);
    void IsPlayerBot(Event *ev);
    void FS_ReadContent(Event *ev);
    void FS_WriteContent(Event *ev);
    void FS_OpenRead(Event *ev);
    void FS_OpenWrite(Event *ev);
    void FS_OpenAppend(Event *ev);

#ifdef USE_HTTP
    void CurlGet(Event *ev);
    void CurlPost(Event *ev);
    void CurlPut(Event *ev);
#endif

public:
    CLASS_PROTOTYPE(ScriptThread);

    ScriptThread();
    ScriptThread(ScriptClass *scriptClass, unsigned char *pCodePos);
    virtual ~ScriptThread();

    ScriptClass *GetScriptClass(void);
    str          FileName(void);

    //
    // synchronization
    //
    void StartedWaitFor(void);
    void StoppedWaitFor(const_str name, bool bDeleting = false);
    void StoppedNotify(void);

    void Execute(Event& ev);
    void Execute(Event *ev);
    void Execute(ScriptVariable *data, int dataSize);
    void Execute();

    ScriptThread *CreateThreadInternal(const ScriptVariable& label);
    ScriptThread *CreateScriptInternal(const ScriptVariable& label);

    void StartTiming(void);
    void StartTiming(int time);
    void StartWaiting();
    void Stop(void);
    void Wait(int time);
    void Pause();

    void Archive(Archiver& arc) override;
    void ArchiveInternal(Archiver& arc);

    void *operator new(size_t size);
    void  operator delete(void *ptr);
};

//
// OSFile Class
//

class OSFile : public Listener
{
    void *file;

public:
    CLASS_PROTOTYPE(OSFile);

    OSFile();
    OSFile(void *inFile);
    virtual ~OSFile();

    void *getOSFile() const;
};

//
// FSFile Class
//

class FSFile : public Listener
{
    int handle;

    void CloseEvent(Event *ev);
    void FlushEvent(Event *ev);
    void ReadEvent(Event *ev);
    void WriteEvent(Event *ev);
    void SeekEvent(Event *ev);
    void TellEvent(Event *ev);

public:
    CLASS_PROTOTYPE(FSFile);

    FSFile();
    FSFile(int inFile);
    virtual ~FSFile();

    fileHandle_t getHandle() const;
};
