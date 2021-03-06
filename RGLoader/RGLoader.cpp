// XtweakXam.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include <xbdm.h>
#include <fstream>
#include <string>
#include <stdio.h>
#include "INIReader.h"
#include "xam.h"
#include "HUD.h"
#include "xshell.h"
#include "HvExpansion.h"
#include "OffsetManager.h"
#include "RPC.h"
// #include "sysext.h"

using namespace std;

static bool fKeepMemory = true;
static bool fExpansionEnabled = false;
static INIReader* reader;
static OffsetManager offsetmanager;
// static DWORD TitleID = 0;

#define setmem(addr, data) { DWORD d = data; memcpy((LPVOID)addr, &d, 4);}

#define XexLoadExecutableOrd 408
#define XexLoadImageOrd 409
#define XEXLOADIMAGE_MAX_SEARCH 9

#define XEXLOAD_DASH    "\\Device\\Flash\\dash.xex"
#define XEXLOAD_DASH2   "\\SystemRoot\\dash.xex"
#define XEXLOAD_SIGNIN  "signin.xex"
#define XEXLOAD_CREATE  "createprofile.xex"
#define XEXLOAD_HUD	    "hud.xex"
#define XEXLOAD_XSHELL  "xshell.xex"
#define XEXLOAD_DEFAULT "default.xex"

/*void setmem(DWORD addr, DWORD data) {
	UINT64 d = data;
	if(addr < 0x40000)
	{
		// hv patch
		if(fExpansionEnabled)
		{
			printf("     (hv patch)\n");
			addr = addr | 0x8000000000000000ULL;
			BYTE* newdata = (BYTE*)XPhysicalAlloc(sizeof(DWORD), MAXULONG_PTR, 0, PAGE_READWRITE);
			memcpy(newdata, &d, sizeof(DWORD));
			writeHVPriv(newdata, addr, sizeof(DWORD));
			XPhysicalFree(newdata);
		}
		else
			printf("     (hv patch, but expansion didn't install :( )\n");
	}
	else
		DmSetMemory((LPVOID)addr, 4, &d, NULL);
}*/

BOOL ExpansionStuff() {
	/*
	0xC8007000 // address alignment fail
	0xC8007001 // size alignment fail
	0xC8007002 // magic/rsa sanity fail
	0xC8007003 // flags/size sanity fail
	0xC8007004 // inner header fail
	0xC8007005 // ...
	*/

	RGLPrint("EXPANSION", "Checking if the HVPP expansion is installed...\n");
	if (HvPeekWORD(0) != 0x5E4E) {
		// install signed and encrypted HVPP expansion
		RGLPrint("EXPANSION", "Installing HVPP expansion...\n");
		DWORD ret = InstallExpansion();
		if (ret != ERROR_SUCCESS) {
			RGLPrint("EXPANSION", "InstallExpansion: %04X\n", ret);
			return FALSE;
		}
		RGLPrint("EXPANSION", "Done!\n");
	}
	else
		RGLPrint("EXPANSION", "Expansion is already installed, skipping...\n");

	return TRUE;
}

BOOL FuseStuff() {
	QWORD fuselines[12];
	for (int i = 0; i < 12; i++) {
		fuselines[i] = HvPeekQWORD(0x8000020000020000 + (i * 0x200));
	}
	for (int i = 0; i < 12; i++) {
		HexPrint((PBYTE)&fuselines[i], 8);
		printf("\n");
	}

	return TRUE;
}

HRESULT __stdcall RGLoaderCommandHandler(LPCSTR szCommand, LPSTR szResponse, DWORD cchResponse, PDM_CMDCONT pdmcc) {
	printf("%s\n", szCommand);
	return XBDM_NOERR;
}

