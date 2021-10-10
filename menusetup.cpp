/*******************************************************************************
 * wirbelscan: A plugin for the Video Disk Recorder
 * See the README file for copyright information and how to reach the author.
 ******************************************************************************/
#include <string>
#include <vector>
#include <array>
#include <ctime>
#include <vdr/config.h>
#include "menusetup.h"
#include "common.h"
#include "satellites.h"
#include "countries.h"
#include "wirbelscan.h"
#include "scanner.h"
#include "common.h"

#define LOGLEN 8

using namespace COUNTRY;
extern cScanner* Scanner;
static const char* ScannerDesc  = "wirbelscan scan thread";

std::array<const char*,7> DVB_Types = {"DVB-T/T2","DVB-C","DVB-S/S2","RESERVE1","RESERVE2","ATSC", "no device found"};

std::vector<const char*> SatNames;
std::vector<const char*> CountryNames;

cMenuScanning* MenuScanning    = nullptr;   // pointer to actual menu
cOsdItem*      DeviceUsed      = nullptr;
cOsdItem*      Progress        = nullptr;
cOsdItem*      CurrTransponder = nullptr;
cOsdItem*      Str             = nullptr;
cOsdItem*      ChanAdd         = nullptr;
cOsdItem*      ChanNew         = nullptr;
cOsdItem*      ScanType        = nullptr;
cOsdItem*      LogMsg[LOGLEN];


int channelcount = 0;
int lProgress = 0;
int lStrength = 0;
std::string lTransponder;
std::string deviceName;
time_t timestamp;

#undef tr
#define tr(str) (str)

/*******************************************************************************
 * class cMenuSettings
 ******************************************************************************/
class cMenuSettings : public cMenuSetupPage {
private:
  int scan_tv;
  int scan_radio;
  int scan_fta;
  int scan_scrambled;
protected:
  void AddCategory(std::string category);
  void Store(void);
public:
  cMenuSettings(void);
  ~cMenuSettings(void) {};
  virtual eOSState ProcessKey(eKeys Key);
};


