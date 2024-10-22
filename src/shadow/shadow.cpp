// SPDX-License-Identifier: GPL-2.0-or-later
// 
// eXdupe deduplication library and file archiver.
//
// Copyrights:
// 2010 - 2024: Lasse Mikkel Reinhold

#include "../unicode.h"

#include <iostream>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <atlbase.h>
#include "../utilities.hpp"

#include <stdio.h>

#include <windows.h>
#include <winbase.h>

#include <Vss.h>
#include <VsWriter.h>
#include <VsBackup.h>

#include "shadow.h"

using namespace std;

#ifdef WINDOWS
  #define CURDIR L(".\\"
  #define DELIM_STR L("\\")
  #define DELIM_CHAR L('\\')
#else
  #define CURDIR L("./"
  #define DELIM_STR L("/"
  #define DELIM_CHAR '/'
#endif



  CComPtr<IVssEnumObject> enum_snapshots;
  CComPtr<IVssBackupComponents> comp;
  CComPtr<IVssAsync> async;
  VSS_OBJECT_PROP prop;


vector<pair<STRING, STRING> > snaps; //(STRING mount, STRING shadow)

vector<pair<STRING, STRING> > get_snaps(void)
{
    return snaps;
}


STRING DisplayVolumePaths(__in PWCHAR VolumeName)
{
    DWORD  CharCount = MAX_PATH_LEN + 1;
    PWCHAR Names     = NULL;
    PWCHAR NameIdx   = NULL;
    BOOL   Success   = FALSE;

    STRING res;

    for (;;) 
    {
        //  Allocate a buffer to hold the paths.
        Names = (PWCHAR) new BYTE [CharCount * sizeof(WCHAR)];

        if ( !Names ) 
        {
            //  If memory can't be allocated, return.
            return L("");
        }
        //  Obtain all of the paths
        //  for this volume.
        Success = GetVolumePathNamesForVolumeNameW(
            VolumeName, Names, CharCount, &CharCount
            );

        if ( Success ) 
        {

            res = Names;

            break;
        }

        if ( GetLastError() != ERROR_MORE_DATA ) 
        {
            break;
        }
        //  Try again with the
        //  new suggested size.
        delete [] Names;
        Names = NULL;
    }

    if ( Success )
    {
        //  Display the various paths.
        for ( NameIdx = Names; 
              NameIdx[0] != L'\0'; 
              NameIdx += STRLEN(NameIdx) + 1 ) 
        {
     //       wprintf(L("  %s", NameIdx);
        }
    //    wprintf(L("\n");
    }

    if ( Names != NULL ) 
    {
        delete [] Names;
        Names = NULL;
    }

    return res;
}








STRING towide(STRING s)
{
    STRING str2(s.length(), L' ');
    std::copy(s.begin(), s.end(), str2.begin());
    return str2;
}



