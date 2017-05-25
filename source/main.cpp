#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <algorithm>
#include <3ds.h>

#define ENTRY_SHARED_COUNT 0x200
#define ENTRY_HOMEMENU_COUNT 0x168
#define ENTRY_LIBRARY_START 0xC3510
#define ENTRY_LIBRARY_COUNT 0x100
#define ENTRY_HISTORY_COUNT 0x11D28

#define SUBMENU_COUNT 7
#define MAX_OPTIONS_PER_SUBMENU 10

#define VERSION_MAJOR 1
#define VERSION_MINOR 3
#define VERSION_MICRO 1

bool dobackup = true;

Handle ptmSysmHandle;

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

typedef struct {
    u64 titleid;
    u32 totalPlayed;
    u16 timesPlayed;
    u16 flags;
    u16 firstPlayed;
    u16 lastPlayed;
    u32 padding;
} ENTRY_LIBRARY;

typedef struct {
    u64 titleid;
    u32 timestamp;
} ENTRY_HISTORY;

typedef struct {
    u16 year;
    u8 month;
    u8 day;
} DATE;

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

Result PTMSYSM_GetPlayHistory(u32* read, u32 offset, u32 count, ENTRY_HISTORY* out)
{
    Result ret;
    u32 *cmdbuf = getThreadCommandBuffer();
    u32 size = count*sizeof(ENTRY_HISTORY);

    cmdbuf[0] = IPC_MakeHeader(0x807,2,2); // 0x8070082
    cmdbuf[1] = offset;
    cmdbuf[2] = count;
    cmdbuf[3] = IPC_Desc_Buffer(size, IPC_BUFFER_W);
    cmdbuf[4] = (u32)out;

    if(R_FAILED(ret = svcSendSyncRequest(ptmSysmHandle)))return ret;

    if (read) *read = cmdbuf[2];
    return (Result)cmdbuf[1];
}

void gfxEndFrame() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

DATE getDate(u32 J) {
    long int f = J + 1401 + (((4 * J + 274277) / 146097) * 3) / 4 - 38;
    long int e = 4 * f + 3;
    long int g = (e % 1461) / 4;
    long int h = 5 * g + 2;
    u8 D = (h % 153) / 5 + 1;
    u8 M = (h / 153 + 2) % 12 + 1;
    u16 Y = (e / 1461) - 4716 + (12 + 2 - M) / 12;
    return {Y, M, D};
}

u32 getJulianDay(DATE* date) {
    int a = (14 - date->month) / 12;
    int y = date->year + 4800 - a;
    int m = date->month + 12*a - 3;
    return (date->day) + ((153*m + 2) / 5) + 365*y + (y / 4) - (y / 100) + (y / 400) - 32045;
}