BOOL KeyVaultStuff() {
	BYTE cpuKey[0x10] = { 0 };
	BYTE kvBuf[0x4000] = { 0 };
	BYTE kvHash[0x14] = { 0 };
	PBYTE kvData = kvBuf + 0x18;

	// 17489/21256.18
	QWORD ppKvAddr = 0x2000162E0;
	QWORD pMasterPub = 0x200011008;

	QWORD pKvAddr = HvPeekQWORD(ppKvAddr);  // keyvault pointer in HV
	// grab the CPU key and KV from the HV
	// there's way better ways to grab the CPU key than this!
	HvPeekBytes(0x18, cpuKey, 0x10);
	// grab the KV
	HvPeekBytes(pKvAddr, kvBuf, 0x4000);

	// calculate the KV hash
	XeCryptHmacSha(cpuKey, 0x10, kvData + 4, 0xD4, kvData + 0xE8, 0x1CF8, kvData + 0x1EE0, 0x2108, kvHash, 0x14);

	BYTE masterPub[sizeof(XECRYPT_RSAPUB_2048)];
	// master public key in the HV
	HvPeekBytes(pMasterPub, masterPub, sizeof(XECRYPT_RSAPUB_2048));

	RGLPrint("KV", "Console Serial: %s\n", kvBuf + 0xB0);

	if (XeCryptBnDwLePkcs1Verify(kvHash, kvData + 0x1DE0, sizeof(XECRYPT_SIG)) == TRUE)
		RGLPrint("WARNING", "KV hash is valid for this console!\n");
	else
		RGLPrint("WARNING", "KV hash is invalid for this console!\n");

	return TRUE;
}

void PatchBlockLIVE(){
	RGLPrint("PROTECTIONS", " * Blocking Xbox Live DNS\r\n");

	char* nullStr = "NO.%sNO.NO\0";
	DWORD nullStrSize = 18;

	XAMOffsets* offsets = offsetmanager.GetXAMOffsets();
	if(!offsets)
	{
		RGLPrint("ERROR", "Failed to load DNS offsets!\r\n");
		return;
	}

	// null out xbox live dns tags
	if(offsets->live_siflc)  //FIXME: check the others
		memcpy( (LPVOID)offsets->live_siflc, (LPCVOID)nullStr, nullStrSize);
	memcpy((LPVOID)offsets->live_piflc, (LPCVOID)nullStr, nullStrSize);
	memcpy((LPVOID)offsets->live_notice, (LPCVOID)nullStr, nullStrSize);
	memcpy((LPVOID)offsets->live_xexds, (LPCVOID)nullStr, nullStrSize);
	memcpy((LPVOID)offsets->live_xetgs, (LPCVOID)nullStr, nullStrSize);
	memcpy((LPVOID)offsets->live_xeas, (LPCVOID)nullStr, nullStrSize);
	memcpy((LPVOID)offsets->live_xemacs, (LPCVOID)nullStr, nullStrSize);
}

DWORD MapDebugDriveAddr = 0x91F2EF60;
typedef VOID(*MAPDEBUGDRIVE)(const PCHAR mntName, const PCHAR mntPath, DWORD enable);
MAPDEBUGDRIVE MapDebugDrive = (MAPDEBUGDRIVE)MapDebugDriveAddr;
MAPDEBUGDRIVE MapDebugDriveOrig;

DWORD MapInternalDrivesAddr = 0x91F2F0F8;
typedef VOID(*MAPINTERNALDRIVES)(VOID);
MAPINTERNALDRIVES MapInternalDrives = (MAPINTERNALDRIVES)MapInternalDrivesAddr;

VOID MapDebugDriveHook(const PCHAR mntName, const PCHAR mntPath, DWORD enable) {
	return MapDebugDriveOrig(mntName, mntPath, TRUE);
}

// Enable USBMASS0-2 in neighborhood
void MountAllDrives(void) {
	RGLPrint("INFO", " * Adding extra devices to xbox neighborhood\r\n");
	MapDebugDriveOrig = reinterpret_cast<MAPDEBUGDRIVE>(HookFunctionStub((PDWORD)MapDebugDriveAddr, MapDebugDriveHook));
	// call MapInternalDrives
	MapInternalDrives();
}

//21076
// Changes the default dashboard
void PatchDefaultDash(string path) {
	RGLPrint("INFO", " * Reconfiguring default dash to: %s\n", path);
	
	ofstream dashxbx;

	//dashxbx.open("Hdd:\\Filesystems\14719-dev\dashboard.xbx", ofstream::out);
	dashxbx.open("Root:\\dashboard.xbx", ofstream::out);

	if(dashxbx.is_open()) {
		dashxbx << path;
		for(int i = path.length(); i < 0x100; i++)
			dashxbx << '\0';
		dashxbx.close();
	} else {
		RGLPrint("ERROR", "unable to write dashboard.xbx\n");
	}
}

