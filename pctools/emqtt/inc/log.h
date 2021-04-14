#ifndef __LOG_H__
#define __LOG_H__

#define _myDebug(fmt, args...) fprintf(stderr, "%s|%d|%s() " fmt "\n", __FILE__, __LINE__, __func__, ##args)
#define LogWarn(fmt, args...) fprintf(stderr, "%s|%d|%s() " fmt "\n", __FILE__, __LINE__, __func__, ##args)
#define LogError(fmt, args...) fprintf(stderr, "%s|%d|%s() " fmt "\n", __FILE__, __LINE__, __func__, ##args)

#endif /* __LOG_H__ */