# The menu music

Two loops, Ogg Vorbis, 48 kHz stereo (porpoise_sound.cpp leaves out anything else):

- `assets/sounds/menu-music.ogg`: Ruben's original, for the legacy looks.
  Re-encoded from the 160 kb/s file at `-q:a 2` (Oct 24, to make room for the second).
- `assets/sounds/menu-music-soundscape.ogg`: "Soundscape" by LOiME, the new look's.
  From the WAV Ruben sent (44.1 kHz, 2:38): the silence after its fade cut at 147.5 s,
  then the first 10 s laid over the last 10 s (the intro rising as the outro fades)
  and the file started at 10 s, so its end runs straight into its start. Then
  `ffmpeg -i loop.wav -ar 48000 -c:a libvorbis -q:a 2`.

The zip must stay under 31,457,280 bytes: the two together are about 3.3 MB.