bool StrCompare(char* one, char* two, int len) {
	for(int i = 0; i < len; i++){
		if(i > 0 && (one[i] == '\0' || two[i] == '\0'))
			return true; 
		if(one[i] != two[i])
			return false;
	}
	return true;
}

NTSTATUS XexpLoadImageHook(LPCSTR xexName, DWORD typeInfo, DWORD ver, PHANDLE modHandle);
typedef NTSTATUS (*XEXPLOADIMAGEFUN)(LPCSTR xexName, DWORD typeInfo, DWORD ver, PHANDLE modHandle); // XexpLoadImage
XEXPLOADIMAGEFUN XexpLoadImageOrig;

int PatchHookXexLoad(void) {
	PDWORD xexLoadHookAddr = (PDWORD)FindInterpretBranchOrdinal("xboxkrnl.exe", XexLoadImageOrd, XEXLOADIMAGE_MAX_SEARCH);
	// InfoPrint("  - Found addr\r\n");
	if(xexLoadHookAddr != NULL)
	{
		//printf("  - Applying hook at %08X  with  save @ %08X\r\n", xexLoadHookAddr, (PDWORD)XexpLoadImageSaveVar);
		XexpLoadImageOrig = reinterpret_cast<XEXPLOADIMAGEFUN>(HookFunctionStub(xexLoadHookAddr, XexpLoadImageHook));
	}

	return 1;
}

NTSTATUS XexpLoadImageHook(LPCSTR xexName, DWORD typeInfo, DWORD ver, PHANDLE modHandle) {
	NTSTATUS ret = XexpLoadImageOrig(xexName, typeInfo, ver, modHandle);

	if (ret >= 0) {
		if (stricmp(xexName, XEXLOAD_HUD) == 0) {
			// printf("\n\n ***RGLoader.xex*** \n   -Re-applying patches to: %s!\n\n", xexName);
			
			bool hudJumpToXShell = reader->GetBoolean("Expansion", "HudJumpToXShell", true);
			if (hudJumpToXShell) {
				// printf("     * Replacing family settings button with \"Jump to XShell\"");
				PatchHudReturnToXShell();
			}
		} else if (stricmp(xexName, XEXLOAD_XSHELL) == 0) {
			// printf("\n\n ***RGLoader.xex*** \n   -Re-applying patches to: %s!\n\n", xexName);
	
			string redirectXShellButton = reader->GetString("Config", "RedirectXShellButton", "none");
			if(redirectXShellButton != "none" && FileExists(redirectXShellButton.c_str())) {
				// printf("     * Remapping xshell start button to %s.\n\n", rTemp.c_str());
				PatchXShellStartPath(redirectXShellButton);
			}
		} else if (stricmp(xexName, XEXLOAD_SIGNIN) == 0) {
			//printf("\n\n ***RGLoader.xex*** \n   -Re-applying patches to: %s!\n", xexName);

			bool noSignInNotice = reader->GetBoolean("Config", "NoSignInNotice", false);
			if(noSignInNotice) {
				// printf("     * Disabling xbox live sign in notice.\n\n");
				SIGNINOffsets* offsets = offsetmanager.GetSigninOffsets();
				if (offsets != NULL) {
					setmem(offsets->NoSignInNotice, 0x38600000);
				} else {
					RGLPrint("ERROR", "Failed to load signin offsets!\r\n");
				}
			}
		}
	}
	return ret;
}

