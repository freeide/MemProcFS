// m_sys_mem.c : implementation related to the Sys/Memory built-in module.
//
// The '/sys/memory' module is responsible for displaying various memory related
// information such as information about the Windows PFN database.
//
// (c) Ulf Frisk, 2020-2021
// Author: Ulf Frisk, pcileech@frizk.net
//
#include "pluginmanager.h"
#include "mm_pfn.h"
#include "util.h"

#define MSYSMEM_PFNMAP_LINELENGTH           56ULL
#define MSYSMEM_PHYSMEMMAP_LINELENGTH       33ULL
#define MSYSMEM_PHYSMEMMAP_LINEHEADER       L"   #         Base            Top"

VOID MSysMem_PhysMemReadLine_Callback(_Inout_opt_ PVOID ctx, _In_ DWORD cbLineLength, _In_ DWORD ie, _In_ PVMM_MAP_PHYSMEMENTRY pe, _Out_writes_(cbLineLength + 1) LPSTR szu8)
{
    Util_snwprintf_u8ln(szu8, cbLineLength,
        L"%04x %12llx - %12llx",
        ie,
        pe->pa,
        pe->pa + pe->cb - 1
    );
}


_Success_(return == 0)
NTSTATUS MSysMem_Read_PfnMap(_Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ QWORD cbOffset)
{
    NTSTATUS nt;
    LPSTR sz;
    QWORD i, o = 0, cbMax, cbLINELENGTH;
    PMMPFN_MAP_ENTRY pe;
    PMMPFNOB_MAP pObPfnMap = NULL;
    CHAR szType[MAX_PATH] = { 0 };
    DWORD tp, cPfnTotal, cPfnStart, cPfnEnd;
    BOOL fModified, fPrototype;
    cPfnTotal = (DWORD)(ctxMain->dev.paMax >> 12);
    cbLINELENGTH = MSYSMEM_PFNMAP_LINELENGTH;
    cPfnStart = (DWORD)(cbOffset / cbLINELENGTH);
    cPfnEnd = (DWORD)min(cPfnTotal - 1, (cb + cbOffset + cbLINELENGTH - 1) / cbLINELENGTH);
    cbMax = 1 + (1ULL + cPfnEnd - cPfnStart) * cbLINELENGTH;
    if(cPfnStart >= cPfnTotal) { return VMMDLL_STATUS_END_OF_FILE; }
    if(!MmPfn_Map_GetPfn(cPfnStart, cPfnEnd - cPfnStart + 1, &pObPfnMap, TRUE)) { return VMMDLL_STATUS_FILE_INVALID; }
    if(!(sz = LocalAlloc(LMEM_ZEROINIT, cbMax))) {
        Ob_DECREF(pObPfnMap);
        return VMMDLL_STATUS_FILE_INVALID;
    }
    for(i = 0; i <= cPfnEnd - cPfnStart; i++) {
        pe = pObPfnMap->pMap + i;
        tp = pe->PageLocation;
        fModified = pe->Modified && ((tp == MmPfnTypeStandby) || (tp == MmPfnTypeModified) || (tp == MmPfnTypeModifiedNoWrite) || (tp == MmPfnTypeActive) || (tp == MmPfnTypeTransition));
        fPrototype = pe->PrototypePte && ((tp == MmPfnTypeStandby) || (tp == MmPfnTypeModified) || (tp == MmPfnTypeModifiedNoWrite) || (tp == MmPfnTypeActive) || (tp == MmPfnTypeTransition));
        o += Util_snwprintf_u8ln(
            sz + o,
            cbLINELENGTH,
            L"%8x%7i %-7S %-10S %i%c%c %16llx\n",
            pe->dwPfn,
            pe->AddressInfo.dwPid,
            MMPFN_TYPE_TEXT[pe->PageLocation],
            MMPFN_TYPEEXTENDED_TEXT[pe->tpExtended],
            pe->Priority,
            fModified ? 'M' : '-',
            fPrototype ? 'P' : '-',
            pe->AddressInfo.va
        );
    }
    nt = Util_VfsReadFile_FromPBYTE(sz, cbMax - 1, pb, cb, pcbRead, cbOffset - cPfnStart * cbLINELENGTH);
    LocalFree(sz);
    Ob_DECREF(pObPfnMap);
    return nt;
}

