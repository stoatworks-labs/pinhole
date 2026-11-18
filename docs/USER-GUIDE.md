# Pinhole user guide

> **Photosensitivity warning.** At its default Exposure this effect only flashes where your
> clip does. Turned up, Exposure brightens a clip's own pulses: at +2 stops 3 of Resolume's
> 33 demo clips flash faster than three times a second over a large part of the picture
> through it, and at +4 stops 4 do, where the clips on their own did not. Flashing at that
> rate can trigger seizures in people with photosensitive epilepsy. Check your clips before you put this in front of an audience (see
> [Flashing](#flashing)).

Pinhole is a pinhole camera and camera obscura for [Resolume](https://resolume.com) Arena
and Avenue, as one FFGL **effect**, **SW Pinhole**. There is no lens: the clip is the scene,
and a hole in a thin plate throws it onto a screen behind.

**The hole doesn't blur a sharp picture; it makes the picture by diffraction.** Every point
of the scene lights the screen with the light pattern a round hole makes, one pattern per
colour, and the picture is the sum of them all. So the picture is sharpest at one hole size
and softer either side of it: a smaller hole spreads light by diffraction, a bigger one by
its own shadow. With small holes red edges spread further than blue ones, a faint veil of
light from the patterns' outer rings lifts the dark parts, and the corners darken in a short
box. Swap the hole for a **Fresnel zone plate** and it focuses harder, but one colour at a
time: the colour the plate is made for comes sharp, and the others land round it as soft
coloured light. Nothing is drawn.

![A tunnel of metal struts lit from its far end, softly focused, with each light strip
spread into a glow](pinhole.png)

*SW Pinhole on its defaults (a 0.31 mm hole 50 mm from the screen) over Resolume's demo clip
IntoTheGlow_21, rendered by the offline harness rather than captured from Resolume.*

> **Before you rely on this:** this is **v0.1.0**, the first release, and early days. The
> optics are measured, not just asserted. An offline harness drives the plugin's own code, at
> two sizes, on an Apple Silicon Mac's GPU and again on Apple's software renderer. The light
> pattern it builds reproduces **69 of the 80** values of the tables Lord Rayleigh printed in
> 1891, to half their last digit; read off the picture, a big hole's edge spreads as a disc
> does and a small hole's as diffraction predicts, a point's image is tightest within
> **1.0–1.5 %** of the best hole the literature gives, and the corners fall as cos⁴ to
> **0.03 %**. Seventeen deliberately wrong versions of the plugin and 51 one-character
> sabotages of its code are all caught, and all 9 controls change the picture. It has been
> loaded into Resolume Arena on a Mac and on Windows, where every control was there and
> changed the picture, but only over still pictures, never in a show. Several of its numbers are
> **chosen**, not measured, and an isolated moving point of light changes shape slightly as
> it moves at large blurs (see [Moving lights](#moving-lights) and
> [Known limits](#known-limits)). **Try it on a spare layer first**, and shout if anything
> misbehaves.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

---

## Installing

Download the build for your platform. For macOS there's a universal `.dmg` or `.zip`
(Apple silicon and Intel), **Developer ID-signed and notarised** so the bundle just loads
(the signing happens on my Mac shortly after each release is published, so a download made
in the first few minutes may need **Open** from the context menu once), and for Windows an
x64 installer or `.zip`. Put `Pinhole.bundle` (macOS) or `Pinhole.dll` (Windows) in
Resolume's FFGL folder, then restart Resolume:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout in its own folder.

The Windows installer puts the DLL there for you, per user, with no admin rights needed. It
defaults to Arena's folder (Avenue's if only Avenue is installed) and lets you change it.
The Windows builds aren't code-signed. Plugin files aren't gated the way `.exe` files are,
so Resolume loads them as normal; only the installer trips SmartScreen, once: **More info**
→ **Run anyway**.

It's in the Effects browser as **SW Pinhole**. Drop it on a clip, a layer or the
composition.

---

## Start here

1. Put SW Pinhole on a clip. At the defaults the hole is the sharpest one for its box, so the
   picture is as sharp as a pinhole camera gets: soft, with every bright point spread into a
   small glow.
2. Move **Diameter** down towards 0.1 mm. The picture softens, and bright edges grow a red
   rim: that's diffraction. Move it up towards 2 mm and it softens again, evenly: that's the
   hole's own shadow. Somewhere near the default it's sharpest.
3. Move **Distance**. A longer box makes a bigger, softer picture of each point; a short one
   darkens the corners hard.
4. Set **Aperture** to Zone Plate and move **Zone Focus**: one colour comes sharp at a time.
5. **View** Obscura turns the picture upside down, as on the wall of a camera obscura.

Diameter is the pinhole's; Open Zones and Zone Focus are the zone plate's. Each does nothing
under the other Aperture.

---

## How it works, in one paragraph

The clip is the picture a hole of zero size would show, the scene's geometric image on the
screen, with the scene far away. The screen is 24 mm tall (a 35 mm film frame's height) at
any resolution, so the blur is the same picture at 720p and at 4K. For each colour band
(blue 400–500 nm, green 500–600, red 600–700) the plugin computes the Fresnel diffraction
pattern of the aperture at the screen, from Fresnel's integral over the hole or over the zone
plate's open rings, and spreads each pixel's light over the screen in that pattern. The
light adds up: the picture keeps the clip's light, only moving it. The pattern is cut where
it holds 99 % of its light. Finally the corners are darkened by cos⁴ of the angle off the
axis, the way light falls off across the screen of a real pinhole camera.

---

## The Camera group

- **Aperture.** Pinhole (a round hole) or Zone Plate (concentric rings, open and blocked in
  turn, sized so the open ones all send light to one point on the screen).
- **Diameter** (0.1–2 mm, Pinhole only). The hole. The default, 0.31 mm, is the sharpest hole
  for a 50 mm box in green light (Mielenz's optimum, d = 1.844 √(λ f)); the best hole grows
  with the square root of the distance and of the wavelength, so it is a little bigger for
  red than for blue. Near the best hole the middle of each point's pattern is up to four times
  brighter than the light through an open hole would be.
- **Open Zones** (1–16, Zone Plate only). How many open rings the plate has (the centre and
  the edge open, so 2n − 1 zones in all). More zones focus harder, but past about five or six
  the plate is soft even in its own colour, because each band holds a spread of wavelengths.
  One open zone is a single hole, about the best pinhole for the distance.
- **Zone Focus** (400–700 nm, Zone Plate only). The colour the plate is made for: that colour
  comes sharp on the screen, the others land round it as soft coloured light. The default is
  550 nm, green.
- **Distance** (10–500 mm). Hole to screen. A longer box spreads each point further (the
  pattern grows with the distance) and darkens the corners less; a short box darkens them
  hard: at 10 mm the corners get about 2 % of the centre's light, at 50 mm about 65 %.

## The Picture group

- **Exposure** (−4 to +4 stops). The picture's brightness. The default, +0.2 stops, is what an
  averaging meter sets on the middle one of Resolume's demo clips, to make up for the darker
  corners. **Turning it up turns a clip's pulses up too** (see [Flashing](#flashing)).
- **View.** Upright (as a print would show it) or Obscura (turned 180°, as on the wall of a
  camera obscura).
- **Edge.** What lies past the frame's edge. Mirror (the default) carries the scene on past
  the frame as its own reflection, so the edges don't darken; Black puts the picture on a
  black wall, so light spreads in from the edges only from inside. It only shows where the
  blur is wide.

## The Look group

- **Mix.** 0 is your clip untouched, 1 is the camera's picture.

Over a transparent part of a clip the screen still shows light spread from the clip's opaque
parts; a transparent pixel sends the hole no light. At Mix 1 the output is opaque.

---

## Recipes

- **The classic pinhole look.** The defaults: soft, honest, with a faint glow round lights.
- **Dreamy and coloured.** Diameter 0.1 mm, Distance up towards 500 mm: everything spreads
  into wide soft discs with red rims.
- **A tiny box.** Distance 10–15 mm, Diameter left alone or a little smaller: a hard
  vignette, the picture fading to dark corners.
- **One sharp colour.** Zone Plate, Open Zones 8, then sweep Zone Focus slowly over a clip
  with white lines or lights: each colour in turn comes into focus.
- **Camera obscura.** View Obscura, Edge Black, Exposure down a stop or two.

---

## Flashing

The camera keeps the clip's light: it moves light about the frame, but a frame's light stays
the frame's (to 0.05 % on a test square). So at its default Exposure it flashes only where
the clip does. Over all 33 of Resolume's demo clips (measured frame by frame at 60 fps, over
the whole picture and over every third-of-the-screen window, counting a change of a tenth of
white as half a flash), none flashes faster than three times a second through it at the
defaults, with the zone plate, or at the largest blur.

**Exposure is the exception.** It turns the whole picture up, and with it the clip's own
pulses: a pulse too dim to count on the clip can become a large flash. At +1 stop none of the
33 crosses the line (three reach it), at +2 stops 3 do (Resolume's Bass 006, Beat 002 and
NeonRoom2_32), at +4 stops 4 (those and Bass 003). If you raise Exposure past a stop, check
your clips first, or keep it near the default for a pulsing clip.

---

## Moving lights

The plugin spreads each point's outer rings from coarser and coarser copies of the picture,
to keep the cost down, and a coarse copy sees a point's light at the middle of its texel. So
as a point of light moves, its spread picture changes shape a little, and comes back to the
same shape every 16 pixels of travel (at 1080p). Measured on a lone point on black: at the
largest blur (a 0.1 mm hole 500 mm from the screen) its picture differs by up to a quarter
between two positions 8 pixels apart, almost all of it in the faint outer rim, which breaks
into notches and closes again; with a 16-zone plate the point's picture changes by about 4 %,
again in its outer rings. Near the defaults it's a fifth of a percent.

**On a real clip it didn't show.** Resolume's Beat 006 (a turning structure studded with
lamps) at the largest blur, watched frame by frame: the lamps' discs move smoothly, and the
picture changes from frame to frame no more roughly than the same clip under a perfect,
position-independent blur does. The rim that changes is too faint to see unless it's
brightened. If you do see a crawl in the rims of moving lights (most likely a bright lamp on
black, at a long Distance or with many zones), that's this.

---

## Performance

On the Mac it was measured on (an Apple M4 Max, in a quiet moment): at the defaults about
**2.4 ms** of GPU time a frame at 720p and **4.9 ms** at 1080p; at the largest blur **2.8 ms**
and **5.4 ms**. At 4K the largest blur took **17.8 ms** (18.1 ms at worst), more than a 60 fps
frame's 16.7 ms on its own. The GPU is shared: timed again with Resolume Arena and other work
running on the same Mac, the same cases took 2.7, 5.6, 3.3 and 8.3 ms, and 31 ms at 4K. The
patterns are rebuilt on the CPU (about 10 ms) only when Aperture, Diameter, Open Zones, Zone
Focus, Distance or the resolution changes, so sweeping one of those costs a little more.
Another GPU will differ, and nothing has been timed inside Resolume.

---

## If it looks wrong

- **It's blurry.** It's a pinhole camera: the sharpest picture a pinhole can give is soft.
  The defaults are already the sharpest hole for the box; a shorter Distance makes everything
  smaller and sharper on the same screen.
- **Diameter does nothing.** Aperture is on Zone Plate. Open Zones and Zone Focus likewise do
  nothing under Pinhole.
- **The corners are dark.** That's cos⁴: a short box darkens them. Lengthen Distance.
- **Edge does nothing.** At the defaults each point spreads only a few dozen pixels, so only
  the frame's very edge changes. It shows at long Distances and with the zone plate.
- **Bright edges have a red rim.** That's diffraction: red spreads further than blue through a
  small hole. A bigger hole, or a shorter box, makes it smaller.
- **The picture got brighter in the middle when I changed the hole.** Near the best hole the
  middle of a point's pattern is brighter than the open light; the total light is unchanged.
- **The dark parts look lifted.** That's the veil from the patterns' outer rings, faithful to a
  real pinhole picture. Lower Exposure, or use a bigger hole.
- **It's flashing.** You've raised Exposure over a pulsing clip. See [Flashing](#flashing).
- **The lights crawl as they move.** See [Moving lights](#moving-lights).

---

## Known limits

- **It has barely met Resolume.** It ran in Arena 7.29.0 on an Apple M4 Max and in Arena
  7.27.1 on Windows (software rendering, no GPU), mounted over Arena's API on still pictures.
  On the Mac all 14 of its parameters were there with their defaults, the same settings gave
  the same picture to the last level, and Mix 0 gave back the clip exactly. The hole was
  sharpest at the default and softer either side, a 10 mm box left the corners at 11 % of the
  centre's brightness, Obscura was the picture turned round to within JPEG's noise, Open Zones
  and Zone Focus changed the picture under Zone Plate and nothing under Pinhole, and Diameter
  did nothing under Zone Plate. Which colour Zone Focus brings sharp was not read off Arena's
  picture (the harness measures that). On Windows the fleet's Arena gate passed: it
  registered as PC01, an effect, with all 14 parameters as declared, and every control changed
  the picture (Open Zones and Zone Focus with the zone plate chosen, Edge in a long box). It
  hasn't run on a moving clip in Resolume or in a show, and its cost inside Arena wasn't
  measured.
- **A moving point changes shape slightly at large blurs** (see
  [Moving lights](#moving-lights)): the cost of spreading the outer rings from coarser copies
  of the picture. It didn't show on a real clip, but it's there. Storing those copies at full
  float precision, or a finer pyramid, would cost more GPU time.
- **4K at the largest blur is over a 60 fps frame** on the Mac it was timed on: 17.8 ms
  (18.1 ms at worst) against a frame's 16.7 ms, and 31 ms with Resolume running alongside.
- **Straight or premultiplied alpha?** The plugin treats your clip's colour as straight alpha,
  which is how Resolume's own demo clips decode. If Resolume hands effects premultiplied
  colour, the light spread from the edges of transparent areas comes out darker than it
  should. Nobody has checked in Resolume yet.
- **Chosen, not measured:** the screen's 24 mm height, the three colour bands (each a flat
  100 nm band, where a real sensor's colours overlap), the cut at 99 % of each pattern's light
  (the last 1 % is dropped, so a bright light's widest veil is a little short), and the
  Exposure default's averaging meter (a centre-weighted meter would choose differently).
- **One pattern for the whole frame.** Fresnel's approximation as used here leaves out the
  pattern's stretch towards the corners, the hole's thickness and its own vignetting, and
  polarisation; the scene is taken to be far away.
- **The best hole sits 1.0–1.5 % below the literature's** in the harness's measurement (the
  default is the literature's, Mielenz's 1.7π), within the check's tolerance; nobody will see
  the difference.
- **The edges of the range** (Distance below 10 mm, a hole above 2 mm) aren't reachable, and
  the pattern's half-float storage loses about 1 % of a lone bright pixel's light in its
  faintest rings.
- **Other GPUs.** The optics are measured on one Apple GPU and Apple's software renderer, and
  it has run in Arena on that Mac and on Windows with Mesa's llvmpipe; the browser demo, the
  plugin's own code compiled for the browser, matched the harness to within one level on the
  same Mac.

---

## About

The last group, **About**, has the plugin's name, version, licence and maker, and buttons that
open this user guide, the project page, the source on GitHub and the support page in your
browser.

## Links

- Project page: [stoatworks-labs.com/software/pinhole](https://stoatworks-labs.com/software/pinhole/)
- Source: [github.com/stoatworks-labs/pinhole](https://github.com/stoatworks-labs/pinhole)
- User guide: [stoatworks-labs.com/software/pinhole/guide](https://stoatworks-labs.com/software/pinhole/guide/)
- Browser demo: [pinhole-demo.stoatworks-labs.com](https://pinhole-demo.stoatworks-labs.com/)

## Reporting something

Something wrong, odd or missing? Open an issue at
[github.com/stoatworks-labs/pinhole/issues](https://github.com/stoatworks-labs/pinhole/issues).
The plugin keeps a short log of what the host did (the GPU it was given, whether its shaders
compiled, whether its buffers could be allocated), which helps a lot:

```
macOS    ~/Library/Logs/pinhole/
Windows  %LOCALAPPDATA%\pinhole\logs\
```
