#include "common.h"
#include "sampman.h"
#include "crossplatform.h"

#include <pspaudio.h>
#include <pspkernel.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern void pspTraceBoot(const char *message);

cSampleManager SampleManager;
uint32 BankStartOffset[MAX_SFX_BANKS]{};

namespace {

constexpr int PSP_MIX_SAMPLES = 512;
constexpr int PSP_OUTPUT_RATE = 44100;
constexpr int PSP_MIX_CHANNELS = 12;
constexpr uint32 PSP_MAX_SAMPLE_BYTES = 128 * 1024;

struct PspChannel {
    int16 *data;
    uint32 sampleCount;
    uint32 position;
    uint32 step;
    uint32 loopStart;
    uint32 loopEnd;
    uint8 volume;
    uint8 pan;
    bool loop;
    bool active;
};

struct PcSampleDesc {
    uint32 offset;
    uint32 size;
    uint32 frequency;
    uint32 loopStart;
    int32 loopEnd;
};

PspChannel channels[NUM_CHANNELS]{};
int32 sampleLoopEnd[TOTAL_AUDIO_SAMPLES]{};
FILE *sampleRaw;
SceUID audioLock = -1;
SceUID audioThread = -1;
int audioHardwareChannel = -1;
volatile bool audioThreadRunning;
bool sampleTableReady;
uint8 effectsMaster = MAX_VOLUME;
uint8 effectsFade = MAX_VOLUME;
alignas(64) int16 mixBuffer[PSP_MIX_SAMPLES * 2];

static void lockAudio()
{
    if(audioLock >= 0)
        sceKernelWaitSema(audioLock, 1, nullptr);
}

static void unlockAudio()
{
    if(audioLock >= 0)
        sceKernelSignalSema(audioLock, 1);
}

static int clampSample(int value)
{
    if(value > 32767) return 32767;
    if(value < -32768) return -32768;
    return value;
}

static int audioMixerThread(SceSize, void *)
{
    while(audioThreadRunning) {
        int32 left[PSP_MIX_SAMPLES]{};
        int32 right[PSP_MIX_SAMPLES]{};

        lockAudio();
        for(int c = 0; c < PSP_MIX_CHANNELS; c++) {
            PspChannel &channel = channels[c];
            if(!channel.active || !channel.data || channel.sampleCount == 0)
                continue;

            const int baseGain = channel.volume * effectsMaster * effectsFade >> 14;
            const int leftPan = channel.pan <= 63 ? 127 : (127 - channel.pan) * 2;
            const int rightPan = channel.pan >= 63 ? 127 : channel.pan * 2;
            const int leftGain = baseGain * leftPan >> 7;
            const int rightGain = baseGain * rightPan >> 7;

            for(int i = 0; i < PSP_MIX_SAMPLES; i++) {
                uint32 source = channel.position >> 16;
                const uint32 end = channel.loopEnd > channel.loopStart &&
                    channel.loopEnd <= channel.sampleCount ? channel.loopEnd : channel.sampleCount;
                if(source >= end) {
                    if(channel.loop) {
                        channel.position = channel.loopStart << 16;
                        source = channel.loopStart;
                    } else {
                        channel.active = false;
                        break;
                    }
                }
                const int sample = channel.data[source];
                left[i] += sample * leftGain >> 7;
                right[i] += sample * rightGain >> 7;
                channel.position += channel.step;
            }
        }
        unlockAudio();

        for(int i = 0; i < PSP_MIX_SAMPLES; i++) {
            mixBuffer[i * 2] = clampSample(left[i]);
            mixBuffer[i * 2 + 1] = clampSample(right[i]);
        }
        if(audioHardwareChannel >= 0)
            sceAudioOutputPannedBlocking(audioHardwareChannel,
                PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, mixBuffer);
        else
            sceKernelDelayThread(10000);
    }
    return 0;
}

static void releaseChannel(PspChannel &channel)
{
    channel.active = false;
    if(channel.data)
        std::free(channel.data);
    std::memset(&channel, 0, sizeof(channel));
    channel.volume = MAX_VOLUME;
    channel.pan = 63;
    channel.step = (DIGITALRATE << 16) / PSP_OUTPUT_RATE;
}

} // namespace

cSampleManager::cSampleManager()
{
    std::memset(this, 0, sizeof(*this));
}

cSampleManager::~cSampleManager() {}

