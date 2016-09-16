#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <3ds.h>

#define ENTRY_SHARED_COUNT 0x200
#define ENTRY_HOMEMENU_COUNT 0x168

#define SUBMENU_COUNT 7
#define MAX_OPTIONS_PER_SUBMENU 10

bool dobackup = true;

Handle ptmSysmHandle;
Handle amHandle;

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

Result AM_DeleteAllDemoLaunchInfos(void)
{
	Result ret;
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0x827,0,0); // 0x8270000

	if(R_FAILED(ret = svcSendSyncRequest(amHandle)))return ret;

	return (Result)cmdbuf[1];
}

void gfxEndFrame() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

bool pathExists(char* path) {
    bool result = false;
    DIR *dir = opendir(path);
    if (dir != NULL) result = true;
    closedir(dir);
    return result;
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

    amInit();
    res = AM_GetTitleCount(MEDIATYPE_NAND, &count1);
    if(R_FAILED(res)) return NULL;
    res = AM_GetTitleCount(MEDIATYPE_SD, &count2);
    if(R_FAILED(res)) return NULL;

    u64* tids = new u64[count1 + count2];

    res = AM_GetTitleList(&count1, MEDIATYPE_NAND, count1, tids);
    printf("Retrieving NAND title list... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = AM_GetTitleList(&count2, MEDIATYPE_SD, count2, &tids[count1]);
    printf("Retrieving SD title list... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    amExit();

    *count = (((u64)count1)<<32 | (u64)count2);
    printf("Found %llu titles.\n", ((*count & 0xFFFFFFFF) + (*count >> 32)));

    return tids;
}

ENTRY_DATA* getSharedEntryList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    ENTRY_DATA* data = new ENTRY_DATA[ENTRY_SHARED_COUNT];

    for (u64 i = 0; i < ENTRY_SHARED_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(ENTRY_DATA), &data[i], sizeof(ENTRY_DATA));
        printf("\x1b[15;0HReading entry data %llu... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(ENTRY_DATA)) break;
        gfxEndFrame();
    }

    return data;
}

SMDH_SHARED* getSharedIconList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    SMDH_SHARED* data = new SMDH_SHARED[ENTRY_SHARED_COUNT];

    for (u64 i = 0; i < ENTRY_SHARED_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(SMDH_SHARED), &data[i], sizeof(SMDH_SHARED));
        printf("\x1b[16;0HReading icon data %llu... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(SMDH_SHARED)) break;
        gfxEndFrame();
    }

    return data;
}

ENTRY_DATA* getHomemenuEntryList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    ENTRY_DATA* data = new ENTRY_DATA[ENTRY_HOMEMENU_COUNT];

    for (u64 i = 0; i < ENTRY_HOMEMENU_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(ENTRY_DATA), &data[i], sizeof(ENTRY_DATA));
        printf("\x1b[15;0HReading entry data %llu... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(ENTRY_DATA)) break;
        gfxEndFrame();
    }

    return data;
}

