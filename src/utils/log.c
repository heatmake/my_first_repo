#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "stdarg.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <stdlib.h>

/******************************************************/
// 头文件内容
#define DEFAULT_ROLLBACK_TIME_MIN (60)
typedef struct {
    va_list ap;
    const char* fmt;
    const char* file;
    struct tm* time;
    void* udata;
    int line;
    int level;
} log_Event;

typedef uint32_t (*log_Hook)(log_Event* ev);
typedef void (*log_LockHook)(bool lock, void* udata);

typedef enum {
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_type;

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define LOG_TRACE(...) log_print(LOG_TRACE, __FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) log_print(LOG_DEBUG, __FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_print(LOG_INFO, __FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_print(LOG_WARN, __FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_print(LOG_ERROR, __FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) log_print(LOG_FATAL, __FILENAME__, __LINE__, __VA_ARGS__)
#define MAX_CALLBACKS 1
#define MAX_FILE_SIZE (20 * 1024 * 1024)
#define LOG_FILE_NAME_LEN 260
#define MAX_PREFIX_LEN 32
#define MAX_VERSION_LEN 40
#define MAX_FILE_NAME_LEN   (LOG_FILE_NAME_LEN + MAX_PREFIX_LEN + MAX_VERSION_LEN + 32)
#define DEFAILT_FILE_DISK_SIZE   (100 * 1024 * 1024)
#define DEFAILT_FILE_CYCLE_TIME  (DEFAULT_ROLLBACK_TIME_MIN * 60)   //Unit:Min
#define FILE_NAME_LEN (MAX_PREFIX_LEN + MAX_VERSION_LEN + 32)

/*链表*/
typedef struct List {
    void* data;
    struct List* next;
} ListNode;

/* 时间 */
typedef struct
{
    uint32_t year;
    uint8_t mon;
    uint8_t day;
    uint8_t hour;
    uint8_t minitue;
    uint8_t sec;
    uint16_t msec;
    uint8_t week;
} SMyDateTime;

typedef struct {
    log_Hook fn;
    char fileName[FILE_NAME_LEN];
    void* udata;
    int level;
    uint32_t fileSize;
    uint64_t lastTimeSec;
} Callback;

typedef struct {
    char logName[FILE_NAME_LEN];
    uint64_t startTime;
    uint64_t endTime;
    uint32_t logSize;
}LogRollBackObj;

static struct {
    int level;
    bool quiet;
    char chPrefix[MAX_PREFIX_LEN + 1];
    char chLogDir[LOG_FILE_NAME_LEN + 1];
    char chVersion[MAX_VERSION_LEN];
    Callback callbacks[MAX_CALLBACKS];
    uint32_t fileSize;
    uint32_t fileDiskSize;
    bool enableWriteFile;
    pthread_mutex_t lockMutex;
    bool logRollBackEnable;
    uint32_t fileCycle;
    uint32_t filesSize;
    ListNode *fileRollBackList; /* LogRollBackObj */
} g_globalLogConfig;

static bool sg_bLogInit = false;
static const char* level_strings[] = {"[TRACE]", "[DEBUG]", "[INFO ]", "[WARN ]", "[ERROR]", "[FATAL]"};
static void LogRollBackDeal();
static void LogRollBackRecording(uint64_t startTime, uint64_t endTime, const char *fileName, ListNode **listHead);

static void ToDateTime(uint64_t ltMSec, SMyDateTime* pDateTime, bool toLocal)
{
    struct tm ltm;
    memset(&ltm, 0, sizeof(struct tm));
    time_t ltSec = (time_t)(ltMSec / 1000);
    if (toLocal) {
        localtime_r(&ltSec, &ltm);
    } else {
        gmtime_r(&ltSec, &ltm);
    }

    pDateTime->year = (uint32_t)(ltm.tm_year + 1900);
    pDateTime->mon = (uint8_t)(ltm.tm_mon + 1);
    pDateTime->day = (uint8_t)ltm.tm_mday;
    pDateTime->hour = (uint8_t)ltm.tm_hour;
    pDateTime->minitue = (uint8_t)ltm.tm_min;
    pDateTime->sec = (uint8_t)ltm.tm_sec;
    pDateTime->week = (uint8_t)ltm.tm_wday; /* Sunday is 0, Monday is 1, and so on. */
    pDateTime->msec = (uint16_t)(ltMSec % 1000);
}

static uint64_t GetTimeSec(void)
{
    uint64_t curSec;
    time_t now;
    (void)time(&now);
    curSec = (uint64_t)now;
    return curSec;
}

static uint64_t GetTimeMSec(void)
{
    struct timeval tv;
    uint64_t timems;
    (void)gettimeofday(&tv, NULL);
    timems = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return timems;
}

uint64_t StrTimeToSec(const char* pcTime, bool bLocalTime)
{
    uint64_t timeSec = 0;
    if (pcTime) {
        struct tm ltm;
        memset(&ltm, 0, sizeof(struct tm));
        strptime(pcTime, "%Y%m%d%H%M%S", &ltm);
        time_t tmSec = mktime(&ltm);
        if (tmSec >= 0) {
            timeSec = tmSec;
            if (!bLocalTime) {
                timeSec += ltm.tm_gmtoff;
            }
        }
    }
    return timeSec;
}

static void InsertNodeToHead(ListNode** list, ListNode* node)
{
    ListNode* temp = *list;
    *list = node;
    node->next = temp;
}

static void InsertNodeByOrder(ListNode** list, ListNode* node, int (*compare)(void* ptr1, void* ptr2))
{
    ListNode* walk = *list;
    ListNode* temp = NULL;

    while (walk->next != NULL) {
        if (compare(walk->next->data, node->data) > 0) {
            temp = walk->next;
            walk->next = node;
            node->next = temp;
            break;
        }

        walk = walk->next;
    }

    if (walk->next == NULL) {
        walk->next = node;
    }
}

void ListLinkDestroy(ListNode** list, bool (*callBack)(void* ptr))
{
    if (list != NULL) {
        ListNode* temp = *list;
        ListNode* temp1 = NULL;

        while (temp != NULL) {
            if (NULL != callBack) {
                (void)callBack(temp->data);
            }
            temp1 = temp;
            temp = temp->next;
            free(temp1);
        }

        *list = NULL;
    }
}

ListNode* ListNodeInsert(ListNode** list, void* data, int (*compare)(void* ptr1, void* ptr2))
{
    ListNode* node = NULL;

    if (list != NULL) {
        node = (ListNode*)malloc(sizeof(ListNode));
        if (node != NULL) {
            node->data = data;
            node->next = NULL;

            if (*list == NULL) {
                *list = node;
            } else if ((compare == NULL) || (compare((*list)->data, data) > 0)) {
                InsertNodeToHead(list, node);
            } else {
                InsertNodeByOrder(list, node, compare);
            }
        }
    }

    return node;
}

void ListNodeRemoveHead(ListNode** list, bool (*callBack)(void* ptr))
{
    ListNode* temp = NULL;

    if ((list != NULL) && (*list != NULL)) {
        temp = *list;
        if ((callBack != NULL) && callBack(temp->data)) {
            *list = (*list)->next;
            free(temp);
        }
    }
}

static void stdout_callback(log_Event* ev)
{
    char buf[16];
    buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
    fprintf(ev->udata, "%s %-5s %s:%d: ", buf, level_strings[ev->level], ev->file, ev->line);
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fflush(ev->udata);
}

static uint32_t file_callback(log_Event* ev)
{
    uint32_t len = 0;
    char buf[64] = { 0 };
    buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
    fprintf(ev->udata, "%s %-5s %s:%d: ", buf, level_strings[ev->level], ev->file, ev->line);
    va_list vac;
    va_copy(vac, ev->ap);
    int fmt_len = vsnprintf(NULL, 0, ev->fmt, vac);
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fflush(ev->udata);
    len += fmt_len + strlen(buf);
    return len;
}

static void lock(void)
{
    pthread_mutex_lock(&g_globalLogConfig.lockMutex);
}

static void unlock(void)
{
    pthread_mutex_unlock(&g_globalLogConfig.lockMutex);
}

static int log_add_callback(log_Hook fn, void* udata, int level)
{
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!g_globalLogConfig.callbacks[i].fn) {
            g_globalLogConfig.callbacks[i].fn = fn;
            g_globalLogConfig.callbacks[i].udata = udata;
            g_globalLogConfig.callbacks[i].level = level;
            g_globalLogConfig.callbacks[i].lastTimeSec = 0;
            g_globalLogConfig.callbacks[i].fileSize = 0;
            return 0;
        }
    }
    return -1;
}

void log_init()
{
    if (!sg_bLogInit) {
        g_globalLogConfig.enableWriteFile = true;
        g_globalLogConfig.level = LOG_DEBUG;
        g_globalLogConfig.quiet = false;
        memset(g_globalLogConfig.chPrefix, 0, sizeof(g_globalLogConfig.chPrefix));
        memset(g_globalLogConfig.chLogDir, 0, sizeof(g_globalLogConfig.chLogDir));
        strcpy(g_globalLogConfig.chPrefix, "log");
        pthread_mutex_init(&g_globalLogConfig.lockMutex, NULL);
        g_globalLogConfig.fileSize = MAX_FILE_SIZE;
        g_globalLogConfig.fileDiskSize = DEFAILT_FILE_DISK_SIZE;
        memset(g_globalLogConfig.chVersion, 0, sizeof(g_globalLogConfig.chVersion));
        strcpy(g_globalLogConfig.chVersion, "V1.0.0-S001");
        memset(g_globalLogConfig.callbacks, 0, sizeof(g_globalLogConfig.callbacks));
        log_add_callback(file_callback, NULL, 0);

        /* log rollBack init */
        g_globalLogConfig.logRollBackEnable = false;
        g_globalLogConfig.fileCycle = DEFAILT_FILE_CYCLE_TIME;
        g_globalLogConfig.filesSize = 0;
        g_globalLogConfig.fileRollBackList = NULL;
        sg_bLogInit = true;
    }
}

void log_deinit()
{
    if (sg_bLogInit) {
        lock();
        for (int i = 0; i < MAX_CALLBACKS; ++i) {
            void* udata = g_globalLogConfig.callbacks[i].udata;
            if (NULL != udata) {
                fclose((FILE*)udata);
            }
        }
        unlock();
        pthread_mutex_destroy(&g_globalLogConfig.lockMutex);
        sg_bLogInit = false;
    }
}

const char* log_level_string(int level)
{
    return level_strings[level];
}

void log_enable_write_file(bool bEnable)
{
    g_globalLogConfig.enableWriteFile = bEnable;
}

void log_set_level(int level)
{
    g_globalLogConfig.level = level;
}

void log_set_file_size(uint32_t fileSize)
{
    g_globalLogConfig.fileSize = fileSize;
}

void log_set_file_disk_size(uint32_t diskSize)
{
    g_globalLogConfig.fileDiskSize = diskSize;
}

void log_set_fw_version(const  char * version)
{
    snprintf(g_globalLogConfig.chVersion, MAX_VERSION_LEN, "%s", version);
}

void log_set_rollback_cycle(uint32_t timeMin)
{
    if (timeMin != 0) {
        g_globalLogConfig.fileCycle = timeMin * 60;
    }
}

void log_set_quiet(bool enable)
{
    g_globalLogConfig.quiet = enable;
}

void log_set_file_prefix(const char* pchPrefix)
{
    if (NULL != pchPrefix)
    {
        memset(g_globalLogConfig.chPrefix, 0, sizeof(g_globalLogConfig.chPrefix));
        int len = (int)strlen(pchPrefix);
        if (len > MAX_PREFIX_LEN)
        {
            len = MAX_PREFIX_LEN;
        }

        if (len > 0)
        {
            strncpy(g_globalLogConfig.chPrefix, pchPrefix, len);
        }
    }
}

void log_set_file_dir(const char* pchFileDir)
{
    if (NULL != pchFileDir)
    {
        memset(g_globalLogConfig.chLogDir, 0, sizeof(g_globalLogConfig.chLogDir));
        int len = (int)strlen(pchFileDir);
        if (len > LOG_FILE_NAME_LEN)
        {
            len = LOG_FILE_NAME_LEN;
        }

        if (len > 0)
        {
            strncpy(g_globalLogConfig.chLogDir, pchFileDir, len);
        }
    }
}

int log_rm_callback(void* udata)
{
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (g_globalLogConfig.callbacks[i].udata == udata) {
            g_globalLogConfig.callbacks[i].fn = NULL;
            g_globalLogConfig.callbacks[i].udata = NULL;
            return 0;
        }
    }
    return -1;
}

static void init_event(log_Event* ev, void* udata)
{
    if (!ev->time) {
        time_t t = time(NULL);
        ev->time = localtime(&t);
    }
    ev->udata = udata;
}

static FILE* OpenLogFile(uint64_t nowSec, char* fileName)
{
    SMyDateTime dt;
    SMyDateTime dtEnd;
    uint64_t timeSecEnd = nowSec + g_globalLogConfig.fileCycle;
    ToDateTime(nowSec * 1000, &dt, true);
    ToDateTime(timeSecEnd * 1000, &dtEnd, true);
    char chLogFileName[MAX_FILE_NAME_LEN] = { 0 };
    snprintf(fileName, FILE_NAME_LEN, "%s%s_%d%02d%02d%02d%02d%02d_%d%02d%02d%02d%02d%02d.log",
             g_globalLogConfig.chPrefix,
             g_globalLogConfig.chVersion,
             dt.year, dt.mon, dt.day, dt.hour, dt.minitue, dt.sec,
             dtEnd.year, dtEnd.mon, dtEnd.day, dtEnd.hour, dtEnd.minitue, dtEnd.sec);
    snprintf(chLogFileName, sizeof(chLogFileName) - 1, "%s%s", g_globalLogConfig.chLogDir, fileName);
    LogRollBackDeal();
    return fopen(chLogFileName, "wb");
}

static void SwitchNewLogFile(Callback* cb)
{
    uint64_t ltNowSec = GetTimeSec();
    uint64_t secEnd = cb->lastTimeSec + g_globalLogConfig.fileCycle;
    if (ltNowSec > cb->lastTimeSec && ((cb->fileSize >= g_globalLogConfig.fileSize) || ltNowSec > secEnd)) {
        char fileName[FILE_NAME_LEN] = {0};
        FILE* pf = OpenLogFile(ltNowSec, fileName);
        if (pf)
        {
            fclose((FILE*)(cb->udata));
            LogRollBackRecording(cb->lastTimeSec, secEnd, cb->fileName, &g_globalLogConfig.fileRollBackList);
            cb->fileSize = 0;
            cb->udata = pf;
            strcpy(cb->fileName, fileName);
        }
        cb->lastTimeSec = ltNowSec;
    }
}

void log_print(int level, const char* file, int line, const char* fmt, ...)
{
    if (level < g_globalLogConfig.level) {
        return;
    }

    log_Event ev = {
        .fmt = fmt,
        .file = file,
        .line = line,
        .level = level,
    };

    lock();

    if (!g_globalLogConfig.quiet) {
        init_event(&ev, stderr);
        va_start(ev.ap, fmt);
        stdout_callback(&ev);
        va_end(ev.ap);
    }

    for (int i = 0; i < MAX_CALLBACKS && g_globalLogConfig.callbacks[i].fn; i++)
    {
        Callback* cb = &g_globalLogConfig.callbacks[i];
        if (g_globalLogConfig.enableWriteFile)
        {
            if (!cb->udata)
            {
                cb->lastTimeSec = GetTimeSec();
                cb->udata = OpenLogFile(cb->lastTimeSec, cb->fileName);
            }

            if (cb->udata)
            {
                init_event(&ev, cb->udata);
                va_start(ev.ap, fmt);
                unsigned int len = cb->fn(&ev);
                va_end(ev.ap);
                cb->fileSize += len;

                SwitchNewLogFile(cb);
            }
        }
    }

    unlock();
}

static uint32_t GetlogRollBackSize(const char *filePath)
{
    struct stat buf;
    stat(filePath, &buf);
    return (uint32_t)buf.st_size;
}

static int LogRollBackCompare(void* ptr1, void* ptr2)
{
    int ret = 0;
    LogRollBackObj *fileObj1 = ptr1;//original list node
    LogRollBackObj *fileObj2 = ptr2;
    if (fileObj1->startTime > fileObj2->startTime) {
        ret = 1;
    } else if (fileObj1->startTime == fileObj2->startTime) {
        if (fileObj1->endTime > fileObj2->endTime) {
            ret = 1;
        }
    } 
    return ret;
}

static void LogRollBackRecording(uint64_t startTime, uint64_t endTime, const char *fileName, ListNode **listHead)
{
    LogRollBackObj *fileObj = NULL;
    if (g_globalLogConfig.logRollBackEnable == false) {
        return;
    }
    fileObj = (LogRollBackObj *)malloc(sizeof(LogRollBackObj));
    if (fileObj) {
        char filePath[LOG_FILE_NAME_LEN + FILE_NAME_LEN] = {0};
        strcpy(fileObj->logName, fileName);
        fileObj->startTime = startTime;
        fileObj->endTime = endTime;
        snprintf(filePath, LOG_FILE_NAME_LEN + FILE_NAME_LEN, "%s%s", g_globalLogConfig.chLogDir, fileName);
        fileObj->logSize = GetlogRollBackSize(filePath);
        if (NULL != ListNodeInsert(listHead, fileObj, LogRollBackCompare)) {
           g_globalLogConfig.filesSize += fileObj->logSize;
        } else {
            free(fileObj);
        }
    }
}

static bool GetLogTime(char *logfileName, uint64_t* time)
{
    bool ret = false;
    uint32_t j = 0;
    char timeStr[16] = {0};
    uint32_t len  = strlen(logfileName);
    for (j = 1; j <= len; j++) {
        if (logfileName[len - j] == '_') {
            snprintf(timeStr, 16, "%s", &logfileName[len - j + 1]);
            logfileName[len - j] = '\0';
            break;
        }
    }
    if (j < len) {
        if (strspn(timeStr, "0123456789")== strlen(timeStr)) {
            *time = StrTimeToSec(timeStr, true);
            ret = true;
        }
    }
    return ret;
}
static void GetlogRollBackStartEndTime(const char *fileName, uint64_t* start, uint64_t* end)
{
    uint32_t flag = 0;
    uint32_t fileLen = 0;
    char logfileName[FILE_NAME_LEN] = {0};
    snprintf(logfileName, FILE_NAME_LEN, "%s", fileName);
    fileLen = strlen(logfileName);
    for (uint32_t i = 1; i <= fileLen; i++) {
        if (logfileName[fileLen - i] == '.') {
            logfileName[fileLen - i] = '\0';
            flag = 1;
            break;
        }
    }
    /* Get start and end time */
    if (1 == flag) {
        bool ret = GetLogTime(logfileName, start);
        if (true == ret) {
            GetLogTime(logfileName, end);
        }
    }
}

static bool LogRollBackObjDestroy(void* ptr)
{
    bool ret = false;
    LogRollBackObj *fileObj = (LogRollBackObj *)ptr;
    if (NULL != fileObj) {
        free(fileObj);
        fileObj = NULL;
        ret = true;
    }
    return ret;
}

static void LogRollBackDeal()
{
    if (g_globalLogConfig.filesSize >= g_globalLogConfig.fileDiskSize) {
        uint32_t filesSize = 0;
        while(g_globalLogConfig.logRollBackEnable == true) {
            char filePath[LOG_FILE_NAME_LEN + FILE_NAME_LEN] = {0};
            LogRollBackObj *fileObj = g_globalLogConfig.fileRollBackList->data;
            filesSize += fileObj->logSize;
            g_globalLogConfig.filesSize -= fileObj->logSize;
            snprintf(filePath, LOG_FILE_NAME_LEN + FILE_NAME_LEN, "%s%s", g_globalLogConfig.chLogDir, fileObj->logName);
            remove(filePath);
            ListNodeRemoveHead(&g_globalLogConfig.fileRollBackList, LogRollBackObjDestroy);
            if (filesSize >= g_globalLogConfig.fileSize && g_globalLogConfig.filesSize < g_globalLogConfig.fileDiskSize) {
                break;
            }
        }
    }
}

void LogRollBackStart()
{
    uint64_t start = 0;
    uint64_t end = 0;
    char *dir = "./";
    DIR* dirp = NULL;
    struct dirent* file;
    lock();
    if (g_globalLogConfig.chLogDir[0] != '\0') {
        dir = g_globalLogConfig.chLogDir;
    }
    dirp = opendir(dir);
    if (NULL == dirp) {
       unlock();
       return;
    }
    g_globalLogConfig.logRollBackEnable = true;
    while ((file = readdir(dirp)) != NULL) {
       if (file->d_type != DT_REG) {
           continue;
       }
       bool flag = true;
       char* fileName = file->d_name;
        for (int i = 0; i < MAX_CALLBACKS; i++)
        {
            //Filter out the log files being written and enter monitoring when closed
            if (0 == strcmp(g_globalLogConfig.callbacks[i].fileName, fileName)) {
                flag = false;
                break;
            }
        }
        int32_t ret = strncmp(fileName, g_globalLogConfig.chPrefix, strlen(g_globalLogConfig.chPrefix));
        if (flag == true && 0 == ret) {
            GetlogRollBackStartEndTime(fileName, &start, &end);
            LogRollBackRecording(start, end, fileName, &g_globalLogConfig.fileRollBackList);
        }
    }
    LogRollBackDeal();//Starting for the first time and performing a rollback process
    unlock();
}

void LogRollBackStop()
{
    lock();
    g_globalLogConfig.logRollBackEnable = false;
    if (g_globalLogConfig.fileRollBackList != NULL) {
        ListLinkDestroy(&g_globalLogConfig.fileRollBackList, LogRollBackObjDestroy);
    }
    unlock();
}

// Example
int main()
{
    /* 日志初始化 */
    log_init();
    /* 启动日志回滚 */
    LogRollBackStart();

    LOG_DEBUG("debug log\n");
    LOG_INFO("info log\n");
    LOG_WARN("warn log\n");
    LOG_ERROR("error log\n");
    LOG_FATAL("fatal log\n");
    /* 停止日志回滚 */
    LogRollBackStop();
    /* 日志反初始化 */
    log_deinit();
    return 0;
}
