# aviary

Tiny pixel birds that live on your X11 desktop and carry letters.

In the spirit of `oneko` — a small creature drawn straight onto the root
window, no browser, no Electron, no toolkit. One C binary, Xlib + Cairo.

They are not pets. The screen is empty until a letter is sent. Then a bird
comes in off one of the edges, delivers, and leaves.

**The phoenix** flies in, drops the letter, and burns itself down to smoke
while you read. It only makes the trip once.

**The owl** is Errol. He comes in off one edge, does not slow down, and flies
straight into the far wall. Then he drops, tumbling, in a cloud of feathers,
lands flat on his back, rights himself, shakes himself out, and only then
remembers the letter — which he unties where he fell. Then he leaves, wobbling.

**The swallow** is the one that is still out in it. Long narrow wings and a
deeply forked tail: fast, and able to turn inside its own body length. It
arrives soaked, and has to shake the water off before it can do anything at all.

**The pigeon** is the one that was actually owed. He does not hover and he does
not burn: he lands, walks over with the letter tied to his leg, bends down and
unties it, then stands on the paper waiting to be noticed. When you close the
letter he crouches, claps his wings, and goes.

Full command reference: **[MANUAL.md](MANUAL.md)**

## Install

```bash
git clone https://github.com/moizxsec/aviary.git
cd aviary
./install.sh
```

That pulls the build dependencies, compiles, installs `aviary` into
`~/.local/bin`, and registers a systemd user service so the daemon comes up
with your session. Open a new shell afterwards.

To build by hand instead:

## Build and run

```bash
sudo apt install libcairo2-dev libx11-dev libxext-dev libxrandr-dev   # build deps
make
./aviary                    # the daemon; nothing appears yet
```

In another shell:

```bash
./aviary send --local "..." --bird swallow    # it rains for this one
./aviary send --local "..." --bird owl
./aviary send --local "..." --bird pigeon
./aviary send --local "..." --bird phoenix
```

`--local` sends nothing and flies the bird that would have arrived, letter and
all. Without it, `send` is a real one: a bird comes to your cursor for the
letter and carries it off, and the same letter goes to the other laptop.

`make install` puts it in `/usr/local/bin`.

## Size and pixels

The sprite is sized against oneko's 32x32 cat: on a 1920x1200 screen the bird's
body comes out about 28 device pixels long.

```
./aviary --pixel 1     # one sprite pixel per screen pixel — oneko fidelity
./aviary --pixel 2     # default: chunky, unmistakably pixel art
./aviary --pixel 3     # very coarse
```

`--pixel N` is the only knob that matters. The scene renders into a buffer
1/N the size of the screen, gets snapped to a fixed palette, and is blown up
with nearest-neighbour sampling. Nothing in the bird knows about any of it —
it just runs at a lower resolution, which is what a sprite is.

## What is actually going on

The bird is drawn procedurally: no sprite sheets, no image files, no tweened
keyframes. It is a flight model with a body drawn on top, which is why it never
loops:

- **Asymmetric wingbeat.** The downstroke takes 40% of the cycle and the
  upstroke 60%, and the wrist folds on the way up. Real birds are not sine
  waves.
- **Foreshortening.** A wing is a flat plane sweeping up and down, so at
  mid-stroke you see it edge-on and it looks short. Skipping this is what makes
  most 2D birds look like paper cutouts.
- **Feather lag.** Each primary trails the one inboard of it, so the stroke
  travels out along the wing as a ripple.
- **Bounding flight.** At cruise it flaps in bursts, then folds up and coasts,
  sinking slightly. That is where the undulating flight path comes from.
- **Banking, pitch, head stabilisation, blinking**, and a tail whose plumes
  follow the path the bird actually flew — sampled by distance travelled, so
  the streamers are the same length whether it is racing or hovering.

The burn is a front that sweeps tail-to-head. Everything behind it is clipped
away and replaced with fire, so the head and beak are the last things to go.

### The pigeon walks, which is harder than flying