DWORD PatchApplyBinary(string filepath) {
	DWORD fileSize = (DWORD)FileSize(filepath.c_str());
	if (fileSize == -1) {
		RGLPrint("ERROR", "Invalid patch path\n");
		return FALSE;
	}
	if (fileSize % 4 != 0) {
		RGLPrint("ERROR", "Invalid patch size\n");
		return FALSE;
	}
	BYTE* patchData = new BYTE[fileSize];
	if (!ReadFile(filepath.c_str(), patchData, fileSize)) {
		RGLPrint("ERROR", "Unable to read patch file\n");
		return FALSE;
	}

	DWORD offset = 0;
	DWORD patchesApplied = 0;
	if(*(DWORD*)&patchData[offset] == RGLP_MAGIC)  // RGLP
		offset += 4;
	DWORD dest = *(DWORD*)&patchData[offset];
	offset += 4;

	while(dest != 0xFFFFFFFF && offset < fileSize){
		DWORD numPatches = *(DWORD*)&patchData[offset];
		offset += 4;
		for(DWORD i = 0; i < numPatches; i++, offset += 4, dest += 4) {
			// printf("     %08X  -> 0x%08X\n", dest, *(DWORD*)&buffer[offset]);
			setmem(dest, *(DWORD*)&patchData[offset]);
		}
		dest = *(DWORD*)&patchData[offset];
		offset += 4;
		patchesApplied++;
	}
	return patchesApplied;
}


void PatchSearchBinary(void) {
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;

	RGLPrint("INFO", " * Searching for additional RGLP binary patch files\n");

	// HDD
	hFind = FindFirstFile("HDD:\\*.rglp", &FindFileData);
	while (hFind != INVALID_HANDLE_VALUE) {
		RGLPrint("INFO", "  **located binary: %s\n", FindFileData.cFileName);

		if (PatchApplyBinary("HDD:\\" + (string)FindFileData.cFileName) <= 0)
			RGLPrint("ERROR", "Cannot apply patch\n");

		if (!FindNextFile(hFind, &FindFileData))
			hFind = INVALID_HANDLE_VALUE;
	}

	// USB
	hFind = FindFirstFile("Mass0:\\*.rglp", &FindFileData);
	while (hFind != INVALID_HANDLE_VALUE) {
		RGLPrint("INFO", "  **located binary: %s\n", FindFileData.cFileName);

		if (PatchApplyBinary("Mass0:\\" + (string)FindFileData.cFileName) <= 0)
			RGLPrint("ERROR", "Cannot apply patch\n");

		if (!FindNextFile(hFind, &FindFileData))
			hFind = INVALID_HANDLE_VALUE;
	}
}

VOID LoadPlugins() {
	string temp = reader->GetString("Plugins", "Plugin1", "none");
	if(temp != "none" && FileExists(temp.c_str())) {
		if (XexLoadImage(temp.c_str(), 8, 0, NULL))
			RGLPrint("ERROR", "Failed to load %s", temp.c_str());
	}
	temp = reader->GetString("Plugins", "Plugin2", "none");
	if (temp != "none" && FileExists(temp.c_str())) {
		if (XexLoadImage(temp.c_str(), 8, 0, NULL))
			RGLPrint("ERROR", "Failed to load %s", temp.c_str());
	}
	temp = reader->GetString("Plugins", "Plugin3", "none");
	if (temp != "none" && FileExists(temp.c_str())) {
		if (XexLoadImage(temp.c_str(), 8, 0, NULL))
			RGLPrint("ERROR", "Failed to load %s", temp.c_str());
	}
	temp = reader->GetString("Plugins", "Plugin4", "none");
	if (temp != "none" && FileExists(temp.c_str())) {
		if (XexLoadImage(temp.c_str(), 8, 0, NULL))
			RGLPrint("ERROR", "Failed to load %s", temp.c_str());
	}
	temp = reader->GetString("Plugins", "Plugin5", "none");
	if (temp != "none" && FileExists(temp.c_str())) {
		if(XexLoadImage(temp.c_str(), 8, 0, NULL))
			RGLPrint("ERROR", "Failed to load %s", temp.c_str());
	}
}