SMDH_HOMEMENU* getHomemenuIconList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving cache file size... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    SMDH_HOMEMENU* data = new SMDH_HOMEMENU[ENTRY_HOMEMENU_COUNT];

    for (u64 i = 0; i < ENTRY_HOMEMENU_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, i*sizeof(SMDH_HOMEMENU), &data[i], sizeof(SMDH_HOMEMENU));
        printf("\x1b[16;0HReading icon data %llu... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
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
            printf("\x1b[18;0HOpening SMDH file %llx... %s %#lx.", tids[i], R_FAILED(res) ? "ERROR" : "OK", res);

            if (R_SUCCEEDED(res)) {
                u32 bytesRead = 0;
                res = FSFILE_Read(smdhHandle, &bytesRead, 0x0, &icons[i], sizeof(SMDH_HOMEMENU));
                printf("\x1b[19;0HReading SMDH %llx... %s %#lx.", tids[i], R_FAILED(res) ? "ERROR" : "OK", res);
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

FS_Archive openExtdata(u32* UniqueID, FS_ArchiveID archiveId) {
    Result res;
    u8 region = 0;

    res = CFGU_SecureInfoGetRegion(&region);
    printf("Retrieving console's region... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FS_Archive archive;
    FS_MediaType media = (archiveId==ARCHIVE_SHARED_EXTDATA) ? MEDIATYPE_NAND : MEDIATYPE_SD;
    u32 low = ((archiveId==ARCHIVE_SHARED_EXTDATA) || (region > 2)) ? UniqueID[0] : UniqueID[region];
    u32 high = (archiveId==ARCHIVE_SHARED_EXTDATA) ? 0x00048000 : 0x00000000;
    u32 archpath[3] = {media, low, high};
    FS_Path fspath = {PATH_BINARY, 12, archpath};

    res = FSUSER_OpenArchive(&archive, archiveId, fspath);
    printf("Opening archive %#lx... %s %#lx.\n", low, R_FAILED(res) ? "ERROR" : "OK", res);

    return archive;
}

FS_Archive openSystemSavedata(u32* UniqueID) {
    Result res;
    u8 region = 0;

    res = CFGU_SecureInfoGetRegion(&region);
    printf("Retrieving console's region... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FS_Archive archive;
    u32 low = (region > 2) ? UniqueID[0] : UniqueID[region];
    u32 archpath[2] = {MEDIATYPE_NAND, low};

    FS_Path fspath = {PATH_BINARY, 8, archpath};

    res = FSUSER_OpenArchive(&archive, ARCHIVE_SYSTEM_SAVEDATA, fspath);
    printf("Opening archive %#lx... %s %#lx.\n", low, R_FAILED(res) ? "ERROR" : "OK", res);

    return archive;
}

void clearStepHistory(bool wait = true) {
    Result res = PTMSYSM_ClearStepHistory();
    if (R_FAILED(res)) promptError("Clear Step History", "Failed to clear step history.");
    printf("Clearing step history... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void clearSoftwareLibrary(bool wait = true) {
    Result res;

    u32 activitylogID[3] = {0x00020202, 0x00020212, 0x00020222};
    FS_Archive syssave = openSystemSavedata(activitylogID);

    res = FSUSER_DeleteFile(syssave, (FS_Path)fsMakePath(PATH_ASCII, "/pld.dat"));
    if (R_FAILED(res)) promptError("Clear Software Library", "Failed to delete software library data.");
    printf("Deleting file \"pld.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_CloseArchive(syssave);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void clearPlayHistory(bool wait = true) {
    Result res = PTMSYSM_ClearPlayHistory();
    if (R_FAILED(res)) promptError("Clear Step History", "Failed to clear play history.");
    printf("Clearing play history... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void resetDemoPlayCount(bool wait = true) {
    Result res = AM_DeleteAllDemoLaunchInfos();
    if (R_FAILED(res)) promptError("Reset Demo Play Count", "Failed to reset demo play count.");
    printf("Reseting demo play count... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void clearSharedIconCache(bool wait = true) {
    Result res;
    u32 sharedID = 0xF000000B;
    FS_Archive shared = openExtdata(&sharedID, ARCHIVE_SHARED_EXTDATA);

    res = FSUSER_DeleteFile(shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"));
    if (R_FAILED(res)) promptError("Clear Shared Icon Cache", "Failed to delete cache file.");
    printf("Deleting file \"idb.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_DeleteFile(shared, (FS_Path)fsMakePath(PATH_ASCII, "/idbt.dat"));
    if (R_FAILED(res)) promptError("Clear Shared Icon Cache", "Failed to delete cache file.");
    printf("Deleting file \"idbt.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_CloseArchive(shared);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

bool backupSharedIconCache(bool wait = true) {
    Result res;
    Handle idb;
    Handle idbt;
    bool success = false;
    u32 sharedID = 0xF000000B;
    FS_Archive shared = openExtdata(&sharedID, ARCHIVE_SHARED_EXTDATA);

    res = FSUSER_OpenFile(&idb, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"), FS_OPEN_READ, 0);
    printf("Opening file \"idb.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&idbt, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idbt.dat"), FS_OPEN_READ, 0);
    printf("Opening file \"idbt.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FILE* backup1 = fopen("/3ds/data/cthulhu/idb.bak", "wb");
    FILE* backup2 = fopen("/3ds/data/cthulhu/idbt.bak", "wb");
    printf("Backing up icon data... ");
    gfxEndFrame();

    u32 fsize1 = sizeof(SMDH_SHARED)*ENTRY_SHARED_COUNT;
    u32 fsize2 = sizeof(ENTRY_DATA)*ENTRY_SHARED_COUNT;
    u8* buffer1 = (u8*)malloc(fsize1);
    u8* buffer2 = (u8*)malloc(fsize2);

    FSFILE_Read(idb, NULL, 0x0, buffer1, fsize1);
    FSFILE_Read(idbt, NULL, 0x0, buffer2, fsize2);
    FSFILE_Close(idb);
    FSFILE_Close(idbt);

    success = ((fwrite(buffer1, 1, fsize1, backup1)==fsize1) && (fwrite(buffer2, 1, fsize2, backup2)==fsize2));
    printf("%s.\n", success ? "OK" : "ERROR");

    if (!success) promptError("Backup Shared Icon Cache", "Couldn't backup icon data.");

    free(buffer1);
    free(buffer2);
    fclose(backup1);
    fclose(backup2);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }

    return success;
}

void updateSharedIconCache() {
    Result res;
    Handle idb;
    Handle idbt;
    u32 sharedID = 0xF000000B;
    FS_Archive shared = openExtdata(&sharedID, ARCHIVE_SHARED_EXTDATA);

    res = FSUSER_OpenFile(&idb, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"idb.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&idbt, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idbt.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"idbt.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    u64 titlecount = 0;
    u64* tids = getTitleList(&titlecount);

    ENTRY_DATA* entries = getSharedEntryList(&idbt);
    SMDH_SHARED* icons = getSharedIconList(&idb);
    SMDH_HOMEMENU* newicons = getSystemIconList(tids, titlecount);

    bool success = false;

    if (dobackup) {
        success = backupSharedIconCache(false);
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
            printf("\x1b[24;0HWriting entry %llu to file... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
            gfxEndFrame();
        }
    }

    delete[] entries;
    delete[] icons;
    delete[] newicons;

    FSFILE_Close(idb);
    FSFILE_Close(idbt);
    FSUSER_CloseArchive(shared);

    printf("Press any key to continue.\n");
    waitKey();
}

void restoreSharedIconCache() {
    Result res;
    Handle idb;
    Handle idbt;
    u32 sharedID = 0xF000000B;
    FS_Archive shared = openExtdata(&sharedID, ARCHIVE_SHARED_EXTDATA);

    FILE* backup1 = fopen("/3ds/data/cthulhu/idb.bak", "wb");
    FILE* backup2 = fopen("/3ds/data/cthulhu/idbt.bak", "wb");

    if (backup1==NULL || backup2==NULL) {
        promptError("Restore Shared Icon Cache", "No usable backup found.");
    } else {
        res = FSUSER_OpenFile(&idb, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
        printf("Opening file \"idb.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
        res = FSUSER_OpenFile(&idbt, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idbt.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
        printf("Opening file \"idbt.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

        u32 fsize1 = sizeof(SMDH_SHARED)*ENTRY_SHARED_COUNT;
        u32 fsize2 = sizeof(ENTRY_DATA)*ENTRY_SHARED_COUNT;
        u8* buffer1 = (u8*)malloc(fsize1);
        u8* buffer2 = (u8*)malloc(fsize2);
        u32 wsize1 = 0;
        u32 wsize2 = 0;

        fread(buffer1, 1, fsize1, backup1);
        fread(buffer2, 1, fsize2, backup2);
        printf("Restoring backup to \"idb.dat\"... ");
        res = FSFILE_Write(idb, &wsize1, 0x0, buffer1, fsize1, 0);
        printf("%s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
        res = FSFILE_Write(idbt, &wsize2, 0x0, buffer2, fsize2, 0);
        printf("%s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || wsize1 < fsize1 || wsize2 < fsize2) promptError("Restore Shared Icon Cache", "Failed to restore backup.");
        else promptError("Restore Shared Icon Cache", "Successfully restored backup.");

        free(buffer1);
        free(buffer2);
        fclose(backup1);
        fclose(backup2);
        FSFILE_Close(idb);
        FSFILE_Close(idbt);
    }

    printf("Press any key to continue.\n");
    waitKey();
}

void clearHomemenuIconCache(bool wait = true) {
    Result res;
    u32 homemenuID[3] = {0x00000082, 0x0000008f, 0x00000098};
    FS_Archive hmextdata = openExtdata(homemenuID, ARCHIVE_EXTDATA);

    res = FSUSER_DeleteFile(hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/Cache.dat"));
    if (R_FAILED(res)) promptError("Clear HOME Menu Icon Cache", "Failed to delete cache file.");
    printf("Deleting file \"Cache.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_DeleteFile(hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/CacheD.dat"));
    if (R_FAILED(res)) promptError("Clear HOME Menu Icon Cache", "Failed to delete cache file.");
    printf("Deleting file \"CacheD.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_CloseArchive(hmextdata);

    if (wait) {
        if (envIsHomebrew()) {
            printf("Press any key to continue.\n");
            waitKey();
        } else {
            printf("Rebooting...\n");
            svcSleepThread(2000000000);
            APT_HardwareResetAsync();
        }
    }
}

bool backupHomemenuIconCache(bool wait = true) {
    Result res;
    Handle cache;
    Handle cached;
    bool success = false;
    u32 homemenuID[3] = {0x00000082, 0x0000008f, 0x00000098};
    FS_Archive hmextdata = openExtdata(homemenuID, ARCHIVE_EXTDATA);

    res = FSUSER_OpenFile(&cache, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/Cache.dat"), FS_OPEN_READ, 0);
    printf("Opening file \"Cache.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&cached, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/CacheD.dat"), FS_OPEN_READ, 0);
    printf("Opening file \"CacheD.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FILE* backup1 = fopen("/3ds/data/cthulhu/CacheD.bak", "wb");
    FILE* backup2 = fopen("/3ds/data/cthulhu/Cache.bak", "wb");
    printf("Backing up icon data... ");
    gfxEndFrame();

    u32 fsize1 = sizeof(SMDH_HOMEMENU)*ENTRY_HOMEMENU_COUNT;
    u32 fsize2 = sizeof(ENTRY_DATA)*ENTRY_HOMEMENU_COUNT;
    u8* buffer1 = (u8*)malloc(fsize1);
    u8* buffer2 = (u8*)malloc(fsize2);

    FSFILE_Read(cached, NULL, 0x0, buffer1, fsize1);
    FSFILE_Read(cache, NULL, 0x0, buffer2, fsize2);
    FSFILE_Close(cached);
    FSFILE_Close(cache);

    success = ((fwrite(buffer1, 1, fsize1, backup1)==fsize1) && (fwrite(buffer2, 1, fsize2, backup2)==fsize2));
    printf("%s.\n", success ? "OK" : "ERROR");

    if (!success) promptError("Backup HOME Menu Icon Cache", "Couldn't backup icon data.");

    free(buffer1);
    free(buffer2);
    fclose(backup1);
    fclose(backup2);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }

    return success;
}

void updateHomemenuIconCache() {
    Result res;
    Handle cache;
    Handle cached;
    u32 homemenuID[3] = {0x00000082, 0x0000008f, 0x00000098};
    FS_Archive hmextdata = openExtdata(homemenuID, ARCHIVE_EXTDATA);

    res = FSUSER_OpenFile(&cache, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/Cache.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"Cache.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&cached, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/CacheD.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"CacheD.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    u64 titlecount = 0;
    u64* tids = getTitleList(&titlecount);

    ENTRY_DATA* entries = getHomemenuEntryList(&cache);
    SMDH_HOMEMENU* icons = getHomemenuIconList(&cached);
    SMDH_HOMEMENU* newicons = getSystemIconList(tids, titlecount);

    bool success = false;

    if (dobackup) {
        success = backupHomemenuIconCache(false);
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
            printf("\x1b[28;0HWriting entry %llu to file... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
            gfxEndFrame();
        }
    }

    delete[] entries;
    delete[] icons;
    delete[] newicons;

    FSFILE_Close(cache);
    FSFILE_Close(cached);
    FSUSER_CloseArchive(hmextdata);

    printf("Press any key to continue.\n");
    waitKey();
}

void restoreHomemenuIconCache() {
    Result res;
    Handle cached;
    Handle cache;
    u32 homemenuID[3] = {0x00000082, 0x0000008f, 0x00000098};
    FS_Archive hmextdata = openExtdata(homemenuID, ARCHIVE_EXTDATA);

    FILE* backup1 = fopen("/3ds/data/cthulhu/CacheD.bak", "rb");
    FILE* backup2 = fopen("/3ds/data/cthulhu/Cache.bak", "rb");

    if (backup1==NULL || backup2==NULL) {
        promptError("Restore HOME Menu Icon Cache", "No usable backup found.");
    } else {
        res = FSUSER_OpenFile(&cached, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/CacheD.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
        printf("Opening file \"CacheD.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
        res = FSUSER_OpenFile(&cache, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/Cache.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
        printf("Opening file \"Cache.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

        u32 fsize1 = sizeof(SMDH_HOMEMENU)*ENTRY_HOMEMENU_COUNT;
        u32 fsize2 = sizeof(ENTRY_DATA)*ENTRY_HOMEMENU_COUNT;
        u8* buffer1 = (u8*)malloc(fsize1);
        u8* buffer2 = (u8*)malloc(fsize2);
        u32 wsize1 = 0;
        u32 wsize2 = 0;

        fread(buffer1, 1, fsize1, backup1);
        fread(buffer2, 1, fsize2, backup2);

        printf("Restoring backup to \"CacheD.dat\"... ");
        res = FSFILE_Write(cached, &wsize1, 0x0, buffer1, fsize1, 0);
        printf("%s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
        printf("Restoring backup to \"Cache.dat\"... ");
        res = FSFILE_Write(cache, &wsize2, 0x0, buffer2, fsize2, 0);
        printf("%s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

        if(R_FAILED(res) || wsize1 < fsize1 || wsize2 < fsize2) promptError("Restore HOME Menu Icon Cache", "Failed to restore backup.");
        else promptError("Restore HOME Menu Icon Cache", "Successfully restored backup.");

        free(buffer1);
        free(buffer2);
        fclose(backup1);
        fclose(backup2);
        FSFILE_Close(cached);
        FSFILE_Close(cache);
    }

    printf("Press any key to continue.\n");
    waitKey();
}

void unpackRepackHomemenuSoftware(bool repack) {
    Result res;
    Handle save;
    u32 homemenuID[3] = {0x00000082, 0x0000008f, 0x00000098};
    FS_Archive hmextdata = openExtdata(homemenuID, ARCHIVE_EXTDATA);

    res = FSUSER_OpenFile(&save, hmextdata, (FS_Path)fsMakePath(PATH_ASCII, "/SaveData.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    char title[49];
    snprintf(title, 48, "%s All HOME Menu Software.", repack ? "Repack" : "Unwrap");
    if (R_FAILED(res)) promptError(title, "Failed to open HOME Menu savedata.");
    printf("Opening file \"SaveData.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    u8* flags = (u8*)malloc(ENTRY_HOMEMENU_COUNT);

    u32 rsize = 0;
    res = FSFILE_Read(save, &rsize, 0xB48, flags, ENTRY_HOMEMENU_COUNT);
    printf("Reading file \"SaveData.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    if (rsize==ENTRY_HOMEMENU_COUNT) {
        u32 wsize = 0;
        memset(flags, repack, ENTRY_HOMEMENU_COUNT);
        res = FSFILE_Write(save, &wsize, 0xB48, flags, ENTRY_HOMEMENU_COUNT, 0);
        if (R_FAILED(res) || wsize < ENTRY_HOMEMENU_COUNT) promptError(title, "Failed to write icon status flags.");
    } else promptError(title, "Failed to read HOME Menu savedata.");

    free(flags);

    FSFILE_Close(save);
    FSUSER_CloseArchive(hmextdata);

    printf("Press any key to continue.\n");
    waitKey();
}

void removeSoftwareUpdateNag(bool wait = true) {
    Result res;
    u32 sharedID = 0xF000000E;
    FS_Archive shared = openExtdata(&sharedID, ARCHIVE_SHARED_EXTDATA);

    res = FSUSER_DeleteFile(shared, (FS_Path)fsMakePath(PATH_ASCII, "/versionlist.dat"));
    if (R_FAILED(res)) promptError("Remove Software Update Nag", "Failed to delete cache file.");
    printf("Deleting file \"versionlist.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_CloseArchive(shared);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void clearGameNotes() {
    Result res;

    u32 gamenotesID[3] = {0x00020087, 0x00020093, 0x0002009C};
    FS_Archive syssave = openSystemSavedata(gamenotesID);

    res = FSUSER_DeleteDirectory(syssave, (FS_Path)fsMakePath(PATH_ASCII, "/memo/"));
    if (R_FAILED(res)) promptError("Clear Game Notes", "Failed to delete game notes.");
    printf("Deleting folder \"memo\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_DeleteFile(syssave, (FS_Path)fsMakePath(PATH_ASCII, "/cfg.bin"));
    printf("Deleting file \"cfg.bin\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_CloseArchive(syssave);

    printf("Press any key to continue.\n");
    waitKey();
}

void resetEShopBGM() {
    Result res;
    u32 eshopID[3] = {0x00000209, 0x00000219, 0x00000229};
    FS_Archive eshopext = openExtdata(eshopID, ARCHIVE_EXTDATA);

    res = FSUSER_DeleteFile(eshopext, (FS_Path)fsMakePath(PATH_ASCII, "/boss_bgm1"));
    if (R_FAILED(res)) promptError("Reset eShop BGM", "Failed to delete eShop BGM file.");
    printf("Deleting file \"boss_bgm1\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_CloseArchive(eshopext);

    printf("Press any key to continue.\n");
    waitKey();
}

void replaceEShopBGM() {
    Result res;
    u32 eshopID[3] = {0x00000209, 0x00000219, 0x00000229};
    FS_Archive eshopext = openExtdata(eshopID, ARCHIVE_EXTDATA);

    FILE* newbgm = fopen("/3ds/data/cthulhu/boss_bgm.aac", "rb"); // getOpenFilename("/3ds/data/cthulhu");
    FILE* newxml = fopen("/3ds/data/cthulhu/boss_xml.xml", "rb");

    if (newbgm==NULL || newxml==NULL) {
        promptError("Replace eShop BGM", "Source file not found.");
        fclose(newbgm);
        fclose(newxml);
        return;
    }

    fseek(newbgm, 0, SEEK_END);
    fseek(newxml, 0, SEEK_END);
    size_t fsize = ftell(newbgm);
    size_t fsizex = ftell(newxml);
    u8* buffer = (u8*)malloc(fsize);
    u8* bufferx = (u8*)malloc(fsizex);
    fseek(newbgm, 0, SEEK_SET);
    fseek(newxml, 0, SEEK_SET);
    fread(buffer, 1, fsize, newbgm);
    fread(bufferx, 1, fsizex, newxml);
    fclose(newbgm);
    fclose(newxml);

    FS_Path bgmpath = fsMakePath(PATH_ASCII, "/boss_bgm1");
    FS_Path xmlpath = fsMakePath(PATH_ASCII, "/boss_xml1");

    FSUSER_DeleteFile(eshopext, bgmpath);
    FSUSER_DeleteFile(eshopext, xmlpath);
    FSUSER_CreateFile(eshopext, bgmpath, 0, fsize);
    FSUSER_CreateFile(eshopext, xmlpath, 0, fsizex);

    Handle oldbgm;
    Handle oldxml;
    res = FSUSER_OpenFile(&oldbgm, eshopext, bgmpath, FS_OPEN_WRITE, 0);
    if (R_FAILED(res)) promptError("Replace eShop BGM", "Failed to open eShop BGM file.");
    printf("Opening file \"boss_bgm1\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&oldxml, eshopext, xmlpath, FS_OPEN_WRITE, 0);
    if (R_FAILED(res)) promptError("Replace eShop BGM", "Failed to open eShop BGM metadata file.");
    printf("Opening file \"boss_xml1\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    u32 wsize = 0;
    printf("Replacing file \"boss_bgm1\"... ");
    res = FSFILE_Write(oldbgm, &wsize, 0x0, buffer, fsize, FS_WRITE_FLUSH);
    if (R_FAILED(res) || wsize < fsize) promptError("Replace eShop BGM", "Failed to replace eShop BGM file.");
    printf("%s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    printf("Replacing file \"boss_xml1\"... ");
    res = FSFILE_Write(oldxml, &wsize, 0x0, bufferx, fsizex, FS_WRITE_FLUSH);
    if (R_FAILED(res) || wsize < fsizex) promptError("Replace eShop BGM", "Failed to replace eShop BGM metadata file.");
    printf("%s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSFILE_Close(oldbgm);
    FSFILE_Close(oldxml);
    FSUSER_CloseArchive(eshopext);

    free(buffer);
    free(bufferx);

    printf("Press any key to continue.\n");
    waitKey();
}

void goBerserk() {
    if (!promptConfirm("Go Berserk and Clear Everything", "CLEAR ALL LOGS AND CACHED ICON DATA?")) return;
    clearStepHistory(false);
    clearPlayHistory(false);
    clearSharedIconCache(false);
    clearHomemenuIconCache(false);
    removeSoftwareUpdateNag(false);
    printf("Rebooting...\n");
    svcSleepThread(2000000000);
    APT_HardwareResetAsync();
}

void moveDataFolder() {
    rename("/3ds/cachetool/idb.bak", "/3ds/data/cthulhu/idb.bak");
    rename("/3ds/cachetool/idbt.bak", "/3ds/data/cthulhu/idbt.bak");
    rename("/3ds/cachetool/Cache.bak", "/3ds/data/cthulhu/Cache.bak");
    rename("/3ds/cachetool/CacheD.bak", "/3ds/data/cthulhu/CacheD.bak");
    rename("/3ds/cachetool/boss_bgm.aac", "/3ds/data/cthulhu/boss_bgm.aac");
    rename("/3ds/cachetool/boss_xml.xml", "/3ds/data/cthulhu/boss_xml.xml");
    rmdir("/3ds/cachetool");
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    if (R_FAILED(srvGetServiceHandle(&ptmSysmHandle, "ptm:sysm"))) promptError("SysMenu PTM Service", "Failed to get ptm:sysm service handle.");
    if (R_FAILED(srvGetServiceHandle(&amHandle, "am:net"))) promptError("Application Manager Service", "Failed to get am:net service handle.");
    cfguInit();
    fsInit();

    mkdir("/3ds/data", 0777);
    mkdir("/3ds/data/cthulhu", 0777);
    char oldpath[] = "/3ds/cachetool";
    if (pathExists(oldpath)) moveDataFolder();

    hidScanInput();
    u32 kHeld = hidKeysHeld();

    u8 menucount[SUBMENU_COUNT] = {6, 4, 3, 4, 4, 5, 3};
    const char* menuentries[SUBMENU_COUNT][MAX_OPTIONS_PER_SUBMENU] = 
    {
        {
            "Activity Log management.",
            "Friends List management.",
            "Shared icon cache management.",
            "HOME Menu icon cache management.",
            "HOME Menu software management.",
            "Miscellaneous."
        },
        {
            "Clear play history.",
            "Clear step history.",
            "Clear software library.",
            "[COMING SOON]" // "Edit software library."
        },
        {
            "[COMING SOON]", // "Clear Friends List.",
            "[COMING SOON]", // "Backup Friends List.",
            "[COMING SOON]" // "Restore Friends List."
        },
        {
            "Clear shared icon cache.",
            "Update shared icon cache.",
            "Backup shared icon cache.",
            "Restore shared icon cache."
        },
        {
            "Clear HOME Menu icon cache.",
            "Update HOME Menu icon cache.",
            "Backup HOME Menu icon cache.",
            "Restore HOME Menu icon cache."
        },
        {
            "Reset demo play count.",
            "[COMING SOON]", // "Reset folder count.",
            "Unwrap all HOME Menu software.",
            "Repack all HOME Menu software.",
            "Remove software update nag."//,
            // "Remove system update nag."
        },
        {
            "Clear Game Notes.",
            "Reset eShop BGM.",
            "Replace eShop BGM."
        }
    };

    u8 option[SUBMENU_COUNT] = {0};
    u8 submenu = 0;

    if (kHeld & KEY_L) goBerserk();
    else while (aptMainLoop()) {
        printf("\x1b[0;0H\x1b[30;47m%-50s", " ");
        printf("\x1b[0;19H%s\x1b[0;0m", "Cthulhu v1.1");

        for (u8 i = 0; i <= menucount[submenu]; i++) {
            if (i < menucount[submenu]) printf("\x1b[%u;2H%-48s", i+1, menuentries[submenu][i]);
            else if (submenu > 0) printf("\x1b[%u;2H%-48s", i+1, "Go back.");
        } printf("\x1b[%u;0H>", option[submenu] + 1);

        printf("\x1b[28;0HPress SELECT to toggle auto backup.");
        printf("\x1b[29;0HAuto backup of icon cache: %s", dobackup ? "ON " : "OFF");

        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_DOWN) {
            if (option[submenu] < menucount[submenu]-(submenu==0)) {
                printf("\x1b[%u;0H ", ++option[submenu]);
            } else {
                printf("\x1b[%u;0H ", option[submenu]+1);
                option[submenu] = 0;
            }
        } else if (kDown & KEY_UP) {
            if (option[submenu] > 0) {
                printf("\x1b[%u;0H ", 1+(option[submenu]--));
            } else {
                printf("\x1b[%u;0H ", option[submenu]+1);
                option[submenu] = menucount[submenu]-(submenu==0);
            }
        }

        if (kDown & KEY_A) {
            if (submenu==0) {
                submenu = option[0]+1;
                consoleClear();
            } else switch(submenu*MAX_OPTIONS_PER_SUBMENU + option[submenu]) {
                case 10: if (promptConfirm("Clear Play History", "This can't be undone without a backup. Are you sure?")) clearPlayHistory(); break;
                case 11: if (promptConfirm("Clear Step History", "This can't be undone without a backup. Are you sure?")) clearStepHistory(); break;
                case 12: if (promptConfirm("Clear Software Library", "This can't be undone without a backup. Are you sure?")) clearSoftwareLibrary(); break;

                case 30: if (promptConfirm("Clear Shared Icon Cache", "This will also clear your software library. Are you sure?")) clearSharedIconCache(); break;
                case 31: if (promptConfirm("Update Shared Icon Cache", "Update shared cached icon data?")) updateSharedIconCache(); break;
                case 32: if (promptConfirm("Backup Shared Icon Cache", "Backup shared cached icon data?")) backupSharedIconCache(); break;
                case 33: if (promptConfirm("Restore Shared Icon Cache", "Restore cached icon data from backup?")) restoreSharedIconCache(); break;

                case 40: if (promptConfirm("Clear HOME Menu Icon Cache", "Delete cached icon data? The system will reboot afterwards.")) clearHomemenuIconCache(); break;
                case 41: if (promptConfirm("Update HOME Menu Icon Cache", "Update HOME Menu cached icon data?")) updateHomemenuIconCache(); break;
                case 42: if (promptConfirm("Backup HOME Menu Icon Cache", "Backup HOME Menu cached icon data?")) backupHomemenuIconCache(); break;
                case 43: if (promptConfirm("Restore HOME Menu Icon Cache", "Restore cached icon data from backup?")) restoreHomemenuIconCache(); break;

                case 50: if (promptConfirm("Reset Demo Play Count", "Reset play count of all installed demos?")) resetDemoPlayCount(); break;
                case 52: if (promptConfirm("Unwrap All HOME Menu Software", "Unwrap all gift-wrapped software on HOME Menu?")) unpackRepackHomemenuSoftware(false); break;
                case 53: if (promptConfirm("Repack All HOME Menu Software", "Gift-wrap all software on HOME Menu?")) unpackRepackHomemenuSoftware(true); break;
                case 54: if (promptConfirm("Remove Software Update Nag", "Remove update nag of all installed software?")) removeSoftwareUpdateNag(); break;

                case 60: if (promptConfirm("Clear Game Notes", "Delete all of your game notes?")) clearGameNotes(); break;
                case 61: if (promptConfirm("Reset eShop BGM", "Restore the original Nintendo eShop music?")) resetEShopBGM(); break;
                case 62: if (promptConfirm("Replace eShop BGM", "Replace the current Nintendo eShop music?")) replaceEShopBGM(); break;

                default: submenu = 0; consoleClear(); break;
            }
        } else if ((kDown & KEY_B) && (submenu > 0)) {
            submenu = 0;
            consoleClear();
        }

        if (kDown & KEY_SELECT) dobackup = !dobackup;

        if (kDown & KEY_START) break;

        gfxEndFrame();
    }

    fsExit();
    cfguExit();
    svcCloseHandle(ptmSysmHandle);
    svcCloseHandle(amHandle);
    gfxExit();
    return 0;
}