**The head bob is not a bob.** A walking pigeon holds its head *still in space*
while its body advances underneath it, then snaps the head forward to a new
fixed point and holds again — roughly 30% of each stride is the thrust, the
other 70% is the hold. So during the hold the head-to-body offset has to
decrease *linearly*, at exactly walking speed. That linearity is the whole
effect. Draw it as a sine wave and you get a chicken.

The feet work the same way: planted while the body passes over them, then swung
forward. A foot that slides along the ground reads as ice.

The stride is driven by **distance covered**, never by a clock, so the bird
cannot moonwalk when it speeds up or slows down.

Landing is the whole body becoming an airbrake: nose up, wings cupped and
beating against the direction of travel, tail fanned wide and dropped, feet
swung out in front. Take-off is a crouch and then a **wing clap** — the
upstroke goes past vertical so the tips meet over the back, which is the crack
you hear when a pigeon leaves in a hurry.

He carries the letter the way a real homing pigeon did: rolled and **tied to
his leg** with thread, not clutched in the beak. Every bird carries it that
way — it is a shared prop (`av_draw_tied_letter`), not species anatomy.

### The swallow is the opposite of the owl

High aspect ratio instead of high area: long narrow wings, very low drag, and
a deep forked tail that opens as a rudder when it banks. That is why it can
corner the way it does, and why it is the one still flying when the weather
turns. `max_force` is 2300 against the owl's 620.

Being wet changes the bird, not just its colour: soaked feathers lie flat, so
the silhouette is slimmer; they clump into points, so the outline grows spikes;
and everything darkens. The shake is not a rouse — it is a violent whipping of
the whole body that throws water off sideways, and it is the only way the bird
gets dry.

### The owl flies slowly because of physics, not mood

An owl's wing area is enormous next to its weight, and everything follows from
that one fact:

- **2.6–4.2 Hz.** A pigeon beats nearly twice that and a finch four times. The
  stroke is slow and deep and lifts the whole body on every downstroke.
- It **glides for long stretches and barely sinks** — `glide_sink` is 34 where
  the default is 130. That is what makes the flight look like it is floating
  rather than falling forward.
- Broad rounded wings, so `fore_floor` is high: the wing never disappears
  edge-on the way a narrow one does.
- **The head is stabilised hard.** Owls hold their heads still while the body
  heaves underneath, and they swivel rather than turn.
- No neck at all. The head sits straight down into the shoulders and overlaps
  them; that overlap is most of why the silhouette reads as an owl.
- Forward-facing eyes in a flat facial disc — the dish that aims sound at his
  ears, and the reason an owl looks *at* you rather than past you.

Errol adds his own problems on top: extra wander, a slow altitude sag, and no
judgement of distance whatsoever. On impact the velocity reverses, a spin rate
is applied to `Flyer.roll` (free rotation, on top of heading), the wings go
limp, and gravity takes over. He lands on his back and has to roll upright.

The shake afterwards is a **rouse** — fluff everything up, shake it out hard,
then settle. Every bird does it after a fright; on an owl it is most of the
personality.

### Drawing for pixels, not just shrinking

Most of the work was not making it small — it was making it *legible* small.
At 28 pixels the smooth version fell apart, so `av_pixel_mode()` switches to a
different set of decisions:

- Soft alpha gradients dither into confetti, so feathers stay **solid** and
  taper by shape.
- The glow halo becomes a cloud of stray pixels, so there is **no halo**.
- A 1px keyline on a 2px-thick wing leaves nothing but keyline, so only the
  **body and head** are outlined.
- A ribbon traced with an outline closes into a wire loop, so plumes are filled
  and never stroked.
- Two plumes swaying independently bow apart far enough that the gap reads as a
  hole, so the sway is damped and the fan tightened.
- The palette leans on the **bright middle** of the fire ramp; a ramp made
  mostly of deep reds just reads as a dark smudge.

## Two laptops

One short code, typed once on each machine. No accounts, no server, nothing to pay for.

**On your laptop:**

```
$ aviary invite --name muez --bird owl

  This laptop is ready.

      A50T-4Z40-6EHW-JG4R-F8TZ-TCXD

  Type that on the other laptop, once.
```