cMenuSettings::cMenuSettings(void) {
  static const char* Symbolrates[] = {tr("AUTO"),"6900","6875","6111","6250","6790","6811","5900","5000","3450","4000","6950","7000","6952","5156","5483",tr("ALL (slow)")};
  static const char* Qams[]        = {tr("AUTO"),"64","128","256",tr("ALL (slow)")};
  static const char* logfiles[]    = {tr("Off"),"stdout","syslog"};
  static const char* inversions[]  = {tr("AUTO/OFF"),tr("AUTO/ON")};
  static const char* atsc_types[]  = {"VSB (aerial)","QAM (cable)","VSB + QAM (aerial + cable)"};

  // devices may have changed meanwhile
  wSetup.InitSystems();

  if (! wSetup.systems[SCAN_TERRESTRIAL] &&
       ! wSetup.systems[SCAN_CABLE] &&
       ! wSetup.systems[SCAN_SATELLITE] &&
       ! wSetup.systems[SCAN_TERRCABLE_ATSC]) {
     AddCategory("NO DEVICES FOUND.");
     return;
     }       

  scan_tv        = (wSetup.scanflags & SCAN_TV       ) > 0;
  scan_radio     = (wSetup.scanflags & SCAN_RADIO    ) > 0;
  scan_scrambled = (wSetup.scanflags & SCAN_SCRAMBLED) > 0;
  scan_fta       = (wSetup.scanflags & SCAN_FTA      ) > 0;

  if (SatNames.empty()) {
     SatNames.reserve(sat_count());
     for(size_t i=0; i<sat_count(); i++)
        SatNames.push_back(sat_list[i].full_name);
     }

  if (CountryNames.empty()) {
     CountryNames.reserve(country_count());
     for(size_t i=0; i<country_count(); i++)
        CountryNames.push_back(country_list[i].full_name);
     }

  SetSection(tr("Setup"));
  AddCategory(tr("General"));
  Add(new cMenuEditStraItem(tr("Source Type"),        &wSetup.DVB_Type,  DVB_Types.size()-1, DVB_Types.data()));
  Add(new cMenuEditIntItem (tr("verbosity"),          &wSetup.verbosity, 0, 6));
  Add(new cMenuEditStraItem(tr("logfile"),            &wSetup.logFile,   3, logfiles));

  AddCategory(tr("Channels"));
  Add(new cMenuEditBoolItem(tr("TV channels"),        &scan_tv));
  Add(new cMenuEditBoolItem(tr("Radio channels"),     &scan_radio));
  Add(new cMenuEditBoolItem(tr("FTA channels"),       &scan_fta));
  Add(new cMenuEditBoolItem(tr("Scrambled channels"), &scan_scrambled));

  if (wSetup.systems[SCAN_CABLE] || wSetup.systems[SCAN_TERRESTRIAL] || wSetup.systems[SCAN_TERRCABLE_ATSC]) {
     AddCategory(tr("Cable and Terrestrial"));
     Add(new cMenuEditStraItem(tr("Country"),             &wSetup.CountryIndex, country_count(), CountryNames.data()));
     if (wSetup.systems[SCAN_CABLE]) {
        Add(new cMenuEditStraItem(tr("Cable Inversion"),  &wSetup.DVBC_Inversion,    2, inversions));
        Add(new cMenuEditStraItem(tr("Cable Symbolrate"), &wSetup.DVBC_Symbolrate,  17, Symbolrates));
        Add(new cMenuEditStraItem(tr("Cable modulation"), &wSetup.DVBC_QAM,          5, Qams));
        Add(new cMenuEditIntItem (tr("Cable Network PID"),&wSetup.DVBC_Network_PID, 16, 0xFFFE, "AUTO"));
        }
     if (wSetup.systems[SCAN_TERRESTRIAL])
        Add(new cMenuEditStraItem(tr("Terr  Inversion"),  &wSetup.DVBT_Inversion,   2, inversions));

     if (wSetup.systems[SCAN_TERRCABLE_ATSC])
        Add(new cMenuEditStraItem(tr("ATSC  Type"),       &wSetup.ATSC_type,        3, atsc_types));

     }

  if (wSetup.systems[SCAN_SATELLITE]) {
     AddCategory(tr("Satellite"));
     Add(new cMenuEditStraItem(tr("Satellite"),        &wSetup.SatIndex, sat_count(), SatNames.data()));
     Add(new cMenuEditBoolItem(tr("DVB-S2"),           &wSetup.enable_s2));
     }

  AddCategory(tr("Scan Mode"));
  Add(new cMenuEditBoolItem(tr("remove invalid channels"),   &wSetup.scan_remove_invalid));
  Add(new cMenuEditBoolItem(tr("update existing channels"),  &wSetup.scan_update_existing));
  Add(new cMenuEditBoolItem(tr("append new channels"),       &wSetup.scan_append_new));
}


void cMenuSettings::Store(void) {
  wSetup.scanflags  = scan_tv       ?SCAN_TV       :0;
  wSetup.scanflags |= scan_radio    ?SCAN_RADIO    :0;
  wSetup.scanflags |= scan_scrambled?SCAN_SCRAMBLED:0;
  wSetup.scanflags |= scan_fta      ?SCAN_FTA      :0;
  wSetup.update = true;
}


void cMenuSettings::AddCategory(std::string category) {
  category.insert(0, "---------------  ");
  Add(new cOsdItem(category.c_str()));
}


eOSState cMenuSettings::ProcessKey(eKeys Key) {
  int direction = 0;
  eOSState state = cMenuSetupPage::ProcessKey(Key);
  switch(Key) {
     case kLeft:
        direction = -1;
        break; 
     case kRight:
        direction = 1;
        break; 
     case kOk:
     case kBack:
        thisPlugin->StoreSetup();
        wSetup.update = true;
        state=osBack;
        break;
     default:;
     }
  if (state == osUnknown) {
     switch(Key) {
        case kGreen:
        case kRed:
        case kBlue:
        case kYellow:
           state=osContinue;
           break;
        default:;
        }
     }

  if (! wSetup.systems[SCAN_TERRESTRIAL] and ! wSetup.systems[SCAN_CABLE] and
      ! wSetup.systems[SCAN_SATELLITE]   and ! wSetup.systems[SCAN_TERRCABLE_ATSC]) {
     // no devices found; recursive call until we reach SCAN_NO_DEVICE.
     if (wSetup.DVB_Type < SCAN_NO_DEVICE)
        ProcessKey(kRight);
     }
  else if (! wSetup.systems[wSetup.DVB_Type]) {
     if (direction) {
        if (wSetup.DVB_Type == SCAN_NO_DEVICE) {
           wSetup.DVB_Type = 1;
           ProcessKey(kLeft); // now, DVB_Type is 0.
           }
        else
           ProcessKey(kRight);
        }
     else {
        if (wSetup.DVB_Type == 0)
           wSetup.DVB_Type = SCAN_NO_DEVICE;
        ProcessKey(kLeft);
        }
     //while(!wSetup.systems[wSetup.DVB_Type] && wSetup.DVB_Type != SCAN_NO_DEVICE) {
     //   dlog(4, "%s unsupported", DVB_Types[wSetup.DVB_Type]);
     //   ProcessKey(direction < 0? kLeft:kRight);
     //   }
     }

  return state;      
}