#ifdef EXTERNAL_3D_SOUND
void cSampleManager::SetSpeakerConfig(int32) {}
uint32 cSampleManager::GetMaximumSupportedChannels() { return NUM_CHANNELS; }
uint32 cSampleManager::GetNum3DProvidersAvailable() { return 1; }
void cSampleManager::SetNum3DProvidersAvailable(uint32 value) { m_nNumberOfProviders = value; }
char *cSampleManager::Get3DProviderName(uint8) { static char name[] = "PSP Audio"; return name; }
void cSampleManager::Set3DProviderName(uint8, char *) {}
int8 cSampleManager::GetCurrent3DProviderIndex() { return 0; }
int8 cSampleManager::SetCurrent3DProvider(uint8) { return 0; }
void cSampleManager::SetChannelEmittingVolume(uint32 channel, uint32 volume) { SetChannelVolume(channel, volume); }
void cSampleManager::SetChannel3DPosition(uint32, float, float, float) {}
void cSampleManager::SetChannel3DDistances(uint32, float, float) {}
#endif

bool8 cSampleManager::IsMP3RadioChannelAvailable() { return false; }
void cSampleManager::ReleaseDigitalHandle() {}
void cSampleManager::ReacquireDigitalHandle() {}

bool8 cSampleManager::Initialise()
{
    effectsMaster = m_nEffectsVolume = MAX_VOLUME;
    effectsFade = m_nEffectsFadeVolume = MAX_VOLUME;
    m_nMusicVolume = MAX_VOLUME;
    m_nMusicFadeVolume = MAX_VOLUME;

    sampleTableReady = InitialiseSampleBanks();
    if(!sampleTableReady) {
        pspTraceBoot("W120 AUDIO/SFX.SDT o SFX.RAW no disponibles; juego continua sin SFX");
        m_bInitialised = true;
        return true;
    }

    for(int i = 0; i < NUM_CHANNELS; i++)
        releaseChannel(channels[i]);
    audioLock = sceKernelCreateSema("re3_audio_lock", 0, 1, 1, nullptr);
    audioHardwareChannel = sceAudioChReserve(-1, PSP_MIX_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if(audioLock < 0 || audioHardwareChannel < 0) {
        pspTraceBoot("W121 canal de audio PSP no reservado; juego continua");
        m_bInitialised = true;
        return true;
    }

    audioThreadRunning = true;
    audioThread = sceKernelCreateThread("re3_audio_mixer", audioMixerThread,
        0x12, 64 * 1024, PSP_THREAD_ATTR_USER, nullptr);
    if(audioThread < 0 || sceKernelStartThread(audioThread, 0, nullptr) < 0) {
        audioThreadRunning = false;
        pspTraceBoot("W122 mezclador de audio PSP no iniciado; juego continua");
    } else {
        pspTraceBoot("S120 mezclador SFX PCM PSP activo");
    }
    m_bInitialised = true;
    return true;
}

void cSampleManager::Terminate()
{
    audioThreadRunning = false;
    if(audioThread >= 0) {
        sceKernelWaitThreadEnd(audioThread, nullptr);
        sceKernelDeleteThread(audioThread);
        audioThread = -1;
    }
    lockAudio();
    for(int i = 0; i < NUM_CHANNELS; i++)
        releaseChannel(channels[i]);
    unlockAudio();
    if(audioHardwareChannel >= 0) {
        sceAudioChRelease(audioHardwareChannel);
        audioHardwareChannel = -1;
    }
    if(audioLock >= 0) {
        sceKernelDeleteSema(audioLock);
        audioLock = -1;
    }
    if(sampleRaw) {
        std::fclose(sampleRaw);
        sampleRaw = nullptr;
    }
    sampleTableReady = false;
    m_bInitialised = false;
}

bool8 cSampleManager::CheckForAnAudioFileOnCD() { return true; }
char cSampleManager::GetCDAudioDriveLetter() { return 0; }
void cSampleManager::UpdateEffectsVolume() {}
void cSampleManager::UpdateStreamsVolume() {}
void cSampleManager::SetEffectsMasterVolume(uint8 value) { m_nEffectsVolume = effectsMaster = value; }
void cSampleManager::SetMusicMasterVolume(uint8 value) { m_nMusicVolume = value; }
void cSampleManager::SetEffectsFadeVolume(uint8 value) { m_nEffectsFadeVolume = effectsFade = value; }
void cSampleManager::SetMusicFadeVolume(uint8 value) { m_nMusicFadeVolume = value; }
void cSampleManager::SetMonoMode(bool8 value) { m_nMonoMode = value; }
bool8 cSampleManager::LoadSampleBank(uint8) { return sampleTableReady; }
void cSampleManager::UnloadSampleBank(uint8) {}
void cSampleManager::UnloadUnusedSampleBank() {}
int8 cSampleManager::IsSampleBankLoaded(uint8) { return sampleTableReady; }
uint8 cSampleManager::IsPedCommentLoaded(uint32 sample) { return sample < TOTAL_AUDIO_SAMPLES && sampleTableReady; }
bool8 cSampleManager::LoadPedComment(uint32 sample) { return sample < TOTAL_AUDIO_SAMPLES && sampleTableReady; }
int32 cSampleManager::GetBankContainingSound(uint32 sample)
{
    return sample >= BankStartOffset[SFX_BANK_PED_COMMENTS] ? SFX_BANK_PED_COMMENTS : SFX_BANK_0;
}
int32 cSampleManager::_GetPedCommentSlot(uint32) { return -1; }
uint32 cSampleManager::GetSampleBaseFrequency(uint32 sample)
{
    return sample < TOTAL_AUDIO_SAMPLES && m_aSamples[sample].nFrequency ? m_aSamples[sample].nFrequency : DIGITALRATE;
}
uint32 cSampleManager::GetSampleLoopStartOffset(uint32 sample)
{
    return sample < TOTAL_AUDIO_SAMPLES ? m_aSamples[sample].nLoopStartSample : 0;
}
int32 cSampleManager::GetSampleLoopEndOffset(uint32 sample)
{
    return sample < TOTAL_AUDIO_SAMPLES ? sampleLoopEnd[sample] : -1;
}
uint32 cSampleManager::GetSampleLength(uint32 sample)
{
    return sample < TOTAL_AUDIO_SAMPLES ? m_aSamples[sample].nByteSize / 2 : 0;
}
bool8 cSampleManager::UpdateReverb() { return false; }
void cSampleManager::SetChannelReverbFlag(uint32, bool8) {}

bool8 cSampleManager::InitialiseChannel(uint32 channel, uint32 sfx, uint8)
{
    if(channel >= PSP_MIX_CHANNELS || sfx >= TOTAL_AUDIO_SAMPLES ||
       !sampleTableReady || !sampleRaw)
        return false;
    const tSample &sample = m_aSamples[sfx];
    if(sample.nByteSize < 2 || sample.nByteSize > PSP_MAX_SAMPLE_BYTES ||
       sample.nFrequency < 4000 || sample.nFrequency > 48000)
        return false;

    int16 *data = static_cast<int16*>(std::malloc(sample.nByteSize));
    if(!data)
        return false;
    if(std::fseek(sampleRaw, sample.nFileOffset, SEEK_SET) != 0 ||
       std::fread(data, 1, sample.nByteSize, sampleRaw) != sample.nByteSize) {
        std::free(data);
        return false;
    }

    lockAudio();
    releaseChannel(channels[channel]);
    channels[channel].data = data;
    channels[channel].sampleCount = sample.nByteSize / 2;
    channels[channel].loopStart = Min(sample.nLoopStartSample, channels[channel].sampleCount - 1);
    channels[channel].loopEnd = sampleLoopEnd[sfx] > 0 ?
        Min(static_cast<uint32>(sampleLoopEnd[sfx]), channels[channel].sampleCount) : channels[channel].sampleCount;
    channels[channel].step = (sample.nFrequency << 16) / PSP_OUTPUT_RATE;
    unlockAudio();
    return true;
}

void cSampleManager::SetChannelVolume(uint32 channel, uint32 volume)
{
    if(channel >= NUM_CHANNELS) return;
    lockAudio();
    channels[channel].volume = Min(volume, static_cast<uint32>(MAX_VOLUME));
    unlockAudio();
}
void cSampleManager::SetChannelPan(uint32 channel, uint32 pan)
{
    if(channel >= NUM_CHANNELS) return;
    lockAudio();
    channels[channel].pan = Min(pan, static_cast<uint32>(MAX_VOLUME));
    unlockAudio();
}
void cSampleManager::SetChannelFrequency(uint32 channel, uint32 frequency)
{
    if(channel >= NUM_CHANNELS) return;
    if(frequency < 1000) frequency = 1000;
    if(frequency > 96000) frequency = 96000;
    lockAudio();
    channels[channel].step = (frequency << 16) / PSP_OUTPUT_RATE;
    unlockAudio();
}
void cSampleManager::SetChannelLoopPoints(uint32 channel, uint32 start, int32 end)
{
    if(channel >= NUM_CHANNELS) return;
    lockAudio();
    if(channels[channel].sampleCount) {
        channels[channel].loopStart = Min(start, channels[channel].sampleCount - 1);
        channels[channel].loopEnd = end > 0 ? Min(static_cast<uint32>(end), channels[channel].sampleCount) : channels[channel].sampleCount;
    }
    unlockAudio();
}
void cSampleManager::SetChannelLoopCount(uint32 channel, uint32 count)
{
    if(channel >= NUM_CHANNELS) return;
    lockAudio();
    channels[channel].loop = count == 0;
    unlockAudio();
}
bool8 cSampleManager::GetChannelUsedFlag(uint32 channel)
{
    if(channel >= NUM_CHANNELS) return false;
    lockAudio();
    const bool used = channels[channel].active;
    unlockAudio();
    return used;
}
void cSampleManager::StartChannel(uint32 channel)
{
    if(channel >= NUM_CHANNELS) return;
    lockAudio();
    channels[channel].position = 0;
    channels[channel].active = channels[channel].data != nullptr;
    unlockAudio();
}
void cSampleManager::StopChannel(uint32 channel)
{
    if(channel >= NUM_CHANNELS) return;
    lockAudio();
    channels[channel].active = false;
    unlockAudio();
}

// Radio/cutscene streaming needs a codec layer and is intentionally kept
// separate from the low-latency PCM SFX mixer introduced in 9Z.
void cSampleManager::PreloadStreamedFile(uint8, uint8, uint32_t) {}
void cSampleManager::PauseStream(bool8, uint8) {}
void cSampleManager::StartPreloadedStreamedFile(uint8) {}
bool8 cSampleManager::StartStreamedFile(uint8, uint32, uint8) { return false; }
void cSampleManager::StopStreamedFile(uint8) {}
int32 cSampleManager::GetStreamedFilePosition(uint8) { return 0; }
void cSampleManager::SetStreamedVolumeAndPan(uint8, uint8, bool8, uint8) {}
int32 cSampleManager::GetStreamedFileLength(uint8) { return 1; }
bool8 cSampleManager::IsStreamPlaying(uint8) { return false; }

bool8 cSampleManager::InitialiseSampleBanks()
{
    FILE *desc = fcaseopen("AUDIO\\SFX.SDT", "rb");
    sampleRaw = fcaseopen("AUDIO\\SFX.RAW", "rb");
    if(!desc || !sampleRaw) {
        if(desc) std::fclose(desc);
        if(sampleRaw) { std::fclose(sampleRaw); sampleRaw = nullptr; }
        return false;
    }

    std::fseek(desc, 0, SEEK_END);
    const long descSize = std::ftell(desc);
    std::rewind(desc);
    long recordSize = 0;
    if(descSize == static_cast<long>(TOTAL_AUDIO_SAMPLES * sizeof(PcSampleDesc)))
        recordSize = sizeof(PcSampleDesc);
    else if(descSize == static_cast<long>(TOTAL_AUDIO_SAMPLES * sizeof(tSample)))
        recordSize = sizeof(tSample);
    else if(descSize > 0 && descSize % sizeof(PcSampleDesc) == 0 &&
            descSize / sizeof(PcSampleDesc) >= TOTAL_AUDIO_SAMPLES)
        recordSize = sizeof(PcSampleDesc);
    else if(descSize > 0 && descSize % sizeof(tSample) == 0 &&
            descSize / sizeof(tSample) >= TOTAL_AUDIO_SAMPLES)
        recordSize = sizeof(tSample);
    bool ok = recordSize != 0;

    for(uint32 i = 0; ok && i < TOTAL_AUDIO_SAMPLES; i++) {
        sampleLoopEnd[i] = -1;
        if(recordSize == static_cast<long>(sizeof(PcSampleDesc))) {
            PcSampleDesc source{};
            ok = std::fread(&source, sizeof(source), 1, desc) == 1;
            m_aSamples[i].nFileOffset = source.offset;
            m_aSamples[i].nByteSize = source.size;
            m_aSamples[i].nFrequency = source.frequency;
            m_aSamples[i].nLoopStartSample = source.loopStart;
            m_aSamples[i].nLoopFileOffset = 0;
            m_aSamples[i].nLoopByteSize = 0;
            sampleLoopEnd[i] = source.loopEnd;
        } else if(recordSize == static_cast<long>(sizeof(tSample))) {
            ok = std::fread(&m_aSamples[i], sizeof(tSample), 1, desc) == 1;
            if(m_aSamples[i].nLoopByteSize)
                sampleLoopEnd[i] = m_aSamples[i].nLoopStartSample + m_aSamples[i].nLoopByteSize / 2;
        } else {
            ok = false;
        }
    }
    std::fclose(desc);
    if(!ok) {
        std::fclose(sampleRaw);
        sampleRaw = nullptr;
        pspTraceBoot("W123 formato AUDIO/SFX.SDT no reconocido");
        return false;
    }
    return true;
}
