# aviary — command manual

Tiny pixel birds that carry letters across your desktop, and between two
laptops. This is the complete command reference.

---

## Install

```bash
git clone https://github.com/moizxsec/aviary.git
cd aviary
./install.sh
```

Pulls build dependencies, compiles, installs `aviary` to `~/.local/bin`, and
registers autostart so the daemon comes back after a reboot. **Open a new shell
afterwards** so `~/.local/bin` is on your `PATH`.

By hand instead:

```bash
make                       # build
make install               # to ~/.local/bin
make install PREFIX=/usr/local
make uninstall             # removes the binary and both autostart entries
```

---

## Everyday

### `aviary`

Write a line, press enter. A bird lifts off your screen carrying it, and the
same letter arrives on the other laptop as a bird flying in.

```bash
aviary
aviary --bird owl          # a different bird, just this once
aviary --from "someone"    # override the name on the letter
aviary --local             # fly it here, do not send it anywhere
```

Starts the daemon itself if it is not already running. Nothing to set up.

`aviary compose` and `aviary write` are the same command spelled out.

### `aviary send "text"`

Fly a bird on **your own screen only**. Nothing is sent to anyone. This is the
one to use when you are trying things out.

```bash
aviary send "testing"
aviary send "testing" --bird phoenix --from muez
```

---

## Pairing the two laptops

Done once, ever.

### `aviary invite`

On your laptop. Mints a pairing code and prints it.

```bash
aviary invite --name muez --bird owl
```

```
  This laptop is ready.

      A50T-4Z40-6EHW-JG4R-F8TZ-TCXD
```

- `--name N` how you are signed at the bottom of your letters
- `--bird B` the bird you usually send

### `aviary join <code>`

On her laptop. One command, once.

```bash
aviary join A50T-4Z40-6EHW-JG4R-F8TZ-TCXD --name her --bird pigeon
```

Case does not matter and the dashes are optional. `I` and `L` are read as `1`,
`O` as `0`, so a hand-copied code still works.

Both sides start their own daemon afterwards. Neither of you types the code
again.

### `aviary link <topic> <passphrase>`

The manual alternative, if you would rather choose the relay topic yourself
instead of having one derived for you.

```bash
aviary link my-own-topic-name "a shared secret" --name muez --bird owl
```

---

## The birds

| name | what it does |
|---|---|
| `pigeon` | lands, walks over with the head-hold stride, waits to be noticed |
| `phoenix` | hovers, drops the letter, burns down to smoke |
| `owl` | flies into the far wall, tumbles to the floor, shakes himself off, then delivers |
| `swallow` | arrives soaked, trailing water, shakes dry before handing it over |

`errol` is an alias for `owl`, `rain` for `swallow`, `dove` for `pigeon`.

Each hands over a different letter: scorched, clean and bright, creased and
smudged, or damp with the ink run.

---

## The daemon

It starts itself when you send, and on login. You should not normally need any
of this.

```bash
aviary restart             # bounce it
aviary stop                # stop it
aviary daemon              # run in the foreground — blocks this terminal
aviary daemon --pixel 3    # chunkier sprites
aviary listen              # watch the relay by hand, without the overlay
```

`--pixel N` is device pixels per sprite pixel. `2` is the default. `1` is
oneko's original one-to-one.

---

## Reading a letter

A letter waits until you click **let it go**. That pill is the only part of the
panel that catches a click — everything else falls through to whatever is
behind it, so nothing is ever trapped under a piece of paper.

If you never click it, it gives up on its own after three minutes.

A phoenix letter does not fade when you let it go. It catches fire.

---

## Files

| path | what |
|---|---|
| `~/.config/aviary/config` | topic, encryption key, your name, your bird. Mode `0600` |
| `~/.config/aviary/mark` | id of the last letter received, so none repeat |
| `~/.config/aviary/since` | fallback resume point, set when you pair |
| `$XDG_RUNTIME_DIR/aviary.sock` | the local socket the daemon listens on |
| `~/.config/autostart/aviary.desktop` | starts it on login |
| `~/.config/systemd/user/aviary.service` | same, where there is a user session |

---

## Offline and reboots

ntfy.sh holds messages for about **twelve hours**. A letter sent to a laptop
that is switched off waits for it and arrives the moment it boots. Longer than
that and the relay drops it, silently.

The resume point is the id of the last letter received, so nothing repeats
across reboots and nothing is skipped.

---

## Privacy

Everything is sealed with **AES-256-GCM** before it leaves. The key is your
pairing code run through 20,000 rounds of SHA-256; the relay topic is a
*different* hash of the same code, so the topic gives nothing away even though
anyone may subscribe to it. ntfy.sh only ever stores base64 ciphertext.

A wrong code fails the GCM tag and the letter will not open.

The one thing the relay does learn is that a topic is active, and roughly when.
Self-hosting ntfy removes even that.

---

## Looking at the birds

Offline frame dumps. No X server needed.

```bash
aviary render DIR 2 owl        # the whole delivery, frame by frame
aviary render DIR 2 owl depart # the departure flight instead
aviary strip out.png 2 owl     # nine key beats tiled into one image
aviary sheet out.png           # phoenix poses, zoomed
aviary psheet out.png          # pigeon poses: the stride, the flare
aviary osheet out.png          # owl poses: the beat, the crash, the head
aviary sizes out.png           # sprite size next to a real 32x32 oneko box
make check                     # renders all four birds; fails if a beat is missing
```

---

## Troubleshooting

**Nothing flies.** Is a daemon up? `aviary restart`. Check `DISPLAY` is set.

**"no daemon on this machine, so nothing flew here"** — the letter *was* sent,
it just did not draw locally. `aviary restart`.

**"a letter arrived that this passphrase cannot open"** — the two laptops are
on different codes. Run `aviary invite` again and re-join.

**Letters stopped arriving.** `aviary listen` in a terminal shows the relay
directly, without the overlay in the way.

**Under Wayland** the overlay runs through XWayland but cannot sit above
native Wayland windows. `oneko` has the same limitation.
