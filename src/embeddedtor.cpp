// Copyright (c) 2026 The Quavence developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "embeddedtor.h"
#include "util.h"
#include "netbase.h"

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#ifdef WIN32
#include <windows.h>
#endif

#ifdef WIN32
static HANDLE hTorJob = NULL;
static PROCESS_INFORMATION piTor = {0};
static bool fEmbeddedTorStarted = false;

static bool IsPortListening(int port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    sa.sin_port = htons(port);

    bool listening = false;
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) == 0) {
        listening = true;
    }
    closesocket(s);
    return listening;
}
#endif

bool StartEmbeddedTor()
{
#ifndef WIN32
    return false;
#else
    if (!GetBoolArg("-embeddedtor", true)) {
        LogPrintf("embedded tor: disabled by -embeddedtor=0\n");
        return false;
    }

    if (mapArgs.count("-proxy") || mapArgs.count("-onion") || GetBoolArg("-notor", false)) {
        LogPrintf("embedded tor: custom proxy or -notor configured; skipping embedded tor\n");
        return false;
    }

    // Check if external Tor Control is already reachable on 9051
    if (IsPortListening(9051)) {
        LogPrintf("embedded tor: external Tor detected on 127.0.0.1:9051; using external Tor\n");
        return false;
    }

    HMODULE hMod = GetModuleHandle(NULL);
    HRSRC hRes = FindResource(hMod, MAKEINTRESOURCE(IDR_TOR_EXE), RT_RCDATA);
    if (!hRes) {
        LogPrint("tor", "embedded tor: IDR_TOR_EXE resource not found in module\n");
        return false;
    }

    HGLOBAL hGlobal = LoadResource(hMod, hRes);
    if (!hGlobal) {
        LogPrintf("embedded tor: failed to load resource\n");
        return false;
    }

    void* pData = LockResource(hGlobal);
    DWORD dwResSize = SizeofResource(hMod, hRes);
    if (!pData || dwResSize == 0) {
        LogPrintf("embedded tor: invalid resource data\n");
        return false;
    }

    boost::filesystem::path torDir = GetDataDir() / "tor";
    boost::filesystem::create_directories(torDir);
    boost::filesystem::path torExePath = torDir / "tor.exe";
    boost::filesystem::path torDataDir = torDir / "data";
    boost::filesystem::create_directories(torDataDir);

    // Extract tor.exe if missing or different size
    bool needExtract = true;
    if (boost::filesystem::exists(torExePath)) {
        boost::system::error_code ec;
        uintmax_t existingSize = boost::filesystem::file_size(torExePath, ec);
        if (!ec && existingSize == dwResSize) {
            needExtract = false;
        }
    }

    if (needExtract) {
        LogPrintf("embedded tor: extracting tor.exe (%u bytes) to %s\n", dwResSize, torExePath.string());
        FILE* fp = fopen(torExePath.string().c_str(), "wb");
        if (!fp) {
            LogPrintf("embedded tor: failed to write %s\n", torExePath.string());
            return false;
        }
        fwrite(pData, 1, dwResSize, fp);
        fclose(fp);
    }

    // Setup Job Object so Windows kernel terminates tor.exe when wallet exits/crashes
    hTorJob = CreateJobObject(NULL, NULL);
    if (hTorJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        memset(&jeli, 0, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hTorJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    const int nSocksPort = 27715;
    const int nControlPort = 27716;

    std::string strArgs = strprintf(
        "\"%s\" --DataDirectory \"%s\" --SocksPort 127.0.0.1:%d --ControlPort 127.0.0.1:%d --CookieAuthentication 1 --Log \"notice stdout\"",
        torExePath.string(), torDataDir.string(), nSocksPort, nControlPort
    );

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<char> cmdLine(strArgs.begin(), strArgs.end());
    cmdLine.push_back('\0');

    BOOL bSuccess = CreateProcessA(
        NULL,
        &cmdLine[0],
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        NULL,
        torDir.string().c_str(),
        &si,
        &piTor
    );

    if (!bSuccess) {
        LogPrintf("embedded tor: failed to launch tor.exe (err=%lu)\n", GetLastError());
        if (hTorJob) {
            CloseHandle(hTorJob);
            hTorJob = NULL;
        }
        return false;
    }

    if (hTorJob) {
        AssignProcessToJobObject(hTorJob, piTor.hProcess);
    }
    ResumeThread(piTor.hThread);
    CloseHandle(piTor.hThread);
    piTor.hThread = NULL;

    LogPrintf("embedded tor: launched pid=%lu, waiting for control port %d...\n", piTor.dwProcessId, nControlPort);

    // Wait up to 5 seconds for Tor ControlPort to become ready
    bool ready = false;
    for (int i = 0; i < 50; i++) {
        MilliSleep(100);
        if (IsPortListening(nControlPort)) {
            ready = true;
            break;
        }
    }

    if (!ready) {
        LogPrintf("embedded tor: timeout waiting for ControlPort %d\n", nControlPort);
        // Continue anyway; TorController will retry
    }

    SoftSetArg("-onion", strprintf("127.0.0.1:%d", nSocksPort));
    SoftSetArg("-torcontrol", strprintf("127.0.0.1:%d", nControlPort));

    LogPrintf("embedded tor: successfully initialized (onion 127.0.0.1:%d, torcontrol 127.0.0.1:%d)\n",
              nSocksPort, nControlPort);

    fEmbeddedTorStarted = true;
    return true;
#endif
}

void StopEmbeddedTor()
{
#ifdef WIN32
    if (fEmbeddedTorStarted && piTor.hProcess) {
        LogPrintf("embedded tor: terminating child process pid=%lu\n", piTor.dwProcessId);
        TerminateProcess(piTor.hProcess, 0);
        WaitForSingleObject(piTor.hProcess, 1000);
        CloseHandle(piTor.hProcess);
        piTor.hProcess = NULL;
    }
    if (hTorJob) {
        CloseHandle(hTorJob);
        hTorJob = NULL;
    }
    fEmbeddedTorStarted = false;
#endif
}
