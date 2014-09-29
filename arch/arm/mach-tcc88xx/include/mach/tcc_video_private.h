
#ifndef VIDEO_PRIVATE_H_
#define VIDEO_PRIVATE_H_

typedef struct TCC_PLATFORM_PRIVATE_PMEM_INFO
{
	unsigned int width;
	unsigned int height;
	unsigned int format;
	unsigned int offset[3];
	unsigned int optional_info[13];
	unsigned char name[6];
	unsigned int unique_addr;
	unsigned int copied; //to gralloc buffer
} TCC_PLATFORM_PRIVATE_PMEM_INFO;
#endif
