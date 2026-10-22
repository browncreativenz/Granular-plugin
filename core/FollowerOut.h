/**
 * @file FollowerOut.h
 * @brief The module's trigger and envelope outputs, as MIDI-style events.
 *
 * On the module, TRIG 1 pulses on every slice the engine takes (detected or
 * forced), TRIG 2 on each transient the output follower keeps, and the two
 * ENV jacks carry the input and output envelopes (src/main.cpp TrigOut,
 * src/jack_routing.h). Here the triggers become notes and the envelopes
 * become 7-bit controller values.
 *
 * Like the module's 10 ms trigger pulses, a trigger is not re-sent while the
 * previous one would still be high: one stereo hit whose left and right
 * channels slice in adjacent blocks gives one note. Envelope values are sent
 * when they change, at most once per 10 ms per controller (the latest value
 * goes out once the interval has passed).
 *
 * Plain C++: the plugin turns these into MIDI messages on its own thread.
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace granular {

struct FollowerEvent
{
    enum Kind : uint8_t { Note, Controller };

    Kind    kind;
    uint8_t number;   // note or controller number
    uint8_t value;    // velocity or controller value
    int     offset;   // sample position in the current host block
};

class FollowerOut
{
  public:
    static constexpr double  kPulseSeconds = 0.010;   // the module's trigger length
    static constexpr double  kCcSeconds    = 0.010;   // shortest gap between envelope values
    static constexpr uint8_t kVelocity     = 100;

    struct Settings
    {
        uint8_t sliceNote = 36;   // C1 in Live: the first Drum Rack pad
        uint8_t eventNote = 38;   // D1
        uint8_t inEnvCc   = 20;
        uint8_t outEnvCc  = 21;
    };

    void Prepare(int hostSampleRate)
    {
        pulse    = (int64_t)std::lround(kPulseSeconds * hostSampleRate);
        ccPeriod = (int64_t)std::lround(kCcSeconds * hostSampleRate);
        Reset();
    }

    /** Forget the counters and envelope values; the next Observe() only
     *  takes a baseline and emits nothing. */
    void Reset()
    {
        clock      = 0;
        primed     = false;
        lastSliceT = lastEventT = INT64_MIN / 2;
        lastInCcT  = lastOutCcT = INT64_MIN / 2;
        lastInCc   = lastOutCc  = -1;
    }

    /**
     * Compare the follower state, as of `offset` samples into the current
     * host block, with the last observation and emit what changed.
     *   slices: every slice taken, detected and manual
     *   events: kept output-follower events
     *   inEnv, outEnv: envelopes, 0..1
     */
    template <class Emit>
    void Observe(const Settings& s, int offset, uint32_t slices, uint32_t events,
                 float inEnv, float outEnv, Emit&& emit)
    {
        const int64_t t = clock + offset;
        if (!primed)
        {
            primed     = true;
            lastSlices = slices;
            lastEvents = events;
        }

        if (slices != lastSlices)
        {
            lastSlices = slices;
            if (t - lastSliceT >= pulse)
            {
                lastSliceT = t;
                emit(FollowerEvent { FollowerEvent::Note, s.sliceNote, kVelocity, offset });
            }
        }
        if (events != lastEvents)
        {
            lastEvents = events;
            if (t - lastEventT >= pulse)
            {
                lastEventT = t;
                emit(FollowerEvent { FollowerEvent::Note, s.eventNote, kVelocity, offset });
            }
        }

        const int in  = Cc(inEnv);
        const int out = Cc(outEnv);
        if (in != lastInCc && t - lastInCcT >= ccPeriod)
        {
            lastInCc  = in;
            lastInCcT = t;
            emit(FollowerEvent { FollowerEvent::Controller, s.inEnvCc, (uint8_t)in, offset });
        }
        if (out != lastOutCc && t - lastOutCcT >= ccPeriod)
        {
            lastOutCc  = out;
            lastOutCcT = t;
            emit(FollowerEvent { FollowerEvent::Controller, s.outEnvCc, (uint8_t)out, offset });
        }
    }

    /** Call at the end of each host block of n samples. */
    void Advance(int n) { clock += n; }

    /** Trigger length in host samples, for scheduling note-offs. */
    int64_t PulseSamples() const { return pulse; }

  private:
    static int Cc(float env)
    {
        const float e = env < 0.0f ? 0.0f : (env > 1.0f ? 1.0f : env);
        return (int)std::lround(e * 127.0f);
    }

    int64_t  pulse      = 480;
    int64_t  ccPeriod   = 480;
    int64_t  clock      = 0;
    bool     primed     = false;
    uint32_t lastSlices = 0, lastEvents = 0;
    int64_t  lastSliceT = 0, lastEventT = 0;
    int64_t  lastInCcT  = 0, lastOutCcT = 0;
    int      lastInCc   = -1, lastOutCc = -1;
};

} // namespace granular
