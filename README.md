# Aviary

Tiny birds that live on a screen and carry letters.

> **The real thing is [`native/`](native/README.md)** — a single C binary that
> draws pixel birds straight onto the X11 desktop, oneko-style. Start there.
>
> What follows is the browser prototype that was used to design the bird: the
> flight model, the burn, and the letter were all worked out here first, then
> ported to C. It is kept as a reference and a fast place to experiment.

They are not pets. Nothing sits on the desktop all day. The screen is empty
until a letter is sent — then a bird comes in off one of the edges, delivers,
and leaves.

**Bird one: the phoenix.** It flies in, drops a sealed scroll, and then burns
itself down to embers and ash while you read. It only makes the trip once.

## Run it

```bash
npm install
npm start          # or: npm run start:root   (adds --no-sandbox)
```

Nothing appears. That is correct — the overlay stays hidden until a bird flies.

## Send a bird

```bash
npm run send -- "I owe you a bird. This one only flies once." --from muez
```

Or from anything that can make an HTTP request on this machine:

```bash
curl "http://127.0.0.1:45874/send?text=hello&from=muez"
```

There is also a small control page at <http://127.0.0.1:45874/> and a
**Test flight** item in the tray menu.

The server binds to `127.0.0.1` only, so nothing is reachable from the network
yet. Making it reach *her* laptop is the next step, not this one.

## What is actually going on

The bird is drawn procedurally on a canvas — no sprites, no image files, no
tweened keyframes. It is a flight model with a body drawn on top, which is why
it does not loop:

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
  follow the path the bird actually flew — sampled by distance travelled, so the
  streamers are the same length whether it is racing or hovering.

The burn is a front that sweeps tail-to-head. Everything behind it is clipped
away and replaced with fire, so the head and beak are the last things left.

## Layout

```
main.js               overlay window, click-through hit testing, HTTP trigger, tray
preload.js            the renderer's only bridge
icon.js               generates the tray icon at runtime (no binary assets)
send.js               CLI sender

renderer/
  index.html          script order matters: util -> particles -> flight -> bird
  style.css           the letter: parchment, burnt edge, wax seal
  js/util.js          math, easing, seeded noise, the fire colour ramp
  js/particles.js     fire / ember / spark / ash / smoke / shockwave
  js/flight.js        Flyer — steering, wingbeat, banking, bounding flight
  js/phoenix.js       Phoenix extends Flyer — anatomy, feathers, ignition
  js/letter.js        the scroll that lands and unfurls
  js/app.js           scene state machine: enter -> settle -> drop -> watch ->
                      burn -> ash -> reading -> done

scripts/capture.js    renders the delivery to shots/*.png, frame by frame
scripts/sheet.js      renders shots/sheet.png — a pose sheet of the bird
scripts/soak.js       checks every scene actually terminates
```

## Working on the bird

You cannot judge a 40px bird by squinting at it. Use the harnesses:

```bash
node_modules/.bin/electron scripts/sheet.js --no-sandbox     # pose sheet, 3x
node_modules/.bin/electron scripts/capture.js --no-sandbox   # the whole delivery
node_modules/.bin/electron scripts/soak.js --no-sandbox      # does it finish?
```

`renderer/sheet.html` poses the bird across a wingbeat cycle, the flight modes,
and the burn stages, all blown up. Edit the bird, re-run, look.

## The next birds

`Flyer` in `js/flight.js` holds everything that is true of any bird — steering,
wingbeat, banking, bounding flight, the transforms. `Phoenix` only adds anatomy
and its own ending. A new bird is a new subclass:

- **Pigeon** — the one that was owed. Heavier, slower wingbeat, more glide,
  lands rather than hovers, walks with a head-bob, waits to be shooed.
- **Chakor** — for something romantic. It is the bird that is supposed to be in
  love with the moon, so it should arrive at night and look up.
- **Rain bird** — soaked through, feathers clumped and dark, shaking water off
  in bursts, flying badly, and delivering anyway.

Weather and time-of-day belong in the scene, not the bird: pick which bird flies
and how it behaves from the recipient's local conditions.