**On hers:**

```bash
aviary join A50T-4Z40-6EHW-JG4R-F8TZ-TCXD --name her --bird pigeon
```

That is the whole setup. Both sides restart their daemon automatically. Neither
of you ever types the code again.

**From then on, sending is one word:**

```bash
aviary
```

Type a line, press enter. A bird lifts off your screen carrying it, and the
same letter arrives on hers as a bird flying in. `aviary --bird owl` to use a
different one just this once.

### What the code is

The code is 120 bits of randomness in Crockford base32 — no I, L, O or U, so
there is nothing to misread copying it across. **Both** the relay topic and the
encryption key are derived from it, with different prefixes, so:

- the topic is a hash of the secret and gives nothing away, even though anyone
  can subscribe to it;
- the key is the same secret run through 20,000 rounds of SHA-256.

Everything is sealed with AES-256-GCM before it leaves, so ntfy.sh only ever
stores base64 ciphertext. A wrong code fails the GCM tag and the letter simply
will not open.

### Reboots and letters that arrive while you are away

ntfy holds recent messages for about twelve hours, so a letter sent to a laptop
that is off waits for it rather than being lost. Three things make that work:

- The resume clock starts **at pairing**, not at the first time the daemon runs,
  so a letter sent before that machine has ever been switched on is still there.
- After every letter the daemon records **the message id** in
  `~/.config/aviary/mark` and resumes from it. Resuming from a timestamp loses
  or repeats anything that shared a second with it; an id is exact.
- Each machine mints its own id when it pairs, so it ignores its own letters
  coming back round the loop.

The daemon is started by an XDG autostart entry, and by a systemd user service
where there is a user session to put one in. Either way it comes back after a
reboot. It also starts itself the moment you send something, so the only way to
have no daemon is to have never used it.

At login the daemon can be up before the X server is, and a systemd user unit
may have no `DISPLAY` in its environment at all. It waits a minute for a server
rather than failing, tries `:0` and `:1` if nothing was named, and the unit is
set never to hit its start limit — a daemon that gives up five times in fifteen
seconds and is then never tried again is how a working install becomes a dead
one across a single reboot.

Nothing it prints at login has anywhere to go, so it keeps
`~/.config/aviary/daemon.log`. `aviary status` prints the tail of it along with
everything else that has to be true for a letter to arrive.

Re-pairing is picked up without a restart. Sending re-reads the config every
time but the listener only read it once, so a daemon left running across an
`aviary join` went on watching the topic it was born with: letters went out
fine and nothing ever came back, which from that end is invisible. It now
watches the config file and re-subscribes when it moves.

If the laptop is off for more than about half a day the relay will have dropped
the message. Self-hosting ntfy raises that limit to whatever you like.

`~/.config/aviary/config` holds the key and is written mode 0600.

## One bird, seen twice

The bird comes down to the line you typed on — the letter rolls itself up on
the cursor, it ties it on, and carries it off the edge of your screen. Nothing
happens on the other laptop until it has gone.

The letter reaches the relay immediately, stamped with how long the sending
bird is still on the sending screen. The far end holds it that long before
flying its own bird in, less whatever the network already spent getting there.
Two birds moving at once on two desks reads as a copy of a bird; one leaving
and then the other arriving reads as the same one.

`travel=` in `~/.config/aviary/config` is the gap across the room, in seconds,
on top of that. It defaults to 2.5.

## The letters

Each bird hands over a different object, in the state that bird left it in.
One `Letter` with four skins (`LS_*`), chosen by species:

| bird | paper | and then |
|---|---|---|
| phoenix | scorched, charred edge, red wax | **burns away after 3s** — a wavy fire line eats it from the bottom up |
| pigeon | clean warm white, shallow deckled edge, green wax | **vanishes after 3s** |
| swallow | cold grey, damp blotches, beads on the surface, ink run downward | stays |
| owl | yellowed, creased, smudged, badly torn | stays |

The phoenix's letter does not fade out — paper does not fade. A burn line rises
through it, everything below it is gone, and the scene throws fire, ash and
smoke along that line as it climbs.

