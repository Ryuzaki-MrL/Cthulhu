#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string>
#include <cstring>
#include <3ds.h>

#define ENTRY_SHARED_COUNT 0x200
#define ENTRY_HOMEMENU_COUNT 0x168

bool dobackup = true;

typedef struct {
    u16 shortDescription[0x40];
    u16 longDescription[0x80];
    u16 publisher[0x40];
} SMDH_META;

typedef struct {
    u8 unknown[0x20];
    SMDH_META titles[0x10];
    u8 smallIcon[0x480];
    u8 largeIcon[0x1200];
} SMDH_SHARED;

typedef struct {
    char magic[0x04];
    u16 version;
    u16 reserved1;
    SMDH_META titles[0x10];
    u8 ratings[0x10];
    u32 region;
    u32 matchMakerId;
    u64 matchMakerBitId;
    u32 flags;
    u16 eulaVersion;
    u16 reserved;
    u32 optimalBannerFrame;
    u32 streetpassId;
    u64 reserved2;
    u8 smallIcon[0x480];
    u8 largeIcon[0x1200];
} SMDH_HOMEMENU;

typedef struct {
    u8 version;
    bool animated;
    u16 crc16[4];
    u8 reserved[0x16];
    u8 mainIconBitmap[0x200];
    u16 mainIconPalette[0x10];
    u16 titles[16][0x80];
    u8 animatedFrameBitmaps[8][0x200];
    u16 animatedFramePalettes[8][0x10];
    u16 animationSequence[0x40];
} SMDH_TWL;

typedef struct {
    u32 unknown[2];
    u64 titleid;
} ENTRY_DATA;

Handle ptmSysmHandle;

Result PTMSYSM_FormatSavedata(void)
{
	Result ret;
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0x813,0,0); // 0x8130000

	if(R_FAILED(ret = svcSendSyncRequest(ptmSysmHandle)))return ret;

	return (Result)cmdbuf[1];
}

Result PTMSYSM_ClearStepHistory(void)
{
	Result ret;
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0x805,0,0); // 0x8050000

	if(R_FAILED(ret = svcSendSyncRequest(ptmSysmHandle)))return ret;

	return (Result)cmdbuf[1];
}

Result PTMSYSM_ClearPlayHistory(void)
{
	Result ret;
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0x80A,0,0); // 0x80A0000

	if(R_FAILED(ret = svcSendSyncRequest(ptmSysmHandle)))return ret;

	return (Result)cmdbuf[1];
}

void gfxEndFrame() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

u32 waitKey() {
    u32 kDown = 0;
    while (aptMainLoop()) {
        hidScanInput();
        kDown = hidKeysDown();
        if (kDown) break;
        gfxEndFrame();
    }
    consoleClear();
    return kDown;
}

bool promptConfirm(std::string title, std::string message) {
    consoleClear();
    printf("\x1b[0;0H\x1b[30;47m%-50s", " ");
    printf("\x1b[0;%uH%s\x1b[0;0m", (25 - (title.size() / 2)), title.c_str());
    printf("\x1b[14;%uH%s", (25 - (message.size() / 2)), message.c_str());
    printf("\x1b[16;14H\x1b[32m(A)\x1b[37m Confirm / \x1b[31m(B)\x1b[37m Cancel");
    u32 kDown = waitKey();
    if (kDown & KEY_A) return true;
    return false;
}

void promptError(std::string title, std::string message) {
    consoleClear();
    printf("\x1b[0;0H\x1b[30;47m%-50s", " ");
    printf("\x1b[0;%uH%s\x1b[0;0m", (25 - (title.size() / 2)), title.c_str());
    printf("\x1b[14;%uH%s", (25 - (message.size() / 2)), message.c_str());
    waitKey();
}

u64* getTitleList(u64* count) {
    Result res;
    u32 count1;
    u32 count2;
    res = AM_GetTitleCount(MEDIATYPE_NAND, &count1);
    if(R_FAILED(res)) return NULL;
    res = AM_GetTitleCount(MEDIATYPE_SD, &count2);
    if(R_FAILED(res)) return NULL;

    u64* tids = new u64[count1 + count2];

    res = AM_GetTitleList(&count1, MEDIATYPE_NAND, count1, tids);
    printf("Retrieving NAND title list... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = AM_GetTitleList(&count2, MEDIATYPE_SD, count2, &tids[count1]);
    printf("Retrieving SD title list... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    *count = (((u64)count1)<<32 | (u64)count2);
    printf("Found %llu titles.\n", ((*count & 0xFFFFFFFF) + (*count >> 32)));

    return tids;
}

ENTRY_DATA* getSharedEntryList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    ENTRY_DATA* data = new ENTRY_DATA[ENTRY_SHARED_COUNT];

    for (u64 i = 0; i < ENTRY_SHARED_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(ENTRY_DATA), &data[i], sizeof(ENTRY_DATA));
        printf("\x1b[15;0HReading entry data %llu... %s %lx.", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(ENTRY_DATA)) break;
        gfxEndFrame();
    }

    return data;
}

