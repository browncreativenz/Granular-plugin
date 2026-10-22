/**
 * @file FollowerMidiOut.h
 * @brief Sends the follower triggers and envelopes on a virtual MIDI port
 *        that the plugin creates ("Capicola 1", "Capicola 2", ...).
 *
 * Live doesn't route MIDI out of audio effects, so the plugin publishes its
 * own port instead, which Live (or anything else) can take as a MIDI input.
 *
 * Threads:
 *   - audio thread: Push() into a lock-free queue, nothing else;
 *   - message thread: a timer opens and closes the port as MIDI Out is
 *     switched on and off (CoreMIDI setup belongs there);
 *   - MIDI thread: sends each event when it falls due, and the note-off a
 *     trigger length later.
 */

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "FollowerOut.h"

class FollowerMidiOut : private juce::Thread, private juce::Timer
{
  public:
    struct Pending
    {
        granular::FollowerEvent event;
        uint8_t channel;   // 1..16
        double  dueMs;     // juce::Time::getMillisecondCounterHiRes() time to send at
    };

    FollowerMidiOut();
    ~FollowerMidiOut() override;

    /** Audio thread: switch output on or off; the port follows shortly. */
    void SetEnabled(bool on) noexcept { enabled.store(on, std::memory_order_relaxed); }

    /** Audio thread: queue an event. Drops it if the queue is full. */
    void Push(const Pending& p) noexcept;

    /** Take the oldest queued event. The MIDI thread is the consumer while
     *  the port is open; tests call it directly. */
    bool Pop(Pending& p) noexcept;

    /** The name the port is published under. */
    juce::String PortName() const { return "Capicola " + juce::String(instance); }

    /** Message thread: open or close the port to match SetEnabled(). A timer
     *  calls this a few times a second; tests call it directly. Returns
     *  whether the port is open. */
    bool UpdatePort();

  private:
    void timerCallback() override { UpdatePort(); }
    void run() override;
    void Send(const juce::MidiMessage& m);

    static constexpr int kQueueSize = 1024;
    juce::AbstractFifo                fifo { kQueueSize };
    std::array<Pending, kQueueSize>   slots {};

    std::atomic<bool>                 enabled { true };
    std::mutex                        portLock;   // message thread vs MIDI thread
    std::unique_ptr<juce::MidiOutput> port;
    int                               instance = 0;

    JUCE_DECLARE_NON_COPYABLE(FollowerMidiOut)
};
