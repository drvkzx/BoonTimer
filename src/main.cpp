// Boon Output - Nexus addon for Guild Wars 2.
// Shows the boons YOU apply, in real time, in a window you can place anywhere.
//
// Data source: arcdps combat events forwarded by Nexus
// ("EV_ARCDPS_COMBATEVENT_LOCAL_RAW"). No game memory is read by this addon.
//
// IMPORTANT: the Nexus boilerplate below (AddonDefinition_t fields, API function
// names, NEXUS_API_VERSION) must match the Nexus.h version you build against.
// Start from the official Nexus addon template and compare; the parts marked
// [CHECK] are the ones most likely to differ between API versions.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "imgui/imgui.h"   // [CHECK] include paths as in the Nexus template
#include "nexus/Nexus.h"   // [CHECK]

#include "boon_tracker.h"

namespace
{
    AddonAPI_t* g_api = nullptr;

    // Payload of the Nexus arcdps bridge event. [CHECK] against Nexus' arcdps
    // integration docs/headers.
    struct ArcCombatArgs
    {
        void* ev;
        void* src;
        void* dst;
        char* skillname;
        uint64_t id;
        uint64_t revision;
    };

    bt::Tracker g_tracker;
    bt::Settings g_cfg;

    // overlay settings
    bool g_locked = false;      // start unlocked so you can place it right away
    float g_x = 400.0f;
    float g_y = 300.0f;
    float g_scale = 1.0f;
    bool g_posDirty = true;
    bool g_debugLog = false;

    // diagnostics (shown in the options panel; use them to verify WvW behaviour)
    std::atomic<uint64_t> g_eventsSeen{0};
    std::atomic<uint64_t> g_buffAppliesByMe{0};
    std::atomic<uint64_t> g_boonsCounted{0};

    std::string SettingsPath()
    {
        const char* dir = g_api->Paths_GetAddonDirectory("BoonOutput"); // [CHECK]
        std::filesystem::create_directories(dir);
        return std::string(dir) + "/settings.ini";
    }

    void SaveSettings()
    {
        std::ofstream f(SettingsPath());
        if (!f) return;
        f << "locked=" << (g_locked ? 1 : 0) << "\n"
          << "x=" << g_x << "\n"
          << "y=" << g_y << "\n"
          << "scale=" << g_scale << "\n"
          << "windowMs=" << g_cfg.windowMs << "\n"
          << "includeSelf=" << (g_cfg.includeSelf ? 1 : 0) << "\n";
    }

    void LoadSettings()
    {
        std::ifstream f(SettingsPath());
        std::string line;
        while (std::getline(f, line))
        {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            if (k == "locked") g_locked = std::atoi(v.c_str()) != 0;
            else if (k == "x") g_x = static_cast<float>(std::atof(v.c_str()));
            else if (k == "y") g_y = static_cast<float>(std::atof(v.c_str()));
            else if (k == "scale") g_scale = static_cast<float>(std::atof(v.c_str()));
            else if (k == "windowMs") g_cfg.windowMs = static_cast<uint32_t>(std::atoi(v.c_str()));
            else if (k == "includeSelf") g_cfg.includeSelf = std::atoi(v.c_str()) != 0;
        }
        if (g_scale < 0.5f || g_scale > 4.0f) g_scale = 1.0f;
        if (g_cfg.windowMs < 1000 || g_cfg.windowMs > 30000) g_cfg.windowMs = 5000;
        g_posDirty = true;
    }

    // Called by Nexus for every arcdps combat event involving the local player.
    void OnCombat(void* aEventArgs)
    {
        if (!aEventArgs) return;
        auto* args = static_cast<ArcCombatArgs*>(aEventArgs);
        if (!args->ev) return; // arcdps sends null ev for agent add/remove

        ++g_eventsSeen;

        auto* ev = static_cast<const bt::CbtEvent*>(args->ev);
        auto* src = static_cast<const bt::Agent*>(args->src);
        auto* dst = static_cast<const bt::Agent*>(args->dst);

        const bool selfIsSource = src && src->self != 0;
        const bool targetIsSelf = dst && dst->self != 0;

        if (selfIsSource && ev->buff && !ev->is_buffremove && ev->buff_dmg == 0 &&
            !ev->is_statechange && !ev->is_activation)
        {
            ++g_buffAppliesByMe;
        }

        if (g_tracker.OnCombat(*ev, selfIsSource, targetIsSelf, GetTickCount64(), g_cfg))
        {
            ++g_boonsCounted;
            if (g_debugLog)
            {
                char msg[128];
                std::snprintf(msg, sizeof(msg), "boon %u (%d ms) -> agent %llu",
                              ev->skillid, ev->value,
                              static_cast<unsigned long long>(ev->dst_agent));
                g_api->Log(LOGL_DEBUG, "BoonOutput", msg); // [CHECK]
            }
        }
    }

