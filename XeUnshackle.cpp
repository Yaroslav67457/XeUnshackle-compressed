/********************************************************************************
 * XeUnshackle HEADLESS — single translation unit
 * Contents: main, Utils, Locale, Dashlaunch, HVFuncs, FreebootPatches
 * No GUI / video / audio / ATG
 ********************************************************************************/


/********************************************************************************
 * LOCALE (English only, XNotify strings)
 ********************************************************************************/

#include "stdafx.h"

LocalisationMessages_t english = {
	// Error Messagebox
	L"Unfortunately an error occurred while running Stage %i\n\nApp will now exit",
	L"Understood",

	// Dashlaunch
	L"Dashlaunch: Loaded! %ls",
	L"[HDD mode]",
	L"[USB mode] DO NOT UNPLUG THE USB!",
	L"Dashlaunch: FAILED! - Section data error (lhelper)",
	L"Dashlaunch: FAILED! - Mount error",
	L"Dashlaunch: FAILED! - Write lhelper failed",
	L"Dashlaunch: FAILED! - Section data error (launch)",
	L"Dashlaunch: FAILED! - Status: %X",

	// Save console info
	L"Saved to ConsoleInfo.txt",
	L"Saving Failed!",

	// MAC backup
	L"Original MAC dumped to OriginalMACAddress.bin",
	L"Failed to dump original MAC!",
};

LocalisationMessages_t* currentLocalisation = &english;

VOID SetLocale()
{
	currentLocalisation = &english;
}

/********************************************************************************
 * UTILS — UART log, mounts, files, patches engine, XNotify, keys/MAC
 ********************************************************************************/

//=============================================================================
// UART printing (Dashlaunch-style) — for debug output only
//=============================================================================
DWORD g_spinvar = 0;

char xtoa(unsigned char inp)
{
	return (inp < 0xa) ? (char)(inp + 0x30) : (char)(inp + 0x57);
}

VOID __declspec(naked) cygPut(char ch)
{
	__asm
	{
		mflr    r12
		stw     r12, -8(r1)
		std     r31, -0x10(r1)
		stwu    r1, -0x18(r1)
		mr      r31, r4

		lis 	r4, 0x7fea
		slwi 	r3, r3, 24
		stw 	r3, 0x1014(r4)
waiter:
		lwz 	r3, 0x1018(r4)
		rlwinm.	r3, r3, 0, 6, 6
		beq 	waiter

		mr      r4, r31
		addi    r1, r1, 0x18
		lwz     r12, -8(r1)
		mtlr    r12
		ld      r31, -10h(r1)
		blr
	}
}

VOID cprintf(const char* s, ...)
{
	va_list argp;
	char temp[512];
	BYTE irql;

	va_start(argp, s);
	vsnprintf(temp, 512, s, argp);
	va_end(argp);

	irql = KfAcquireSpinLock(&g_spinvar);
	for (int i = 0; temp[i] != 0; i++)
		cygPut(temp[i]);
	cygPut(0x0D);
	cygPut(0x0A);
	KfReleaseSpinLock(&g_spinvar, irql);
}

//=============================================================================
// Mount helpers (Usb / Hdd) — required by Dashlaunch
//=============================================================================
#define OBJ_USR_STRING	"\\??\\%s"
#define MOUNT_USB		"Usb:"
#define DEVICE_USB0		"\\Device\\Mass0"
#define MOUNT_HDD		"Hdd:"
#define DEVICE_HDD		"\\Device\\Harddisk0\\Partition1"

static HRESULT CreateMount(const char* szDrive, const char* szDevice)
{
	STRING DeviceName, LinkName;
	CHAR szDestinationDrive[MAX_PATH];
	sprintf_s(szDestinationDrive, MAX_PATH, OBJ_USR_STRING, szDrive);
	RtlInitAnsiString(&DeviceName, szDevice);
	RtlInitAnsiString(&LinkName, szDestinationDrive);
	ObDeleteSymbolicLink(&LinkName);
	return (HRESULT)ObCreateSymbolicLink(&LinkName, &DeviceName);
}

BOOL MountUsb()
{
	return CreateMount(MOUNT_USB, DEVICE_USB0) == 0;
}

BOOL MountHdd()
{
	return CreateMount(MOUNT_HDD, DEVICE_HDD) == 0;
}

