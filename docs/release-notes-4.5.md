<p align="center"><img src="https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/raw/Main/docs/images/banner.png" alt="Porpoise" width="100%"></p>

**Porpoise 4.5**

A new look, PlayStation Move motion controls, up to four players in Wii games, Triforce arcade games, and VRR.

### The new look
- **A new Porpoise look** built around your collection: real disc cases you move through with the stick, in **Light**, **Dark** or **OLED**
- **The side menu:** press **Triangle**, or swipe in from the right edge of the touch pad, to reach Settings and everything else from anywhere
- **The island**, top right: your controllers, their batteries, the camera and the time. Swipe down on the touch pad to open it
- **New menu music**, "Soundscape" by LOiME, and a new boot animation
- The classic look is still here as **Porpoise Legacy**, along with **Revolution** and **Star Cube**

### PlayStation Move
- **Play Wii games with a PlayStation Move as the Wii Remote:** aiming, swings and rumble on the Move
- **Both Move models work**, the original and the newer one
- **A PS3 Eye camera keeps your aim from drifting.** Pick the ball color it tracks in the Controller Hub
- **Point at Porpoise's menus with the Move:** hold Start for a second

### Controller Hub
- **The Wii controller setup, rebuilt:** pick what you're holding, then what it plays as. Also in the in-game menu
- **Up to four players:** each player gets a page (tap **L1 / R1**). Pick what they hold (DualSense, PlayStation Move, Move + Navigation, Move + DualSense) and what their DualSense plays as. Saved for every Wii game
- **The Navigation controller** as the Nunchuk, and Moves for players 2 to 4, through **Move Management by REXCRAFT88**
- The pointer follows a tilted controller all the way round

### Triforce arcade games (beta)
- Your own Triforce dumps show in the library with your GameCube games, marked **Triforce**
- **Touch pad** inserts a coin, **L1** opens the test menu, **L3 + R3** is the service button
- Put your own **`segaboot.gcm`** in `/data/porpoise/bios/` for the test menu. Some games need it to get past their startup checks; if a game still won't start, use a patched copy of it

### Display
- **Refresh Rate** in Settings → Video: **Fixed 120 Hz**, or **VRR (beta)**, where each frame shows the moment it's ready, up to 120 Hz
- **Frame Generation works with VRR on**, 60 to 120 included
- **Display** in Settings → Video shows what your TV is running at
- **The menus run at 120 Hz** on a 120 Hz screen
- Frame Generation now says when your setup keeps it from running

### Faster and steadier
- Several games run faster or steadier, and Auto Boost's tested picks now apply by themselves
- Less stutter in games that stream from virtual memory
- Snappier menus and controller setup
- Every menu translated, the new ones included
- A **Performance Profiler** (Settings → About) for detailed logs of a slow game

> [!NOTE]
> Found a bug? **Settings → About → Report a Bug** saves Porpoise's logs. Send them with the game's name on the **[RIPALDA Discord](https://discord.gg/GgDE5Vynyu)**.

Thanks to **REXCRAFT88** for Move Management and his Move work, which makes multiplayer Move and the Navigation controller possible.

Porpoise is free. If you'd like to support it: **[ko-fi.com/ripalda](https://ko-fi.com/ripalda)**

Keep sending your logs. They're how every build gets better.

## Install or update

**Use ShadowMountPlus 1.7 beta 4 or newer.** With older versions, Porpoise can close the moment it opens on some setups (firmware 12.x with the Lapy JB Daemon or LegacyJB).

**From 2.0 or later:** **Settings → About → Updates**.

**By hand:** download **`Porpoise-4.5.zip`**, delete `/data/homebrew/PPSA99764/` on the PS5, and copy the **`PPSA99764`** folder into **`/data/homebrew/`**. With FTP, use binary transfers (FileZilla: Transfer → Transfer type → Binary), or some text files won't copy. Your games, saves and settings in `/data/porpoise/` are kept.

Porpoise contains no games, BIOS or firmware. Play only games you own. Not affiliated with Nintendo, Sony or RetroAchievements. Licenses are in `licenses/README.txt` inside the zip.

`Porpoise-4.5.zip` SHA-256: `e772faa79b9e9609d23ff0e82ebc7a8b2156594e97f34aed7d6ac3cbb3f8caeb`
