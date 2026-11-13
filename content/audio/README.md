# Audio content manifests

This folder holds the **committed** audio manifests. The actual sound files
live in `bin/audio/` (gitignored) and are installed locally:

- `bin/audio/sounds/<set folders>/*.wav|mp3|flac` — CC0 SFX packs (footsteps,
  dig, UI, hits, nature loops).
- `bin/audio/music/*.mp3` — user-supplied music (Ragnarok Online OST picks).
  **Never commit these files** (Gravity copyright).

Missing files are warned once at startup and stay silent — the game runs
fine without any audio files installed.

## Sound files layout (matches sounds.json `files` paths)

```
bin/audio/sounds/
  footstep/{grass,stone,wood,sand,snow,gravel,leaf,paws,hooves,barefoot}/variant_1..4.wav
  dig/{stone,soft,wood}/variant_1..2.wav
  dug/{stone,soft,wood}/variant_1..2.wav
  melee/{swing,hit}/variant_1..3.wav
  ranged/shot/variant_1..2.wav
  creature/{hurt,death}/variant_1..2.wav
  pickup/item/variant_1..2.wav
  eat/food/variant_1..2.wav
  equip/armor/variant_1.wav
  place/block/variant_1..2.wav
  splash/water/variant_1..2.wav
  ui/{click,open,close}/variant_1..2.wav
  ambient/{forest,wind,desert,ocean,cave,rain,torch}/loop_1.wav + stingers
```

Suggested CC0 sources (verify the license page when downloading):

- Kenney UI Audio — https://kenney.nl/assets/ui-audio (CC0)
- Kenney Impact Sounds — https://kenney.nl/assets/impact-sounds (CC0)
- OpenGameArt "Fantozzi's Footsteps" — grass/sand/stone/dirt/wood steps (CC0)
- OpenGameArt nature ambiences (wind, birds, crickets, rain, waves) — filter
  by CC0, e.g. https://opengameart.org/

## Music (RO OST selection)

Download from https://downloads.khinsider.com/game-soundtracks/album/ragnarok-online-original-soundtrack
(per-track MP3 links are free; bulk download needs a free account) and place
these files in `bin/audio/music/`:

- overworld: `1-08 Theme of Prontera.mp3`, `1-14 Theme of Payon.mp3`,
  `2-04 Be Nice'n Easy.mp3`, `2-05 One Step Closer.mp3`,
  `2-16 Morning Gloomy.mp3`, `2-02 Dreamer's Dream.mp3`
- travel: `1-23 Travel.mp3`, `1-25 Plateau.mp3`, `2-06 Brassy Road.mp3`,
  `2-01 Everlasting Wanderers.mp3`, `1-12 Streamside.mp3`
- forest: `1-03 Peaceful Forest.mp3`
- desert: `1-24 Desert.mp3`, `1-11 Theme of Morroc.mp3`
- caves: `1-19 Under the ground.mp3`, `1-16 Labyrinth.mp3`
- night: `1-07 Wind of Tragedy.mp3`, `2-14 Fear....mp3`

Playlists are mapped to contexts in `music_manifest.json` (longest contained
context key wins; `overworld` is the fallback, `night` overrides at night).