/*******************************************************************************
 * class cMenuScanning
 ******************************************************************************/
cMenuScanning::cMenuScanning(void) :
  needs_update(true), log_busy(false), transponder(0), transponders(1) {
  SetHelp(tr("Stop"), tr("Start"), tr("Settings"), "");
  MenuScanning = this;

  wSetup.InitSystems();

  if (!wSetup.systems[SCAN_TERRESTRIAL] &&
      !wSetup.systems[SCAN_CABLE] &&
      !wSetup.systems[SCAN_SATELLITE] &&
      !wSetup.systems[SCAN_TERRCABLE_ATSC]) {
     AddCategory("NO DEVICES FOUND.");
     return;
     }

  AddCategory(tr("Status"));
  std::string status(DVB_Types[wSetup.DVB_Type]);
  status += " ";
  if (wSetup.DVB_Type == SCAN_SATELLITE)
     status += sat_list[wSetup.SatIndex].full_name;
  else
     status += country_list[wSetup.CountryIndex].full_name;

  ScanType = new cOsdItem(status.c_str());
  Add(ScanType);

  DeviceUsed = new cOsdItem("Device:");
  Add(DeviceUsed);
  
  Progress = new cOsdItem("Scan:");
  Add(Progress);

  CurrTransponder = new cOsdItem(" ");
  Add(CurrTransponder);

  Str = new cOsdItem("STR");
  Add(Str);

  AddCategory(tr("Channels"));

  std::string flags;

  if ((wSetup.scanflags & (SCAN_TV | SCAN_RADIO)) == SCAN_TV)
     flags = "TV only";
  else if ((wSetup.scanflags & (SCAN_TV | SCAN_RADIO)) == SCAN_RADIO)
     flags = "Radio only";
  else if ((wSetup.scanflags & (SCAN_TV | SCAN_RADIO)) == (SCAN_RADIO | SCAN_TV))
     flags = "TV + Radio";
  else
     flags = "don''t add channels";

  if ((wSetup.scanflags & (SCAN_FTA | SCAN_SCRAMBLED)) == SCAN_FTA)
     flags += " (Free to Air only)";
  else if ((wSetup.scanflags & (SCAN_FTA | SCAN_SCRAMBLED)) == SCAN_SCRAMBLED)
     flags += " (Scrambled only)";
  else if ((wSetup.scanflags & (SCAN_FTA | SCAN_SCRAMBLED)) == (SCAN_FTA | SCAN_SCRAMBLED))
     flags += " (Free to Air + Scrambled)";
  else
     flags += " (don''t add channels)";

  ChanAdd = new cOsdItem(flags.c_str());
  Add(ChanAdd);

  ChanNew = new cOsdItem("known Channels:");
  Add(ChanNew);

  AddCategory(tr("Log Messages"));
  for(int i=0; i<LOGLEN; i++) {
     LogMsg[i] = new cOsdItem(" ");
     Add(LogMsg[i]);
     }
}


cMenuScanning::~cMenuScanning(void) {
  MenuScanning = nullptr;
}


void cMenuScanning::SetChanAdd(uint32_t flags) {
  static std::string s;

  if ((wSetup.scanflags & (SCAN_TV | SCAN_RADIO)) == SCAN_TV)
     s = "TV only";
  else if ((wSetup.scanflags & (SCAN_TV | SCAN_RADIO)) == SCAN_RADIO)
     s = "Radio only";
  else if ((wSetup.scanflags & (SCAN_TV | SCAN_RADIO)) == (SCAN_RADIO | SCAN_TV))
     s = "TV + Radio";
  else
     s = "don''t add channels";

  if ((wSetup.scanflags & (SCAN_FTA | SCAN_SCRAMBLED)) == SCAN_FTA)
     s += " (Free to Air only)";
  else if ((wSetup.scanflags & (SCAN_FTA | SCAN_SCRAMBLED)) == SCAN_SCRAMBLED)
     s += " (Scrambled only)";
  else if ((wSetup.scanflags & (SCAN_FTA | SCAN_SCRAMBLED)) == (SCAN_FTA | SCAN_SCRAMBLED))
     s += " (Free to Air + Scrambled)";
  else
     s += " (don''t add channels)";

  ChanAdd->SetText(s.c_str(), true);
  ChanAdd->Set();
  MenuScanning->Display();
}


