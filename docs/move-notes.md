# PlayStation Move on PS5: notes (test branch)

How the console's own Move library (libSceMove, PS5 build) reaches a Move,
read from the player's console for compatibility. No Sony code is used.

- Init: `open("/dev/hid", O_RDONLY)`, `sceMbusInit()`.
- Open (user, type, index): `sceMbusResolveByUserId(5, user, 1)` gives the
  Move's device id (device type 5 = Move). A failure there is
  `0x80ee0006`, the error a payload gets.
  Then `ioctl(fd, 0x800c4802, {device, 3, 1})` returns the HID handle, and
  `sceMbusAddHandle(5, user, handle)` registers it.
- Read: `ioctl(fd, 0xC0204834, {i32 handle, void *buf @8, u32 16 @16, i32 *flag @24})`,
  up to 16 reports of 64 bytes. 0xC0184852 (kind 2/3) reads the Move's stored calibration.
- Output (light, rumble): `ioctl(fd, 0x80104837, {i32 handle, void *report @8})`.
- Close: `sceMbusRemoveHandle(5, handle)`, `ioctl(fd, 0x80084803, {3, handle})`.
- An app can't load libSceMove: /system isn't visible to it (0x80020002), and
  the module service has no Move entry for PS5 apps.
- Input report (64 bytes per entry from 0xC0204834; the Move's own report starts
  at byte 9): +1 Select 0x01, Start 0x08; +2 Triangle 0x10, Circle 0x20,
  Cross 0x40, Square 0x80; +3 PS 0x01; +4 Move 0x40, T 0x80 (low nibble a
  counter); +5 trigger 0-255; +12 battery; +13 accelerometer then gyro, two
  samples each, 16-bit with 0x8000 as zero. Same layout as the PS3 Move's.
- The PS3 Eye from an app: libSceUsbd isn't offered to apps (its imports stay
  empty and the first call jumps to 0). The camera's FreeBSD device file
  (/dev/ugenB.A) works instead: USB_DO_REQUEST for control, USB_FS_* on
  endpoint 0x81 for the video.
- The camera's /dev/ugen node is hidden from apps too (only the Wi-Fi/Bluetooth
  chip's shows), so the Eye runs in a helper payload (tools/eye-helper) that
  Porpoise hands to the ELF loader. An app may not listen on a port (bind:
  errno 13), so the helper listens on TCP 127.0.0.1:47710 and Porpoise
  connects; it sends the ball's position per frame and stops when Porpoise
  closes the connection.
- Light and rumble: `ioctl(fd, 0x80104839, {i32 handle, void *buf @8})`, buf
  `{1, r, g, b, 1, rumble, 0, 0}` (byte 0: set the light, byte 4: set rumble).
  Sent again every 1.5 s so the ball stays lit.
- Player 1's Wii Remote (src/porpoise_move.cpp): Move A, T B, Cross 1,
  Circle 2, Start +, Select -, PS Home, Square re-centres the pointer. The
  pointer is the ball's place in the camera's picture (mirrored), a screen
  half being ten of the ball's radii; the Move's accelerometer and gyro go to
  the core in the Remote's axes (x left, y back, z up). The DualSense stays
  the Nunchuk.
- Test 14: the pointer comes from the Move's own motion, through the same
  read_motion as a DualSense's gyroscope (its own Slot, Square held as R1),
  so the Wii Remote Setup measures the screen for it. Samples go over in a
  DualSense's axes held flat: (x, y, z) = (Move x, Move z, -Move y). The
  gyroscope's counts-to-rad/s (sign and size) are fitted from how fast
  gravity turns in the accelerometer. Settings > Wii: PlayStation Move (Test).
  The camera is off unless /data/porpoise-move-camera exists.
- The ball's color (Settings: Move Ball Color; Magenta, Blue, Red, Green, Cyan,
  Yellow, Orange, Violet) is what the light is sent and what the PS3 Eye looks
  for: the helper only takes pixels within 45 degrees of that hue. Porpoise
  tells the helper over the same TCP connection: 'a'..'h' the color, 'I' note
  the other lights now (sent while the Move's light is off for a moment), 'X'
  forget them, 'S' stop. The noted lights are boxes on the half-size picture,
  kept in /data/porpoise-eye-ignore.txt; the helper answers "ignoring N
  lights". After REXCRAFT88's Move Studio (colored spheres, ignored wall
  lights).
- Centering (Square held, R1 on a DualSense, the setup's middle point) held
  still also takes the gyroscope's drift from its last 32 readings; the pointer
  snaps to the middle; a Move back after more than 2 s starts its motion
  afresh; and a game that starts within 5 minutes of a centring, held the same
  way, keeps that centre. All after REXCRAFT88's feature/move-nav-integration.

## Oct 24: swings, and what the camera does

- Swings: the game now gets the strongest sample since the last poll, not
  the newest (a swing's peak falls between polls). The Move's swing curve is
  linear up to 1.6 g (times Swing strength), then eases toward 3.4 g (it
  stopped at 2.6 before, under what Wii tennis counts as a swing).
- The camera does not steer the pointer: aiming is the Move's own motion. The
  helper now sends a 160 x 120 picture (protocol: 'V' / 'v' turn it on and
  off; packet type 4, then 57,600 bytes of RGB) ten times a second while a
  screen shows it (the Controller Hub, the five points).

## The debug folder (Move Test 18 (10))

/data/porpoise/debug, laid out in src/porpoise_debuglog.hpp: app/, crashes/,
controllers/ and games/<game> [<id>]/<date-time>/ (game.log, motion-*.csv,
move.csv, camera.csv, core.log, trace.txt). Settings > About > Debug Logs;
Save a Report gathers the newest. On this branch Debug Logs aren't turned off
at load (ui_settings.cpp); the release must turn them off again.
