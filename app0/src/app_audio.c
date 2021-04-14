#include "project_includer.h"


/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define AUDIO_FILE_PATH "/tmp/audiofile/"

/*----------------------------------------------*
 | 静态变量                                     |
 *----------------------------------------------*/
static int gAudioTotalSize = 0, gCurAudioFileNum = 0;
static T_VoiceFileHead VoiceFileListHead;
static pthread_mutex_t g_audio_file_lock = PTHREAD_MUTEX_INITIALIZER;

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
static int VoiceFileListAdd(int cycle, int delay, int voiceUrlLen, const char *voiceUrl)
{
	T_VoiceFile *newNode = NULL, *tmpNode = NULL;
	newNode = malloc(sizeof(T_VoiceFile) + voiceUrlLen);
	if (newNode == NULL) {
		LogDbg("newNode malloc failed, maybe no enough memory"); return -1;
	}
	memset(newNode, 0, sizeof(T_VoiceFile));
	newNode->cycle = cycle;
	newNode->delay = delay;
	newNode->voiceUrlLen = voiceUrlLen;
	strncpy(newNode->voiceUrl, voiceUrl, newNode->voiceUrlLen);
	newNode->next = NULL;
	pthread_mutex_lock(&(VoiceFileListHead.lock));
	tmpNode = (T_VoiceFile *)VoiceFileListHead.next;
	if (tmpNode == NULL)
		VoiceFileListHead.next = newNode;
	else {
		while (tmpNode->next)
			tmpNode = tmpNode->next;
		tmpNode->next = newNode;
	}
	pthread_mutex_unlock(&(VoiceFileListHead.lock));
	return 0;
}

static T_VoiceFile *VoiceFileListGet(void)
{
	T_VoiceFile *node = NULL;
	pthread_mutex_lock(&(VoiceFileListHead.lock));
	node = VoiceFileListHead.next;
	if (VoiceFileListHead.next)
		VoiceFileListHead.next = VoiceFileListHead.next->next;
	pthread_mutex_unlock(&(VoiceFileListHead.lock));
	return node;
}

static void *VoiceFileHandlerThread(void *args)
{
#define MAX_AUDIO_FILE_NUM 2
	LogDbg("init");

	mkdir(AUDIO_FILE_PATH, 0777);
	memset(&VoiceFileListHead,0,sizeof(VoiceFileListHead));
	pthread_mutex_init(&(VoiceFileListHead.lock), NULL);

	T_VoiceFile *getNode = NULL;

	while (1)
	{
		if (gCurAudioFileNum>=MAX_AUDIO_FILE_NUM) {
			SysDelay(100);
			continue;
		}
		getNode = VoiceFileListGet();
		if (!getNode) {
			SysDelay(100);
			continue;
		}
		if (DldCertainAudio(getNode) == 0) {
			gCurAudioFileNum++;
		}
		free(getNode);
	}
	pthread_exit(0);
#undef MAX_AUDIO_FILE_NUM
}

static int _nameFilter(const struct dirent *d)
{
	char *p = strrchr(d->d_name, '.');
	if (p && (!strcmp(p, ".mp3") || !strcmp(p, ".wav")))
		return 1;
	return 0;
}

static void unlinkOldestAudio(const char *filename)
{
	int n;
	char oldestFilename[128] = {};
	struct dirent **namelist;
	n = scandir(AUDIO_FILE_PATH, &namelist, _nameFilter, alphasort);
	if (n < 0) {
		LogError("no mp3 or wav file in "AUDIO_FILE_PATH"");
		return;
	} else {
		snprintf(oldestFilename, sizeof(oldestFilename), ""AUDIO_FILE_PATH"%s", namelist[0]->d_name);
		struct stat st = {};
		if (strcmp(filename, oldestFilename) && (stat(oldestFilename, &st) == 0))
		{
			gAudioTotalSize -= st.st_size;
			gCurAudioFileNum--;
			unlink(oldestFilename);
			LogDbg("remove %s, size=%d, leftsize=%d", oldestFilename, st.st_size, gAudioTotalSize);
		}
		while (n--)
			free(namelist[n]);
		if (namelist)
			free(namelist);
	}
}

static void *AudioTotalsizeKeeper(void *args)
{
#define MAX_AUDIO_FILE_SIZE (2 * 1024 * 1024)
	char *filename = (char *)args;
	char *p = strrchr(filename, '.');
	if (p && (!strcmp(p, ".mp3") || !strcmp(p, ".wav")))
	{
		pthread_mutex_lock(&g_audio_file_lock);
		if (strstr(filename, AUDIO_FILE_PATH))
		{
			struct stat st = {};
			if (stat(filename, &st) == 0) gAudioTotalSize += st.st_size;

			while (gAudioTotalSize > MAX_AUDIO_FILE_SIZE) {
				LogDbg("AudioTotalSize=%d more than MAX_AUDIO_FILE_SIZE=%d", gAudioTotalSize, MAX_AUDIO_FILE_SIZE);
				unlinkOldestAudio(filename);
			}
			while (access(filename, F_OK) == 0)
				SysDelay(100);

			gAudioTotalSize -= st.st_size;
			gCurAudioFileNum--;
		}
		pthread_mutex_unlock(&g_audio_file_lock);
	}
	return NULL;
#undef MAX_AUDIO_FILE_SIZE
}

void NewVoiceFile(int cycle, int delay, int voiceUrlLen, const char *voiceUrl)
{
	VoiceFileListAdd(cycle, delay, voiceUrlLen, voiceUrl);
}

void KeepAudioTotalsize(const char *filename)
{
	pthread_t tid;
	if (pthread_create(&tid, NULL, AudioTotalsizeKeeper, (void *)filename))
	{
		LogDbg("VoiceFileHandlerThread create fail");
		return;
	}
	pthread_detach(tid);
}

void audio_file_handler(void)
{
	pthread_t tid;
	if (pthread_create(&tid, NULL, VoiceFileHandlerThread, NULL)) {
		LogDbg("VoiceFileHandlerThread create fail");
		return;
	}
	pthread_detach(tid);
}
