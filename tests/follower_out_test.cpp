/**
 * @file follower_out_test.cpp
 * @brief FollowerOut: triggers become notes with the module's 10 ms hold,
 *        envelopes become controller values sent on change.
 */

#include <cstdio>
#include <vector>

#include "Check.h"
#include "FollowerOut.h"

using namespace granular;

namespace {

struct Recorder
{
    std::vector<FollowerEvent> events;
    void operator()(const FollowerEvent& e) { events.push_back(e); }

    int Notes(uint8_t note) const
    {
        int n = 0;
        for (const auto& e : events) n += (e.kind == FollowerEvent::Note && e.number == note);
        return n;
    }
    int Controllers(uint8_t cc) const
    {
        int n = 0;
        for (const auto& e : events) n += (e.kind == FollowerEvent::Controller && e.number == cc);
        return n;
    }
};

void Triggers()
{
    FollowerOut f;
    f.Prepare(48000);   // 10 ms = 480 samples
    CHECK(f.PulseSamples() == 480, "pulse %lld", (long long)f.PulseSamples());
    const FollowerOut::Settings s;
    Recorder r;

    /* The first observation is a baseline: counters already nonzero don't
     * fire (envelope controllers do send their starting value). */
    f.Observe(s, 0, 7, 3, 0.0f, 0.0f, r);
    CHECK(r.Notes(s.sliceNote) == 0 && r.Notes(s.eventNote) == 0, "baseline fired a note");
    CHECK(r.Controllers(s.inEnvCc) == 1 && r.Controllers(s.outEnvCc) == 1, "starting envelope values not sent");

    /* A slice fires its note at the observed position. */
    r.events.clear();
    f.Observe(s, 100, 8, 3, 0.0f, 0.0f, r);
    CHECK(r.events.size() == 1 && r.events[0].number == s.sliceNote && r.events[0].offset == 100
          && r.events[0].value == FollowerOut::kVelocity, "slice note");

    /* Another slice inside 10 ms is the same pulse; after it, a new note. */
    f.Observe(s, 400, 9, 3, 0.0f, 0.0f, r);
    CHECK(r.Notes(s.sliceNote) == 1, "retriggered inside the pulse");
    f.Observe(s, 580, 10, 3, 0.0f, 0.0f, r);
    CHECK(r.Notes(s.sliceNote) == 2, "missed a slice after the pulse");

    /* The hold carries across host blocks. */
    f.Advance(600);
    f.Observe(s, 100, 11, 3, 0.0f, 0.0f, r);    // 120 samples after the last note
    CHECK(r.Notes(s.sliceNote) == 2, "retriggered across a block boundary");
    f.Observe(s, 500, 12, 3, 0.0f, 0.0f, r);
    CHECK(r.Notes(s.sliceNote) == 3, "missed a slice in the next block");

    /* Output events have their own note and their own hold. */
    f.Observe(s, 510, 12, 4, 0.0f, 0.0f, r);
    CHECK(r.Notes(s.eventNote) == 1, "output event note");

    /* Settings choose the numbers. */
    FollowerOut::Settings t;
    t.sliceNote = 60;
    t.eventNote = 61;
    f.Advance(10000);
    f.Observe(t, 0, 13, 5, 0.0f, 0.0f, r);
    CHECK(r.Notes(60) == 1 && r.Notes(61) == 1, "settings ignored");

    /* Reset takes a new baseline. */
    r.events.clear();
    f.Reset();
    f.Observe(s, 0, 1000, 1000, 0.0f, 0.0f, r);
    CHECK(r.Notes(s.sliceNote) == 0 && r.Notes(s.eventNote) == 0, "fired after Reset");
}

void Envelopes()
{
    FollowerOut f;
    f.Prepare(44100);
    const FollowerOut::Settings s;
    Recorder r;
    f.Observe(s, 0, 0, 0, 0.5f, 1.0f, r);
    CHECK(r.events.size() == 2, "%zu events", r.events.size());
    CHECK(r.events[0].number == s.inEnvCc && r.events[0].value == 64, "in %d", r.events[0].value);
    CHECK(r.events[1].number == s.outEnvCc && r.events[1].value == 127, "out %d", r.events[1].value);

    /* Sent only when the 7-bit value changes, at most every 10 ms (441
     * samples); a change inside the interval goes out once it has passed. */
    r.events.clear();
    f.Observe(s, 500, 0, 0, 0.501f, 1.0f, r);
    CHECK(r.events.empty(), "resent an unchanged value");
    f.Observe(s, 520, 0, 0, 0.6f, 2.0f, r);   // out clamps at 127: unchanged
    CHECK(r.events.size() == 1 && r.events[0].value == 76 && r.events[0].offset == 520, "changed value");
    f.Observe(s, 600, 0, 0, 0.7f, 1.0f, r);   // 80 samples later: held back
    CHECK(r.events.size() == 1, "sent inside the 10 ms interval");
    f.Observe(s, 961, 0, 0, 0.7f, 1.0f, r);   // 441 samples after 520: due
    CHECK(r.events.size() == 2 && r.events[1].value == 89 && r.events[1].offset == 961, "held value not sent");
    f.Advance(1000);
    f.Observe(s, 500, 0, 0, -1.0f, 0.0f, r);
    CHECK(r.Controllers(s.inEnvCc) == 3 && r.events.back().value == 0, "clamps low");
}

} // namespace

int main()
{
    Triggers();
    Envelopes();
    std::printf("follower out: %s\n", test::Failures() ? "FAIL" : "ok");
    return test::Failures() ? 1 : 0;
}