## Layout

```
src/aviary.h      shared types
src/util.c        math, easing, seeded noise, the fire colour ramp, world scale
src/pixel.c       the pixel-art pipeline: palette, ordered dither, nearest blit
src/particles.c   fire / ember / spark / ash / smoke / shockwave
src/flight.c      Flyer — steering, wingbeat, banking, bounding flight,
                  and wing_pose(), the foreshortening both birds share
src/phoenix.c     Phoenix extends Flyer — anatomy, feathers, ignition
src/pigeon.c      Pigeon extends Flyer — anatomy, the walk, landing, take-off
src/owl.c         Owl extends Flyer — floaty flight, the collision, the rouse
src/swallow.c     Swallow extends Flyer — darting flight, wet state, shake-dry
src/letter.c      the scroll that lands and unfurls, drawn in Cairo
src/scene.c       state machine: enter -> settle -> drop -> watch -> burn ->
                  ash -> reading -> done, and the pigeon's own path:
                  enter -> land -> walk -> setdown -> stay -> takeoff,
                  and the owl's: enter -> charge -> strike -> tumble ->
                  sprawl -> shake -> setdown -> stay -> takeoff
src/main.c        X11 overlay, click-through shaping, unix-socket trigger
src/render.c      the offline harnesses
```

## Working on the bird

You cannot judge a 28px sprite by squinting at a running daemon. Everything is
inspectable offline, with no X server involved:

```bash
./aviary render shots 2 phoenix        # the whole delivery, frame by frame
./aviary render shots/pigeon 2 pigeon
./aviary render shots/owl    2 owl
./aviary strip  shots/strip.png 2 pigeon   # nine key beats tiled into one image
./aviary sheet  shots/sheet.png            # phoenix poses, zoomed
./aviary psheet shots/pigeon-sheet.png     # pigeon poses: the stride, the flare
./aviary osheet shots/owl-sheet.png        # owl poses: the beat, the crash, the head
./aviary sizes  shots/sizes.png            # sprite size next to a 32x32 oneko box
```

The pigeon sheet is the one that matters when touching the walk: row one is a
single stride, and the head should sit *motionless* across the middle cells
while the body slides forward under it.

`render` exits non-zero if the scene fails to finish or any beat never happens,
so `make check` is a real test.

## Notes

- Needs a compositing X server for the 32-bit ARGB visual. GNOME, KDE, and
  picom-style compositors all qualify.
- Under **XWayland** (Ubuntu's default GNOME session) it runs, but an X window
  cannot stay above native Wayland clients. `oneko` has the same limitation.
- With a **second screen** plugged in, both are one X screen as far as Xlib is
  concerned — the root window is the two of them side by side. A window
  covering that root spans both, and anything centred in it lands on the seam.
  The overlay covers one monitor instead, chosen per delivery through RandR:
  the one holding the terminal you typed in, or the one under the pointer for a
  letter arriving. Docking, unplugging and rotating are all picked up the same
  way, because the geometry is looked up fresh every time and never cached.
- The overlay covers that screen with an **empty XShape input region**, so every
  click falls through to whatever is underneath. The letter is the one
  rectangle that catches them — and that rectangle is put through the same
  transform the pill is drawn with, so it is where the pill is even while the
  panel is still opening.
- The trigger socket is a unix socket in `$XDG_RUNTIME_DIR`, mode 0600. Nothing
  is exposed to the network.

## The next birds

`Flyer` in `flight.c` holds everything true of any bird — steering, wingbeat,
banking, bounding flight, the transforms. `Phoenix` only adds anatomy and its
own ending. A new bird is a new struct that embeds `Flyer`:

- **Chakor** — for something romantic. It is the bird that is supposed to be in
  love with the moon, so it should arrive at night and look up.
- **Rain bird** — soaked through, feathers clumped and dark, shaking water off
  in bursts, flying badly, and delivering anyway.

Weather and time of day belong in the scene, not the bird: pick *which* bird
flies, and how it behaves, from the recipient's local conditions.
