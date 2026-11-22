# Settings and files

[Back to the README](../README.md)

## Settings

Open them with **+** > Settings. They open on a main page with three
pages: Playback, Display and About. **Playback** has:

- a sleep timer,
- playing the last station again when KaiRadio opens,
- turning the screen off while a station plays.

**Display** has:

- the language (see [Languages](languages.md)),
- the accent color (or one taken from the station's logo),
- a background image of your own, with as much blur as you like,
- a 12- or 24-hour clock,
- the globe or a flat map (World view),
- the tab to start in.

**About** shows the version, credits and licenses.

Backgrounds are the JPEG or PNG images you put in the `Backgrounds` folder of
KaiRadio's `config` folder (for example
`sdmc:/switch/kairadio/config/Backgrounds/`). A new image shows the next time
Settings opens.

## Where KaiRadio keeps its files

Favorites, recent stations, settings, backgrounds, added languages (`lang`)
and the audio log are kept in a `config` folder next to `KaiRadio.nro`, in its
own folder:

| Where `KaiRadio.nro` is | Its files |
|-------------------------|-----------|
| `sdmc:/switch/KaiRadio.nro` | `sdmc:/switch/kairadio/config/` |
| `sdmc:/switch/kairadio/KaiRadio.nro` | `sdmc:/switch/kairadio/config/` |
| `sdmc:/brewstore/apps/kairadio/KaiRadio.nro` | `sdmc:/brewstore/apps/kairadio/config/` |

Before 1.0.0-rc2 they were in `sdmc:/config/kairadio`. The first time a newer
KaiRadio opens, it copies them over (and leaves that folder as it is, so you
can delete it once everything is there).