void cMenuScanning::SetStatus(int status) {
  int type = Scanner?Scanner->DvbType() : wSetup.DVB_Type;
  static const char* st[] = {
     "STOP","RUN","No device available - exiting!",
     "No DVB-S2 device available - trying fallback to DVB-S",
     " "};
  static std::string s;

  s = DVB_Types[type];
  s += " ";
  if (type == SCAN_SATELLITE)
     s += sat_list[wSetup.SatIndex].full_name;
  else
     s += country_list[wSetup.CountryIndex].full_name;
  s += " ";
  if (Scanner)
     s += st[Scanner->Status()];
  else
     s += st[status];

  ScanType->SetText(s.c_str(), true);
  ScanType->Set();
  MenuScanning->Display();
}


void cMenuScanning::SetCounters(int curr_tp, int all_tp) {
  transponder = curr_tp;
  transponders = all_tp;
}


void cMenuScanning::SetProgress(const int progress) {
  static char s[256];
  time_t t = time(0) - timestamp;
  if (transponder > 0) {
     snprintf(s, 256, "Scan: %d%% running %dm%.2dsec (%d/%d)",
         progress, (int) t/60, (int) t%60, transponder, transponders);
     lProgress = (int) (0.5 + (100.0 * transponder) / transponders);
     }
  else
     snprintf(s, 256, "Scan: %d%% running %dm%.2dsec",
         lProgress, (int) t/60, (int) t%60);
  Progress->SetText(s, true);
  Progress->Set();
  if (needs_update) {
     SetStatus(0);
     SetDeviceInfo("", false);
     needs_update = false;
     }
  MenuScanning->Display();
}


void cMenuScanning::SetTransponder(const TChannel* transponder) {
  std::string s;
  ((TChannel*) transponder)->PrintTransponder(s);
  CurrTransponder->SetText(s.c_str(), true);
  CurrTransponder->Set();
  MenuScanning->Display();
}


void cMenuScanning::SetStr(unsigned strength, bool locked) {
  static char s[256];

  if (strength > 100U)
     dlog(0, "strength = " + IntToStr(strength) + '!');

  if (locked and !strength)
     snprintf(s, 256, "STR -- [_________] %s", locked?"LOCKED":"");
  else
  switch(strength) {
     case  0       : snprintf(s, 256, "STR %-3d%% []           %s", strength, locked?"LOCKED":""); break;
     case  1 ... 10: snprintf(s, 256, "STR %-3d%% []           %s", strength, locked?"LOCKED":""); break;
     case 11 ... 20: snprintf(s, 256, "STR %-3d%% [_]          %s", strength, locked?"LOCKED":""); break;
     case 21 ... 30: snprintf(s, 256, "STR %-3d%% [__]         %s", strength, locked?"LOCKED":""); break;
     case 31 ... 40: snprintf(s, 256, "STR %-3d%% [___]        %s", strength, locked?"LOCKED":""); break;
     case 41 ... 50: snprintf(s, 256, "STR %-3d%% [____]       %s", strength, locked?"LOCKED":""); break;
     case 51 ... 60: snprintf(s, 256, "STR %-3d%% [_____]      %s", strength, locked?"LOCKED":""); break;
     case 61 ... 70: snprintf(s, 256, "STR %-3d%% [______]     %s", strength, locked?"LOCKED":""); break;
     case 71 ... 80: snprintf(s, 256, "STR %-3d%% [_______]    %s", strength, locked?"LOCKED":""); break;
     case 81 ... 90: snprintf(s, 256, "STR %-3d%% [________]   %s", strength, locked?"LOCKED":""); break;
     default:        snprintf(s, 256, "STR %-3d%% [_________]  %s", strength, locked?"LOCKED":""); break;
     }

  Str->SetText(s, true);
  Str->Set();
  MenuScanning->Display();
}


void cMenuScanning::SetChan(int count) {
  static std::string s;
  s = "known Channels: " + IntToStr(channelcount = count);

  ChanNew->SetText(s.c_str(), true);
  ChanNew->Set();
  MenuScanning->Display();
}


