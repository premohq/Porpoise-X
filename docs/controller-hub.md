# Controller Hub (the Wii controls overhaul)

The plan for the new Wii controller screen. Decided with Ruben on Oct 24, 2026,
from the concepts on the "Porpoise 4.6 Concepts" canvas, page "Move and
DualSense setup" (boards D, E, F, Controller maps, and the two Calibrate
boards). The first part is in the Move Test build of Oct 24 (see "Main
controller first"); the rest is still to build.

## Layout

- **D is the base:** setup cards across the top, then sections that fold open
  one at a time. A closed section still shows a one-line summary. The
  picture on the left follows the open section (how to hold it, the button
  map, the live camera, the swing graph).
- **F's three tiles** (Hold it, Calibrate, Feel) are the quick way in. "All
  settings" opens D. F is not a "simple mode", just the easier front door.
- Name: **Controller Hub**. The in-game menu gets a Controller Hub entry with
  the same layout.
- Every part works with a controller: the D-pad or stick moves between cards,
  section heads and rows; Cross opens and closes a section.
- Built in the current design first, then for all four themes and Light.

## Main controller first, then the second

(Oct 24: replaces the six combined setup cards.)

- **Main controller** (plays as the Wii Remote), each with its icon:
  DualSense, PlayStation Move, Wii Remote (Coming soon), DualShock 4
  (Coming soon).
- **Second controller** (plays as the Nunchuk), depending on the main one:
  - DualSense: None, the same DualSense, a second DualSense.
  - Move: None, a DualSense, the Navigation controller (Coming soon).
  - Wii Remote: a real Nunchuk (Coming soon, with the Wii Remote).
- No Beta or Alpha tags anywhere. Locked things just say Coming soon.
- Moving the controllers in the hold pictures (left/right, upright, sideways)
  comes later.

Built (Move Test 18, Oct 24, second pass): the hub rebuilt from D and F. The
controllers across the top; under them the three tiles (Hold it, Calibrate,
Feel) and All settings, which opens the folding sections with the picture on
the left following the open one. Ruben's diagrams for every picture
(tools/make-controller-diagrams.py), each control labeled beside it with a
line that leaves the control, turns, and runs to its label. The D-pad or a
stick hides the pointer until the controller is moved on purpose. Saving:
Just this game by default, or Every Wii game; calibration, the ball's color
and the Move itself are always every game's.

Still to build: the boxing grip, left / right hand, Quick moves, Swing
needed, Full tilt at, Measure my swings, the live camera picture, and the
Wii Remote picture lighting up beside the maps.

## How you hold it

- Grips: Auto, Both hands (the standard), Upright with the trigger toward
  the TV, Upright with the face toward you.
- New, optional (never the default): the **boxing grip**. Hands wrapped
  around both grips, the four fingers resting on the touch pad, the
  controller standing up and curving into the hands, sticks facing down
  under the hands. Needs its own axes and pose.
- Left hand / right hand flips the labels in the picture.
- Upright holds show the controller turned the way it's held.

## Buttons

- The labeled pictures from the concepts: Ruben's own controller diagrams,
  lines to each button, each label saying what the button does on the Wii.
  Pointing at a label lights the same button on a Wii Remote picture beside it.
- Button sets: Porpoise's (locked), Mine 1, Mine 2.
- Check the diagrams are free to ship before they go in.

## Pointer

- Aim with: Move + camera, Gyro, Touch pad, Right stick. Then Speed, Reach the
  edges and Steadiness (Steady, Balanced, Snappy).
- With the camera, the left side shows the live camera view tracking the ball.
  The PS3 Eye drawing sits on top of the TV picture.
- **Calibrate** sweeps the page away into full screen: point at the middle and
  hold, then the corners (five points), then free play to try it. The five
  points fill the whole screen, so a **Ready** screen comes first.
- Camera above or below the TV: it doesn't change gyro aiming. Once the camera
  corrects the aim it will (it sees the ball from above or below); the five
  points measure that too, and an Above / Below hint can help then.

## Ball color

- Move setups pick the ball's color from presets the PS3 Eye can track.
- The Move's dot in the island shows the ball's current color.
- Detection has to look for the chosen color (today's helper takes the
  biggest bright, strongly colored spot of any color).

## Motion and feel

For the Move and the DualSense, each with its own values:

- Swing needed, Swing power, Quick moves (Smooth to Instant), Full tilt at.
- Flick to shake: On / Off. Wii MotionPlus: Auto / On / Off.
- **Measure my swings:** set it down (drift), center, tilt all the way both
  ways (sets steering), three hard swings (sets Swing needed and Swing
  power), then a quick-moves test.
- Why: players said swings felt too hard to land and steering missed sudden
  moves.

## Sensor bar

Shown, locked, Coming soon (for real Wii Remotes).

## Saved setups

- Save for this game, or for every game. **Just this game** is the default.

## Players 2 to 4 (4.5 Beta 4)

- The switcher at the top right: tap L1 / R1 (a hold of R1 is still the
  centring), or point and press Cross. Player 1's page is the hub as before.
- Each player: what they hold (DualSense, PlayStation Move, Move + Navigation,
  Move + DualSense), and for a DualSense what it plays as (the same as
  player 1, or a Remote, Remote + Nunchuk, sideways, Classic, GameCube). A
  Move is the Remote, with the Nunchuk when it has a second controller.
- Saved for every Wii game (wii_p2_hold..wii_p4_hold, wii_p2_as..wii_p4_as).
  Pointer speed, steadiness and swing strength are player 1's for everyone.
- Players 2 to 4's Moves and Navigation controllers come from REXCRAFT88's
  Move Management over local UDP (Move 9976 + player, Navigation 9980 +
  player), listened to in Wii games and while the hub is open. Player 1's
  Move is the PS5's own when it has one; Move Management's otherwise.
- In Two controllers, player 2's DualSense is player 1's Nunchuk; players 3
  and 4 still play.

## Navigation controller

- It has no motion sensors. When it's the Nunchuk, Nunchuk shake has to be a
  button.
- Proposed layout: stick = Nunchuk stick, L1 = C, L2 = Z, D-pad, Cross = −,
  Circle = +, PS = Home.

## From REXCRAFT88's branch

Credited in Settings > About > Credits (right after Ruben's own lines) and in
CREDITS.md under "PlayStation Move", whatever is or isn't taken. The code
comments name his branch wherever an idea came from it. Taken in the Oct 24
build: 1 to 5 and 8 below.

`REXCRAFT88/Porpoise-Dolphin-Emulator-for-PS5`, branch
`feature/move-nav-integration` (built on the public 4.0, not on test/move).
His Move and Navigation come from a separate payload, Move Studio (based on
AnyPad PS5), which runs its own Bluetooth host and its own PS3 Eye process,
and sends input to Porpoise over local UDP. His own notes say that while it
runs, a DualSense that turns off can't reconnect until rest mode or a
restart. Our Move goes through the console's own Move support instead, so that
doesn't happen, and rumble and the light work.

Both landed on the same axes (Move x, Move z, −Move y) and the same idea of
feeding the Move through the DualSense's aiming path.

Worth taking:

1. Recentering also relearns the gyro's drift when the Move is held still
   (he averages the last 32 gyro samples and skips it if they vary).
2. Reset the Move's motion state (drift, fusion, smoothing, center) when it
   disconnects and comes back.
3. Clear the pointer smoothing when centering, so it lands in the middle at once.
4. Keep the center from the setup screen when the game starts.
5. Camera: color-specific detection with shape scoring; "ignore this light"
   areas for lamps and wall lights; small preview frames from the helper for
   the live view (his are 160 × 120).
6. A Nunchuk plug / unplug toggle in-game (his Navigation PS button does it).
7. Pick Remote + Nunchuk automatically when a second hand is connected.
8. Ball colors per player: blue, red, green, magenta (his tested set); cyan,
   yellow, orange and violet still to test.

Keep ours:

- The console's own Move connection (no Bluetooth takeover).
- Our button layout. His has no Minus and no Home: Move A, T B, Cross 1,
  Square 2, Triangle +, Select recenters, Circle a second B.
- The swing soft knee and Swing strength, and the gyro scale fitted from gravity.

Not now:

- His Bluetooth host, until the DualSense reconnect problem is fixed.
- Opening the setup screen every time a Move is connected (it would undo the
  Options quick start).

Open questions:

- A wired Navigation through the helper, the way the Eye works: untested.
- More Moves (a second hand, other players) through the console's own path,
  which takes a user and an index: untested.
- His Porpoise binds UDP ports; ours got errno 13 when binding. Only matters
  if Porpoise ever reads his bridge.
