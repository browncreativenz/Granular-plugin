/**
 * @file FollowerMidiOut.cpp
 * @brief See FollowerMidiOut.h.
 */

#include "FollowerMidiOut.h"

#include <algorithm>
#include <set>

namespace {

/* Instance numbers for port names: each instance takes the lowest free one. */
std::mutex    numbersLock;
std::set<int> numbersInUse;

int TakeNumber()
{
    std::lock_guard<std::mutex> lock(numbersLock);
    int n = 1;
    while (numbersInUse.count(n)) n++;
    numbersInUse.insert(n);
    return n;
}

void ReleaseNumber(int n)
{
    std::lock_guard<std::mutex> lock(numbersLock);
    numbersInUse.erase(n);
}

} // namespace

FollowerMidiOut::FollowerMidiOut()
    : juce::Thread("Capicola MIDI out"), instance(TakeNumber())
{
    startTimerHz(4);
}

FollowerMidiOut::~FollowerMidiOut()
{
    stopTimer();
    stopThread(1000);   // sends any outstanding note-offs on the way out
    {
        std::lock_guard<std::mutex> lock(portLock);
        port.reset();
    }
    ReleaseNumber(instance);
}

void FollowerMidiOut::Push(const Pending& p) noexcept
{
    const auto scope = fifo.write(1);
    if (scope.blockSize1 > 0)      slots[(size_t)scope.startIndex1] = p;
    else if (scope.blockSize2 > 0) slots[(size_t)scope.startIndex2] = p;
}

bool FollowerMidiOut::Pop(Pending& p) noexcept
{
    const auto scope = fifo.read(1);
    if (scope.blockSize1 > 0) { p = slots[(size_t)scope.startIndex1]; return true; }
    if (scope.blockSize2 > 0) { p = slots[(size_t)scope.startIndex2]; return true; }
    return false;
}

bool FollowerMidiOut::UpdatePort()
{
    const bool want = enabled.load(std::memory_order_relaxed);
    bool open;
    {
        std::lock_guard<std::mutex> lock(portLock);
        open = (port != nullptr);
    }
    if (want && !open)
    {
        auto created = juce::MidiOutput::createNewDevice(PortName());
        if (created == nullptr) return false;   // no MIDI system (e.g. a headless machine)
        {
            std::lock_guard<std::mutex> lock(portLock);
            port = std::move(created);
        }
        startThread(juce::Thread::Priority::high);
        return true;
    }
    if (!want && open)
    {
        stopThread(1000);
        std::lock_guard<std::mutex> lock(portLock);
        port.reset();
        return false;
    }
    return open;
}

void FollowerMidiOut::Send(const juce::MidiMessage& m)
{
    std::lock_guard<std::mutex> lock(portLock);
    if (port != nullptr) port->sendMessageNow(m);
}

void FollowerMidiOut::run()
{
    /* Due events, plus the note-offs the notes sent so far still owe. */
    std::vector<Pending> due, offs;
    due.reserve(kQueueSize);
    offs.reserve(kQueueSize);
    const double pulseMs = 1000.0 * granular::FollowerOut::kPulseSeconds;

    while (!threadShouldExit())
    {
        Pending p;
        while (Pop(p)) due.push_back(p);

        const double now = juce::Time::getMillisecondCounterHiRes();
        std::stable_sort(due.begin(), due.end(),
                         [](const Pending& a, const Pending& b) { return a.dueMs < b.dueMs; });

        size_t sent = 0;
        offs.clear();
        for (; sent < due.size() && due[sent].dueMs <= now; sent++)
        {
            const Pending& e = due[sent];
            const int ch = e.channel;
            if (e.event.kind == granular::FollowerEvent::Controller)
            {
                Send(juce::MidiMessage::controllerEvent(ch, e.event.number, e.event.value));
            }
            else if (e.event.value > 0)
            {
                Send(juce::MidiMessage::noteOn(ch, e.event.number, (juce::uint8)e.event.value));
                Pending off = e;
                off.event.value = 0;   // velocity 0 marks the note-off
                off.dueMs += pulseMs;
                offs.push_back(off);
            }
            else
            {
                Send(juce::MidiMessage::noteOff(ch, e.event.number));
            }
        }
        due.erase(due.begin(), due.begin() + (long)sent);
        due.insert(due.end(), offs.begin(), offs.end());

        wait(1);
    }

    /* Don't leave notes hanging. */
    for (const Pending& e : due)
        if (e.event.kind == granular::FollowerEvent::Note && e.event.value == 0)
            Send(juce::MidiMessage::noteOff(e.channel, e.event.number));
}