SMDH_SHARED* getSharedIconList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    SMDH_SHARED* data = new SMDH_SHARED[ENTRY_SHARED_COUNT];

    for (u64 i = 0; i < ENTRY_SHARED_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(SMDH_SHARED), &data[i], sizeof(SMDH_SHARED));
        printf("\x1b[16;0HReading icon data %llu... %s %lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(SMDH_SHARED)) break;
        gfxEndFrame();
    }

    return data;
}

ENTRY_DATA* getHomemenuEntryList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    ENTRY_DATA* data = new ENTRY_DATA[ENTRY_HOMEMENU_COUNT];

    for (u64 i = 0; i < ENTRY_HOMEMENU_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(ENTRY_DATA), &data[i], sizeof(ENTRY_DATA));
        printf("\x1b[15;0HReading entry data %llu... %s %lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(ENTRY_DATA)) break;
        gfxEndFrame();
    }

    return data;
}

SMDH_HOMEMENU* getHomemenuIconList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    SMDH_HOMEMENU* data = new SMDH_HOMEMENU[ENTRY_HOMEMENU_COUNT];

    for (u64 i = 0; i < ENTRY_HOMEMENU_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(SMDH_HOMEMENU), &data[i], sizeof(SMDH_HOMEMENU));
        printf("\x1b[16;0HReading icon data %llu... %s %lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(SMDH_HOMEMENU)) break;
        gfxEndFrame();
    }

    return data;
}

SMDH_HOMEMENU* getSystemIconList(u64* tids, u64 count) {
    u32 count1 = (u32)(count >> 32);
    u32 count2 = (u32)(count & 0xFFFFFFFF);
    u64 countt = (count1 + count2);

    if (countt == 0) return NULL;

    SMDH_HOMEMENU* icons = new SMDH_HOMEMENU[countt];

    u64 i = 0;
    u64 loaded = 0;

    while (i < countt) {
        Handle smdhHandle;
        Result res;
        bool isTWL = ((tids[i] >> 32) & 0x8000) != 0;

        // TODO: DSiWare support

        if (!isTWL) {
            u32 archivePathData[] = {(u32)(tids[i] & 0xFFFFFFFF), (u32)(tids[i] >> 32), (i >= count1) ? MEDIATYPE_SD : MEDIATYPE_NAND, 0x00000000};
            const u32 filePathData[] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000};

            res = FSUSER_OpenFileDirectly(&smdhHandle, ARCHIVE_SAVEDATA_AND_CONTENT, (FS_Path){PATH_BINARY, 0x10, (u8*)archivePathData}, (FS_Path){PATH_BINARY, 0x14, (u8*)filePathData}, FS_OPEN_READ, 0);
            printf("\x1b[18;0HOpening SMDH file %llx... %s %lx.", tids[i], R_FAILED(res) ? "ERROR" : "OK", res);

            if (R_SUCCEEDED(res)) {
                u32 bytesRead = 0;
                res = FSFILE_Read(smdhHandle, &bytesRead, 0x0, &icons[i], sizeof(SMDH_HOMEMENU));
                printf("\x1b[19;0HReading SMDH %llx... %s %lx.", tids[i], R_FAILED(res) ? "ERROR" : "OK", res);
                FSFILE_Close(smdhHandle);
                if (bytesRead == sizeof(SMDH_HOMEMENU)) loaded++;
            }
        }

        i++;
        printf("\x1b[20;0HLoaded %llu / %llu SMDH\n", loaded, countt);
        gfxEndFrame();
    }

    return icons;
}

FS_Archive openSharedExtdata() {
    Result res;
    FS_Archive archive;
    u32 archpath[3] = {MEDIATYPE_NAND, 0xF000000B, 0x00048000};
    FS_Path fspath = {PATH_BINARY, 12, archpath};

    res = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, fspath);
    printf("Opening shared extdata archive... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    
    return archive;
}

