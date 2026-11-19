# Music mod pack

A mod pack can replace the game's battle and exploration music with your own tracks (wish of DevJunior, 19.11.2026).
The files stay yours: nothing of them goes into the repository.

## Files

Put them in a folder `music` of a mod pack (`mods/<pack>/music/`, added through the app's menu, Mods), as OGG Vorbis,
MP3 or 16-bit PCM WAV, any sample rate; anything else the phone plays (AAC / M4A, Opus, FLAC, WebM) goes through
Android's own decoder. The file's content decides, not its name. The name says when it plays (upper or lower case):

| File | Plays |
|---|---|
| `village_explore` | in the village (rooms 1xx) |
| `village_battle` | in a fight in the village |
| `castle_explore`, `castle_battle` | the castle (2xx) |
| `island_explore`, `island_battle` | the island (3xx) |
| `explore`, `battle` | any zone without its own file |

Example: `mods/Remake Music/music/village_battle.ogg`. Each track loops; tracks change with a 1.6 s crossfade.

## When

- **Battle:** a Ganado has found Leon (seen or heard him, or another raised the alarm; up to 30 m), or another enemy
  (dogs, bosses) is within 12 m, for half a second; it ends eight seconds after none of that. Sneaking past unaware
  Ganados keeps the exploration track. The merchant, chickens, crows, fish, traps and vehicles do not count. Short
  breaks (a kick's or finisher's prompt, a menu for a moment) keep the track.
- **Exploration:** in a room where an enemy was seen since entering it, while the game itself plays room music. Save
  rooms, the merchant's rooms and rooms the game keeps silent keep the game's own music (or silence).
- **Never:** in cutscenes, movies, Leon's death and when the player is not Leon: the game's own music.
- While a track plays the game's music gives way (patch 0929); its sound effects and ambience stay. The tracks have
  their own slider on the app's AUDIO page, "Mod music"; the page's music button off silences them too.

The log (`music:` lines) tells when a battle starts and ends and which track plays.