NTSTATUS MSysMem_Read(_In_ PVMMDLL_PLUGIN_CONTEXT ctx, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ QWORD cbOffset)
{
    NTSTATUS nt = VMMDLL_STATUS_FILE_INVALID;
    PVMMOB_MAP_PHYSMEM pObPhysMemMap = NULL;
    if(!_wcsicmp(ctx->wszPath, L"physmemmap.txt") && VmmMap_GetPhysMem(&pObPhysMemMap)) {
        nt = Util_VfsLineFixed_Read(
            MSysMem_PhysMemReadLine_Callback, NULL, MSYSMEM_PHYSMEMMAP_LINELENGTH, MSYSMEM_PHYSMEMMAP_LINEHEADER,
            pObPhysMemMap->pMap, pObPhysMemMap->cMap, sizeof(VMM_MAP_PHYSMEMENTRY),
            pb, cb, pcbRead, cbOffset
        );
        Ob_DECREF_NULL(&pObPhysMemMap);
    }
    if(!_wcsicmp(ctx->wszPath, L"pfndb.txt")) {
        nt = MSysMem_Read_PfnMap(pb, cb, pcbRead, cbOffset);
    }
    if(!_wcsicmp(ctx->wszPath, L"pfnaddr.txt")) {
        nt = ctxVmm->f32 ?
            Util_VfsReadFile_FromDWORD((DWORD)ctxVmm->kernel.opt.vaPfnDatabase, pb, cb, pcbRead, cbOffset, FALSE) :
            Util_VfsReadFile_FromQWORD((QWORD)ctxVmm->kernel.opt.vaPfnDatabase, pb, cb, pcbRead, cbOffset, FALSE);
    }
    return nt;
}

BOOL MSysMem_List(_In_ PVMMDLL_PLUGIN_CONTEXT ctx, _Inout_ PHANDLE pFileList)
{
    DWORD cPfn, cPhys;
    PVMMOB_MAP_PHYSMEM pObPhysMemMap = NULL;
    // PFN database:
    if(ctxVmm->kernel.opt.vaPfnDatabase) {
        cPfn = (DWORD)(ctxMain->dev.paMax >> 12);
        VMMDLL_VfsList_AddFile(pFileList, L"pfndb.txt", cPfn * MSYSMEM_PFNMAP_LINELENGTH, NULL);
        VMMDLL_VfsList_AddFile(pFileList, L"pfnaddr.txt", ctxVmm->f32 ? 8 : 16, NULL);
    }
    // Physical Memory Map:
    VmmMap_GetPhysMem(&pObPhysMemMap);
    cPhys = pObPhysMemMap ? pObPhysMemMap->cMap : 0;
    VMMDLL_VfsList_AddFile(pFileList, L"physmemmap.txt", UTIL_VFSLINEFIXED_LINECOUNT(cPhys) * MSYSMEM_PHYSMEMMAP_LINELENGTH, NULL);
    Ob_DECREF(pObPhysMemMap);
    return TRUE;
}

VOID M_SysMem_Initialize(_Inout_ PVMMDLL_PLUGIN_REGINFO pRI)
{
    if((pRI->magic != VMMDLL_PLUGIN_REGINFO_MAGIC) || (pRI->wVersion != VMMDLL_PLUGIN_REGINFO_VERSION)) { return; }
    if((pRI->tpSystem != VMM_SYSTEM_WINDOWS_X64) && (pRI->tpSystem != VMM_SYSTEM_WINDOWS_X86)) { return; }
    wcscpy_s(pRI->reg_info.wszPathName, 128, L"\\sys\\memory");         // module name
    pRI->reg_info.fRootModule = TRUE;                                   // module shows in root directory
    pRI->reg_fn.pfnList = MSysMem_List;                                 // List function supported
    pRI->reg_fn.pfnRead = MSysMem_Read;                                 // Read function supported
    pRI->pfnPluginManager_Register(pRI);
}
