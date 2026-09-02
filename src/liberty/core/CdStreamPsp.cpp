#include "common.h"
#include "CdStream.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

namespace {
constexpr int kMaxImages = MAX_CDIMAGES;
int imageFiles[kMaxImages] = {-1, -1};
char imageNames[kMaxImages][128]{};
int imageCount = 0;
int lastPosition = 0;
int channelStatus[MAX_CDCHANNELS]{};
}

void CdStreamInitThread(void) {}

void CdStreamInit(int32 numChannels)
{
    for(int i = 0; i < MAX_CDCHANNELS; ++i)
        channelStatus[i] = i < numChannels ? STREAM_NONE : STREAM_ERROR;
}

uint32 GetGTA3ImgSize(void)
{
    if(imageCount == 0 || imageFiles[0] < 0) return 0;
    struct stat info{};
    return fstat(imageFiles[0], &info) == 0 ? uint32(info.st_size) : 0;
}

void CdStreamShutdown(void) { CdStreamRemoveImages(); }

int32 CdStreamRead(int32 channel, void *buffer, uint32 offset, uint32 size)
{
    if(channel < 0 || channel >= MAX_CDCHANNELS || !buffer) return STREAM_ERROR;
    const uint32 image = _GET_INDEX(offset);
    if(image >= uint32(imageCount) || imageFiles[image] < 0) return STREAM_ERROR;
    const off_t byteOffset = off_t(_GET_OFFSET(offset)) * CDSTREAM_SECTOR_SIZE;
    const size_t bytes = size_t(size) * CDSTREAM_SECTOR_SIZE;
    lastPosition = int32(offset + size);
    channelStatus[channel] = STREAM_READING;
    if(lseek(imageFiles[image], byteOffset, SEEK_SET) < 0) {
        channelStatus[channel] = STREAM_ERROR;
        return STREAM_ERROR;
    }
    const ssize_t got = read(imageFiles[image], buffer, bytes);
    if(got != ssize_t(bytes)) {
        channelStatus[channel] = STREAM_ERROR;
        return STREAM_ERROR;
    }

    // This PSP backend performs the read synchronously.  CdStreamRead reports
    // that the request was accepted, while status/sync must report STREAM_NONE
    // immediately afterwards.  Returning STREAM_SUCCESS from GetStatus/Sync
    // makes LoadAllRequestedModels retry the already completed read forever.
    channelStatus[channel] = STREAM_NONE;
    return STREAM_SUCCESS;
}

int32 CdStreamGetStatus(int32 channel)
{
    if(channel < 0 || channel >= MAX_CDCHANNELS) return STREAM_ERROR;
    return channelStatus[channel];
}

int32 CdStreamGetLastPosn(void) { return lastPosition; }
int32 CdStreamSync(int32 channel) { return CdStreamGetStatus(channel); }

void AddToQueue(Queue *queue, int32 item)
{
    queue->items[queue->tail] = item;
    queue->tail = (queue->tail + 1) % queue->size;
}

int32 GetFirstInQueue(Queue *queue) { return queue->head == queue->tail ? -1 : queue->items[queue->head]; }
void RemoveFirstInQueue(Queue *queue) { if(queue->head != queue->tail) queue->head = (queue->head + 1) % queue->size; }

bool CdStreamAddImage(char const *path)
{
    if(imageCount >= kMaxImages) return false;
    char pspPath[128];
    std::strncpy(pspPath, path, sizeof(pspPath) - 1);
    pspPath[sizeof(pspPath) - 1] = '\0';
    for(char *p = pspPath; *p; ++p)
        if(*p == '\\') *p = '/';

    int fd = open(pspPath, O_RDONLY);
    if(fd < 0) return false;
    imageFiles[imageCount] = fd;
    std::strncpy(imageNames[imageCount], pspPath, sizeof(imageNames[imageCount]) - 1);
    ++imageCount;
    return true;
}

char *CdStreamGetImageName(int32 cd) { return cd >= 0 && cd < imageCount ? imageNames[cd] : nullptr; }

void CdStreamRemoveImages(void)
{
    for(int i = 0; i < imageCount; ++i) {
        if(imageFiles[i] >= 0) close(imageFiles[i]);
        imageFiles[i] = -1;
        imageNames[i][0] = '\0';
    }
    imageCount = 0;
}

int32 CdStreamGetNumImages(void) { return imageCount; }

void CdStreamQueueAudioRead(int fd, void *buffer, size_t bytes, size_t seek,
                            std::function<void(AudioReadCmd*)> callback)
{
    AudioReadCmd command{buffer, fd, bytes, seek, callback};
    lseek(fd, off_t(seek), SEEK_SET);
    read(fd, buffer, bytes);
    if(callback) callback(&command);
}

void CdStreamDiscardAudioRead(int) {}
