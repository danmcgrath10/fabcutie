#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_core/juce_core.h>

namespace fabcutie::dsp
{
    // Auto-unmasking: every instance publishes its output on a key bus, and
    // an instance whose key is another instance feeds that output to its
    // sidechain, so dynamic bands set to External duck under it (the bass
    // making room for the kick) without any routing in the host.
    //
    // The bus is a lock-free ring with one writer (the owning instance's
    // audio thread) and any number of readers (other instances' audio
    // threads). Hosts process tracks in any order, often in parallel, so a
    // reader follows the writer at a distance of up to a block or so; for a
    // level detector with attack and release times that is close enough.
    class KeyBus
    {
    public:
        static constexpr int numChannels = 2;
        static constexpr int capacity = 1 << 14; // per channel; ~340 ms at 48 kHz

        KeyBus() : data (new float[(size_t) (numChannels * capacity)]())
        {
        }

        // Owner's audio thread. Mono is copied to both channels; past two
        // channels only the first two are kept.
        void write (const float* const* channels, int channelCount, int numSamples) noexcept
        {
            if (channelCount <= 0 || numSamples <= 0 || numSamples > capacity / 4)
                return;

            const auto start = written.load (std::memory_order_relaxed);

            for (int c = 0; c < numChannels; ++c)
            {
                const auto* src = channels[std::min (c, channelCount - 1)];
                auto* dest = data.get() + (size_t) c * capacity;

                for (int i = 0; i < numSamples; ++i)
                    dest[(size_t) ((start + i) & (capacity - 1))] = src[i];
            }

            written.store (start + numSamples, std::memory_order_release);
        }

        // Owner, message thread: a fresh owner starts from silence.
        void clear() noexcept
        {
            std::fill_n (data.get(), (size_t) (numChannels * capacity), 0.0f);
        }

        // Where one reader is up to. Each reading instance keeps its own.
        struct Reader
        {
            std::int64_t position = -1; // samples read so far; -1 to resync
            int idleBlocks = 0;         // blocks in a row with nothing new written
        };

        // Blocks in a row with nothing new before the key counts as stopped
        // (track disabled, muted by the host, or removed) and reads silence.
        static constexpr int maxIdleBlocks = 4;

        // Reader's audio thread: fills numSamples of each dest channel with
        // the newest audio, continuing smoothly from the last read while the
        // writer keeps pace. Returns false (and writes silence) when the bus
        // has nothing to give.
        bool read (Reader& reader, float* const* dest, int destChannels, int numSamples) const noexcept
        {
            auto silence = [&]
            {
                for (int c = 0; c < destChannels; ++c)
                    std::fill_n (dest[c], numSamples, 0.0f);
                return false;
            };

            if (numSamples <= 0 || numSamples > capacity / 4)
                return silence();

            const auto end = written.load (std::memory_order_acquire);

            if (end < numSamples)
                return silence();

            const auto available = end - reader.position;
            std::int64_t from;

            if (reader.position < 0 || available < 0 || available > 3 * (std::int64_t) numSamples)
            {
                // First read, a writer that restarted, or one we fell far
                // behind: jump to the newest block.
                from = end - numSamples;
                reader.idleBlocks = 0;
            }
            else if (available >= numSamples)
            {
                from = reader.position;
                reader.idleBlocks = 0;
            }
            else
            {
                // The writer has not got this far yet (or has stopped): take
                // the newest block again rather than wait.
                from = end - numSamples;
                reader.idleBlocks = available == 0 ? reader.idleBlocks + 1 : 0;
            }

            reader.position = std::max (from + numSamples, reader.position);

            if (reader.idleBlocks > maxIdleBlocks)
                return silence();

            for (int c = 0; c < destChannels; ++c)
            {
                const auto* src = data.get() + (size_t) std::min (c, numChannels - 1) * capacity;

                for (int i = 0; i < numSamples; ++i)
                    dest[c][i] = src[(size_t) ((from + i) & (capacity - 1))];
            }

            return true;
        }

        std::int64_t getWritten() const noexcept { return written.load (std::memory_order_acquire); }

    private:
        std::unique_ptr<float[]> data;
        std::atomic<std::int64_t> written { 0 };
    };

    // A fixed set of key buses, one per instance in the process, found by
    // the instance's saved ID. Slots are claimed and released on the message
    // thread and looked up from audio threads without locking; a slot's
    // memory lives as long as the pool, so a reader never touches freed
    // memory even if the key instance goes away mid-block.
    class KeyBusPool
    {
    public:
        static constexpr int numSlots = 64;

        // Message thread. Returns nullptr when every slot is taken (the
        // instance then simply cannot be used as a key).
        KeyBus* claim (std::uint64_t id)
        {
            const juce::ScopedLock sl (lock);

            for (auto& slot : slots)
            {
                if (slot.owner.load() != 0)
                    continue;

                if (slot.bus == nullptr)
                    slot.bus = std::make_unique<KeyBus>();
                else
                    slot.bus->clear();

                slot.owner.store (id, std::memory_order_release);
                return slot.bus.get();
            }

            return nullptr;
        }

        // Message thread: the instance's ID changed (a session restored it).
        void rename (const KeyBus* bus, std::uint64_t id)
        {
            const juce::ScopedLock sl (lock);

            for (auto& slot : slots)
                if (slot.bus.get() == bus)
                    slot.owner.store (id, std::memory_order_release);
        }

        void release (const KeyBus* bus)
        {
            const juce::ScopedLock sl (lock);

            for (auto& slot : slots)
                if (slot.bus.get() == bus)
                    slot.owner.store (0, std::memory_order_release);
        }

        // Any thread, lock-free.
        const KeyBus* find (std::uint64_t id) const noexcept
        {
            if (id == 0)
                return nullptr;

            for (auto& slot : slots)
                if (slot.owner.load (std::memory_order_acquire) == id)
                    return slot.bus.get();

            return nullptr;
        }

    private:
        struct Slot
        {
            std::atomic<std::uint64_t> owner { 0 };
            std::unique_ptr<KeyBus> bus; // only set under the lock, before owner
        };

        juce::CriticalSection lock;
        std::array<Slot, numSlots> slots;
    };
}
