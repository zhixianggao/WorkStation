#ifndef __APP_AUDIO_H__
#define __APP_AUDIO_H__

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define AUDIO_SAMPLE_FILE "/data/PUB/AudioSample.json"
#define MAX_TTS_PARAMS_LENGTH 64

/*----------------------------------------------*
 | 结构体定义                                   |
 *----------------------------------------------*/
typedef struct VoiceFile
{
	int cycle;
	int delay;
	int voiceUrlLen;
	void *next;
	char voiceUrl[];
} T_VoiceFile, *PT_VoiceFile;

typedef struct VoiceFileHead
{
	pthread_mutex_t lock;
	T_VoiceFile *next;
} T_VoiceFileHead;


/*----------------------------------------------*
 | 可供外部使用函数                              |
 *----------------------------------------------*/
void audio_file_handler(void);
void NewVoiceFile(int cycle, int delay, int voiceUrlLen, const char *voiceUrl);
void KeepAudioTotalsize(const char *filename);

#endif /* __APP_AUDIO_H__ */