#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ont/mqtt.h>
#include <ont/log.h>
#include <ont/video.h>
#include <ont/video_rvod.h>
#include <ont/video_cmd.h>
#include "device_onvif.h"
#include "sample_config.h"
#include "status_sync.h"

#ifdef ONT_OS_POSIX
#include <signal.h>
#endif

#ifdef WIN32
#include "windows.h"
#include "Dbghelp.h"
#include "tchar.h"
#include "assert.h"
#pragma comment(lib, "Dbghelp.lib")
#endif

static void sample_log(void *ctx, ont_log_level_t ll , const char *format, ...)
{
    static const char *level[] = {
        "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
    };
    va_list ap;
	time_t timer;
	struct tm *timeinfo;
    char buffer [64];
	timer = time(NULL);
	timeinfo = localtime(&timer);
    strftime (buffer,sizeof(buffer),"%Y/%m/%d %H:%M:%S",timeinfo);

	printf("[%s] [%s]", buffer, level[ll]);

    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    printf("\n");
}


int ont_video_live_stream_start(void *dev, int channel, const char *push_url, const char* deviceid);
int ont_videocmd_parse(void *dev, const char*str, const char*uuid);
int ont_video_playlist_singlestep(void *dev);


int ont_add_onvif_devices(ont_device_t *dev)
{
	int i;
	int j = cfg_get_channel_num();
	struct _onvif_channel * channels = cfg_get_channels();
	for (i = 0; i < j; i++)
	{
		if (ont_onvifdevice_adddevice(	cfg_get_cluster(),
										channels[i].channelid, channels[i].url, channels[i].user, channels[i].pass) != 0)
		{
			ONT_LOG0(ONTLL_ERROR, "Failed to login the onvif server");
		} else {
            uint8_t devid[32];
            ont_platform_snprintf((char *)devid, 32, "%d", dev->device_id);
            ont_video_live_stream_start(dev, channels[i].channelid, NULL, (const char *)devid);
        }
	}
	return j;
}


#ifdef WIN32
void CreateDumpFile(LPCWSTR lpstrDumpFilePathName, EXCEPTION_POINTERS *pException)
{

	TCHAR szFull[260];
	TCHAR szDrive[3];
	TCHAR szDir[256];
	GetModuleFileName(NULL, szFull, sizeof(szFull) / sizeof(char));
	_tsplitpath(szFull, szDrive, szDir, NULL, NULL);
	strcpy(szFull, szDrive);
	strcat(szFull, szDir);
	strcat(szFull, "//");
	strcat(szFull, lpstrDumpFilePathName);

	// 创建Dump文件
	//
	HANDLE hDumpFile = CreateFile(szFull, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	// Dump信息
	//
	MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
	dumpInfo.ExceptionPointers = pException;
	dumpInfo.ThreadId = GetCurrentThreadId();
	dumpInfo.ClientPointers = TRUE;

	// 写入Dump文件内容
	//
	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, NULL, NULL);

	CloseHandle(hDumpFile);
}

LONG ApplicationCrashHandler(EXCEPTION_POINTERS *pException)
{
	//FatalAppExit(-1, "*** Unhandled Exception! ***");

	CreateDumpFile("video_sample.dmp", pException);

	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int ont_video_dev_set_channels(void *_dev, int channels);

int ont_video_live_stream_start(void *dev, int channel, const char *push_url, const char* deviceid);
int ont_video_live_stream_play(void *dev, int channel, const char *push_url, const char* deviceid);
int ont_video_live_stream_ctrl(void *dev, int channel, int stream);
int ont_video_vod_stream_start(void *dev, t_ont_video_file *fileinfo, const char *playflag, const char *push_url);
int ont_video_stream_make_keyframe(void *dev, int channel);
int ont_video_dev_ptz_ctrl(void *dev, int channel, int mode, t_ont_video_ptz_cmd cmd, int speed);

extern int ont_video_dev_query_files(void *dev, int channel, int startindex, int max, t_ont_video_file **files, int *filenum, int *totalcount);
extern int ont_video_dev_channel_sync(void *dev);

t_ont_dev_video_callbacks _gcallback ={
	ont_video_live_stream_ctrl,
	ont_video_live_stream_play,
	ont_video_stream_make_keyframe,
	ont_video_dev_ptz_ctrl,
	ont_video_vod_stream_start,
	ont_video_dev_query_files 
};


int main( int argc, char *argv[] )
{

#ifdef WIN32
	SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
#endif

	//ont_onvif_device_discovery();
    ont_device_t *dev=NULL;
	int err;
	int delta = 0;

	ont_device_cmd_t *cmd = NULL;

    ont_log_set_logger(NULL, sample_log);
	ont_log_set_log_level(ONTLL_DEBUG);

#ifdef WIN32
	//cfg_initilize("E:\\share\\video_proj\\video_sdk\\bin\\debug\\config.json");
	cfg_initilize("config.json");
#else
	cfg_initilize("config.json");
#endif
	//cfg_initilize("config.json");
	int product_id = cfg_get_profile()->productid;

#ifdef ONT_OS_POSIX
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;//设定接受到指定信号后的动作为忽略
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) == -1 || //初始化信号集为空
	    sigaction(SIGPIPE, &sa, 0) == -1)
    {
        //屏蔽SIGPIPE信号
	    exit(1);
	}
#endif

    err = ont_device_create(product_id, ONTDEV_MQTT, "videotest2", &dev);
    if (ONT_ERR_OK != err) {
        ONT_LOG1(ONTLL_ERROR, "Failed to create device instance, error=%d", err);
        return -1;
    }
	err = ont_device_connect(dev,
							 ONT_SERVER_ADDRESS,
							 ONT_SERVER_PORT,
							 &cfg_get_profile()->key[0],
							 "videotest2",
                             120);
    if (ONT_ERR_OK != err)
    {
        ont_device_destroy(dev);

        ONT_LOG1(ONTLL_ERROR, "Failed to connect to the server, error=%d", err);
        return -1;
    }
	ONT_LOG1(ONTLL_INFO, "get dev id %d", dev->device_id);


	ont_add_onvif_devices(dev);


	/*add channel*/
	ont_video_dev_channel_sync(dev);
    while (1)
    {
		if (delta)
		{
			ont_platform_sleep(delta>100?100:delta);
		}
		err = ont_device_keepalive(dev);
        if (ONT_ERR_OK != err)
        {
            ONT_LOG1(ONTLL_ERROR, "device is not alive, error=%d", err);
            break;
        }

		/*
			GET PLATFORM CMD
		*/
		cmd = ont_device_get_cmd(dev);
		if(NULL != cmd)
		{
			ont_videocmd_handle(dev, cmd, &_gcallback);
            ont_device_cmd_destroy(cmd);
		}
		delta = ont_video_playlist_singlestep(dev);
	}

    ont_device_destroy(dev);
    return 0;
}
