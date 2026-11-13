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

## Installed set (2026-11-13)

`third_party/asset_cache/audio-sources/install_sounds.py` (local, gitignored
workspace) downloads the packs below, converts to 44.1 kHz PCM wav, slices
multi-step takes into individual footfalls by transient peaks, synthesizes
the two wind loops, and lays everything out per `sounds.json`. Re-run it to
refresh `bin/audio/sounds/`.

| Set family | Source (all CC0 unless noted) |
|---|---|
| footstep grass/snow/wood, paws, barefoot, dig/place impacts | Kenney Impact Sounds (kenney.nl) |
| footstep stone/sand | Fantozzi's Footsteps (OGA, qubodup) |
| footstep snow/gravel | 42 Snow and Gravel Footsteps (OGA, qubodup) |
| footstep wood/leaf/gravel, dig soft/wood | Different steps... (OGA, TinyWorlds) |
| footstep hooves | Horse gallop on different surfaces (OGA; only the CC0 tracks clicky/grass/ground/muffled) |
| melee swing, ranged shot | Swishes Sound Pack (OGA, artisticdude) |
| melee hit, dug stone/soft | 100 CC0 SFX (OGA, rubberduck) |
| creature hurt/death, eat | 80 CC0 Creature SFX (OGA, rubberduck) |
| pickup, equip, ui open/close | Kenney RPG Audio (kenney.nl) |
| ui click | Kenney UI Audio (kenney.nl) |
| splash | 40 CC0 water/splash/slime SFX (OGA, rubberduck) |
| ocean loop | Calm ocean waves (Freesound, SamsterBirdies, CC0) |
| forest loop | Ambiance_Wind_Forest_Calm_Loop_Stereo (Freesound, Nox_Sound, CC0) |
| bird stingers | Ambient Bird Sounds (OGA, isaiah658) |
| crickets stingers | Cricket sounds (OGA, syncopika) |
| cave loop, drip stingers | Dripping water loop (OGA, qubodup) |
| rain loop | Rain (loopable) (OGA, ylmir) |
| torch crackle | Fire Crackling (OGA, AntumDeluge) |
| wind/desert loops | synthesized by install_sounds.py (no external license) |

The forest and ocean loops in `bin/audio/sounds/` are those CC0 recordings
converted to 44.1 kHz 16-bit PCM. Bird one-shots stay separate from the
forest bed.

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

Playlists are mapped to contexts in `music_manifest.json`. A downloaded
file matches its manifest entry when the letters and digits agree, so
khinsider names like `1-08. Theme of Prontera.mp3` satisfy
`1-08 Theme of Prontera.mp3`. Files that match nothing are added only to
the overworld playlist.

Music and biome ambience are separate. A forest, desert or cave context
with none of its own files leaves the music bus silent; the biome loop
keeps playing. `night` replaces the music playlist only when those files
exist and it is night. `overworld` plays in the menu and in biomes without
their own key.
