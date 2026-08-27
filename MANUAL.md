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

Pulls build dependencies (cairo, X11, Xext, **Xrandr**, curl, OpenSSL),
compiles, installs `aviary` to `~/.local/bin`, and registers autostart so the daemon comes back after a reboot. **Open a new shell
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

The same thing as `aviary`, in one line, without the prompt. The bird comes to
the cursor for it, the same way. Once you are paired it goes to the other
laptop.

```bash
aviary send "on my way"
aviary send "on my way" --bird phoenix --from muez
aviary send --local "testing"      # send nothing; show what she would see
```

`--local` sends nothing and flies the **arriving** bird here instead — letter,
pill and all. It is the half of this you never see from the sending end, and
the one to use when you are trying things out.

Before this, `send` was local-only always: a command called send that did not
send, which from the terminal is indistinguishable from one whose letters are
being lost.

---

### `aviary status`

What is running, what is paired, and what is wrong. Run this first when
something is not working.

```
  daemon      running
              pid 3412
  paired      yes — as muez, usual bird owl
  topic       av-df69e64cc1a2c56e039909b3
  resume      from message kMg9n4lFsPGs
  relay       reachable — 1 letter still held for this topic
  display     :0 — root 3840x1200, 2 screens
              eDP-1      1920x1200+0+0       (primary)
              HDMI-1     1920x1080+1920+120
  at login    autostart entry present
              systemd unit enabled, running
  log         ~/.config/aviary/daemon.log
              ...the last few lines
```

Worth knowing how to read:

- **daemon NOT running** — nothing can be delivered here, whatever the other
  laptop does.
- **relay ... N letters still held** during a silence means the letters are
  arriving at the relay and the listener is the problem, not the sender.
- **at login ... MISSING** is why it worked until the first reboot.

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
aviary status              # what is running and what is wrong
aviary restart             # bounce it
aviary stop                # stop it
aviary daemon              # run in the foreground — blocks this terminal
aviary daemon --pixel 3    # chunkier sprites
aviary listen              # watch the relay by hand, without the overlay
```

`--pixel N` is device pixels per sprite pixel. `2` is the default. `1` is
oneko's original one-to-one.

It writes `~/.config/aviary/daemon.log`, because started from a login autostart
entry there is nowhere else for it to say anything.

At login it may come up before the X server does, so it waits up to a minute
for one, and tries `:0` and `:1` if `DISPLAY` is not set at all.

---

## One bird, two desks

Sending is not two animations at once. It is one bird, seen twice.

Press enter and the bird comes down to **the line you typed on** — the letter
rolls itself up there, on the cursor, not in the middle of the screen. It ties
it on, and carries it off the edge.

Only then does anything happen on the other laptop. The letter goes to the
relay straight away, but it is stamped with how long this bird is still on
this screen, and the far end holds it until that time is up. Two birds moving
at the same moment on two desks reads as a copy of a bird; one leaving and
then the other arriving reads as the same one.

The wait is the sending bird's own flight — 9s for a phoenix, about 11s for an
owl — plus `travel`, which is the gap across the room and is yours to set:

```
# ~/.config/aviary/config
travel=2.5
```

`0` makes it arrive the moment the other bird is out of frame. Anything up to
two minutes is allowed. `aviary status` prints the current value.

Time the relay spent carrying the letter counts towards the wait, so a slow
network shortens the hold rather than adding to it. If the two clocks disagree
badly the whole wait is used instead, which is late rather than wrong.

The one thing this cannot do is start the clock from her side. If her laptop
is asleep the letter waits at the relay, and her bird flies in when she comes
back — as it should.

---

## Two screens

Both screens are one X screen as far as X is concerned: the root window is the
two of them side by side. The overlay covers **one monitor**, picked per
delivery — the one holding the terminal you typed in, or the one under the
pointer for a letter arriving. Otherwise the letter lands in the middle of the
root window, which on a dual-monitor desk is the seam between the two.

Docking, unplugging and rotating need nothing done: the geometry is looked up
fresh for every letter.

`aviary status` lists the monitors it can see.

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
| `~/.config/aviary/config` | topic, encryption key, your name, your bird, `travel`. Mode `0600` |
| `~/.config/aviary/mark` | id of the last letter received, so none repeat |
| `~/.config/aviary/since` | fallback resume point, set when you pair |
| `~/.config/aviary/pid` | the running daemon, so `stop` kills it and not you |
| `~/.config/aviary/daemon.log` | what the daemon said, including at login |
| `$XDG_RUNTIME_DIR/aviary.sock` | the local socket the daemon listens on |
| `~/.config/autostart/aviary.desktop` | starts it on login |
| `~/.config/systemd/user/aviary.service` | same, where there is a user session |

---

## Offline and reboots

ntfy.sh holds messages for about **twelve hours**. A letter sent to a laptop
that is switched off waits for it and arrives the moment it boots. Longer than
that and the relay drops it, silently.

The resume point is the id of the last letter received, so nothing repeats
across reboots and nothing is skipped. Pairing clears it, because an id from
the old topic means nothing in the new one — handed to the relay it is not an
error, it simply brings back everything the topic is still holding.

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

**Start with `aviary status`.** It checks everything below in one go.

**Nothing flies.** Is a daemon up? `aviary restart`. Check `DISPLAY` is set.

**It stopped working after a reboot.** `aviary status` — look at the *at login*
lines. If the autostart entry is missing, run `./install.sh` again. If it is
there, `~/.config/aviary/daemon.log` says what happened when it tried.

**The letter lands between two screens.** Fixed: the overlay now sits on one
monitor. If it is on the wrong one, it follows the pointer for arriving
letters — move the mouse to the screen you are looking at.

**The bird collects from the middle, not from my cursor.** The terminal is
asked where its cursor is and has 200ms to answer; a few do not. The daemon
log says which happened. `tmux` and `screen` answer for the pane, not the
window, so the spot is off by however the panes are arranged.

**Her bird arrives too soon, or too late.** `travel=` in the config, in
seconds. It is added to the sending bird's own flight time, not used instead
of it.

**I can send but nothing comes back.** Almost always a daemon that was left
running across a re-pairing, watching the topic it started with. It notices on
its own now; `aviary restart` settles it either way. `aviary status` will show
letters held at the relay that never arrived.

**"no daemon on this machine, so nothing flew here"** — the letter *was* sent,
it just did not draw locally. `aviary restart`.

**"a letter arrived that this passphrase cannot open"** — the two laptops are
on different codes. Run `aviary invite` again and re-join.

**"let it go" does not respond.** Fixed, in three places: the pill's clickable
rectangle was recorded where the panel would settle rather than where it was
being drawn while it opened, so for the third of a second after it appeared
the click landed a few pixels off; the overlay's idea of the clickable area
and the letter's disagreed about the padding; and the overlay never applied
its empty click-through region until the first letter opened, so until then it
quietly ate every click on that screen. `aviary status` shows the log, which
now records the pill's rectangle and each letter let go.

**Letters stopped arriving.** `aviary listen` in a terminal shows the relay
directly, without the overlay in the way.

**Under Wayland** the overlay runs through XWayland but cannot sit above
native Wayland windows. `oneko` has the same limitation.
