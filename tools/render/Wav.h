/**
 * @file Wav.h
 * @brief Minimal WAV file reading and writing for the command-line renderer.
 *
 * Reads PCM 16/24/32-bit and IEEE float 32-bit, mono or stereo, including
 * WAVE_FORMAT_EXTENSIBLE. Writes 32-bit float stereo.
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace granular {

struct StereoAudio
{
    uint32_t           sampleRate = 48000;
    std::vector<float> l, r;
};

namespace wav_detail {

inline uint32_t U32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

inline void Put32(std::FILE* f, uint32_t v) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; std::fwrite(b, 1, 4, f); }
inline void Put16(std::FILE* f, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; std::fwrite(b, 1, 2, f); }

} // namespace wav_detail

/** Read a WAV file. Returns false and sets error on failure. */
inline bool ReadWav(const std::string& path, StereoAudio& out, std::string& error)
{
    using namespace wav_detail;

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path; return false; }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + got);
    std::fclose(f);

    if (data.size() < 12 || std::memcmp(&data[0], "RIFF", 4) || std::memcmp(&data[8], "WAVE", 4))
    { error = path + " is not a WAV file"; return false; }

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;

    for (size_t p = 12; p + 8 <= data.size();)
    {
        const uint32_t size = U32(&data[p + 4]);
        const size_t   body = p + 8;
        if (body + size > data.size()) { error = path + " is truncated"; return false; }
        if (!std::memcmp(&data[p], "fmt ", 4) && size >= 16)
        {
            format   = U16(&data[body]);
            channels = U16(&data[body + 2]);
            rate     = U32(&data[body + 4]);
            bits     = U16(&data[body + 14]);
            if (format == 0xFFFE && size >= 26) format = U16(&data[body + 24]);   // extensible
        }
        else if (!std::memcmp(&data[p], "data", 4))
        {
            pcm      = &data[body];
            pcmBytes = size;
        }
        p = body + size + (size & 1);
    }

    if (!pcm || !channels) { error = path + " has no audio data"; return false; }
    if (channels > 2)      { error = path + ": only mono and stereo are supported"; return false; }

    const bool isFloat = (format == 3 && bits == 32);
    const bool isPcm   = (format == 1 && (bits == 16 || bits == 24 || bits == 32));
    if (!isFloat && !isPcm) { error = path + ": unsupported sample format"; return false; }

    const size_t bytes  = bits / 8;
    const size_t frames = pcmBytes / (bytes * channels);
    out.sampleRate = rate;
    out.l.resize(frames);
    out.r.resize(frames);

    for (size_t i = 0; i < frames; i++)
        for (uint16_t c = 0; c < channels; c++)
        {
            const uint8_t* s = pcm + (i * channels + c) * bytes;
            float v;
            if (isFloat)        { uint32_t u = U32(s); std::memcpy(&v, &u, 4); }
            else if (bits == 16) v = (float)(int16_t)U16(s) / 32768.0f;
            else if (bits == 24) v = (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) / 8388608.0f;
            else                 v = (float)((double)(int32_t)U32(s) / 2147483648.0);
            (c == 0 ? out.l : out.r)[i] = v;
        }
    if (channels == 1) out.r = out.l;
    return true;
}

/** Write a 32-bit float stereo WAV file. */
inline bool WriteWav(const std::string& path, const StereoAudio& in)
{
    using namespace wav_detail;

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t frames = (uint32_t)in.l.size();
    const uint32_t bytes  = frames * 8;

    std::fwrite("RIFF", 1, 4, f); Put32(f, 36 + bytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); Put32(f, 16);
    Put16(f, 3);                       // IEEE float
    Put16(f, 2);                       // stereo
    Put32(f, in.sampleRate);
    Put32(f, in.sampleRate * 8);       // byte rate
    Put16(f, 8);                       // block align
    Put16(f, 32);                      // bits
    std::fwrite("data", 1, 4, f); Put32(f, bytes);
    for (uint32_t i = 0; i < frames; i++)
    {
        std::fwrite(&in.l[i], 4, 1, f);
        std::fwrite(&in.r[i], 4, 1, f);
    }
    const bool ok = !std::ferror(f);
    return (std::fclose(f) == 0) && ok;
}

} // namespace granular