    void RenderRows(const std::vector<bt::Row>& rows, uint64_t /*now*/)
    {
        for (const auto& r : rows)
        {
            // Fade out as the last application gets older than the window.
            float age = static_cast<float>(r.msSinceLast) / static_cast<float>(g_cfg.windowMs);
            float alpha = age < 0.6f ? 1.0f : (age > 1.0f ? 0.35f : 1.0f - (age - 0.6f) / 0.4f * 0.65f);
            ImVec4 col(r.boon->r, r.boon->g, r.boon->b, alpha);
            ImGui::TextColored(col, "%s  x%d   +%.1fs   %.1fs ago",
                               r.boon->name, r.targets,
                               r.lastDurationMs / 1000.0f, r.msSinceLast / 1000.0f);
        }
    }

    void AddonRender()
    {
        const uint64_t now = GetTickCount64();
        auto rows = g_tracker.Snapshot(now, g_cfg);

        if (g_locked && rows.empty()) return; // nothing to show, stay invisible

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (g_locked)
        {
            // Click-through, no frame: just text floating on the screen.
            flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
        }

        const ImGuiCond cond = (g_locked || g_posDirty) ? ImGuiCond_Always : ImGuiCond_Appearing;
        ImGui::SetNextWindowPos(ImVec2(g_x, g_y), cond);
        g_posDirty = false;

        if (ImGui::Begin(g_locked ? "##BoonOutput" : "Boon Output - drag to move, lock in options##BoonOutput",
                         nullptr, flags))
        {
            ImGui::SetWindowFontScale(g_scale);

            if (rows.empty())
            {
                // Preview so you can place the window out of combat.
                ImGui::TextDisabled("Boons you apply will show here.");
                ImGui::TextDisabled("(counted: %llu applications)", static_cast<unsigned long long>(g_boonsCounted.load()));
            }
            else
            {
                RenderRows(rows, now);
            }

            if (!g_locked)
            {
                ImVec2 p = ImGui::GetWindowPos();
                if (p.x != g_x || p.y != g_y)
                {
                    g_x = p.x;
                    g_y = p.y;
                }
            }
        }
        ImGui::End();
    }

    void AddonOptions()
    {
        bool lockedBefore = g_locked;
        ImGui::Checkbox("Lock window (click-through, hides frame)", &g_locked);
        if (lockedBefore != g_locked) SaveSettings();

        if (ImGui::DragFloat("X", &g_x, 1.0f, 0.0f, 10000.0f, "%.0f")) g_posDirty = true;
        if (ImGui::DragFloat("Y", &g_y, 1.0f, 0.0f, 10000.0f, "%.0f")) g_posDirty = true;
        ImGui::SliderFloat("Text scale", &g_scale, 0.5f, 3.0f, "%.2f");

        int seconds = static_cast<int>(g_cfg.windowMs / 1000);
        if (ImGui::SliderInt("Target window (s)", &seconds, 1, 30)) g_cfg.windowMs = static_cast<uint32_t>(seconds) * 1000;
        ImGui::Checkbox("Count boons I apply to myself", &g_cfg.includeSelf);

        if (ImGui::Button("Save")) SaveSettings();
        ImGui::SameLine();
        if (ImGui::Button("Clear history")) g_tracker.Reset();

        ImGui::Separator();
        ImGui::Checkbox("Log counted events to Nexus log", &g_debugLog);
        ImGui::TextDisabled("Diagnostics (use these to check what arrives in WvW):");
        ImGui::Text("Combat events received: %llu", static_cast<unsigned long long>(g_eventsSeen.load()));
        ImGui::Text("Buff applications by you (any buff): %llu", static_cast<unsigned long long>(g_buffAppliesByMe.load()));
        ImGui::Text("Boons counted: %llu", static_cast<unsigned long long>(g_boonsCounted.load()));
    }

    void AddonLoad(AddonAPI_t* aApi)
    {
        g_api = aApi;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(g_api->ImguiContext)); // [CHECK]
        ImGui::SetAllocatorFunctions(
            reinterpret_cast<void* (*)(size_t, void*)>(g_api->ImguiMalloc),
            reinterpret_cast<void (*)(void*, void*)>(g_api->ImguiFree)); // [CHECK]

        LoadSettings();

        g_api->GUI_Register(RT_Render, AddonRender);          // [CHECK]
        g_api->GUI_Register(RT_OptionsRender, AddonOptions);  // [CHECK]
        g_api->Events_Subscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombat);
    }

    void AddonUnload()
    {
        g_api->Events_Unsubscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombat);
        g_api->GUI_Deregister(AddonRender);   // [CHECK]
        g_api->GUI_Deregister(AddonOptions);  // [CHECK]
        SaveSettings();
    }

    AddonDefinition_t g_def{};
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    // [CHECK] field names/types vs your Nexus.h; pick your own unique negative Signature.
    g_def.Signature = -0x42304F55;
    g_def.APIVersion = NEXUS_API_VERSION;
    g_def.Name = "Boon Output";
    g_def.Version.Major = 0;
    g_def.Version.Minor = 1;
    g_def.Version.Build = 0;
    g_def.Version.Revision = 0;
    g_def.Author = "you";
    g_def.Description = "Shows the boons you apply, live, anywhere on screen. Uses arcdps combat events via Nexus.";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    return &g_def;
}