int shadow(vector<STRING> volumes) 
{

    if(volumes.size() == 0)
        return 1;

    for(unsigned int i = 0; i < volumes.size(); i++)
        volumes[i] = remove_delimitor(volumes[i]) + DELIM_STR;


  // Initialize COM and open ourselves wide for callbacks by
  // CoInitializeSecurity.
  HRESULT hr;
  hr = ::CoInitialize(NULL);
  if (SUCCEEDED(hr)) {
    hr = ::CoInitializeSecurity(
        NULL,  //  Allow *all* VSS writers to communicate back!
        -1,  //  Default COM authentication service
        NULL,  //  Default COM authorization service
        NULL,  //  reserved parameter
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY,  //  Strongest COM authentication level
        RPC_C_IMP_LEVEL_IDENTIFY,  //  Minimal impersonation abilities
        NULL,  //  Default COM authentication settings
        EOAC_NONE,  //  No special options
        NULL);  //  Reserved parameter
  }

  abort(FAILED(hr), L("Snapshot failed to initialize COM at CoInitializeSecurity()"));


  hr = ::CreateVssBackupComponents(&comp);
  if (SUCCEEDED(hr))
    hr = comp->InitializeForBackup(NULL);
  if (SUCCEEDED(hr))
    hr = comp->SetBackupState(true, true, VSS_BT_COPY, false);

  abort(FAILED(hr), L("Snapshot failed at SetBackupState(). Please run eXdupe or Command Prompt as administrator."));

  hr = comp->GatherWriterMetadata(&async);
  if (SUCCEEDED(hr))
    hr = async->Wait();


  abort(FAILED(hr), L("Snapshot failed to gather write data at GatherWriterMetadata()"));

  VSS_ID id = {};
  hr = comp->StartSnapshotSet(&id);

  VSS_ID dummy = {};
  std::vector<int> created;

  if (SUCCEEDED(hr)) 
  {
        for(unsigned int i = 0; i < volumes.size(); i++)
        {
            STRING v = volumes[i];
            hr = comp->AddToSnapshotSet(const_cast<LPWSTR>(towide(v).c_str()), GUID_NULL, &dummy);
            abort(FAILED(hr), L("Snapshot failed to start at AddToSnapshotSet()"));
            created.push_back(dummy.Data1);

        }
    }
  abort(FAILED(hr), L("Snapshot failed to start at StartSnapshotSet(). Wait a few minutes. See interfering snapshots with 'vssadmin list writers' or 'vssadmin list shadows'"));

  async.Release();
  hr = comp->PrepareForBackup(&async);
  if (SUCCEEDED(hr))
    hr = async->Wait();

  abort(FAILED(hr), L("Snapshot failed at PrepareForBackup(). Wait a few minutes. See interfering snapshots with 'vssadmin list writers' or 'vssadmin list shadows'"));

  async.Release();
  hr = comp->DoSnapshotSet(&async);
  if (SUCCEEDED(hr))
    hr = async->Wait();

  abort(FAILED(hr), L("Snapshot failed at DoSnapshotSet()"));

  hr = comp->Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &enum_snapshots);
  abort(FAILED(hr), L("Snapshot failed to query at Query()"));

  ULONG fetched = 0;

  for(;;)
  {
      hr = enum_snapshots->Next(1, &prop, &fetched);
      if(hr != S_OK)
          break;

      STRING s = prop.Obj.Snap.m_pwszSnapshotDeviceObject;
      STRING v = DisplayVolumePaths(prop.Obj.Snap.m_pwszOriginalVolumeName);

      if (std::find(created.begin(), created.end(), prop.Obj.Snap.m_SnapshotId.Data1) != created.end())
      snaps.push_back(make_pair(v, s));

  }
    
  abort(created.size() != snaps.size(), L("Snapshot failed with unknown error"));

  return 1;
}



STRING snap(STRING path)
{
    STRING path_orig = path;

    if(path == L(""))
        path = L(".");

    path = lcase(abs_path(path));

    for(unsigned int i = 0; i < snaps.size(); i++)
    {
        STRING m = lcase(snaps[i].first);

        if(path.size() >= m.size() && path.substr(0, m.size()) == m)
        {
            path.replace(0, m.size(), snaps[i].second + DELIM_STR);
            return path;
        }
    }
    return path_orig;
}

 

STRING snappart(STRING path)
{
    STRING path_orig = path;
    path = lcase(abs_path(path));

    for(unsigned int i = 0; i < snaps.size(); i++)
    {
        STRING m = lcase(snaps[i].second);

        if(path.size() >= m.size() && path.substr(0, m.size()) == m)
        {
            return snaps[i].second;
        }
    }
    return L("");
}

 

STRING volpart(STRING path)
{
    STRING path_orig = path;
    path = lcase(abs_path(path));

    for(unsigned int i = 0; i < snaps.size(); i++)
    {
        STRING m = lcase(snaps[i].second);

        if(path.size() >= m.size() && path.substr(0, m.size()) == m)
        {
            return snaps[i].first;
        }
    }
    return L("");
}



STRING unsnap(STRING path)
{
    STRING path_orig = path;
    path = abs_path(path);

    for(unsigned int i = 0; i < snaps.size(); i++)
    {
        STRING m = lcase(snaps[i].second);

        if(path.size() >= m.size() && lcase(path.substr(0, m.size())) == m)
        {
            path.replace(0, m.size() + 1, snaps[i].first);
            return path;
        }
    }
    return path_orig;
}



void unshadow(void)
{
  ULONG fetched = 0;
  for(unsigned int i = 0; i < snaps.size(); i++)
  {
      enum_snapshots->Next(1, &prop, &fetched);
      VssFreeSnapshotProperties(&prop.Obj.Snap);
  }  
}

