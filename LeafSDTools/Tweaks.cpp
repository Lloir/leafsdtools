#include "stdafx.h"
#include "Tweaks.h"
#include "LeafSDTools.h"
#include "Display.h"
#include "Touch.h"
#include "Logger.h"
#include <stdarg.h>
#include <tlhelp32.h>

#define NUM_TWEAKS 5
const char* tweakLabels[] = {"VIDEO IN MOTION", "DUMP REGISTRY", "DUMP SYSTEM", "EXEC SCRIPT", "EXIT"};

#define DUMP_REGISTRY_PATH L"\\SystemSD\\registry.txt"
#define DUMP_SYSTEM_PATH   L"\\SystemSD\\sysinfo.txt"
#define SCRIPT_PATH        L"\\SystemSD\\script.txt"

// ---------------------------------------------------------------------------
// Tiny buffered UTF-8 text writer used by the dump tools.
// ---------------------------------------------------------------------------
struct DumpFile {
    HANDLE h;
    char buf[8192];
    DWORD len;
};

static bool DumpOpen(DumpFile* f, LPCWSTR path) {
    f->len = 0;
    f->h = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return f->h != INVALID_HANDLE_VALUE;
}

static void DumpFlush(DumpFile* f) {
    DWORD written;
    if (f->len > 0) WriteFile(f->h, f->buf, f->len, &written, NULL);
    f->len = 0;
}

static void DumpClose(DumpFile* f) {
    DumpFlush(f);
    CloseHandle(f->h);
}

static void DumpRaw(DumpFile* f, const char* s, DWORD n) {
    if (n > sizeof(f->buf)) n = sizeof(f->buf);
    if (f->len + n > sizeof(f->buf)) DumpFlush(f);
    memcpy(f->buf + f->len, s, n);
    f->len += n;
}

static void DumpText(DumpFile* f, const char* s) {
    DumpRaw(f, s, strlen(s));
}

static void DumpWide(DumpFile* f, LPCWSTR ws) {
    char out[1024];
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, sizeof(out), NULL, NULL);
    if (n > 1) DumpRaw(f, out, n - 1);
}

static void DumpLine(DumpFile* f, const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 1] = 0;
    DumpText(f, line);
    DumpText(f, "\r\n");
}

// ---------------------------------------------------------------------------
// Registry dump (recursive)
// ---------------------------------------------------------------------------
static void DumpRegValue(DumpFile* f, LPCWSTR name, DWORD type, const BYTE* data, DWORD size) {
    DumpText(f, "  ");
    DumpWide(f, name[0] ? name : L"(default)");
    DumpText(f, " = ");
    if ((type == REG_SZ || type == REG_EXPAND_SZ) && size >= sizeof(WCHAR)) {
        DumpText(f, "\"");
        DumpWide(f, (LPCWSTR)data);
        DumpText(f, "\"");
    } else if (type == REG_DWORD && size >= 4) {
        char tmp[32];
        sprintf(tmp, "dword:0x%08X", *(const DWORD*)data);
        DumpText(f, tmp);
    } else {
        char tmp[8];
        DumpText(f, "hex:");
        for (DWORD i = 0; i < size && i < 64; i++) {
            sprintf(tmp, "%02X ", data[i]);
            DumpText(f, tmp);
        }
        if (size > 64) DumpText(f, "...");
    }
    DumpText(f, "\r\n");
}