void utf2ascii(char* dst, u16* src) {
    if (!src || !dst) return;
    while(*src) *(dst++)=(*(src++))&0xFF;
    *dst=0x00;
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

bool promptConfirm(const char* title, const char* message) {
    consoleClear();
    printf("\x1b[0;0H\x1b[30;47m%-50s", " ");
    printf("\x1b[0;%uH%s\x1b[0;0m", (25 - (strlen(title) >> 1)), title);
    printf("\x1b[14;%uH%s", (25 - (strlen(message) >> 1)), message);
    printf("\x1b[16;14H\x1b[32m(A)\x1b[37m Confirm / \x1b[31m(B)\x1b[37m Cancel");
    u32 kDown = waitKey();
    if (kDown & KEY_A) return true;
    return false;
}

void promptError(const char* title, const char* message) {
    consoleClear();
    printf("\x1b[0;0H\x1b[30;47m%-50s", " ");
    printf("\x1b[0;%uH%s\x1b[0;0m", (25 - (strlen(title) >> 1)), title);
    printf("\x1b[14;%uH%s", (25 - (strlen(message) >> 1)), message);
    waitKey();
}

u64* getTitleList(u64* count) {
    Result res;
    u32 count1;
    u32 count2;

    amInit();
    res = AM_GetTitleCount(MEDIATYPE_NAND, &count1);
    if(R_FAILED(res)) return nullptr;
    res = AM_GetTitleCount(MEDIATYPE_SD, &count2);
    if(R_FAILED(res)) return nullptr;

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

    if (countt==0 || !tids) return nullptr;

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
            printf("\x1b[18;0HOpening SMDH file %#018llx... %s %#lx.", tids[i], R_FAILED(res) ? "ERROR" : "OK", res);

            if (R_SUCCEEDED(res)) {
                u32 bytesRead = 0;
                res = FSFILE_Read(smdhHandle, &bytesRead, 0x0, &icons[i], sizeof(SMDH_HOMEMENU));
                printf("\x1b[19;0HReading SMDH %#018llx... %s %#lx.", tids[i], R_FAILED(res) ? "ERROR" : "OK", res);
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

ENTRY_LIBRARY* getActivityEntryList(Handle* source) {
    Result res;
    u64 filesize = 0;

    res = FSFILE_GetSize(*source, &filesize);
    printf("Retrieving savefile size... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    ENTRY_LIBRARY* data = new ENTRY_LIBRARY[ENTRY_LIBRARY_COUNT];

    for (u64 i = 0; i < ENTRY_LIBRARY_COUNT; i++) {
        u32 rsize = 0;
        res = FSFILE_Read(*source, &rsize, ENTRY_LIBRARY_START + i*sizeof(ENTRY_LIBRARY), &data[i], sizeof(ENTRY_LIBRARY));
        printf("\x1b[15;0HReading entry data %llu... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
        if(R_FAILED(res) || rsize < sizeof(ENTRY_LIBRARY)) break;
        gfxEndFrame();
    }

    return data;
}

FS_Archive openExtdata(u32* UniqueID, FS_ArchiveID archiveId) {
    Result res;
    u8 region = 0;

    res = CFGU_SecureInfoGetRegion(&region);
    printf("Retrieving console's region... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FS_Archive archive;
    FS_MediaType media = (archiveId==ARCHIVE_SHARED_EXTDATA) ? MEDIATYPE_NAND : MEDIATYPE_SD;
    u32 low = (archiveId==ARCHIVE_SHARED_EXTDATA) ? *UniqueID : UniqueID[region];
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
    u32 low = UniqueID[region];
    u32 archpath[2] = {MEDIATYPE_NAND, low};

    FS_Path fspath = {PATH_BINARY, 8, archpath};

    res = FSUSER_OpenArchive(&archive, ARCHIVE_SYSTEM_SAVEDATA, fspath);
    printf("Opening archive %#lx... %s %#lx.\n", low, R_FAILED(res) ? "ERROR" : "OK", res);

    return archive;
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

    u32 activitylogID[] = {0x00020202, 0x00020212, 0x00020222, 0x00020222, 0x00020262, 0x00020272, 0x00020282};
    FS_Archive syssave = openSystemSavedata(activitylogID);

    res = FSUSER_DeleteFile(syssave, (FS_Path)fsMakePath(PATH_ASCII, "/pld.dat"));
    if (R_FAILED(res)) promptError("Clear Software Library", "Failed to delete software library data.");
    printf("Deleting file \"pld.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_ControlArchive(syssave, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    FSUSER_CloseArchive(syssave);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void editLibraryEntry(ENTRY_LIBRARY* library, u16 selected) {
    if (library[selected].titleid == 0xFFFFFFFFFFFFFFFF) return; // TODO: titleid editing
    u8 task = 0, option = 0, update = true;
    DATE firstPlayed = getDate(2451545 + library[selected].firstPlayed);
    DATE lastPlayed = getDate(2451545 + library[selected].lastPlayed);
    consoleClear();
    while(aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        // TODO: make scroll faster by using hidKeysHeld

        if (kDown & KEY_DOWN) {
            switch(task) {
                case 0: {
                    if (option < 3) {
                        printf("\x1b[%u;0H ", ++option);
                    } else {
                        printf("\x1b[%u;0H ", option+1);
                        option = 0;
                    }
                } break;
                case  1: { // Edit play count
                    if (library[selected].timesPlayed < 0xFFFF) library[selected].timesPlayed++;
                    else library[selected].timesPlayed = 0;
                } break;
                case  3: { // Edit hours played
                    if (library[selected].totalPlayed < 3600*4660) library[selected].totalPlayed+=3600;
                    else library[selected].totalPlayed -= 3600*4660;
                } break;
                case  4: { // Edit minutes played
                    if (library[selected].totalPlayed < 3600*4660+59*60) library[selected].totalPlayed+=60;
                    else library[selected].totalPlayed -= 3600*4660+59*60;
                } break;
                case  5: { // Edit seconds played
                    if (library[selected].totalPlayed < 3600*4660+59*60+59) library[selected].totalPlayed++;
                    else library[selected].totalPlayed -= 3600*4660+59*60+59;
                } break;
                case  6: { // Edit first played month
                    if (firstPlayed.month < 12) firstPlayed.month++;
                    else firstPlayed.month = 1;
                } break;
                case  7: { // Edit first played day
                    u8 d;
                    switch(firstPlayed.month) {
                        case 2: d = 28 + (firstPlayed.year % 4 == 0); break;
                        case 1: case 3: case 5: case 7: case 8: case 10: case 12: d = 31; break;
                        default: d = 30; break;
                    } if (firstPlayed.day < d) {
                        firstPlayed.day++;
                    } else firstPlayed.day = 0;
                } break;
                case  8: { // Edit first played year
                    if (firstPlayed.year < 2179) firstPlayed.year++;
                    else firstPlayed.year = 2000;
                } break;
                case  9: { // Edit last played month
                    if (lastPlayed.month < 12) lastPlayed.month++;
                    else lastPlayed.month = 1;
                } break;
                case 10: { // Edit last played day
                    u8 d;
                    switch(lastPlayed.month) {
                        case 2: d = 28 + (lastPlayed.year % 4 == 0); break;
                        case 1: case 3: case 5: case 7: case 8: case 10: case 12: d = 31; break;
                        default: d = 30; break;
                    } if (lastPlayed.day < d) {
                        lastPlayed.day++;
                    } else lastPlayed.day = 0;
                } break;
                case 11: { // Edit last played year
                    if (lastPlayed.year < 2179) lastPlayed.year++;
                    else lastPlayed.year = 2000;
                } break;
            }
        } else if (kDown & KEY_UP) {
            switch(task) {
                case 0: {
                    if (option > 0) {
                        printf("\x1b[%u;0H ", 1+(option--));
                    } else {
                        printf("\x1b[%u;0H ", option+1);
                        option = 3;
                    }
                } break;
                case  1: { // Edit play count
                    if (library[selected].timesPlayed > 0) library[selected].timesPlayed--;
                    else library[selected].timesPlayed = 0xFFFF;
                } break;
                case  3: { // Edit hours played
                    if (library[selected].totalPlayed > 3600) library[selected].totalPlayed-=3600;
                    else library[selected].totalPlayed += 3600*4660;
                } break;
                case  4: { // Edit minutes played
                    if (library[selected].totalPlayed > 60) library[selected].totalPlayed-=60;
                    else library[selected].totalPlayed += 60*60;
                } break;
                case  5: { // Edit seconds played
                    if (library[selected].totalPlayed > 0) library[selected].totalPlayed--;
                    else library[selected].totalPlayed += 60;
                } break;
                case  6: { // Edit first played month
                    if (firstPlayed.month > 1) firstPlayed.month--;
                    else firstPlayed.month = 12;
                } break;
                case  7: { // Edit first played day
                    if (firstPlayed.day > 1) {
                        firstPlayed.day--;
                    } else switch(firstPlayed.month) {
                        case 2: firstPlayed.day = 28 + (firstPlayed.year % 4 == 0); break;
                        case 1: case 3: case 5: case 7: case 8: case 10: case 12: firstPlayed.day = 31; break;
                        default: firstPlayed.day = 30; break;
                    }
                } break;
                case  8: { // Edit first played year
                    if (firstPlayed.year > 2000) firstPlayed.year--;
                    else firstPlayed.year = 2179;
                } break;
                case  9: { // Edit last played month
                    if (lastPlayed.month > 1) lastPlayed.month--;
                    else lastPlayed.month = 12;
                } break;
                case 10: { // Edit last played day
                    if (lastPlayed.day > 1) {
                        lastPlayed.day--;
                    } else switch(lastPlayed.month) {
                        case 2: lastPlayed.day = 28 + (lastPlayed.year % 4 == 0); break;
                        case 1: case 3: case 5: case 7: case 8: case 10: case 12: lastPlayed.day = 31; break;
                        default: lastPlayed.day = 30; break;
                    }
                } break;
                case 11: { // Edit last played year
                    if (lastPlayed.year > 2000) lastPlayed.year--;
                    else lastPlayed.year = 2179;
                } break;
            }
        } else if (task > 2) {
            if (kDown & KEY_LEFT) {
                if (task % 3 > 0) task--;
            } else if (kDown & KEY_RIGHT) {
                if (task % 3 < 2) task++;
            }
        }

        if (kDown & KEY_A) {
            if (task==0) task = 3*option + (option==0);
            else task = 0;
        } else if (kDown & KEY_B) {
            if (task > 0) task = 0;
            else break;
        }

        if (update || kDown) {
            printf("\x1b[0;0H%#018llx", library[selected].titleid);
            printf("\x1b[1;2H\x1b[0mTimes Played: \x1b[%sm%05u", (task==1) ? "30;47" : "0", library[selected].timesPlayed);
            printf("\x1b[2;2H\x1b[0mTotal Play Time: \x1b[%sm%04lu\x1b[0m:\x1b[%sm%02lu\x1b[0m:\x1b[%sm%02lu", (task==3) ? "30;47" : "0", library[selected].totalPlayed / 3600, (task==4) ? "30;47" : "0", (library[selected].totalPlayed / 60) % 60, (task==5) ? "30;47" : "0", library[selected].totalPlayed % 60);
            printf("\x1b[3;2H\x1b[0mFirst Played: \x1b[%sm%02u\x1b[0m/\x1b[%sm%02u\x1b[0m/\x1b[%sm%04u", (task==6) ? "30;47" : "0", firstPlayed.month, (task==7) ? "30;47" : "0", firstPlayed.day, (task==8) ? "30;47" : "0", firstPlayed.year);
            printf("\x1b[4;2H\x1b[0mLast Played: \x1b[%sm%02u\x1b[0m/\x1b[%sm%02u\x1b[0m/\x1b[%sm%04u", (task==9) ? "30;47" : "0", lastPlayed.month, (task==10) ? "30;47" : "0", lastPlayed.day, (task==11) ? "30;47" : "0", lastPlayed.year);
            u32 average = library[selected].totalPlayed / library[selected].timesPlayed;
            printf("\x1b[6;2H\x1b[0mAverage Play Time: %04lu:%02lu:%02lu", average / 3600, (average / 60) % 60, average % 60);
            printf("\x1b[%u;0H>", 1 + option);
            update = false;
        }

        gfxEndFrame();
    }
    library[selected].firstPlayed = getJulianDay(&firstPlayed) - 2451545;
    library[selected].lastPlayed = getJulianDay(&lastPlayed) - 2451545;
}

void editSoftwareLibrary() {
    Result res;
    Handle pld;
    Handle idb;
    Handle idbt;

    u32 activitylogID[] = {0x00020202, 0x00020212, 0x00020222, 0x00020222, 0x00020262, 0x00020272, 0x00020282};
    FS_Archive syssave = openSystemSavedata(activitylogID);

    u32 sharedID = 0xF000000B;
    FS_Archive shared = openExtdata(&sharedID, ARCHIVE_SHARED_EXTDATA);

    res = FSUSER_OpenFile(&idb, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idb.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"idb.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&idbt, shared, (FS_Path)fsMakePath(PATH_ASCII, "/idbt.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"idbt.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    res = FSUSER_OpenFile(&pld, syssave, (FS_Path)fsMakePath(PATH_ASCII, "/pld.dat"), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    printf("Opening file \"pld.dat\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    ENTRY_LIBRARY* library = getActivityEntryList(&pld);
    ENTRY_DATA* entries = getSharedEntryList(&idbt);
    SMDH_SHARED* icons = getSharedIconList(&idb);
    u64* tids = new u64[ENTRY_SHARED_COUNT];
    for (int i = 0; i < ENTRY_SHARED_COUNT; i++) tids[i] = entries[i].titleid;

    u8 selected = 0, scroll = 0, update = true;

    while(aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_DOWN) {
            if (selected<14) selected++;
            else if ((selected + scroll) < (ENTRY_LIBRARY_COUNT - 15)) scroll++;
            else if (selected<28) selected++;
            else { selected = 0; scroll = 0; }
        } else if (kDown & KEY_UP) {
            if (selected>13) selected--;
            else if (scroll>0) scroll--;
            else if (selected>0) selected--;
            else { selected = 28; scroll = ENTRY_LIBRARY_COUNT - 29; }
        }

        if (kDown & KEY_A) {
            editLibraryEntry(library, selected+scroll);
        } else if (kDown & KEY_X) {
            memset(&library[selected+scroll], 0xFF, sizeof(ENTRY_LIBRARY));
        } else if (kDown & KEY_Y) {
            if (library[selected+scroll].flags < 4) library[selected+scroll].flags <<= 1;
            else library[selected+scroll].flags = 1;
        } else if (kDown & KEY_B) break;

        if (update || kDown) {
            consoleClear();
            printf("\x1b[0;0HSOFTWARE LIBRARY:");
            u32 i = 0;
            while (i < ENTRY_LIBRARY_COUNT) {
                if (i > 28) break;
                switch (library[i+scroll].flags) {
                    case 0x0000: case 0x0001: printf("\x1b[31m"); break; // Set color to red if unlisted
                    case 0x0002: case 0x0006: printf("\x1b[32m"); break; // Set color to green if listed
                    case 0x0004: case 0x0008: printf("\x1b[33m"); break; // Set color to yellow if pending
                    default: printf("\x1b[0m"); break;                   // Set color to white if unknown
                }
                char title[32];
                utf2ascii(title, icons[std::distance(tids, std::find(tids, tids + ENTRY_SHARED_COUNT, library[i+scroll].titleid))].titles[1].shortDescription);
                printf("\x1b[%lu;0H  %.22s\x1b[%lu;25H%#018llx", 1 + i, title, 1 + i, library[i+scroll].titleid);
                i++;
            }
            printf("\x1b[%u;0H\x1b[0m>", 1 + selected);
            update = false;
        }

        gfxEndFrame();
    }

    if (promptConfirm("Edit Software Library", "Save changes?")) {
        for (u64 i = 0; i < ENTRY_LIBRARY_COUNT; i++) {
            u32 wsize = 0;
            res = FSFILE_Write(pld, &wsize, ENTRY_LIBRARY_START + i*sizeof(ENTRY_LIBRARY), &library[i], sizeof(ENTRY_LIBRARY), 0);
            printf("\x1b[15;0HWriting entry data %llu to file... %s %#lx.\n", i+1, R_FAILED(res) ? "ERROR" : "OK", res);
            if(R_FAILED(res) || wsize < sizeof(ENTRY_LIBRARY)) break;
            gfxEndFrame();
        }
    }

    delete[] library;
    delete[] entries;
    delete[] icons;
    delete[] tids;

    FSFILE_Close(pld);
    FSFILE_Close(idb);
    FSFILE_Close(idbt);
    FSUSER_ControlArchive(syssave, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    FSUSER_CloseArchive(syssave);
    FSUSER_CloseArchive(shared);

    printf("Press any key to continue.\n");
    waitKey();
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
    FSUSER_CloseArchive(shared);

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

    bool success = dobackup ? backupSharedIconCache(false) : true;

    if (success || promptConfirm("Update Shared Icon Cache", "Couldn't backup icon data. Continue anyway?")) {
        for (u64 i = 0; i < ENTRY_SHARED_COUNT; i++) {
            for (u64 pos = 0; pos < ((titlecount & 0xFFFFFFFF) + (titlecount >> 32)); pos++) {
                if (((tids[pos] >> 32) & 0x8000)==0 && tids[pos]==entries[i].titleid) {
                    memcpy(icons[i].titles, newicons[pos].titles, sizeof(SMDH_META)*0x10);
                    memcpy(icons[i].smallIcon, newicons[pos].smallIcon, 0x480);
                    memcpy(icons[i].largeIcon, newicons[pos].largeIcon, 0x1200);
                    printf("\x1b[22;0HReplacing entry %#018llx...", entries[i].titleid);
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
    delete[] tids;

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
        FSUSER_CloseArchive(shared);
    }

    printf("Press any key to continue.\n");
    waitKey();
}

void clearHomemenuIconCache(bool wait = true) {
    Result res;
    u32 homemenuID[] = {0x00000082, 0x0000008f, 0x00000098, 0x00000098, 0x000000a1, 0x000000a9, 0x000000b1};
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
    u32 homemenuID[] = {0x00000082, 0x0000008f, 0x00000098, 0x00000098, 0x000000a1, 0x000000a9, 0x000000b1};
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
    FSUSER_CloseArchive(hmextdata);

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
    u32 homemenuID[] = {0x00000082, 0x0000008f, 0x00000098, 0x00000098, 0x000000a1, 0x000000a9, 0x000000b1};
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

    bool success = dobackup ? backupHomemenuIconCache(false) : true;

    if (success || promptConfirm("Update HOME Menu Icon Cache", "Couldn't backup icon data. Continue anyway?")) {
        for (u64 i = 0; i < ENTRY_HOMEMENU_COUNT; i++) {
            for (u64 pos = 0; pos < ((titlecount & 0xFFFFFFFF) + (titlecount >> 32)); pos++) {
                if (((tids[pos] >> 32) & 0x8000)==0 && tids[pos]==entries[i].titleid) {
                    memcpy(&icons[i], &newicons[pos], sizeof(SMDH_HOMEMENU));
                    printf("\x1b[22;0HReplacing entry %#018llx...", entries[i].titleid);
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
    delete[] tids;

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
    u32 homemenuID[] = {0x00000082, 0x0000008f, 0x00000098, 0x00000098, 0x000000a1, 0x000000a9, 0x000000b1};
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
        FSUSER_CloseArchive(hmextdata);
    }

    printf("Press any key to continue.\n");
    waitKey();
}

void unpackRepackHomemenuSoftware(bool repack) {
    Result res;
    Handle save;
    u32 homemenuID[] = {0x00000082, 0x0000008f, 0x00000098, 0x00000098, 0x000000a1, 0x000000a9, 0x000000b1};
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
    FSUSER_ControlArchive(hmextdata, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
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

    FSUSER_ControlArchive(shared, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    FSUSER_CloseArchive(shared);

    if (wait) {
        printf("Press any key to continue.\n");
        waitKey();
    }
}

void resetFolderCount() {
    Result res;
    Handle save;

    u32 homemenuID[] = {0x00020082, 0x0002008f, 0x00020098, 0x00020098, 0x000200a1, 0x000200a9, 0x000200b1};
    FS_Archive syssave = openSystemSavedata(homemenuID);

    res = FSUSER_OpenFile(&save, syssave, (FS_Path)fsMakePath(PATH_ASCII, "/Launcher.dat"), FS_OPEN_WRITE, 0);
    if (R_FAILED(res)) promptError("Reset Folder Count", "Failed to open HOME Menu savedata.");
    u8 count = 1;
    res = FSFILE_Write(save, NULL, 0xD80, &count, sizeof(u8), 0);
    res = FSFILE_Write(save, NULL, 0xD85, &count, sizeof(u8), 0);
    printf("Reseting folder count to 1... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSFILE_Close(save);
    FSUSER_ControlArchive(syssave, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    FSUSER_CloseArchive(syssave);

    printf("Press any key to continue.\n");
    waitKey();
}

void clearGameNotes() {
    Result res;
    char path[16];

    u32 gamenotesID[] = {0x00020087, 0x00020093, 0x0002009c, 0x0002009c, 0x000200a5, 0x000200ad, 0x000200b5};
    FS_Archive syssave = openSystemSavedata(gamenotesID);

    for (int i = 0; i < 16; i++) {
        snprintf(path, 13, "/memo/memo%02u", i);
        res = FSUSER_DeleteFile(syssave, (FS_Path)fsMakePath(PATH_ASCII, path));
        printf("Deleting file \"%s\" %s %#lx.\n", path, R_FAILED(res) ? "ERROR" : "OK", res);
    }

    res = FSUSER_DeleteDirectory(syssave, (FS_Path)fsMakePath(PATH_ASCII, "/memo/"));
    if (R_FAILED(res)) promptError("Clear Game Notes", "Failed to delete game notes.");
    printf("Deleting directory \"memo\"... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    FSUSER_ControlArchive(syssave, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    FSUSER_CloseArchive(syssave);

    printf("Press any key to continue.\n");
    waitKey();
}

void resetEShopBGM() {
    Result res;
    u32 eshopID[] = {0x00000209, 0x00000219, 0x00000229, 0x00000229, 0x00000269, 0x00000279, 0x00000289};
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
    u32 eshopID[] = {0x00000209, 0x00000219, 0x00000229, 0x00000229, 0x00000269, 0x00000279, 0x00000289};
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

void changeAcceptedEULAVersion() {
    Result res;
    u8 eulaData[4];
    u8 index = 0;
    res = CFGU_GetConfigInfoBlk2(4, 0xD0000, eulaData);

    printf("Fetching EULA data... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
    if (R_FAILED(res)) {
        promptError("Change Accepted EULA Version", "Failed to get EULA data.");
        return;
    }
    consoleClear();

    while(aptMainLoop()) {
        printf("\x1b[0;0HPress LEFT, RIGHT or SELECT to select byte.\n");
        printf("Press UP or DOWN to modify selected byte.\n");
        printf("Press X for 0x0000 and Y for 0xFFFF.\n");
        printf("Press A or B to save and return.\n\n");
        printf("Current accepted EULA version: \x1b[%sm%02X\x1b[0m.\x1b[%sm%02X\x1b[0m\n", (index==1) ? "30;47" : "0", eulaData[1], (index==0) ? "30;47" : "0", eulaData[0]);

        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_LEFT || kDown & KEY_RIGHT || kDown & KEY_SELECT) index ^= 1;
        if (kDown & KEY_UP) eulaData[index]--;
        if (kDown & KEY_DOWN) eulaData[index]++;

        if (kDown & KEY_Y) *((u16*)eulaData) = 0xFFFF;
        if (kDown & KEY_X) *((u16*)eulaData) = 0x0000;

        if ((kDown & KEY_A || kDown & KEY_B) && promptConfirm("Change Accepted EULA Version", "Exit now?")) {
            if (promptConfirm("Change Accepted EULA Version", "Save Changes?")) {
                res = CFG_SetConfigInfoBlk8(4, 0xD0000, eulaData);
                printf("Setting new EULA version... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
                res = CFG_UpdateConfigNANDSavegame();
                printf("Updating Config savegame... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);
            } break;
        }
    }

    printf("Press any key to continue.\n");
    waitKey();
}

void toggleNSMenu() {
    Result res;
    u8 region = 0;

    res = CFGU_SecureInfoGetRegion(&region);
    printf("Retrieving console's region... %s %#lx.\n", R_FAILED(res) ? "ERROR" : "OK", res);

    u8 titleID[0x8];
    u8 homemenuRegion[] = {0x82, 0x8f, 0x98, 0x98, 0xa1, 0xa9, 0xb1};
    u8 homemenuID[] = {0x02, homemenuRegion[region], 0x00, 0x00, 0x30, 0x00, 0x04, 0x00};
    u8 testmenuID[] = {0x02, 0x81, 0x00, 0x00, 0x30, 0x00, 0x04, 0x00};

    CFG_GetConfigInfoBlk4(0x8, 0x00110001, titleID);
    bool isTestMenu = !memcmp(titleID, testmenuID, 0x8);

    if (isTestMenu) CFG_SetConfigInfoBlk8(0x8, 0x00110001, homemenuID);
    else CFG_SetConfigInfoBlk8(0x8, 0x00110001, testmenuID);
    CFG_UpdateConfigNANDSavegame();

    printf("Switched to %s.\n", isTestMenu ? "HOME Menu" : "Test Menu");
    printf("Press START to reboot.\nAny other key to continue.\n");
    if (waitKey() & KEY_START) APT_HardwareResetAsync();
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
    // if (R_FAILED(srvGetServiceHandle(&amHandle, "am:net"))) promptError("Application Manager Service", "Failed to get am:net service handle.");
    amInit();
    cfguInit();
    fsInit();

    mkdir("/3ds/data", 0777);
    mkdir("/3ds/data/cthulhu", 0777);
    char oldpath[] = "/3ds/cachetool";
    if (pathExists(oldpath)) moveDataFolder();

    u8 menucount[SUBMENU_COUNT] = {6, 4, 3, 4, 4, 5, 5};
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
            "Edit software library."
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
            "Reset folder count.",
            "Unwrap all HOME Menu software.",
            "Repack all HOME Menu software.",
            "Remove software update nag."
            // "Remove system update nag."
        },
        {
            "Clear Game Notes.",
            "Reset eShop BGM.",
            "Replace eShop BGM.",
            "Change accepted EULA version.",
            "Toggle HOME/Test Menu"
        }
    };

    u8 option[SUBMENU_COUNT] = {0};
    u8 submenu = 0;

    while (aptMainLoop()) {
        printf("\x1b[0;0H\x1b[30;47m%-50s", " ");
        printf("\x1b[0;18HCthulhu v%01u.%01u.%01u\x1b[0;0m", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

        for (u8 i = 0; i<=menucount[submenu]; i++) {
            if (i < menucount[submenu]) printf("\x1b[%u;2H%-48s", i+1, menuentries[submenu][i]);
            else if (submenu > 0) printf("\x1b[%u;2H%-48s", i+1, "Go back.");
        } printf("\x1b[%u;0H>", option[submenu] + 1);

        printf("\x1b[27;0HPress START to reboot the 3DS.");
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
                case 13: consoleClear(); editSoftwareLibrary(); break;

                case 30: if (promptConfirm("Clear Shared Icon Cache", "This will also clear your software library. Are you sure?")) clearSharedIconCache(); break;
                case 31: if (promptConfirm("Update Shared Icon Cache", "Update shared cached icon data?")) updateSharedIconCache(); break;
                case 32: if (promptConfirm("Backup Shared Icon Cache", "Backup shared cached icon data?")) backupSharedIconCache(); break;
                case 33: if (promptConfirm("Restore Shared Icon Cache", "Restore cached icon data from backup?")) restoreSharedIconCache(); break;

                case 40: if (promptConfirm("Clear HOME Menu Icon Cache", "Delete cached icon data? The system will reboot afterwards.")) clearHomemenuIconCache(); break;
                case 41: if (promptConfirm("Update HOME Menu Icon Cache", "Update HOME Menu cached icon data?")) updateHomemenuIconCache(); break;
                case 42: if (promptConfirm("Backup HOME Menu Icon Cache", "Backup HOME Menu cached icon data?")) backupHomemenuIconCache(); break;
                case 43: if (promptConfirm("Restore HOME Menu Icon Cache", "Restore cached icon data from backup?")) restoreHomemenuIconCache(); break;

                case 50: if (promptConfirm("Reset Demo Play Count", "Reset play count of all installed demos?")) resetDemoPlayCount(); break;
                case 51: if (promptConfirm("Reset Folder Count", "Reset folder count back to 1?")) resetFolderCount(); break;
                case 52: if (promptConfirm("Unwrap All HOME Menu Software", "Unwrap all gift-wrapped software on HOME Menu?")) unpackRepackHomemenuSoftware(false); break;
                case 53: if (promptConfirm("Repack All HOME Menu Software", "Gift-wrap all software on HOME Menu?")) unpackRepackHomemenuSoftware(true); break;
                case 54: if (promptConfirm("Remove Software Update Nag", "Remove update nag of all installed software?")) removeSoftwareUpdateNag(); break;
                // case 55: if (promptConfirm("Remove System Update Nag", "Delete automatic system update data?")) removeSystemUpdateNag(); break;

                case 60: if (promptConfirm("Clear Game Notes", "Delete all of your game notes?")) clearGameNotes(); break;
                case 61: if (promptConfirm("Reset eShop BGM", "Restore the original Nintendo eShop music?")) resetEShopBGM(); break;
                case 62: if (promptConfirm("Replace eShop BGM", "Replace the current Nintendo eShop music?")) replaceEShopBGM(); break;
                case 63: consoleClear(); changeAcceptedEULAVersion(); break;
                case 64: consoleClear(); toggleNSMenu(); break;

                default: consoleClear(); submenu = 0; break;
            }
        } else if ((kDown & KEY_B) && (submenu > 0)) {
            submenu = 0;
            consoleClear();
        }

        if (kDown & KEY_SELECT) dobackup ^= true;

        if (kDown & KEY_START) {
            if (envIsHomebrew()) break;
            else APT_HardwareResetAsync();
        }

        gfxEndFrame();
    }

    fsExit();
    cfguExit();
    amExit();
    svcCloseHandle(ptmSysmHandle);
    // svcCloseHandle(amHandle);
    gfxExit();

    return 0;
}