void cMenuScanning::SetDeviceInfo(std::string Info, bool update) {
  std::string s("Device ");
  if (update)
     deviceName = Info;

  s += deviceName;
  DeviceUsed->SetText(s.c_str(), true);
  DeviceUsed->Set();
  MenuScanning->Display();
}


void cMenuScanning::AddLogMsg(std::string Msg) {
  if (log_busy) return;
  log_busy = true;
  for(int i=0; i<LOGLEN-1; i++) {
     LogMsg[i]->SetText(LogMsg[i+1]->Text(), true);    
     LogMsg[i]->Set();
     }
  LogMsg[LOGLEN - 1]->SetText(Msg.c_str(), true);
  LogMsg[LOGLEN - 1]->Set();
  MenuScanning->Display();
  log_busy = false;
}


void cMenuScanning::AddCategory(std::string category) {
  category.insert(0, "---------------  ");
  Add(new cOsdItem(category.c_str()));
}


eOSState cMenuScanning::ProcessKey(eKeys Key) {
  if (wSetup.update) {
     SetStatus(4);
     SetChanAdd(wSetup.scanflags);
     wSetup.update = false;
     }
  eOSState state = cMenuSetupPage::ProcessKey(Key);
  switch (Key) {
     case kUp:
     case kDown:
        return osContinue;
     default:;
     }
  if (state == osUnknown) {
     switch(Key) {
        case kBack:
        case kOk:
           state=osBack;
           return state;

        case kGreen:
           if (wSetup.systems[SCAN_TERRESTRIAL] ||
               wSetup.systems[SCAN_CABLE] ||
               wSetup.systems[SCAN_SATELLITE] ||
               wSetup.systems[SCAN_TERRCABLE_ATSC]) {
              state=osContinue;
              needs_update = true;
              StartScan();
              }
           break;

        case kRed:
           if (wSetup.systems[SCAN_TERRESTRIAL] ||
               wSetup.systems[SCAN_CABLE] ||
               wSetup.systems[SCAN_SATELLITE] ||
               wSetup.systems[SCAN_TERRCABLE_ATSC]) {
              state=osContinue;
              needs_update = true;
              StopScan();
              }
           break;

        case kYellow:
           if (wSetup.systems[SCAN_TERRESTRIAL] ||
               wSetup.systems[SCAN_CABLE] ||
               wSetup.systems[SCAN_SATELLITE] ||
               wSetup.systems[SCAN_TERRCABLE_ATSC])
              return AddSubMenu(new cMenuSettings());

        default:
           break;
        }
    }
  if (Scanner && Scanner->Active() && (state != osBack))
     return osContinue;
  return state;      
}


bool cMenuScanning::StopScan(void) {
  DoStop();
  return true;
}


bool cMenuScanning::StartScan(void) {
  int type = wSetup.DVB_Type;
  dlog(0, "StartScan(" + std::string(DVB_Types[type]) + ')');

  if (!wSetup.systems[type]) {
     dlog(0, "Skipping scan: CANNOT SCAN - No device!");
     Skins.Message(mtInfo, tr("CANNOT SCAN - No device!"));
     mSleep(6000);
     return false;
     }

  return DoScan(type);
}


void cMenuScanning::Store(void) {
  thisPlugin->StoreSetup();
}



/*******************************************************************************
 * Stop Scanner now, we're destroying the plugin..
 ******************************************************************************/
void stopScanners(void) {
 if (Scanner) {
    dlog(0, "Stopping scanner.");
    Scanner->SetShouldstop(true);
    }
}


/*******************************************************************************
 * create new scanner.
 ******************************************************************************/
bool DoScan(int DVB_Type) {
  if (Scanner && Scanner->Active()) {
     dlog(0, "ERROR: already scanning");
     return false;
     }
  wSetup.InitSystems();
  if (DVB_Type == SCAN_NO_DEVICE || ! wSetup.systems[DVB_Type]) {
     dlog(0, "ERROR: no device found");
     return false;
     }
  timestamp = time(0);
  channelcount = 0;
  Scanner = new cScanner(ScannerDesc, DVB_Type);
  return true;
}


/*******************************************************************************
 * Stop Scanner.
 ******************************************************************************/
void DoStop(void) {
  if (Scanner && Scanner->Active())
     Scanner->SetShouldstop(true);
}