static void DumpRegKey(DumpFile* f, HKEY root, LPCWSTR path, int depth) {
    HKEY hKey;
    if (depth > 12) return;
    if (RegOpenKeyEx(root, path, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return;

    DumpText(f, "[");
    DumpWide(f, path);
    DumpText(f, "]\r\n");

    for (DWORD i = 0;; i++) {
        WCHAR name[256];
        BYTE data[1024];
        DWORD nameLen = 256, dataLen = sizeof(data), type = 0;
        LONG r = RegEnumValue(hKey, i, name, &nameLen, NULL, &type, data, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r == ERROR_MORE_DATA) {
            DumpWide(f, L"  "); DumpWide(f, name); DumpText(f, " = <too large>\r\n");
            continue;
        }
        if (r != ERROR_SUCCESS) break;
        DumpRegValue(f, name, type, data, dataLen);
    }
    DumpText(f, "\r\n");

    for (DWORD i = 0;; i++) {
        WCHAR sub[256];
        WCHAR child[512];
        DWORD subLen = 256;
        if (RegEnumKeyEx(hKey, i, sub, &subLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        if (path[0]) wsprintf(child, L"%s\\%s", path, sub);
        else wcscpy(child, sub);
        DumpRegKey(f, root, child, depth + 1);
    }
    RegCloseKey(hKey);
}

static void DumpRegistry() {
    DumpFile f;
    PrintToScreen(1, "Dumping registry to SD...\n");
    if (!DumpOpen(&f, DUMP_REGISTRY_PATH)) {
        PrintToScreen(1, "Could not create registry.txt on SD.\n");
        LogError(L"Registry dump: CreateFile failed", GetLastError());
        Sleep(2000);
        return;
    }
    DumpText(&f, ";;;; HKEY_LOCAL_MACHINE\r\n");
    DumpRegKey(&f, HKEY_LOCAL_MACHINE, L"", 0);
    DumpText(&f, ";;;; HKEY_CURRENT_USER\r\n");
    DumpRegKey(&f, HKEY_CURRENT_USER, L"", 0);
    DumpText(&f, ";;;; HKEY_CLASSES_ROOT\r\n");
    DumpRegKey(&f, HKEY_CLASSES_ROOT, L"", 0);
    DumpClose(&f);
    PrintToScreen(1, "Done: \\SystemSD\\registry.txt\n");
    Sleep(1500);
}

// ---------------------------------------------------------------------------
// System dump: OS/CPU info, processes, modules, directory tree
// ---------------------------------------------------------------------------
typedef HANDLE (WINAPI *PFN_Snapshot)(DWORD, DWORD);
typedef BOOL (WINAPI *PFN_Process32)(HANDLE, LPPROCESSENTRY32);
typedef BOOL (WINAPI *PFN_Module32)(HANDLE, LPMODULEENTRY32);
typedef BOOL (WINAPI *PFN_CloseSnapshot)(HANDLE);

#ifndef TH32CS_GETALLMODS
#define TH32CS_GETALLMODS 0x80000000
#endif

static void DumpOsInfo(DumpFile* f) {
    OSVERSIONINFO vi;
    SYSTEM_INFO si;
    MEMORYSTATUS ms;
    WCHAR buf[256];

    DumpText(f, "==== OS / CPU ====\r\n");
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (GetVersionEx(&vi))
        DumpLine(f, "WinCE %lu.%lu build %lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    GetSystemInfo(&si);
    DumpLine(f, "CPU arch=%u type=%lu level=%u rev=0x%04X cores=%lu", si.wProcessorArchitecture,
        si.dwProcessorType, si.wProcessorLevel, si.wProcessorRevision, si.dwNumberOfProcessors);
    DumpLine(f, "Page size=%lu, app address range 0x%08X-0x%08X", si.dwPageSize,
        (DWORD)si.lpMinimumApplicationAddress, (DWORD)si.lpMaximumApplicationAddress);
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    DumpLine(f, "RAM total=%lu KB free=%lu KB", ms.dwTotalPhys / 1024, ms.dwAvailPhys / 1024);

    memset(buf, 0, sizeof(buf));
    if (SystemParametersInfo(SPI_GETPLATFORMTYPE, sizeof(buf), buf, 0)) {
        DumpText(f, "Platform type: "); DumpWide(f, buf); DumpText(f, "\r\n");
    }
    memset(buf, 0, sizeof(buf));
    if (SystemParametersInfo(SPI_GETOEMINFO, sizeof(buf), buf, 0)) {
        DumpText(f, "OEM info: "); DumpWide(f, buf); DumpText(f, "\r\n");
    }
    DumpText(f, "\r\n");
}

static void DumpProcesses(DumpFile* f) {
    HINSTANCE hTh = LoadLibrary(L"toolhelp.dll");
    DumpText(f, "==== PROCESSES ====\r\n");
    if (!hTh) { DumpText(f, "toolhelp.dll not available\r\n\r\n"); return; }

    PFN_Snapshot pSnap = (PFN_Snapshot)GetProcAddress(hTh, L"CreateToolhelp32Snapshot");
    PFN_Process32 pFirst = (PFN_Process32)GetProcAddress(hTh, L"Process32First");
    PFN_Process32 pNext = (PFN_Process32)GetProcAddress(hTh, L"Process32Next");
    PFN_Module32 pMFirst = (PFN_Module32)GetProcAddress(hTh, L"Module32First");
    PFN_Module32 pMNext = (PFN_Module32)GetProcAddress(hTh, L"Module32Next");
    PFN_CloseSnapshot pClose = (PFN_CloseSnapshot)GetProcAddress(hTh, L"CloseToolhelp32Snapshot");

    if (pSnap && pFirst && pNext && pClose) {
        HANDLE snap = pSnap(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe;
            pe.dwSize = sizeof(pe);
            for (BOOL ok = pFirst(snap, &pe); ok; ok = pNext(snap, &pe)) {
                DumpLine(f, "pid=0x%08X threads=%lu base=0x%08X", pe.th32ProcessID, pe.cntThreads, pe.th32MemoryBase);
                DumpText(f, "  "); DumpWide(f, pe.szExeFile); DumpText(f, "\r\n");
            }
            pClose(snap);
        }
    }
    DumpText(f, "\r\n==== LOADED MODULES (all processes) ====\r\n");
    if (pSnap && pMFirst && pMNext && pClose) {
        HANDLE snap = pSnap(TH32CS_SNAPMODULE | TH32CS_GETALLMODS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 me;
            me.dwSize = sizeof(me);
            for (BOOL ok = pMFirst(snap, &me); ok; ok = pMNext(snap, &me)) {
                DumpLine(f, "base=0x%08X size=%lu proc=0x%08X", (DWORD)me.modBaseAddr, me.modBaseSize, me.th32ProcessID);
                DumpText(f, "  "); DumpWide(f, me.szExePath); DumpText(f, "\r\n");
            }
            pClose(snap);
        }
    }
    DumpText(f, "\r\n");
    FreeLibrary(hTh);
}

static DWORD g_dirEntries;

static void DumpDirTree(DumpFile* f, LPCWSTR dir, int depth) {
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATA fd;
    if (g_dirEntries > 40000) return;
    wsprintf(pattern, L"%s\\*", dir);
    HANDLE h = FindFirstFile(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        WCHAR full[MAX_PATH];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wsprintf(full, L"%s\\%s", dir, fd.cFileName);
        g_dirEntries++;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        char head[64];
        sprintf(head, "%c %10lu 0x%08X ", isDir ? 'D' : 'F', fd.nFileSizeLow, fd.dwFileAttributes);
        DumpText(f, head);
        DumpWide(f, full);
        DumpText(f, "\r\n");
        // Don't descend into the SD cards we're writing to.
        if (isDir && depth < 6 && wcsicmp(full, L"\\SystemSD") != 0 && wcsicmp(full, L"\\Storage Card") != 0)
            DumpDirTree(f, full, depth + 1);
    } while (FindNextFile(h, &fd));
    FindClose(h);
}

static void DumpSystem() {
    DumpFile f;
    PrintToScreen(1, "Dumping system info to SD...\n");
    if (!DumpOpen(&f, DUMP_SYSTEM_PATH)) {
        PrintToScreen(1, "Could not create sysinfo.txt on SD.\n");
        LogError(L"System dump: CreateFile failed", GetLastError());
        Sleep(2000);
        return;
    }
    DumpOsInfo(&f);
    DumpProcesses(&f);
    DumpText(&f, "==== FILESYSTEM ====\r\n");
    g_dirEntries = 0;
    DumpDirTree(&f, L"", 0);
    DumpClose(&f);
    PrintToScreen(1, "Done: \\SystemSD\\sysinfo.txt\n");
    Sleep(1500);
}

// ---------------------------------------------------------------------------
// Script runner: each non-empty line of \SystemSD\script.txt is
//   <path to exe> [arguments]
// Lines starting with ';' or '#' are comments. Each program is waited on for
// up to 30 seconds and its exit code is logged.
// ---------------------------------------------------------------------------
static void RunScript() {
    HANDLE h = CreateFile(SCRIPT_PATH, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        PrintToScreen(1, "No \\SystemSD\\script.txt found.\n");
        Sleep(2000);
        return;
    }
    static char text[4096];
    DWORD got = 0;
    ReadFile(h, text, sizeof(text) - 1, &got, NULL);
    CloseHandle(h);
    text[got] = 0;

    PrintToScreen(1, "Running script from SD...\n");
    for (char* line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == ';' || *line == '#') continue;

        WCHAR wline[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, line, -1, wline, MAX_PATH);
        WCHAR* args = wcschr(wline, L' ');
        if (args) *args++ = 0;

        PrintToScreen(1, "> %s\n", line);
        PROCESS_INFORMATION pi;
        if (!CreateProcess(wline, args, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) {
            PrintToScreen(1, "  failed (error %lu)\n", GetLastError());
            LogError(L"Script: CreateProcess failed", GetLastError());
            continue;
        }
        DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        PrintToScreen(1, wait == WAIT_TIMEOUT ? "  still running\n" : "  exit code %lu\n", code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    PrintToScreen(1, "Script finished.\n");
    Sleep(2000);
}

void RenderTweakMenu() {
    int y = 20;
    for (int i = 0; i < NUM_TWEAKS; i++) {
        RenderButton(780, y, 180, 65, tweakLabels[i]);
        y += 65 + 10;
    }
}

void PatchVideoInMotion() {
    PrintToScreen(1, "Searching for speed lock address...\n");
    // This is a placeholder for the actual memory signature search and patch logic
    // On many WinCE units, patching the return value of a specific API or a flag in the media player process works.
    PrintToScreen(1, "Feature not yet available for this firmware version.\n");
    Sleep(2000);
}

void RunTweaks() {
    ResetTextRenderer();
    DrawBackground(0x0010);
    PrintToScreen(2, "T w e a k s   &   T o o l s");
    
    while (true) {
        RenderTweakMenu();
        LCDTouchEvent* ev = WaitForTouch(INFINITE);
        if (ev) {
            int btn = GetPressedButton(ev->xCoord, ev->yCoord, 780, 20, 180, 65, 10, NUM_TWEAKS);
            if (btn == 0) PatchVideoInMotion();
            if (btn == 1) DumpRegistry();
            if (btn == 2) DumpSystem();
            if (btn == 3) RunScript();
            if (btn == 4) break;
            
            WaitForScreenUntouch();
        }
    }
}