FS_Archive openHomemenuExtdata() {
    Result res;
    u8 region = 0;
    u32 extpath[3] = {0x00000082, 0x0000008f, 0x00000098};

    res = CFGU_SecureInfoGetRegion(&region);
    printf("Retrieving console's region... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FS_Archive archive;
    u32 archpath[3] = {MEDIATYPE_SD, (region > 2) ? 0x00000082 : extpath[region], 0x00000000};
    FS_Path fspath = {PATH_BINARY, 12, archpath};

    res = FSUSER_OpenArchive(&archive, ARCHIVE_EXTDATA, fspath);
    printf("Opening homemenu extdata archive... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    return archive;
}

void clearStepHistory(bool wait = true) {
    Result res = PTMSYSM_ClearStepHistory();
    printf("Clearing step history... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    if (R_FAILED(res)) promptError("Clear Step History", "Failed to clear step history.");
    else if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void clearPlayHistory(bool wait = true) {
    Result res = PTMSYSM_ClearPlayHistory();
    printf("Clearing play history... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    if (R_FAILED(res)) promptError("Clear Step History", "Failed to clear play history.");
    else if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void clearSharedIconCache(bool wait = true) {
    Result res;
    FS_Archive shared = openSharedExtdata();

    res = FSUSER_DeleteFile(shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"));
    printf("Deleting file \"idb.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    if (R_FAILED(res)) promptError("Clear Shared Icon Cache", "Failed to delete cache file.");
    res = FSUSER_DeleteFile(shared, (FS_Path)fsMakePath(PATH_ASCII, "/idbt.dat"));
    printf("Deleting file \"idbt.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    if (R_FAILED(res)) promptError("Clear Shared Icon Cache", "Failed to delete cache file.");

    FSUSER_CloseArchive(shared);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void updateSharedIconCache(bool wait = true) {
    Result res;
    Handle idb;
    Handle idbt;
    FS_Archive shared = openSharedExtdata();

    res = FSUSER_OpenFile(&idb, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"idb.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&idbt, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idbt.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"idbt.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    u64 titlecount = 0;
    u64* tids = getTitleList(&titlecount);

    ENTRY_DATA* entries = getSharedEntryList(&idbt);
    SMDH_SHARED* icons = getSharedIconList(&idb);
    SMDH_HOMEMENU* newicons = getSystemIconList(tids, titlecount);

    bool success = false;

    if (dobackup) {
        FILE* backup = fopen("/3ds/cachetool/shared.bak", "wb");
        printf("Backing up icon data... ");
        gfxEndFrame();

        u32 fsize = sizeof(SMDH_SHARED)*ENTRY_SHARED_COUNT;
        success = (fwrite(icons, 1, fsize, backup)==fsize);
        printf("%s.\n", success ? "OK" : "ERROR");
        fclose(backup);
    } else {
        printf("Skipping icon data backup.\n");
        success = true;
    }

    if ((success) || (promptConfirm("Update Shared Icon Cache", "Couldn't backup icon data. Continue anyway?"))) {
        for (u64 i = 0; i < ENTRY_SHARED_COUNT; i++) {
            for (u64 pos = 0; pos < ((titlecount & 0xFFFFFFFF) + (titlecount >> 32)); pos++) {
                if (((tids[pos] >> 32) & 0x8000)==0 && tids[pos]==entries[i].titleid) {
                    memcpy(icons[i].titles, newicons[pos].titles, sizeof(SMDH_META)*0x10);
                    memcpy(icons[i].smallIcon, newicons[pos].smallIcon, 0x480);
                    memcpy(icons[i].largeIcon, newicons[pos].largeIcon, 0x1200);
                    printf("\x1b[22;0HReplacing entry %llx...", entries[i].titleid);
                    break;
                }
            }

            u32 wsize = 0;
            res = FSFILE_Write(idb, &wsize, i*sizeof(SMDH_SHARED), &icons[i], sizeof(SMDH_SHARED), 0);
            printf("\x1b[24;0HWriting entry %llu to file... %s %lx.", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
            gfxEndFrame();
        }
    }

    delete[] entries;
    delete[] icons;
    delete[] newicons;

    FSFILE_Close(idb);
    FSFILE_Close(idbt);
    FSUSER_CloseArchive(shared);

    if (wait) {
        printf("\nPress any key to continue.\n");
        waitKey();
    }
}

void restoreSharedIconCache() {
    Result res;
    Handle idb;
    FS_Archive shared = openSharedExtdata();

    FILE* backup = fopen("/3ds/cachetool/shared.bak", "rb");
    if (backup==NULL) promptError("Restore Shared Icon Cache", "No usable backup found.");
    else {
        res = FSUSER_OpenFile(&idb, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
        printf("Opening file \"idb.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

        u32 fsize = sizeof(SMDH_SHARED)*ENTRY_SHARED_COUNT;
        u8* buffer = (u8*)malloc(fsize);
        u32 wsize = 0;

        fread(buffer, 1, fsize, backup);
        printf("Restoring backup to \"idb.dat\"... ");
        res = FSFILE_Write(idb, &wsize, 0x0, buffer, fsize, 0);
        printf("%s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || wsize < fsize) promptError("Restore Shared Icon Cache", "Failed to restore backup.");
        else promptError("Restore Shared Icon Cache", "Successfully restored backup.");

        free(buffer);
        fclose(backup);
        FSFILE_Close(idb);
    }
}

void clearHomemenuIconCache(bool wait = true) {
    Result res;
    FS_Archive hmextdata = openHomemenuExtdata();

    res = FSUSER_DeleteFile(hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/Cache.dat"));
    printf("Deleting file \"Cache.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    if (R_FAILED(res)) promptError("Clear HOME Menu Icon Cache", "Failed to delete cache file.");
    res = FSUSER_DeleteFile(hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/CacheD.dat"));
    printf("Deleting file \"CacheD.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    if (R_FAILED(res)) promptError("Clear HOME Menu Icon Cache", "Failed to delete cache file.");

    FSUSER_CloseArchive(hmextdata);

    if (wait) {
        printf("Rebooting...\n");
        svcSleepThread(2000000000);
        APT_HardwareResetAsync();
    }
}

void updateHomemenuIconCache(bool wait = true) {
    Result res;
    Handle cache;
    Handle cached;
    FS_Archive hmextdata = openHomemenuExtdata();

    res = FSUSER_OpenFile(&cache, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/Cache.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"Cache.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&cached, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/CacheD.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"CacheD.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    u64 titlecount = 0;
    u64* tids = getTitleList(&titlecount);

    ENTRY_DATA* entries = getHomemenuEntryList(&cache);
    SMDH_HOMEMENU* icons = getHomemenuIconList(&cached);
    SMDH_HOMEMENU* newicons = getSystemIconList(tids, titlecount);

    bool success = false;

    if (dobackup) {
        FILE* backup = fopen("/3ds/cachetool/homemenu.bak", "wb");
        printf("Backing up icon data... ");
        gfxEndFrame();

        u32 fsize = sizeof(SMDH_HOMEMENU)*ENTRY_HOMEMENU_COUNT;
        success = (fwrite(icons, 1, fsize, backup)==fsize);
        printf("%s.\n", success ? "OK" : "ERROR");
        fclose(backup);
    } else {
        printf("Skipping icon data backup.\n");
        success = true;
    }

    if ((success) || (promptConfirm("Update HOME Menu Icon Cache", "Couldn't backup icon data. Continue anyway?"))) {
        for (u64 i = 0; i < ENTRY_HOMEMENU_COUNT; i++) {
            for (u64 pos = 0; pos < ((titlecount & 0xFFFFFFFF) + (titlecount >> 32)); pos++) {
                if (((tids[pos] >> 32) & 0x8000)==0 && tids[pos]==entries[i].titleid) {
                    memcpy(&icons[i], &newicons[pos], sizeof(SMDH_HOMEMENU));
                    printf("\x1b[22;0HReplacing entry %llx...", entries[i].titleid);
                    break;
                }
            }

            u32 wsize = 0;
            res = FSFILE_Write(cached, &wsize, i*sizeof(SMDH_HOMEMENU), &icons[i], sizeof(SMDH_HOMEMENU), 0);
            printf("\x1b[28;0HWriting entry %llu to file... %s %lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
            gfxEndFrame();
        }
    }

    delete[] entries;
    delete[] icons;
    delete[] newicons;

    FSFILE_Close(cache);
    FSFILE_Close(cached);
    FSUSER_CloseArchive(hmextdata);

    if (wait) {
        printf("\nPress any key to continue.\n");
        waitKey();
    }
}

void restoreHomemenuIconCache() {
    Result res;
    Handle cached;
    FS_Archive hmextdata = openHomemenuExtdata();

    FILE* backup = fopen("/3ds/cachetool/homemenu.bak", "rb");
    if (backup==NULL) promptError("Restore HOME Menu Icon Cache", "No usable backup found.");
    else {
        res = FSUSER_OpenFile(&cached, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/CacheD.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
        printf("Opening file \"CacheD.dat\"... %s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

        u32 fsize = sizeof(SMDH_HOMEMENU)*ENTRY_HOMEMENU_COUNT;
        u8* buffer = (u8*)malloc(fsize);
        u32 wsize = 0;

        fread(buffer, 1, fsize, backup);
        printf("Restoring backup to \"CacheD.dat\"... ");
        res = FSFILE_Write(cached, &wsize, 0x0, buffer, fsize, 0);
        printf("%s %lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || wsize < fsize) promptError("Restore HOME Menu Icon Cache", "Failed to restore backup.");
        else promptError("Restore HOME Menu Icon Cache", "Successfully restored backup.");

        free(buffer);
        fclose(backup);
        FSFILE_Close(cached);
    }
}

void goBerserk() {
    if (!promptConfirm("Go Berserk and Clear Everything", "CLEAR ALL PLAY LOGS AND CACHED ICON DATA?")) return;
    clearStepHistory(false);
    clearPlayHistory(false);
    clearSharedIconCache(false);
    clearHomemenuIconCache(false);
    printf("Rebooting...\n");
    svcSleepThread(2000000000);
    APT_HardwareResetAsync();
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    // ptmSysmInit();

    if (R_FAILED(srvGetServiceHandle(&ptmSysmHandle, "ptm:sysm"))) {
        promptError("SysMenu PTM Service", "Failed to get ptm:sysm service handle.");
        gfxExit();
        return 0;
    }

    cfguInit();
    amInit();
    fsInit();

    mkdir("/3ds/cachetool", 0777);

    hidScanInput();
    u32 kHeld = hidKeysHeld();

    u8 option = 0;

    if (kHeld & KEY_L) goBerserk();
    else while (aptMainLoop()) {
        printf("\x1b[0;0H\x1b[30;47m%-50s", " ");
        printf("\x1b[0;13H%s\x1b[0;0m", "Cthulhu (CacheTool) v1.0");
        printf("\x1b[1;2HClear step history.");
        printf("\x1b[2;2HClear play history.");
        printf("\x1b[3;2HClear shared icon cache.");
        printf("\x1b[4;2HUpdate shared icon cache.");
        printf("\x1b[5;2HRestore shared icon cache.");
        printf("\x1b[6;2HClear HOME Menu icon cache.");
        printf("\x1b[7;2HUpdate HOME Menu icon cache.");
        printf("\x1b[8;2HRestore HOME Menu icon cache.");
        printf("\x1b[9;2HGO BERSERK AND CLEAR EVERYTHING!");
        printf("\x1b[%u;0H>", option + 1);

        printf("\x1b[28;0HPress SELECT to toggle auto backup.");
        printf("\x1b[29;0HAuto backup of icon cache: %s", dobackup ? "ON " : "OFF");

        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_DOWN) {
            if (option < 8) printf("\x1b[%u;0H ", ++option);
            // else option = 0;
        }
        if (kDown & KEY_UP) {
            if (option > 0) printf("\x1b[%u;0H ", 1+(option--));
            // else option = 7;
        }

        if (kDown & KEY_SELECT) {
            dobackup = !dobackup;
        }

        if ((kDown & KEY_A)) {
            switch(option) {
                case 0: if (promptConfirm("Clear Step History", "This can't be undone. Are you sure?")) clearStepHistory(); break;
                case 1: if (promptConfirm("Clear Play History", "This can't be undone. Are you sure?")) clearPlayHistory(); break;
                case 2: if (promptConfirm("Clear Shared Icon Cache", "This will also clear your Activity Log title list.Are you sure?")) clearSharedIconCache(); break;
                case 3: if (promptConfirm("Update Shared Icon Cache", "Update shared cached icon data?")) updateSharedIconCache(); break;
                case 4: if (promptConfirm("Restore Shared Icon Cache", "Restore cached icon data from backup?")) restoreSharedIconCache(); break;
                case 5: if (promptConfirm("Clear HOME Menu Icon Cache", "Delete cached icon data? The system will reboot afterwards.")) clearHomemenuIconCache(); break;
                case 6: if (promptConfirm("Update HOME Menu Icon Cache", "Update HOME Menu cached icon data?")) updateHomemenuIconCache(); break;
                case 7: if (promptConfirm("Restore HOME Menu Icon Cache", "Restore cached icon data from backup?")) restoreHomemenuIconCache(); break;
                case 8: goBerserk(); break;
            }
        }

        if (kDown & KEY_START) break;

        gfxEndFrame();
    }

    fsExit();
    amExit();
    cfguExit();
    svcCloseHandle(ptmSysmHandle);
    // ptmSysmExit();
    gfxExit();
    return 0;
}