//=============================================================================
// File helpers
//=============================================================================
BOOL FileExists(PCHAR path)
{
	OBJECT_ATTRIBUTES obAtrib;
	FILE_NETWORK_OPEN_INFORMATION netInfo;
	STRING filePath;
	RtlInitAnsiString(&filePath, path);
	InitializeObjectAttributes(&obAtrib, &filePath, 0x40, NULL);
	if (path[0] != '\\')
		obAtrib.RootDirectory = (HANDLE)0xFFFFFFFD;
	if (NT_SUCCESS(NtQueryFullAttributesFile(&obAtrib, &netInfo)))
	{
		if ((netInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			return TRUE;
	}
	return FALSE;
}

MemoryBuffer::MemoryBuffer(DWORD dwSize)
{
	m_pBuffer = NULL;
	m_dwDataLength = 0;
	m_dwBufferSize = 0;
	if (dwSize != 0 && dwSize < UINT_MAX)
	{
		m_pBuffer = (BYTE*)malloc(dwSize + 1);
		if (m_pBuffer)
		{
			m_dwBufferSize = dwSize;
			m_pBuffer[0] = 0;
		}
	}
}

MemoryBuffer::~MemoryBuffer()
{
	if (m_pBuffer) free(m_pBuffer);
	m_pBuffer = NULL;
	m_dwDataLength = 0;
	m_dwBufferSize = 0;
}

BOOL MemoryBuffer::CheckSize(DWORD dwSize)
{
	if (m_dwBufferSize >= (m_dwDataLength + dwSize))
		return TRUE;
	DWORD dwNewSize = max(m_dwDataLength + dwSize, m_dwBufferSize * 2);
	BYTE* pNew = (BYTE*)realloc(m_pBuffer, dwNewSize + 1);
	if (!pNew) return FALSE;
	m_pBuffer = pNew;
	m_dwBufferSize = dwNewSize;
	return TRUE;
}

BOOL MemoryBuffer::Add(const void* p, DWORD dwSize)
{
	if (!CheckSize(dwSize)) return FALSE;
	memcpy(m_pBuffer + m_dwDataLength, p, dwSize);
	m_dwDataLength += dwSize;
	m_pBuffer[m_dwDataLength] = 0;
	return TRUE;
}

BYTE* MemoryBuffer::GetData() const { return m_pBuffer; }
DWORD MemoryBuffer::GetDataLength() const { return m_dwDataLength; }
VOID MemoryBuffer::Rewind() { m_dwDataLength = 0; if (m_pBuffer) m_pBuffer[0] = 0; }

BOOL CReadFile(const CHAR* FileName, MemoryBuffer& pBuffer)
{
	HANDLE hFile = CreateFile(FileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		cprintf("CReadFile - CreateFile failed");
		return FALSE;
	}
	DWORD dwFileSize = GetFileSize(hFile, NULL);
	PBYTE lpBuffer = (BYTE*)malloc(dwFileSize);
	if (!lpBuffer)
	{
		CloseHandle(hFile);
		cprintf("CReadFile - malloc failed");
		return FALSE;
	}
	DWORD dwRead = 0;
	if (!ReadFile(hFile, lpBuffer, dwFileSize, &dwRead, NULL) || dwRead != dwFileSize)
	{
		free(lpBuffer);
		CloseHandle(hFile);
		cprintf("CReadFile - ReadFile failed");
		return FALSE;
	}
	CloseHandle(hFile);
	pBuffer.Add(lpBuffer, dwFileSize);
	free(lpBuffer);
	return TRUE;
}

BOOL CWriteFile(const CHAR* FilePath, const VOID* Data, DWORD Size)
{
	HANDLE fHandle = CreateFile(FilePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fHandle == INVALID_HANDLE_VALUE)
	{
		cprintf("[CWriteFile] CreateFile failed. Error: %X", GetLastError());
		return FALSE;
	}
	DWORD writeSize = Size;
	if (!WriteFile(fHandle, Data, writeSize, &writeSize, NULL))
	{
		cprintf("[CWriteFile] WriteFile failed. Error: %X", GetLastError());
		CloseHandle(fHandle);
		return FALSE;
	}
	CloseHandle(fHandle);
	return TRUE;
}

//=============================================================================
// Patch engine
//=============================================================================
static CRITICAL_SECTION patchsec;
static BOOL bCritSecInit = FALSE;
#define MAX_SPINS_DEFAULT 65280

DWORD ApplyPatches(CHAR* FilePath, const VOID* DefaultPatches)
{
	if (!bCritSecInit)
	{
		RtlInitializeCriticalSectionAndSpinCount(&patchsec, MAX_SPINS_DEFAULT);
		bCritSecInit = TRUE;
	}

	DWORD patchCount = 0;
	MemoryBuffer mbPatches;
	DWORD* patchData = (DWORD*)DefaultPatches;

	if (FilePath != NULL && FileExists(FilePath))
	{
		if (!CReadFile(FilePath, mbPatches))
		{
			cprintf("ApplyPatches - CReadFile failed");
			return 0;
		}
		patchData = (DWORD*)mbPatches.GetData();
	}

	if (patchData == NULL)
		return 0;

	RtlEnterCriticalSection(&patchsec);
	while (*patchData != 0xFFFFFFFF)
	{
		BOOL inHvMode = (patchData[0] < 0x40000);
		QWORD patchAddr = inHvMode
			? (0x200000000ULL * (patchData[0] / 0x10000)) + patchData[0]
			: (QWORD)patchData[0];

		if (inHvMode)
		{
			cprintf("Applying HV patch at %X", (DWORD)patchAddr);
			Hvx::HvPokeBytes(patchAddr, &patchData[2], patchData[1] * sizeof(DWORD));
		}
		else
		{
			cprintf("Applying Kernel patch at %X", patchData[0]);
			memcpy((VOID*)patchData[0], &patchData[2], patchData[1] * sizeof(DWORD));
			__dcbst(0, (VOID*)patchData[0]);
			__sync();
			__isync();
		}
		patchData += (patchData[1] + 2);
		patchCount++;
	}
	RtlLeaveCriticalSection(&patchsec);
	return patchCount;
}

//=============================================================================
// Feedback (no media / no GUI loop — only MessageBox + XNotify)
//=============================================================================
static MESSAGEBOX_RESULT result;
static XOVERLAPPED overlapped;

VOID ShowErrorAndExit(int Stage)
{
	LPCWSTR buttons[1] = { currentLocalisation->ExitConfirm };
	WCHAR maintext[100];
	swprintf_s(maintext, currentLocalisation->ErrorandExit, Stage);

	if (XShowMessageBoxUI(0, L"XeUnshackle - Error", maintext, 1, buttons, 0, XMB_ERRORICON, &result, &overlapped) == ERROR_IO_PENDING)
	{
		while (!XHasOverlappedIoCompleted(&overlapped))
			Sleep(50);
	}
	XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
}

#define NAME_XAM "xam.xex"
#define XNotifyQueueUI_ORD 656
typedef VOID(*XNotifyQueueUI)(int type, DWORD dwUserIndex, ULONGLONG qwAreas, PWCHAR displayText, PVOID contextData);
static XNotifyQueueUI XNotify = NULL;

VOID ShowNotify(PWCHAR NotifyText)
{
	if (!XNotify)
	{
		XNotify = (XNotifyQueueUI)GetProcAddress(GetModuleHandle(NAME_XAM), (LPCSTR)XNotifyQueueUI_ORD);
		if (!XNotify) return;
	}
	XNotify(14, 0, 2, NotifyText, NULL);
}

//=============================================================================
// Console type / keys / MAC
//=============================================================================
#define CONSOLE_TYPE_XENON		0x00000000
#define CONSOLE_TYPE_ZEPHYR		0x10000000
#define CONSOLE_TYPE_FALCON		0x20000000
#define CONSOLE_TYPE_JASPER		0x30000000
#define CONSOLE_TYPE_TRINITY	0x40000000
#define CONSOLE_TYPE_CORONA		0x50000000
#define CONSOLE_TYPE_WINCHESTER	0x60000000
#define CONSOLE_TYPE_FLAGS_MASK	0xF0000000
#define CONSOLE_TYPE_FROM_FLAGS	(XboxHardwareInfo->Flags & CONSOLE_TYPE_FLAGS_MASK)

string GetMoboByHWFlags()
{
	switch (CONSOLE_TYPE_FROM_FLAGS)
	{
	case CONSOLE_TYPE_XENON:      return "Xenon";
	case CONSOLE_TYPE_ZEPHYR:     return "Zephyr";
	case CONSOLE_TYPE_FALCON:     return "Falcon";
	case CONSOLE_TYPE_JASPER:     return "Jasper";
	case CONSOLE_TYPE_TRINITY:    return "Trinity";
	case CONSOLE_TYPE_CORONA:     return "Corona";
	case CONSOLE_TYPE_WINCHESTER: return "Winchester";
	default:                      return "Unknown";
	}
}

extern WCHAR wCPUKeyBuf[];
extern WCHAR wDVDKeyBuf[];
extern WCHAR wConTypeBuf[];

VOID SaveConsoleDataToFile()
{
	char ConsoleInfoBuf[160];
	ZeroMemory(ConsoleInfoBuf, sizeof(ConsoleInfoBuf));
	sprintf_s(ConsoleInfoBuf, "%ws\n%ws\n%ws\n", wConTypeBuf, wCPUKeyBuf, wDVDKeyBuf);

	if (CWriteFile("GAME:\\ConsoleInfo.txt", ConsoleInfoBuf, sizeof(ConsoleInfoBuf)))
		ShowNotify(currentLocalisation->SaveConInfo_Success);
	else
		ShowNotify(currentLocalisation->SaveConInfo_Failed);
}

#define MACFilePath "GAME:\\OriginalMACAddress.bin"
WORD XCONFIG_SECURED_CATEGORY = 0x2;
WORD XCONFIG_SECURED_MAC_ADDRESS = 0x1;

VOID BackupOrigMAC()
{
	if (FileExists((PCHAR)MACFilePath))
	{
		cprintf("[BackupOrigMAC] MAC backup already exists. Skipping...");
		return;
	}

	BYTE OrigMACAddress[6];
	if (NT_SUCCESS(ExGetXConfigSetting(XCONFIG_SECURED_CATEGORY, XCONFIG_SECURED_MAC_ADDRESS, OrigMACAddress, 6, NULL)))
	{
		if (CWriteFile(MACFilePath, OrigMACAddress, sizeof(OrigMACAddress)))
			ShowNotify(currentLocalisation->Dump_MAC_Success);
		else
			ShowNotify(currentLocalisation->Dump_MAC_Fail);
	}
}

/********************************************************************************
 * HV FUNCS — Peek/Poke expansion, freeboot HV helpers
 ********************************************************************************/

#pragma warning(push)
#pragma warning(disable:4826) // Get rid of the sign-extended warning

#define HvxCall QWORD _declspec(naked)
DWORD HvPeekPokeExpID = 0x48565050;
namespace Hvx
{
	const BYTE HvPeekPokeExp[] = {
		0x48, 0x58, 0x50, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x34, 0x1F, 0xD6, 0xDA,
		0x2F, 0xCA, 0xA8, 0x17, 0xF0, 0x30, 0xCC, 0x44, 0x0A, 0x41, 0xFA, 0x7C, 0xA0, 0xC1, 0xFD, 0x33,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x7A, 0x02, 0x59, 0x43, 0x9E, 0xE6, 0x93, 0xD5, 0x01, 0xC9, 0x48, 0x4D, 0xB2, 0xBF, 0x9D, 0x18,
		0xA9, 0x16, 0x5E, 0xFF, 0x1E, 0xD5, 0xB6, 0xA8, 0x79, 0x60, 0xA8, 0x2F, 0xC1, 0x8D, 0x20, 0x8A,
		0xEB, 0x46, 0xC3, 0x01, 0xEC, 0xC4, 0xDB, 0xDF, 0xA1, 0x04, 0xD1, 0xDF, 0x23, 0x69, 0x5E, 0xCC,
		0x50, 0xC3, 0xF4, 0xDD, 0xA4, 0x80, 0x7D, 0x05, 0x2D, 0x57, 0xFF, 0x60, 0xA5, 0x58, 0x69, 0x27,
		0x9A, 0x33, 0x70, 0xE8, 0xC2, 0x82, 0xDC, 0xDA, 0xE2, 0x4D, 0xE1, 0xF8, 0xA1, 0xD2, 0xCC, 0x8D,
		0x98, 0x05, 0xD1, 0xA3, 0x2E, 0x37, 0x06, 0x0F, 0xF7, 0xEF, 0x70, 0xEF, 0x28, 0xD7, 0xAE, 0xD5,
		0x5B, 0x19, 0x1B, 0x13, 0x50, 0x0D, 0x15, 0x00, 0x92, 0x01, 0xEE, 0x38, 0xA7, 0x61, 0xB1, 0xA1,
		0x27, 0xBF, 0xB0, 0x42, 0x0F, 0x07, 0xC3, 0x8F, 0x78, 0xAD, 0xAF, 0x87, 0x8F, 0x34, 0x8A, 0xA6,
		0x69, 0x7D, 0x40, 0xB6, 0x5C, 0x9E, 0x01, 0xEC, 0x9F, 0x5E, 0x78, 0x30, 0x09, 0x5A, 0x21, 0xFF,
		0x35, 0x8C, 0x13, 0xBE, 0xBC, 0x92, 0x67, 0xE3, 0x17, 0x0B, 0x09, 0x1C, 0xE2, 0x9D, 0xEC, 0xFD,
		0xFB, 0x6C, 0x49, 0x3A, 0xCC, 0xE7, 0x99, 0xB6, 0xB3, 0x8A, 0x8F, 0xEF, 0xF7, 0xA0, 0x28, 0x4F,
		0x72, 0xC7, 0x3D, 0xD7, 0xCC, 0xEB, 0xB2, 0x1B, 0x74, 0x93, 0xD7, 0x02, 0x05, 0xD5, 0xE1, 0x25,
		0x35, 0xD7, 0xE0, 0x50, 0xEA, 0xCF, 0x82, 0x69, 0xE3, 0x3B, 0x6F, 0xFA, 0x13, 0xF0, 0x5B, 0xD9,
		0x1A, 0xB0, 0xD3, 0x0B, 0x85, 0x4D, 0x6A, 0xFC, 0x31, 0x45, 0xC3, 0xA3, 0xFA, 0x7E, 0xDA, 0x93,
		0x84, 0xD4, 0xE7, 0xFA, 0xAB, 0x7C, 0x22, 0x96, 0x54, 0x69, 0x7F, 0xC9, 0xF8, 0xC4, 0x5A, 0xC2,
		0x82, 0x99, 0x34, 0x46, 0x81, 0x6F, 0x06, 0x33, 0x19, 0x94, 0x74, 0xA4, 0x76, 0x02, 0x4F, 0xB9,
		0x48, 0x56, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x01, 0x90,
		0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00,
		0x7D, 0x88, 0x02, 0xA6, 0xFB, 0xE1, 0xFF, 0xF0, 0xF9, 0x81, 0xFF, 0xF8, 0x94, 0x21, 0xFF, 0x00,
		0x7C, 0x9F, 0x23, 0x78, 0x2C, 0x1F, 0x00, 0x00, 0x41, 0x82, 0x00, 0xC0, 0x2C, 0x1F, 0x00, 0x01,
		0x41, 0x82, 0x00, 0xC0, 0x2C, 0x1F, 0x00, 0x02, 0x41, 0x82, 0x00, 0xC0, 0x2C, 0x1F, 0x00, 0x03,
		0x41, 0x82, 0x00, 0xC0, 0x2C, 0x1F, 0x00, 0x0A, 0x41, 0x82, 0x00, 0x64, 0x2C, 0x1F, 0x00, 0x05,
		0x41, 0x82, 0x00, 0xD4, 0x2C, 0x1F, 0x00, 0x06, 0x41, 0x82, 0x00, 0xD4, 0x2C, 0x1F, 0x00, 0x07,
		0x41, 0x82, 0x00, 0xD4, 0x2C, 0x1F, 0x00, 0x08, 0x41, 0x82, 0x00, 0xD4, 0x2C, 0x1F, 0x00, 0x0B,
		0x40, 0x80, 0x00, 0x00, 0x80, 0x60, 0x00, 0x4C, 0x7C, 0x62, 0x1A, 0x14, 0x80, 0x63, 0x00, 0x08,
		0x7C, 0x69, 0x03, 0xA6, 0x7C, 0xC3, 0x33, 0x78, 0x38, 0x80, 0x00, 0x01, 0x4E, 0x80, 0x04, 0x21,
		0x7C, 0x66, 0x1B, 0x78, 0x7C, 0xE9, 0x03, 0xA6, 0x2C, 0x1F, 0x00, 0x04, 0x41, 0x82, 0x00, 0x6C,
		0x2C, 0x1F, 0x00, 0x09, 0x41, 0x82, 0x00, 0xA0, 0x48, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x05,
		0x7C, 0xA6, 0x2B, 0x78, 0x3C, 0x80, 0x7C, 0x60, 0x50, 0xA6, 0x54, 0x6A, 0x60, 0x84, 0x02, 0xA6,
		0x50, 0xC4, 0x32, 0xE8, 0x7C, 0x68, 0x02, 0xA6, 0x38, 0x63, 0x00, 0x30, 0x90, 0x83, 0x00, 0x00,
		0x7C, 0x00, 0x18, 0x6C, 0x7C, 0x00, 0x1F, 0xAC, 0x7C, 0x00, 0x04, 0xAC, 0x4C, 0x00, 0x01, 0x2C,
		0x7C, 0x79, 0x4A, 0xA6, 0x48, 0x00, 0x00, 0x98, 0x88, 0x65, 0x00, 0x00, 0x48, 0x00, 0x00, 0x90,
		0xA0, 0x65, 0x00, 0x00, 0x48, 0x00, 0x00, 0x88, 0x80, 0x65, 0x00, 0x00, 0x48, 0x00, 0x00, 0x80,
		0xE8, 0x65, 0x00, 0x00, 0x48, 0x00, 0x00, 0x78, 0x8B, 0xE5, 0x00, 0x00, 0x9B, 0xE6, 0x00, 0x00,
		0x38, 0xA5, 0x00, 0x01, 0x38, 0xC6, 0x00, 0x01, 0x42, 0x00, 0xFF, 0xF0, 0x38, 0x60, 0x00, 0x00,
		0x48, 0x00, 0x00, 0x5C, 0x98, 0xC5, 0x00, 0x00, 0x48, 0x00, 0x00, 0x40, 0xB0, 0xC5, 0x00, 0x00,
		0x48, 0x00, 0x00, 0x38, 0x90, 0xC5, 0x00, 0x00, 0x48, 0x00, 0x00, 0x30, 0xF8, 0xC5, 0x00, 0x00,
		0x48, 0x00, 0x00, 0x28, 0x8B, 0xE6, 0x00, 0x00, 0x9B, 0xE5, 0x00, 0x00, 0x7C, 0x00, 0x28, 0x6C,
		0x7C, 0x00, 0x2F, 0xAC, 0x7C, 0x00, 0x04, 0xAC, 0x4C, 0x00, 0x01, 0x2C, 0x38, 0xA5, 0x00, 0x01,
		0x38, 0xC6, 0x00, 0x01, 0x42, 0x00, 0xFF, 0xE0, 0x38, 0x60, 0x00, 0x00, 0x7C, 0x00, 0x28, 0x6C,
		0x7C, 0x00, 0x2F, 0xAC, 0x7C, 0x00, 0x04, 0xAC, 0x4C, 0x00, 0x01, 0x2C, 0x38, 0x21, 0x01, 0x00,
		0xEB, 0xE1, 0xFF, 0xF0, 0xE9, 0x81, 0xFF, 0xF8, 0x7D, 0x88, 0x03, 0xA6, 0x4E, 0x80, 0x00, 0x20
	};

	namespace SysCalls
	{
		static HvxCall HvxGetVersion(DWORD magic, DWORD mode, UINT64 dest, UINT64 src, UINT32 len, UINT64 arg_r8 = NULL) // Freeboot uses this as a backdoor
		{
			__asm
			{
				li r0, 0 // HvxGetVersion
				sc
				blr
			}
		}


		//static HvxCall HvxPostOutput(BYTE code)
		static HvxCall HvxPostOutput(BYTE code, UINT64 shellCodeAddress) // BadUpdate exploit uses this as a backdoor
		{
			__asm
			{
				li	r0, 0xD
				sc
				blr
			}
		}

		static HvxCall HvxKeysExecute(PVOID pvPayload, DWORD cbPayload, QWORD Arg1, QWORD Arg2, QWORD Arg3, QWORD Arg4)
		{
			__asm
			{
				li	r0, 0x40
				sc
				blr
			}
		}

		static HvxCall HvxExpansionInstall(DWORD PhysicalAddress, DWORD CodeSize) {
			__asm
			{
			li	r0, 0x72
			sc
			blr
			}
		}

		static HvxCall HvxExpansionCall(DWORD ExpansionId, QWORD Param1 = 0, QWORD Param2 = 0, QWORD Param3 = 0, QWORD Param4 = 0) {
			__asm
			{
				li	r0, 0x73
				sc
				blr
			}
		}

	}

	HRESULT InitHvPeekPokeExp()
	{
		cprintf("Init HV expansion");
		// Allocate physical memory for this expansion
		VOID* pPhysExp = XPhysicalAlloc(0x1000, MAXULONG_PTR, 0, PAGE_READWRITE);
		DWORD physExpAdd = (DWORD)MmGetPhysicalAddress(pPhysExp);

		// Copy over our expansion data
		ZeroMemory(pPhysExp, 0x1000);
		memcpy(pPhysExp, HvPeekPokeExp, sizeof(HvPeekPokeExp));

		HRESULT result = (HRESULT)HvExpansionInstall(physExpAdd, 0x1000);

		// Free our allocated data
		XPhysicalFree(pPhysExp);

		// Return our install result
		return result;
	}

	// wrappers for syscalls (for organization)
	QWORD HvGetVersion(DWORD magic, DWORD mode, UINT64 dest, UINT64 src, UINT32 len, UINT64 arg_r8)
	{
		return SysCalls::HvxGetVersion(magic, mode, dest, src, len, arg_r8);
	}
	//QWORD HvPostOutput(BYTE code)
	QWORD HvPostOutput(BYTE code, UINT64 shellCodeAddress)
	{
		return SysCalls::HvxPostOutput(code, shellCodeAddress);
	}
	QWORD HvKeysExecute(PVOID pvPayload, DWORD cbPayload, QWORD arg1 = NULL, QWORD arg2 = NULL, QWORD arg3 = NULL, QWORD arg4 = NULL)
	{
		return SysCalls::HvxKeysExecute(pvPayload, cbPayload, arg1, arg2, arg3, arg4);
	}
	QWORD HvExpansionInstall(DWORD PhysicalAddress, DWORD CodeSize)
	{
		return SysCalls::HvxExpansionInstall(PhysicalAddress, CodeSize);
	}
	QWORD HvExpansionCall(DWORD ExpansionId, QWORD Param1 = NULL, QWORD Param2 = NULL, QWORD Param3 = NULL, QWORD Param4 = NULL)
	{
		return SysCalls::HvxExpansionCall(ExpansionId, Param1, Param2, Param3, Param4);
	}

	BYTE    HvPeekBYTE(QWORD Address) {
		return (BYTE)HvExpansionCall(HvPeekPokeExpID, PEEK_BYTE, Address);
	}
	WORD    HvPeekWORD(QWORD Address) {
		return (WORD)HvExpansionCall(HvPeekPokeExpID, PEEK_WORD, Address);
	}
	DWORD   HvPeekDWORD(QWORD Address) {
		return (DWORD)HvExpansionCall(HvPeekPokeExpID, PEEK_DWORD, Address);
	}
	QWORD   HvPeekQWORD(QWORD Address) {
		return HvExpansionCall(HvPeekPokeExpID, PEEK_QWORD, Address);
	}
	HRESULT HvPeekBytes(QWORD Address, PVOID Buffer, DWORD Size) {

		// Create a physical buffer to peek into
		VOID* data = XPhysicalAlloc(Size, MAXULONG_PTR, 0, PAGE_READWRITE);
		ZeroMemory(data, Size);

		HRESULT result = (HRESULT)HvExpansionCall(HvPeekPokeExpID,
			PEEK_BYTES, Address, (QWORD)MmGetPhysicalAddress(data), Size);

		// If its successful copy it back
		if (result == S_OK) memcpy(Buffer, data, Size);

		// Free our physical data and return our result
		XPhysicalFree(data);
		return result;
	}
	HRESULT HvPokeBYTE(QWORD Address, BYTE Value) {
		return (HRESULT)HvExpansionCall(HvPeekPokeExpID, POKE_BYTE, Address, Value);
	}
	HRESULT HvPokeWORD(QWORD Address, WORD Value) {
		return (HRESULT)HvExpansionCall(HvPeekPokeExpID, POKE_WORD, Address, Value);
	}
	HRESULT HvPokeDWORD(QWORD Address, DWORD Value) {
		return (HRESULT)HvExpansionCall(HvPeekPokeExpID, POKE_DWORD, Address, Value);
	}
	HRESULT HvPokeQWORD(QWORD Address, QWORD Value) {
		return (HRESULT)HvExpansionCall(HvPeekPokeExpID, POKE_QWORD, Address, Value);
	}
	HRESULT HvPokeBytes(QWORD Address, PVOID Buffer, DWORD Size) {

		// Create a physical buffer to poke from
		VOID* data = XPhysicalAlloc(Size, MAXULONG_PTR, 0, PAGE_READWRITE);
		memcpy(data, Buffer, Size);

		HRESULT result = (HRESULT)HvExpansionCall(HvPeekPokeExpID,
			POKE_BYTES, Address, (QWORD)MmGetPhysicalAddress(data), Size);

		// Free our physical data and return our result
		XPhysicalFree(data);
		return result;
	}
	QWORD HvPeekSPR(SOC_SPRS SPR) {
		return HvExpansionCall(HvPeekPokeExpID, PEEK_SPR, SPR, 0, 0);
	}
	QWORD HvPokeSPR(SOC_SPRS SPR, QWORD Value) {
		return HvExpansionCall(HvPeekPokeExpID, POKE_SPR, SPR, Value, 0);
	}
	QWORD HvGetFuseline(DWORD fuse)
	{
		if ((fuse * 0x40) < 0x300)
			return HvPeekQWORD(SPACE_FUSES + ((fuse * 0x40) << 3));
		return 0;
	}

	// DUMP 1BL ROM

	DWORD Dump1blMagic()
	{
		DWORD Magic = HvPeekDWORD(MEM_1BL_LOC);
		cprintf("1BL Magic: %08X", Magic);
		return Magic;
	}

	HRESULT HvDump1blRom(PVOID Buffer) {
		cprintf("Dumping 1bl...");
		// Create a physical buffer to peek into
		VOID* data = XPhysicalAlloc(MEM_1BL_SZ, MAXULONG_PTR, 0, PAGE_READWRITE);
		ZeroMemory(data, MEM_1BL_SZ);

		HRESULT result = (HRESULT)HvExpansionCall(HvPeekPokeExpID,
			PEEK_BYTES, MEM_1BL_LOC, (QWORD)MmGetPhysicalAddress(data), MEM_1BL_SZ);

		// If its successful copy it back
		if (result == S_OK) memcpy(Buffer, data, MEM_1BL_SZ);

		// Free our physical data and return our result
		XPhysicalFree(data);
		return result;
	}

	// Check if we have access via the peek poke expansion
	BOOL CheckPPExpHVAccess()
	{
		DWORD Magic = HvPeekWORD(0);
		cprintf("[CheckPPExpHVAccess] HV Magic: %04X", Magic);
		if (Magic != 0x5E4E && Magic != 0x4E4E) // Dev & Ret HV Magic
		{
			cprintf("[CheckPPExpHVAccess] FAIL");
			return FALSE;
		}
		cprintf("[CheckPPExpHVAccess] SUCCESS!");
		return TRUE;
	}

	BOOL SetupPPExpHVAccess()
	{
		HRESULT res = InitHvPeekPokeExp();

		if (res != ERROR_SUCCESS)
		{
			cprintf("[SetupPPExpHVAccess] FAIL");
			return FALSE;
		}
		
		cprintf("[SetupPPExpHVAccess] SUCCESS!");
		return TRUE;
	}


	BOOL ToggleMemProtect(BOOL state)
	{
		if (state)
		{
			if (Hvx::HvGetVersion(0x72627472, SET_PROT_ON) == 1)
			{
				cprintf("Memory protections re-enabled!");
				return TRUE;
			}
			else
				cprintf("Failed to set memory protection status!");
		}
		else
		{
			if (Hvx::HvGetVersion(0x72627472, SET_PROT_OFF) == 1)
			{
				cprintf("Memory protections disabled!");
				return TRUE;
			}
			else
				cprintf("Failed to set memory protection status!");
		}

		return FALSE; // If we reached here it failed...
	}

	// Payload that removes the checks on expansion install. I doubt this is the correct/best way to do this but w/e
	const BYTE REMOVE_EXP_CHECKS_PAYLOAD[] = {
		0x7D, 0x88, 0x02, 0xA6, 0xF9, 0x81, 0xFF, 0xF8, 0xFB, 0xC1, 0xFF, 0xE8, 0xFB, 0xE1, 0xFF, 0xF0,
		0x38, 0x21, 0xFF, 0xD0, 0x7C, 0x9F, 0x23, 0x78, 0x3C, 0x80, 0x40, 0x9A, 0x60, 0x84, 0x00, 0x08,
		0xE8, 0x7F, 0x00, 0x88, 0x90, 0x83, 0x00, 0x00, 0x38, 0xA0, 0x00, 0x7F, 0x7C, 0x63, 0x28, 0x78,
		0x7C, 0x00, 0x1F, 0xAC, 0x3C, 0x80, 0x3B, 0xA0, 0x60, 0x84, 0x00, 0x00, 0xE8, 0x7F, 0x00, 0x90,
		0x90, 0x83, 0x00, 0x00, 0x38, 0xA0, 0x00, 0x7F, 0x7C, 0x63, 0x28, 0x78, 0x7C, 0x00, 0x1F, 0xAC,
		0x3C, 0x80, 0x60, 0x00, 0x60, 0x84, 0x00, 0x00, 0xE8, 0x7F, 0x00, 0x98, 0x90, 0x83, 0x00, 0x00,
		0x38, 0xA0, 0x00, 0x7F, 0x7C, 0x63, 0x28, 0x78, 0x7C, 0x00, 0x1F, 0xAC, 0x38, 0x60, 0x12, 0x34,
		0x38, 0x21, 0x00, 0x30, 0xEB, 0xC1, 0xFF, 0xE8, 0xEB, 0xE1, 0xFF, 0xF0, 0xE9, 0x81, 0xFF, 0xF8,
		0x7D, 0x88, 0x03, 0xA6, 0x4E, 0x80, 0x00, 0x20, 0x80, 0x00, 0x01, 0x06, 0x00, 0x03, 0x08, 0x9C,
		0x80, 0x00, 0x01, 0x06, 0x00, 0x03, 0x08, 0xA0, 0x80, 0x00, 0x01, 0x06, 0x00, 0x03, 0x08, 0xA4,
		0x80, 0x00, 0x01, 0x06, 0x00, 0x03, 0x08, 0xA8
	};

	// Uses the backdoor added by BadUpdate exploit to give us easier access using traditional peek poke expansion
	BOOL DisableExpChecks()
	{

		DWORD dwBufferSize = (sizeof(REMOVE_EXP_CHECKS_PAYLOAD) + 0x7F) & ~0x7F;

		// Allocate memory for the clean update data buffer.
		BYTE* pBuffer = (BYTE*)XPhysicalAlloc(dwBufferSize, MAXULONG_PTR, 0x10000, PAGE_READWRITE | PAGE_NOCACHE | MEM_LARGE_PAGES);
		if (pBuffer == NULL)
		{
			cprintf("[DisableExpChecks] Failed to allocate memory for shell code data");
			return FALSE;
		}

		ZeroMemory(pBuffer, dwBufferSize);

		memcpy(pBuffer, REMOVE_EXP_CHECKS_PAYLOAD, sizeof(REMOVE_EXP_CHECKS_PAYLOAD));

		QWORD ShellCodePhys = 0x8000000000000000 | (QWORD)MmGetPhysicalAddress(pBuffer);

		cprintf("[DisableExpChecks] Attempting to run custom shellcode payload");
		// Execute code in hypervisor mode.
		QWORD Result = HvPostOutput(0, ShellCodePhys);

		cprintf("[DisableExpChecks] Result: %X", Result); // We set the result to 0x1234 at the end of the payload but there's no other return values

		return TRUE;
	}




}
#pragma warning(pop)

/********************************************************************************
 * FREEBOOT PATCHES — patch blobs + apply helpers
 ********************************************************************************/

// Check https://github.com/Byrom90/Xbox_360_Research/blob/main/xeBuild_Patches/KHV_17559/17559_KHV_Patchset.s for more patch notes. 

const BYTE FREEBOOT_HV_PRIMARY_PATCHES[] = {
    // Memory Protections Pt1
    0x00, 0x00, 0x15, 0x4C, 0x00, 0x00, 0x00, 0x04, 0x38, 0x80, 0x00, 0x07, 0x7C, 0x21, 0x20, 0x78, 0x7C, 0x35, 0xEB, 0xA6, 0x48, 0x00, 0x11, 0xC2,
    // Memory Protections Pt2
    0x00, 0x00, 0x11, 0xBC, 0x00, 0x00, 0x00, 0x01, 0x48, 0x00, 0x15, 0x4E,

    // HV peek poke etc by replacing HvxGetVers syscall
    0x00, 0x00, 0xB5, 0x10, 0x00, 0x00, 0x00, 0x48, 0x7D, 0x08, 0x02, 0xA6, 0xA0, 0x60, 0x00, 0x06,
    0x38, 0x80, 0x00, 0x21, 0x7C, 0x63, 0x20, 0x78, 0xB0, 0x60, 0x00, 0x06, 0x38, 0x60, 0x00, 0x21,
    0x48, 0x00, 0xB5, 0x3F, 0x38, 0x60, 0x00, 0x0A, 0x48, 0x00, 0xB5, 0x3F, 0x7D, 0x08, 0x03, 0xA6,
    0x48, 0x00, 0x02, 0xDA, 0x3C, 0x80, 0x80, 0x00, 0x60, 0x84, 0x02, 0x00, 0x78, 0x84, 0x07, 0xC6,
    0x64, 0x84, 0xEA, 0x00, 0x54, 0x63, 0xC0, 0x0E, 0x90, 0x64, 0x10, 0x14, 0x80, 0x64, 0x10, 0x18,
    0x54, 0x63, 0x01, 0x8D, 0x41, 0x82, 0xFF, 0xF8, 0x4E, 0x80, 0x00, 0x20, 0x3D, 0x60, 0x72, 0x62,
    0x61, 0x6B, 0x74, 0x72, 0x7F, 0x03, 0x58, 0x40, 0x41, 0x9A, 0x00, 0x08, 0x48, 0x00, 0x1C, 0xCA,
    0x2B, 0x04, 0x00, 0x04, 0x41, 0x99, 0x00, 0x94, 0x41, 0x9A, 0x00, 0x44, 0x38, 0xA0, 0x15, 0x4C,
    0x3C, 0xC0, 0x38, 0x80, 0x2B, 0x04, 0x00, 0x02, 0x40, 0x9A, 0x00, 0x0C, 0x60, 0xC6, 0x00, 0x07,
    0x48, 0x00, 0x00, 0x0C, 0x2B, 0x04, 0x00, 0x03, 0x40, 0x9A, 0x00, 0x1C, 0x38, 0x00, 0x00, 0x00,
    0x90, 0xC5, 0x00, 0x00, 0x7C, 0x00, 0x28, 0x6C, 0x7C, 0x00, 0x2F, 0xAC, 0x7C, 0x00, 0x04, 0xAC,
    0x4C, 0x00, 0x01, 0x2C, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20, 0x7D, 0x88, 0x02, 0xA6,
    0xF9, 0x81, 0xFF, 0xF8, 0xF8, 0x21, 0xFF, 0xF1, 0x7C, 0xA8, 0x03, 0xA6, 0x7C, 0xE9, 0x03, 0xA6,
    0x80, 0x86, 0x00, 0x00, 0x90, 0x85, 0x00, 0x00, 0x7C, 0x00, 0x28, 0x6C, 0x7C, 0x00, 0x2F, 0xAC,
    0x7C, 0x00, 0x04, 0xAC, 0x4C, 0x00, 0x01, 0x2C, 0x38, 0xA5, 0x00, 0x04, 0x38, 0xC6, 0x00, 0x04,
    0x42, 0x00, 0xFF, 0xE0, 0x4E, 0x80, 0x00, 0x20, 0x38, 0x21, 0x00, 0x10, 0xE9, 0x81, 0xFF, 0xF8,
    0x7D, 0x88, 0x03, 0xA6, 0x4E, 0x80, 0x00, 0x20, 0x2B, 0x04, 0x00, 0x05, 0x40, 0x9A, 0x00, 0x14,
    0x7C, 0xC3, 0x33, 0x78, 0x7C, 0xA4, 0x2B, 0x78, 0x7C, 0xE5, 0x3B, 0x78, 0x48, 0x00, 0xA8, 0x82,
    0x38, 0x60, 0x00, 0x02, 0x4E, 0x80, 0x00, 0x20,
    // Replace the actual syscall so it branches to custom function in the above patches
    0x00, 0x01, 0x5F, 0xD0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xB5, 0x64,

    0xFF, 0xFF, 0xFF, 0xFF
};

const BYTE FREEBOOT_HV_PATCHES[] = {// Prob don't need all of them but appears to work fine
    // ??? - Branches to a function added before the peek poke syscall in the patches above
    0x00, 0x00, 0x18, 0x80, 0x00, 0x00, 0x00, 0x01, 0x48, 0x00, 0xB5, 0x13, 0x00, 0x00, 0x00, 0xF0,
    // Devkit xex aes key
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ???
    0x00, 0x00, 0x31, 0x20, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // Disable fuse blowing ???
    0x00, 0x00, 0xA5, 0x60, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20,
    // HvxLoadImageData
    0x00, 0x02, 0xA3, 0x0C, 0x00, 0x00, 0x00, 0x02, 0x60, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,
    // HvxResolveImports - Bypass STATUS_REVISION_MISMATCH - C0000059
    0x00, 0x02, 0xAA, 0x80, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // HvxResolveImports - Bypass STATUS_REVISION_MISMATCH - C0000059
    0x00, 0x02, 0xAA, 0x8C, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // HvxSecuritySetDetected
    0x00, 0x00, 0x6B, 0xB0, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // HvxSecurityGetDetected
    0x00, 0x00, 0x6C, 0x48, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // HvxSecuritySetActivated
    0x00, 0x00, 0x6C, 0x98, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // HvxSecurityGetActivated
    0x00, 0x00, 0x6D, 0x08, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // HvxSecuritySetStat ???
    0x00, 0x00, 0x6D, 0x58, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // HvxKeysGetKey
    0x00, 0x00, 0x81, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x30,
    // KV Patch ??? XeCryptBnQwBeSigVerify
    0x00, 0x00, 0x70, 0xBC, 0x00, 0x00, 0x00, 0x01, 0x38, 0x60, 0x00, 0x01,
    // KV Patch ???
    0x00, 0x00, 0x72, 0x68, 0x00, 0x00, 0x00, 0x01, 0x38, 0x60, 0x00, 0x00,
    // NOP MachineCheck
    0x00, 0x00, 0x72, 0xB4, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // NOP MachineCheck
    0x00, 0x00, 0x72, 0xC4, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // NOP MachineCheck
    0x00, 0x00, 0x72, 0xEC, 0x00, 0x00, 0x00, 0x02, 0x60, 0x00, 0x00, 0x00, 0x39, 0x60, 0x00, 0x01,
    // ??? - Both HvxImageTransformImageKey & HvxCreateImageMapping use this
    0x00, 0x02, 0x4D, 0x58, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20,
    // Replaces HvpPkcs1Verify branch
    0x00, 0x02, 0x64, 0xF0, 0x00, 0x00, 0x00, 0x01, 0x38, 0x60, 0x00, 0x01,
    // ???
    0x00, 0x02, 0x9B, 0x08, 0x00, 0x00, 0x00, 0x0E, 0x2B, 0x3C, 0x00, 0x00, 0x41, 0x9A, 0x00, 0x30,
    0x2F, 0x03, 0x00, 0x00, 0x40, 0x9A, 0x00, 0x10, 0x38, 0x80, 0x00, 0xF0, 0x48, 0x00, 0x00, 0x18,
    0x60, 0x00, 0x00, 0x00, 0x2B, 0x1D, 0x00, 0x00, 0x38, 0x9F, 0x04, 0x40, 0x40, 0x9A, 0x00, 0x08,
    0x38, 0x80, 0x00, 0x54, 0x7F, 0x83, 0xE3, 0x78, 0x4B, 0xFF, 0x65, 0xC1, 0x3B, 0xE0, 0x00, 0x00,
    // HvxImageTransformImageKey - Disables a conditional branch
    0x00, 0x02, 0xB7, 0x78, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // HvxCreateImageMapping hash check
    0x00, 0x02, 0xCA, 0xE8, 0x00, 0x00, 0x00, 0x01, 0x38, 0x60, 0x00, 0x00,
    // HvxCreateImageMapping HV XEX region check ???
    0x00, 0x02, 0xCD, 0xD8, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // HvxExpansionInstall - Disable conditional branch after XeCryptBnQwBeSigVerify
    0x00, 0x03, 0x08, 0x9C, 0x00, 0x00, 0x00, 0x04, 0x40, 0x9A, 0x00, 0x08, 0x3B, 0xA0, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,
    // Function is branched to from HvxExpansionInstall - Another check???
    0x00, 0x03, 0x04, 0xE8, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,
    // Function is branched to from HvxExpansionInstall - Another check???
    0x00, 0x03, 0x04, 0xFC, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00,

    0xFF, 0xFF, 0xFF, 0xFF
};

BOOL ApplyFreebootHVPatches()
{
    cprintf("[ApplyFreebootHVPatches] Attempting to apply Primary Freeboot HV Patches...");

    DWORD NumPriPatchesApplied = ApplyPatches(NULL, FREEBOOT_HV_PRIMARY_PATCHES);

    cprintf("[ApplyFreebootHVPatches] %i HV patches applied!", NumPriPatchesApplied);

    BOOL SetMemRes = Hvx::ToggleMemProtect(FALSE);
    if (!SetMemRes)
    {
        cprintf("[ApplyFreebootHVPatches] Failed to disable memory protections... Skipping remaining patches");
        return FALSE;
    }

    cprintf("[ApplyFreebootHVPatches] Attempting to apply remaining Freeboot HV Patches...");

    DWORD NumSecPatchesApplied = ApplyPatches(NULL, FREEBOOT_HV_PATCHES);

    cprintf("[ApplyFreebootHVPatches] %i HV patches applied!", NumSecPatchesApplied);

    return TRUE;
}


const BYTE FREEBOOT_KERN_PATCHES[] = { // Prob don't need all of them but appears to work fine
    // Patches XexpConvertError
    0x80, 0x07, 0xB9, 0x20, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches branch to XexpVerifyMediaType within XexpLoadXexHeaders
    0x80, 0x07, 0xC4, 0xB8, 0x00, 0x00, 0x00, 0x01, 0x38, 0x60, 0x00, 0x01,
    // Patches branch to RtlImageXexHeaderString within XexpLoadFile
    0x80, 0x07, 0xC5, 0xE8, 0x00, 0x00, 0x00, 0x01, 0x38, 0x60, 0x00, 0x00,
    // Patch within XexpLoadFile
    0x80, 0x07, 0xC6, 0x34, 0x00, 0x00, 0x00, 0x01, 0x39, 0x60, 0x00, 0x00,
    // Patch within XexpLoadFile
    0x80, 0x07, 0xC6, 0x84, 0x00, 0x00, 0x00, 0x01, 0x39, 0x60, 0x00, 0x00,
    // Patches XexpVerifyMinimumVersion
    0x80, 0x07, 0xAF, 0x08, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patch within SfcxInspectLargeDataBlock
    0x80, 0x09, 0x4F, 0x78, 0x00, 0x00, 0x00, 0x01, 0x3A, 0xE0, 0x00, 0x10,
    // Patch within SataCdRomAuthenticationExInitialize
    0x80, 0x09, 0x98, 0xD0, 0x00, 0x00, 0x00, 0x01, 0x2B, 0x0B, 0x00, 0xFF,
    // Patches to disable fatal error screen E66 (VdDisplayFatalError). Caused by DVD drive(ERROR_XSS_CDROM_COULD_NOT_CREATE_DEVICE)
    0x80, 0x09, 0x92, 0xB4, 0x00, 0x00, 0x00, 0x05, 0x38, 0x60, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,
    // Patches XeKeysVerifyRSASignature
    0x80, 0x10, 0x9C, 0x90, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20,
    // Patches XeKeysSecurityConvertError
    0x80, 0x10, 0xA7, 0x88, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches XeKeysDvdAuthExConvertError
    0x80, 0x10, 0xAA, 0x68, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches _XeKeysRevokeIsValid
    0x80, 0x10, 0xAF, 0x30, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20,
    // Patches XeKeysRevokeIsRevoked
    0x80, 0x10, 0xB1, 0x38, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches _XeKeysRevokeIsRevoked
    0x80, 0x10, 0xB0, 0xE8, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches XeKeysRevokeIsDeviceRevoked
    0x80, 0x10, 0xB2, 0x78, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches XeKeysRevokeConvertError
    0x80, 0x10, 0xB3, 0xF8, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches XeKeysConsoleSignatureVerification
    0x80, 0x10, 0xBF, 0x20, 0x00, 0x00, 0x00, 0x05, 0x2B, 0x05, 0x00, 0x00, 0x38, 0x60, 0x00, 0x01,
    0x41, 0x9A, 0x00, 0x08, 0x90, 0x65, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
    // Patches XeCryptBnQwBeSigVerify
    0x80, 0x11, 0x19, 0x90, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20,
    // Patch within MassConfigureTransferCable (Skips hdd security sector verification???)
    0x80, 0x0E, 0x17, 0x54,0x00, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x54,
    // Patch within MassConfigureTransferCable
    0x80, 0x0E, 0x17, 0xCC, 0x00, 0x00, 0x00, 0x03, 0x83, 0x5C, 0x00, 0x98, 0x60, 0x00, 0x00, 0x00,
    0x60, 0x00, 0x00, 0x00,
    // Patches SataDiskAuthenticateDevice (Disables hdd security sector verification???)
    0x80, 0x15, 0xD9, 0xD8,0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20,
    // Patches here use the space available within XeKeysConsoleSignatureVerification due to it being patched out earlier and no longer used.
    // Contains multiple functions. launch.xex loader and two delay loops
    0x80, 0x10, 0xBF, 0x40, 0x00, 0x00, 0x00, 0x2C, 0x40, 0x98, 0x00, 0x08, 0x4E, 0x80, 0x00, 0x20,
    0x3C, 0x60, 0x80, 0x10, 0x3C, 0xA0, 0x00, 0x00, 0x38, 0x80, 0x00, 0x00, 0x60, 0x84, 0x00, 0x08,
    0x60, 0x63, 0xBF, 0xD0, 0x38, 0xC0, 0x00, 0x00, 0x4B, 0xF7, 0x18, 0x61, 0x38, 0x60, 0x00, 0x00,
    0x3C, 0x80, 0x80, 0x10, 0x60, 0x84, 0xBF, 0xEC, 0x4C, 0x00, 0x01, 0x2C, 0x90, 0x64, 0x00, 0x00,
    0x4B, 0xF5, 0x54, 0x64, 0x38, 0xA1, 0x00, 0x54, 0x3C, 0xE0, 0x80, 0x10, 0x60, 0xE7, 0xBF, 0xEC,
    0x81, 0x07, 0x00, 0x00, 0x4C, 0x00, 0x01, 0x2C, 0x2B, 0x08, 0x00, 0x00, 0x41, 0x9A, 0x00, 0x0C,
    0x7F, 0xFF, 0xFB, 0x78, 0x4B, 0xFF, 0xFF, 0xEC, 0x4E, 0x80, 0x00, 0x20, 0x2B, 0x03, 0x00, 0x14,
    0x40, 0x9A, 0x00, 0x24, 0x3C, 0xE0, 0x80, 0x10, 0x60, 0xE7, 0xBF, 0xEC, 0x81, 0x07, 0x00, 0x00,
    0x4C, 0x00, 0x01, 0x2C, 0x2B, 0x08, 0x00, 0x00, 0x41, 0x9A, 0x00, 0x0C, 0x7F, 0xFF, 0xFB, 0x78,
    0x4B, 0xFF, 0xFF, 0xEC, 0x4B, 0xFF, 0xC4, 0x44, 0x5C, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x5C,
    0x46, 0x6C, 0x61, 0x73, 0x68, 0x5C, 0x6C, 0x61, 0x75, 0x6E, 0x63, 0x68, 0x2E, 0x78, 0x65, 0x78,
    0x00, 0x00, 0x00, 0x00, /*0x12, 0x34, 0x56, 0x78,*/ // This is set to zero after launch.xex is loaded on boot. It appears to used to delay execution of other functions. (XexLoadExecutable & XeKeysGetKeyProperties)
    0x00, 0x00, 0x00, 0x00, // Setting to zero allow the functions to run as if it was set on boot

    // Patch within Phase1Initialization. Branch to check if xam was loaded. This is the first part of the launch.xex loader
    0x80, 0x06, 0x13, 0xCC, 0x00, 0x00, 0x00, 0x01, 0x48, 0x0A, 0xAB, 0x75,
    // Patch within XexLoadExecutable to branch to the delay loop mentioned above
    0x80, 0x07, 0xD7, 0xF8, 0x00, 0x00, 0x00, 0x01, 0x48, 0x08, 0xE7, 0x85,
    // Patches XeKeysGetKeyProperties to branch to the delay loop mentioned above
    0x80, 0x10, 0x8E, 0x70, 0x00, 0x00, 0x00, 0x01, 0x48, 0x00, 0x31, 0x34,
    
    // = ADDITONAL PATCHES=
    // Usbdsec patches for controllers - Credit to InvoxiPlayGames
    0x80, 0x0D, 0x87, 0x48, 0x00, 0x00, 0x00, 0x02, 0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20,
    0x80, 0x0F, 0x98, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x10,

    

    0xFF, 0xFF, 0xFF, 0xFF
};


BOOL ApplyFreebootKernPatches()
{
    cprintf("[ApplyFreebootKernPatches] Attempting to apply Freeboot Kernel Patches...");

    DWORD NumKernPatchesApplied = ApplyPatches(NULL, FREEBOOT_KERN_PATCHES);

    cprintf("[ApplyFreebootKernPatches] %i Kernel patches applied!", NumKernPatchesApplied);


    cprintf("[ApplyFreebootKernPatches] All patches applied for full Freeboot patchset!");

    return TRUE;
}

// Undo the patches made during the initial exploit to avoid any potential conflicts with the Freeboot patches

const BYTE BU_REVERT_PATCHES[] = {
    // Comment this out to leave the HvxPostOutput backdoor in place
    0x00, 0x01, 0x60, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x25, 0x40,

    /// Undo HV RSA patch to allow xex files - Restores branch to XeCryptBnQwBeSigVerify in HvpImageSignatureVerification
    0x00, 0x02, 0x9B, 0x04, 0x00, 0x00, 0x00, 0x01, 0x4B, 0xFF, 0x6B, 0x7D, // Freeboot adds patches at 0x29B08 to cover this

    // Undo Kernel RSA patch to allow xex files - Restores branch to XeCryptBnQwBeSigVerify in XexpVerifyXexHeaders
    0x80, 0x07, 0xBF, 0xDC, 0x00, 0x00, 0x00, 0x01, 0x48, 0x08, 0xDD, 0x75, 

    // End of patches
    0xFF, 0xFF, 0xFF, 0xFF
};

VOID RevertBadExploitPatches()
{
    cprintf("[RevertBadExploitPatches] Attempting to revert BadUpdate changes...");

    DWORD NumPatchesApplied = ApplyPatches(NULL, BU_REVERT_PATCHES);

    cprintf("[RevertBadExploitPatches] %i patches applied!", NumPatchesApplied);
}

VOID RestoreRoL()
{
    cprintf("[RevertBadExploitPatches] Restoring RoL state...");
    // Disable SMC RoL LED override - Restores the LEDs default state and allows controller pairing to function correctly - Credit to InvoxiPlayGames
    BYTE rol_led_buf[0x10] = { 0x99,0x00,0x00,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    HalSendSMCMessage(rol_led_buf, NULL);
}


// Patches to fix various things
const BYTE ADDITIONAL_PATCHES[] = {
    // Wipes HvpProtectedFlags that have been set on boot due to patches not being in place
    // This should hopefully fix at least one issue. (dvd drive checks causing "The disc is unreadable" error when running xex files extracted from discs)
    0x00, 0x01, 0x66, 0x18, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // End of patches
    0xFF, 0xFF, 0xFF, 0xFF
};

VOID ApplyAdditionalPatches()
{
    cprintf("[ApplyAdditionalPatches] Attempting to apply additional patches for fixes etc...");

    DWORD NumPatchesApplied = ApplyPatches(NULL, ADDITIONAL_PATCHES);

    cprintf("[ApplyAdditionalPatches] %i patches applied!", NumPatchesApplied);
}
/********************************************************************************
 * DASHLAUNCH — load launch.xex from embedded sections
 ********************************************************************************/

// Excuse the mess. A lot of this would be simplified if launch.xex was updated to not rely on static paths for lhelper

BOOL bDLloaded = FALSE;
#define lhPath_USB "\\Device\\Mass0\\lhelper.xex"
#define lhPath_Mount_USB "Usb:\\lhelper.xex"

#define lhPath_HDD "\\Device\\Harddisk0\\Partition1\\lhelper.xex"
#define lhPath_Mount_HDD "Hdd:\\lhelper.xex"

// Credit to c0z - Some of this was adapted from the Dashlaunch 2.11 source
// Use functions taken from Dashlaunch to load Dashlaunch :-)

DWORD g_status[2]; // 0 = return status, 1 = completion signal
VOID* pLaunchXexData;
DWORD dwLaunchXexSize;

static VOID LoadDLPlugin()
{
	g_status[0] = XexLoadImageFromMemory(pLaunchXexData, dwLaunchXexSize, "launch.xex", XEX_LOADIMG_TYPE_SYSTEM_DLL, 0, NULL); // XEX_LOADIMG_FLAG_DLL
	g_status[1] = 1;
	__dcbst(0, g_status);
	__sync();
	__isync();
}

WCHAR wDLStatusBuf[150];
BOOL bDLisLoaded = FALSE;
INT SysLoadDashlaunch()
{
	ZeroMemory(wDLStatusBuf, sizeof(wDLStatusBuf));
	if (GetModuleHandle("launch.xex")) // Check it hasn't been loaded already
	{
		cprintf("[SysLoadDashlaunch] Already loaded!");
		BOOL blhHDD = FileExists(lhPath_HDD);
		swprintf_s(wDLStatusBuf, currentLocalisation->DL_Loaded, blhHDD ? currentLocalisation->DL_Mode_Hdd : currentLocalisation->DL_Mode_Usb);
		bDLisLoaded = TRUE;
		return 2; // Already Loaded
	}

	cprintf("[SysLoadDashlaunch] Attempting to load launch.xex...");

	// Check if the console has a Hdd attached so we can copy lhelper there.
	// Ideally Hdd would be preferred to allow users to unplug the Usb without disrupting dashlaunch functionality.
	// If not, fallback to using Usb
	BOOL bUseHdd = (XboxHardwareInfo->Flags & 0x20) == 0x20;
	if (!FileExists(bUseHdd ? lhPath_HDD : lhPath_USB))
	{
		cprintf("[SysLoadDashlaunch] Attempting to copy lhelper.xex to %s root...", bUseHdd ? "HDD" : "USB");
		VOID* pLHData;
		DWORD dwLHSize;
		if (!XGetModuleSection(GetModuleHandle(NULL), bUseHdd ? "LHHDD" : "LHUSB", &pLHData, &dwLHSize))
		{
			cprintf("[SysLoadDashlaunch] Failed to load! Failed to get lhelper section data!");
			swprintf_s(wDLStatusBuf, currentLocalisation->DL_Fail_SecData);
			return 0; // Failed
		}

		if (bUseHdd)
		{
			if (!MountHdd())
			{
				cprintf("[SysLoadDashlaunch] Failed to load! Failed to create Hdd mount point!");
				swprintf_s(wDLStatusBuf, currentLocalisation->DL_Fail_Mount);
				return 0; // Failed
			}
		}
		else
		{
			if (!MountUsb())
			{
				cprintf("[SysLoadDashlaunch] Failed to load! Failed to create Usb mount point!");
				swprintf_s(wDLStatusBuf, currentLocalisation->DL_Fail_Mount);
				return 0; // Failed
			}
		}

		// Copy the embedded lhelper.xex to the root of storage device so the embedded launch.xex can use it once loaded
		if (!CWriteFile(bUseHdd ? lhPath_Mount_HDD : lhPath_Mount_USB, pLHData, dwLHSize)) // 7B error when not using a mount point
		{
			cprintf("[SysLoadDashlaunch] Failed to load! Failed to write lhelper.xex to %s root!", bUseHdd ? "HDD" : "USB");
			swprintf_s(wDLStatusBuf, currentLocalisation->DL_Fail_Write);
			return 0; // Failed
		}
		cprintf("[SysLoadDashlaunch] lhelper.xex copied to %s root!", bUseHdd ? "HDD" : "USB");
		Sleep(200);
	}

	if (!XGetModuleSection(GetModuleHandle(NULL), bUseHdd ? "LXHDD" : "LXUSB", &pLaunchXexData, &dwLaunchXexSize))
	{
		cprintf("[SysLoadDashlaunch] Failed to load! Failed to get launch section data!");
		swprintf_s(wDLStatusBuf, currentLocalisation->DL_Fail_Launch_SecData);
		return 0; // Failed
	}

	HANDLE pthread;
	DWORD pthreadid;
	DWORD sta;
	g_status[0] = 0;
	g_status[1] = 0;
	__dcbst(0, g_status);
	__sync();
	__isync();

	sta = ExCreateThread(&pthread, 0, &pthreadid, (PVOID)XapiThreadStartup, (LPTHREAD_START_ROUTINE)LoadDLPlugin, NULL, 0x2);
	XSetThreadProcessor(pthread, 4);
	SetThreadPriority(pthread, THREAD_PRIORITY_TIME_CRITICAL);
	ResumeThread(pthread);
	CloseHandle(pthread);

	// wait for thread to run it's course
	while (g_status[1] == 0)
	{
		Sleep(100);
	}

	if (!g_status[0])
	{
		cprintf("[SysLoadDashlaunch] Loaded successfully");
		swprintf_s(wDLStatusBuf, currentLocalisation->DL_Loaded, bUseHdd ? currentLocalisation->DL_Mode_Hdd : currentLocalisation->DL_Mode_Usb);
		bDLisLoaded = TRUE;
		return 1; // Success
	}
	else
	{
		cprintf("[SysLoadDashlaunch] Failed! Status: %X", g_status[0]);
		swprintf_s(wDLStatusBuf, currentLocalisation->DL_Fail_Status, g_status[0]);
		return 0; // Failed
	}
}

/********************************************************************************
 * MAIN
 ********************************************************************************/
//================================================================================
// XeUnshackle HEADLESS / MINIMAL
//
// Only the functional part:
//   - apply full freeboot HV + Kernel patches
//   - load Dashlaunch
//   - revert BadUpdate patches
//   - save ConsoleInfo.txt + OriginalMAC
//   - exit to dashboard
//
// No GUI, no video, no ATG, no XMedia, no D3D.
//================================================================================


// headless build

// Globals still used by Utils / Dashlaunch
WCHAR wCPUKeyBuf[150];
WCHAR wDVDKeyBuf[50];
WCHAR wConTypeBuf[50];
WCHAR wBadStorageStatusBuf[21];

typedef BOOLEAN (*pfnBadStorageExecute)(PBOOLEAN RetailFormatted);

//--------------------------------------------------------------------------------------
VOID __cdecl main()
{
	SetLocale();

	// ------------------------------------------------------------------
	// Stage 1-4 : HV expansion + freeboot HV patches
	// ------------------------------------------------------------------
	if (!Hvx::CheckPPExpHVAccess())
	{
		if (!Hvx::DisableExpChecks())
		{
			cprintf("[XeUnshackle] Stage 1 failed!");
			ShowErrorAndExit(1);
		}
		cprintf("[XeUnshackle] Stage 1 success!");

		if (!Hvx::SetupPPExpHVAccess())
		{
			cprintf("[XeUnshackle] Stage 2 failed!");
			ShowErrorAndExit(2);
		}
		cprintf("[XeUnshackle] Stage 2 success!");

		if (!Hvx::CheckPPExpHVAccess())
		{
			cprintf("[XeUnshackle] Stage 3 failed!");
			ShowErrorAndExit(3);
		}
		cprintf("[XeUnshackle] Stage 3 success!");

		if (!ApplyFreebootHVPatches())
		{
			cprintf("[XeUnshackle] Stage 4 failed!");
			ShowErrorAndExit(4);
		}
		cprintf("[XeUnshackle] Stage 4 success!");

		cprintf("[XeUnshackle] Calling KeFlushEntireTb");
		KeFlushEntireTb();
	}

	// ------------------------------------------------------------------
	// Stage 5 : Kernel patches + RoL
	// ------------------------------------------------------------------
	cprintf("[XeUnshackle] Checking kernel patch state");
	if (*(DWORD*)0x80108E70 != 0x48003134)
	{
		if (!ApplyFreebootKernPatches())
		{
			cprintf("[XeUnshackle] Stage 5 failed!");
			ShowErrorAndExit(5);
		}
		ApplyAdditionalPatches();
		RestoreRoL();

		cprintf("[XeUnshackle] Calling KeFlushEntireTb");
		KeFlushEntireTb();
		cprintf("[XeUnshackle] Stage 5 success!");
	}

	// ------------------------------------------------------------------
	// Bad Storage (optional)
	// ------------------------------------------------------------------
	HMODULE bstorDLL = LoadLibrary("GAME:\\BadStorage.xex.dll");
	ZeroMemory(wBadStorageStatusBuf, sizeof(wBadStorageStatusBuf));
	if (bstorDLL != NULL)
	{
		BOOLEAN bstorIsRetailFormatted = FALSE;
		if (((pfnBadStorageExecute)GetProcAddress(bstorDLL, (LPCSTR)1))(&bstorIsRetailFormatted))
		{
			swprintf_s(wBadStorageStatusBuf, L"Bad Storage: Applied");
			cprintf("[XeUnshackle] Bad Storage executed successfully");
		}
		else
		{
			swprintf_s(wBadStorageStatusBuf, L"Bad Storage: %s",
				bstorIsRetailFormatted ? L"Skipped" : L"Failed");
			cprintf("[XeUnshackle] Bad Storage executed unsuccessfully");
		}
		FreeLibrary(bstorDLL);
	}
	else
	{
		swprintf_s(wBadStorageStatusBuf, L"Bad Storage: Missing");
		cprintf("[XeUnshackle] BadStorage.xex.dll not found");
	}

	// ------------------------------------------------------------------
	// Dashlaunch + revert BadUpdate patches
	// ------------------------------------------------------------------
	if (SysLoadDashlaunch() == 1)
	{
		RevertBadExploitPatches();
	}

	cprintf("[XeUnshackle] All patches applied (HEADLESS mode)");

	// ------------------------------------------------------------------
	// Collect keys and auto-save
	// ------------------------------------------------------------------
	ZeroMemory(wConTypeBuf, sizeof(wConTypeBuf));
	swprintf_s(wConTypeBuf, L"Console type: %S", GetMoboByHWFlags().c_str());

	QWORD fuse3 = Hvx::HvGetFuseline(3);
	QWORD fuse5 = Hvx::HvGetFuseline(5);
	ZeroMemory(wCPUKeyBuf, sizeof(wCPUKeyBuf));
	swprintf_s(wCPUKeyBuf, L"CPUKey: %08X%08X%08X%08X",
		fuse3 >> 32, fuse3 & 0xffffffff, fuse5 >> 32, fuse5 & 0xffffffff);

	BYTE DVDKeyBytes[16];
	QWORD kvAddress = Hvx::HvPeekQWORD(0x00000002000163C0);
	Hvx::HvPeekBytes(kvAddress + 0x100, DVDKeyBytes, 16);
	ZeroMemory(wDVDKeyBuf, sizeof(wDVDKeyBuf));
	swprintf_s(wDVDKeyBuf, L"DVDKey: %08X%08X%08X%08X",
		*(DWORD*)(DVDKeyBytes), *(DWORD*)(DVDKeyBytes + 4),
		*(DWORD*)(DVDKeyBytes + 8), *(DWORD*)(DVDKeyBytes + 12));

	cprintf("[XeUnshackle] %ws", wConTypeBuf);
	cprintf("[XeUnshackle] %ws", wCPUKeyBuf);
	cprintf("[XeUnshackle] %ws", wDVDKeyBuf);

	SaveConsoleDataToFile();   // writes GAME:\ConsoleInfo.txt
	BackupOrigMAC();           // writes OriginalMACAddress.bin if missing

	cprintf("[XeUnshackle] Exiting to dashboard...");
	XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
}
