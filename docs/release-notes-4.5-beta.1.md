<p align="center"><img src="https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/raw/Main/docs/images/banner.png" alt="Porpoise" width="100%"></p>

**Porpoise 4.5 Beta 1**

A big one. A new look, PlayStation Move motion controls, a rebuilt Wii controller setup and a lot of speed work. It's a beta, so some of it is still being finished, and your logs decide what gets fixed first.

### The new look
- **A new Porpoise look** built around your collection: real disc cases you move through with the stick, in **Light**, **Dark** or **OLED**
- **The side menu:** press **Triangle**, or swipe in from the right edge of the touch pad, to reach Settings and everything else from anywhere
- **The island**, top right: your controllers, their batteries, the camera and the time. **Swipe down on the touch pad** to open it. Slide your finger across the pad to pick a shortcut (Memory Cards, Settings, Online, Screenshots), then click the pad to open it. Swipe up to close it
- **Settings regrouped** into fewer tabs, with quick settings in reach
- **New menu music**, "Soundscape" by LOiME, and a new boot animation
- The classic look is still here as **Porpoise Legacy**, along with **Revolution** and **Star Cube**. A first-start screen lets you pick

### PlayStation Move
- **Play Wii games with a PlayStation Move as the Wii Remote:** aiming, swings and rumble on the Move
- **A PS3 Eye camera keeps your aim from drifting.** Pick the ball color it tracks in the Controller Hub
- **Point at Porpoise's menus with the Move:** hold Start for a second in the library or the in-game menu
- Swinging and aiming feel good. **Calibration is still a work in progress**, so tell us how it goes in your room

### Controller Hub
- **The Wii controller setup, rebuilt:** pick what you're holding, then what it plays as. Also in the in-game menu
- **Move + DualSense:** the Move as the Wii Remote, the DualSense as the Nunchuk, in either hand
- **One DualSense as the Wii Remote and Nunchuk**, or two DualSense controllers
- **Square** applies, **Triangle** resets the tab

### Navigation controller (alpha)
- **A PlayStation Navigation controller can now be the Nunchuk** next to a Move: its stick is the Nunchuk's, **L1** is C and **L2** is Z
- The PS5 doesn't support the Navigation controller itself, so it connects through **REXCRAFT88's Move Studio**, which has to be running
- I haven't been able to test this one myself yet. It's switched on so you can try it: pick **Navigation** under the second controller in the Controller Hub. If it doesn't work, send your logs

### Faster and steadier
- **Punch-Out!!** runs at full speed, menus and fights included
- **TimeSplitters 2** holds 60
- **Mario Superstar Baseball** is steadier
- **F-Zero GX** runs better
- **Pokémon XD** gets its Auto Boost fixes
- **Auto Boost fixes now actually apply**, and Porpoise's tested picks apply by themselves
- **Less stutter in games that stream from virtual memory**, and smoother camera changes in **Star Wars Rogue Squadron II**
- A **Performance Profiler** (Settings → About, with Debug Logs on) for anyone who wants to send us detailed logs of a slow game
- The app itself is quicker to move around and steadier

### Still in progress
- **Star Wars Rogue Squadron II:** stops at the end of the first mission
- **Resident Evil:** black screen on New Game
- **Eternal Darkness:** stops after the opening credits

These need more testing. Logs from these games help most.

> [!NOTE]
> Found a bug? **Settings → About → Report a Bug** saves Porpoise's logs. Send them with the game's name on the **[RIPALDA Discord](https://discord.gg/GgDE5Vynyu)**.

Thanks to **REXCRAFT88** for his Move Studio work, which shaped the Move calibration and makes the Navigation controller possible.

Porpoise is free. If you'd like to support it: **[ko-fi.com/ripalda](https://ko-fi.com/ripalda)**

Keep the logs coming. Every one helps us get the final 4.5 right, and we'll be posting updates as fixes land. Have a great Porpoise 4.5 launch!

## Install or update

**Use ShadowMountPlus 1.7 beta 4 or newer.** With older versions, Porpoise can close the moment it opens on some setups (firmware 12.x with the Lapy JB Daemon or LegacyJB).

**From 2.0 or later:** **Settings → About → Updates**, with **Beta Updates** on.

**By hand:** download the zip, delete `/data/homebrew/PPSA99764/` on the PS5, and copy the **`PPSA99764`** folder into **`/data/homebrew/`**. With FTP, use binary transfers (FileZilla: Transfer → Transfer type → Binary), or some text files won't copy. Your games, saves and settings in `/data/porpoise/` are kept.

Porpoise contains no games, BIOS or firmware. Play only games you own. Not affiliated with Nintendo, Sony or RetroAchievements. Licenses are in `licenses/README.txt` inside the zip.
