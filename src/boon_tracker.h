#pragma once
// Boon Output - tracking logic.
// Pure C++17, no game or ImGui dependency, so it can be tested on its own.
//
// Data source: arcdps combat events (forwarded by Nexus). Nothing here reads
// game memory.

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace bt
{
    // Mirror of arcdps' cbtevent (64 bytes). Layout must match arcdps exactly.
    struct CbtEvent
    {
        uint64_t time;
        uint64_t src_agent;
        uint64_t dst_agent;
        int32_t value;       // buff application: duration applied (ms)
        int32_t buff_dmg;    // 0 for a plain buff application
        uint32_t overstack_value;
        uint32_t skillid;    // buff id
        uint16_t src_instid;
        uint16_t dst_instid;
        uint16_t src_master_instid;
        uint16_t dst_master_instid;
        uint8_t iff;
        uint8_t buff;
        uint8_t result;
        uint8_t is_activation;
        uint8_t is_buffremove;
        uint8_t is_ninety;
        uint8_t is_fifty;
        uint8_t is_moving;
        uint8_t is_statechange;
        uint8_t is_flanking;
        uint8_t is_shields;
        uint8_t is_offcycle;
        uint8_t pad61, pad62, pad63, pad64;
    };
    static_assert(sizeof(CbtEvent) == 64, "CbtEvent must match arcdps' 64 byte layout");

    // Mirror of arcdps' ag.
    struct Agent
    {
        char* name;
        uintptr_t id;
        uint32_t prof;
        uint32_t elite;
        uint32_t self;
        uint16_t team;
    };

    struct BoonInfo
    {
        uint32_t id;
        const char* name;
        float r, g, b;
    };

    inline constexpr std::array<BoonInfo, 12> kBoons = {{
        {725,   "Fury",        0.95f, 0.45f, 0.20f},
        {1187,  "Quickness",   0.85f, 0.40f, 0.85f},
        {30328, "Alacrity",    0.35f, 0.65f, 0.95f},
        {740,   "Might",       0.90f, 0.75f, 0.25f},
        {717,   "Protection",  0.40f, 0.80f, 0.90f},
        {718,   "Regeneration",0.45f, 0.85f, 0.45f},
        {726,   "Vigor",       0.75f, 0.90f, 0.45f},
        {719,   "Swiftness",   0.95f, 0.85f, 0.35f},
        {743,   "Aegis",       0.70f, 0.75f, 0.95f},
        {1122,  "Stability",   0.85f, 0.65f, 0.35f},
        {26980, "Resistance",  0.80f, 0.80f, 0.80f},
        {873,   "Resolution",  0.55f, 0.90f, 0.70f},
    }};

    inline int BoonIndex(uint32_t skillId)
    {
        for (size_t i = 0; i < kBoons.size(); ++i)
        {
            if (kBoons[i].id == skillId)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    struct Settings
    {
        bool includeSelf = true;     // count applications on yourself
        uint32_t windowMs = 5000;    // "recent" window for target counts
    };

    struct Row
    {
        const BoonInfo* boon = nullptr;
        int targets = 0;             // distinct targets hit inside the window
        int lastDurationMs = 0;      // duration of the most recent application
        uint64_t msSinceLast = 0;    // time since the most recent application
        uint32_t applications = 0;   // applications inside the window
    };

    class Tracker
    {
    public:
        // Returns true if the event was counted as "a boon I applied".
        // selfIsSource / targetIsSelf come from the arcdps agent structs.
        bool OnCombat(const CbtEvent& ev, bool selfIsSource, bool targetIsSelf, uint64_t nowMs, const Settings& cfg)
        {
            // Plain buff application: not a state change, not an activation,
            // not a removal, not a damage tick.
            if (ev.is_statechange != 0 || ev.is_activation != 0 || ev.is_buffremove != 0)
            {
                return false;
            }
            if (ev.buff == 0 || ev.buff_dmg != 0 || ev.value <= 0)
            {
                return false;
            }
            if (!selfIsSource)
            {
                return false;
            }
            if (targetIsSelf && !cfg.includeSelf)
            {
                return false;
            }
            int idx = BoonIndex(ev.skillid);
            if (idx < 0)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            Entry& e = entries_[idx];
            e.events.push_back({ev.dst_agent, nowMs, ev.value});
            Prune(e, nowMs, kKeepMs);
            return true;
        }

        // Rows for every boon with activity inside the window, in kBoons order.
        std::vector<Row> Snapshot(uint64_t nowMs, const Settings& cfg)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<Row> rows;
            for (size_t i = 0; i < kBoons.size(); ++i)
            {
                auto it = entries_.find(static_cast<int>(i));
                if (it == entries_.end())
                {
                    continue;
                }
                Entry& e = it->second;
                Prune(e, nowMs, kKeepMs);

                Row row;
                row.boon = &kBoons[i];
                std::unordered_map<uint64_t, bool> seen;
                bool haveLast = false;
                for (auto rit = e.events.rbegin(); rit != e.events.rend(); ++rit)
                {
                    if (!haveLast)
                    {
                        row.lastDurationMs = rit->durationMs;
                        row.msSinceLast = nowMs - rit->timeMs;
                        haveLast = true;
                    }
                    if (nowMs - rit->timeMs <= cfg.windowMs)
                    {
                        row.applications++;
                        seen[rit->target] = true;
                    }
                }
                row.targets = static_cast<int>(seen.size());
                if (row.applications > 0)
                {
                    rows.push_back(row);
                }
            }
            return rows;
        }

        void Reset()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.clear();
        }

    private:
        // Keep a bit more history than the largest selectable window (30s).
        static constexpr uint64_t kKeepMs = 35000;

        struct Applied
        {
            uint64_t target;
            uint64_t timeMs;
            int durationMs;
        };
        struct Entry
        {
            std::vector<Applied> events;
        };

        static void Prune(Entry& e, uint64_t nowMs, uint64_t keepMs)
        {
            size_t drop = 0;
            while (drop < e.events.size() && nowMs - e.events[drop].timeMs > keepMs)
            {
                ++drop;
            }
            if (drop > 0)
            {
                e.events.erase(e.events.begin(), e.events.begin() + static_cast<std::ptrdiff_t>(drop));
            }
        }

        std::mutex mutex_;
        std::unordered_map<int, Entry> entries_;
    };
}
