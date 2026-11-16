# Boon Output (Nexus addon for Guild Wars 2)

Shows the boons **you apply**, in real time, in a window you can put anywhere on screen.

```
Quickness  x5   +2.0s   0.8s ago
Alacrity   x5   +2.0s   1.1s ago
Might      x3   +10.0s  3.4s ago
```

- `x5` = distinct targets you hit with that boon inside the target window (default 5 s)
- `+2.0s` = duration of your most recent application
- `0.8s ago` = time since that application (rows fade as they age)

## How it gets its data

Nexus forwards arcdps combat events to addons (`EV_ARCDPS_COMBATEVENT_LOCAL_RAW`).
This addon only looks at buff-application events where **you** are the source and
the buff id is one of the 12 standard boons. It does **not** read game memory and
it does not use any of Reffect's code or approach.

Requirements: Nexus, and arcdps with Nexus' arcdps integration.

## Placing it

1. Load the addon. It starts **unlocked**: a small framed window appears. Drag it anywhere.
2. Fine-tune with the X / Y fields in Nexus > Options > Boon Output.
3. Tick **Lock window**. The frame disappears and the window becomes click-through.

Position and settings are saved to `addons/BoonOutput/settings.ini`.

## Build with GitHub (nothing to install on your PC)

GitHub compiles it for you on a Windows machine in the cloud (free for public repositories).

1. Create a free account at github.com, then **New repository** (name it e.g. `boon-output`, set it to **Public**, tick "Add a README" so the repo isn't empty).
2. In the repo click **Add file > Upload files**, drag in the `src` and `test` folders (and `README.md` if you like), then **Commit changes**.
3. Still in the repo, click **Add file > Create new file**. In the name box type exactly `.github/workflows/build.yml`
   (typing the slashes creates the folders), paste in the contents of `build.yml` from this package, then **Commit changes**.
   (Do it this way because dragging a folder that starts with a dot is unreliable in the browser.)
4. Open the **Actions** tab. A run called "Build BoonOutput.dll" starts by itself (or press **Run workflow**). It takes a few minutes.
5. When it has a green tick, open the run and download **BoonOutput** from the *Artifacts* section at the bottom. Unzip it to get `BoonOutput.dll`.
6. Put `BoonOutput.dll` in `Guild Wars 2/addons/` and load it from the Nexus window.

If the run goes red, open it, click the failed step, and paste the red error text into the chat. The most likely cause is a
`[CHECK]` line in `main.cpp` whose name differs in the real `Nexus.h`; the fix is usually one word. After you edit a file on
GitHub (pencil icon > Commit), the build runs again by itself.

The workflow downloads `Nexus.h` and ImGui from the official `RaidcoreGG/MouseLookHandler` addon on every build and prints the
commit it used. If that repo's layout changes, the first step fails with a clear message.

## Alternative: build on your own PC without Visual Studio (MSYS2 / MinGW, free)

1. Install MSYS2 from https://www.msys2.org and open the **MSYS2 MINGW64** shell (not "MSYS2 MSYS").
2. Install the compiler: `pacman -S mingw-w64-x86_64-gcc`
3. Make this folder layout:

   ```
   boon-output/
     src/main.cpp
     src/boon_tracker.h
     src/nexus/Nexus.h      <- copy from an official Raidcore addon, e.g. RaidcoreGG/MouseLookHandler (src/nexus/)
     src/imgui/...          <- copy the whole src/imgui folder from the same repo
   ```
   Use the ImGui copy from a Raidcore addon rather than the newest ImGui: Nexus shares its ImGui
   context with addons, so the versions should match.
4. In the MINGW64 shell, inside `boon-output`:

   ```
   g++ -std=c++17 -O2 -shared -static -Isrc -o BoonOutput.dll \
       src/main.cpp src/imgui/imgui.cpp src/imgui/imgui_draw.cpp \
       src/imgui/imgui_tables.cpp src/imgui/imgui_widgets.cpp
   ```
5. Compare the lines marked `[CHECK]` in `main.cpp` with your `Nexus.h` and fix any name that differs
   (the compiler will point at them). Rebuild.
6. Put `BoonOutput.dll` in `Guild Wars 2/addons/` and load it from the Nexus window.

The Nexus docs link a GCC + MSYS2 example (NexPad) if you want to compare. If the DLL fails to load, check
the Nexus log: a wrong `NEXUS_API_VERSION` or a missing export are the usual causes.

Visual Studio also works if you can get it (use the official Nexus addon template, same `[CHECK]` step).

`boon_tracker.h` (the logic) is tested here: `cd test && g++ -std=c++17 -I../src test_tracker.cpp -o t && ./t`.

## Check it works in WvW before you trust it

The Nexus glue in `main.cpp` has **not been compiled or run** (no Windows/Nexus toolchain was available
where this was written). Also, I have not confirmed that every buff-application event reaches addons in WvW.
In Options > Boon Output there are three counters:

- *Combat events received*: should rise in any fight
- *Buff applications by you*: should rise when you cast boons on allies
- *Boons counted*: what the overlay actually uses

If the first counter rises in WvW but the others stay at 0 while you are giving boons, WvW is not
delivering those events and there is nothing this addon can do about it.

## Notes

- ArenaNet does not approve or endorse any third-party tool and keeps the right to act on accounts. Use at your own risk.
- This shows information about your own boon output only. It is not a replacement for Reffect's effect/resource displays.