BOOL Initialize(HANDLE hModule) {
	RGLPrint("INFO", "===RGLoader Runtime Patcher - Version 02===\n");

	Mount("\\Device\\Harddisk0\\Partition1", "\\System??\\Hdd:");
	Mount("\\Device\\Harddisk0\\Partition1", "\\System??\\HDD:");

	Mount("\\Device\\Mass0", "\\System??\\Mass0:");

	// Mount("\\SystemRoot", "\\System??\\Root:");

	// install the expansion
	fExpansionEnabled = (ExpansionStuff() == TRUE);
	
	// check for ini
	reader = new INIReader("Mass0:\\rgloader.ini");
	if(reader->ParseError() < 0)
		reader = new INIReader("Hdd:\\rgloader.ini");
	if(reader->ParseError() < 0) {
		RGLPrint("ERROR", "Unable to open ini file!\n");
		MountAllDrives();
		fKeepMemory = false;
		return FALSE;
	}

	// booleans - config
	if (!reader->GetBoolean("Config", "NoRGLP", false))
		PatchSearchBinary();
	if (reader->GetBoolean("Config", "RPC", false)) {
		if (fExpansionEnabled)
			RPCServerStartup();
		else
			RGLPrint("INFO", "RPC is enabled in the config but the expansion isn't installed!\n");
	}
	// booleans - expansion
	if (reader->GetBoolean("Expansion", "MountAllDrives", false))
		MountAllDrives();
	if (reader->GetBoolean("Expansion", "PersistentPatches", false))
		PatchHookXexLoad();
	if (reader->GetBoolean("Expansion", "BootAnimation", false) && FileExists("Root:\\RGL_bootanim.xex"))
		PatchDefaultDash("\\SystemRoot\\RGL_bootanim.xex");
	if (reader->GetBoolean("Expansion", "RetailProfileEncryption", false))
		XamProfileCryptoHookSetup();
	// booleans - protections
	if (reader->GetBoolean("Protections", "BlockLiveDNS", false))
		PatchBlockLIVE();
	bool disableExpansionInstall = reader->GetBoolean("Protections", "DisableExpansionInstall", true);
	bool disableShadowboots = reader->GetBoolean("Protections", "DisableShadowboot", true);
	
	// strings
	string defaultDashboard = reader->GetString("Config", "DefaultDashboard", "none");
	if (defaultDashboard != "none" && FileExists(defaultDashboard.c_str()))
		PatchDefaultDash(defaultDashboard);

	RGLPrint("INFO", "Patches successfully applied!\n");

	if (fExpansionEnabled) {
		FuseStuff();
		KeyVaultStuff();

		if (disableExpansionInstall) {
			if (DisableExpansionInstalls() == TRUE)
				RGLPrint("PROTECTIONS", "HvxExpansionInstall unpatched successfully!\n");
		}

		if (disableShadowboots) {
			if (DisableShadowbooting() == TRUE)
				RGLPrint("PROTECTIONS", "HvxShadowboot disabled!\n");
		}
	}

	// skip plugin loading
	DVD_TRAY_STATE dts = XamLoaderGetDvdTrayState();
	if (dts == DVD_TRAY_STATE_OPENING || dts == DVD_TRAY_STATE_OPEN) {
		RGLPrint("INFO", "Skipping RGLoader plugin init...\n");
		return TRUE;
	}

	/*
	// Thanks to Diamond!
	DeleteLink("SysExt:", FALSE);
	DeleteLink("SysAux:", FALSE);

	char* extPath = "\\Device\\Harddisk0\\Partition1\\fs\\ext\\";
	char* auxPath = "\\Device\\Harddisk0\\Partition1\\fs\\aux\\";

	// create the directories used for aux/ext/flash
	CreateDirectory("\\Device\\Harddisk0\\Partition1\\fs\\", NULL);
	CreateDirectory(extPath, NULL);
	CreateDirectory(auxPath, NULL);

	// set paths
	strcpy((PCHAR)0x816090A8, "\\Device\\Harddisk0\\Partition1\\fs\\ext");
	strcpy((PCHAR)0x816090D0, "\\Device\\Harddisk0\\Partition1\\fs\\aux");
	strcpy((PCHAR)0x816106E0, extPath);
	strcpy((PCHAR)0x81610744, auxPath);
	*/

	// load plugins after expansion shit
	RGLPrint("INFO", "Loading plugins...\n");
	LoadPlugins();

	return TRUE;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD dwReason, LPVOID lpReserved) {
	if(dwReason == DLL_PROCESS_ATTACH) {
		Initialize(hModule);

		//set load count to 1
		if(!fKeepMemory) {
			*(WORD*)((DWORD)hModule + 64) = 1;
			return FALSE;
		} else return TRUE;
	}
	return TRUE;
}



