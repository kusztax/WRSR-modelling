# Workshop item

`item/` is the Workshop item's content, without the plugin itself (the deploy
script adds `plugins/wrsr_rail_mod.dll` and `.ini`).

| File | What |
|---|---|
| `RailPriority_On.txt` + `.png` | Options → Scripts button: priority ON (stops the flag) |
| `RailPriority_Off.txt` + `.png` | Options → Scripts button: priority OFF (starts the flag) |
| `RailPriority_OffFlag.txt` | the flag: a script that only sleeps; while it runs, the priority is off in this save |
| `SOVIETInstructions.txt` | the game's script header, © 2017 Michal Kuchárik (Tau); every script includes it |

`art/*.svg` are the icon sources; the PNGs in `item/` are rendered from them
at 512 × 512.

The scripts show a notification whose texts are language ids 1957001–1957003;
`src/lang.cpp` serves them in the game's language.
