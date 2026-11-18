# Image Exporter (Geode mod)

Turns an image into Geometry Dash objects inside the editor, with **8 normal methods + 10 monochrome styles** and a hard cap of **10,000 objects**.
Technical basis: the *Art importer* mod (Grian/CelestialGecko): object `3092` (a 1 u square), color through absolute HSV on channel 1010, and `createObjectsFromString`.

## The 8 methods

| # | Method | Idea | Best for |
|---|--------|------|----------|
| 1 | Uniform grid | 1 cell = 1 object, average color | pixel art, fast |
| 2 | Adaptive quadtree | splits in 4 only where there is detail (best-first, uses exactly the limit) | general |
| 3 | Adaptive BSP (k-d) | optimal cut in X or Y, rectangles of any proportion | **photos** (fast) |
| 4 | Layer pyramid | big block + detail layers only where the color differs (rate-distortion criterion) | images with flat areas |
| 5 | Palette regions | quantizes to a palette (median-cut, or exact if <= 256 colors) and merges areas into greedy rectangles; finds the resolution/colors by itself | **illustrations and pixel art** (very few objects) |
| 6 | Horizontal strips | per-row strips with color tolerance; picks rows/tolerance by itself | "scanline" style |
| 7 | Rotated rectangles | random search + hill climbing guided by the error map (Geometrize style) | painterly look |
| 8 | Hybrid BSP + rotated | BSP base (15 %) and the rest as rotated rectangles over the leftovers | **maximum quality** |

All of them guarantee `objects <= limit` (adjustable from 500 to 10,000 in the window). With transparency, 7 and 8 switch to BSP by themselves (overlapping rectangles cannot leave gaps) and the window tells you so.

## Monochrome styles (10 extra styles, separate "Light" tab in the window)

Designed to be **light**: black ink (channel 1010) on white paper (channel 1011), **no per-object HSV keys**, and usually far fewer objects than the maximum. The object string is ~25 % shorter than with HSV, and ASCII is about 20 times smaller. Choosing a normal method disables the style and vice versa. The setting *White ink on black* is for dark backgrounds.

| # | Style | Idea |
|---|-------|------|
| 9 | Halftone (diamonds) | squares rotated 45 degrees; the size follows darkness |
| 10 | Line engraving | parallel lines of varying thickness per segment |
| 11 | Cross-hatching | stripes at 45, 135, 0 and 90 degrees depending on tone |
| 12 | Stippling (dots) | dots with density proportional to darkness (weighted Poisson) |
| 13 | Bayer dithering | ordered black/white, merged into rectangles |
| 14 | Ink (comic) | adaptive threshold: solid blots, very few objects |
| 15 | Stroke sketch | short oriented strokes on the edges (Sobel + non-maximum suppression) |
| 16 | Spiral | Archimedean spiral of varying thickness |
| 17 | 10 PRINT diagonals | one / or \ per cell depending on local structure |
| 18 | ASCII art | **one text row = one text object (914)**; the whole image in ~50-200 objects |

Each style has a minimum visible spacing, so **with a 10,000 limit they usually use quite a bit less** (cross-hatching ~2,000, engraving ~4,600-6,500, ASCII ~150).
Useful settings: *White ink on black*, *Black/White channel*, *ASCII: character width* and *ASCII: character height/width*.

Object counts for a photo at the 10,000 limit: halftone 9,872 | engraving 6,537 | cross-hatching 2,071 | stippling 9,977 | Bayer 9,891 | ink 10,000 (7,934 on the painting) | strokes 7,212 | spiral 9,963 | 10 PRINT 9,987.

### About automatic level deletion
I have found no public information about the servers' exact rule, **so I cannot guarantee that a level will not be deleted**. What the mod does is attack the causes people mention: fewer objects, no HSV in the monochrome styles, and a **"~KB"** indicator in the window with the approximate size of the (uncompressed) level string so you can see how heavy each option is. The normal HSV methods are around 800-900 KB at 10,000 objects; the monochrome ones between ~130 and ~680 KB. To go even lighter, lower the object limit. Try a small level first.

### Color channels, grays and "Light styles" in color

**No triggers.** The colors are written directly into the level's color settings (what the editor's *Base Color* selector reads) and each object only carries its channel (`21,<channel>`): no HSV and no extra objects.

**Free channels.** Before placing, the mod checks which channels the level uses (the base and detail color of **all objects**, the already-defined colors, and channels copied by another channel) and spreads the image's colors **only over free channels**, starting at *First palette channel* (default 2). If channels 2-999 run out, the allocation **continues at 1020 and goes up to 9999** (1000-1019 are special GD channels -BG, G1, G2, black, white...- and are never used). **Channel 1 is never used.** The mod also remembers the channels it handed out in the open editor, so placing a second image takes different channels and **does not overwrite** the colors of the previous one. If there are not enough, it warns you and places nothing.

**Methods 1-8: three checks** (none checked = HSV color, as always):
* *Channels*: the colors are reduced to a palette (default 64, adjustable) and each color is a channel.
* *Gray HSV*: the image in grayscale using HSV.
* *Gray channels*: the same, but with channels (up to 32 gray tones).

**Light styles (9-18):**
* By default they are black and white on **2 free channels** (black and white). With the setting *Black and white with channels 1010/1011* they use the special channels.
* With the *Colors in channels (no HSV)* check they come out **in color and cleaner**: a **color mosaic** (BSP) with the image's tone goes underneath, and the style goes on top with a darker version of the same tone. That way there are no transparent gaps or stray white/black dots. The base compensates for the real ink coverage of each area, and long strokes are split where the color changes.
* Half of the palette channels (*Light styles: color channels*, default 48) are for the base and the other half for the ink.

**Z-order is always exported** (key 25) and the light styles put the base/paper on a layer below the ink (key 24). If key 25 is missing, GD uses object 3092's default z and the paper can end up on top: that caused the blank images.

* **Unverified in GD:** defining the colors uses `GJEffectManager::getColorAction`, `getAllColorActions` and the `ColorAction` fields (per the 2.2081 bindings); channel detection uses `GameObject::m_baseColor/m_detailColor`.

### About ASCII (read this)
* The text object is **914**, with the text in base64 in key **31** (format taken from community libraries, not tested here in GD).
* **Rows are split into text objects** of *ASCII: characters per object* (60 by default; 14 or fewer in color) placed next to each other, so GD does not clip or break up long rows. There are up to 400 columns (260 in color).
* In color, each text piece uses the tone of its area (its own channel) over a color mosaic.
* **The font is a setting of the whole level**, not of the object: pick a monospaced one (e.g. font 5, Joystix).
* You have to **calibrate** it per font: *character width* (units at scale 1; default 24, an estimate), *height/width* (default 1.0) and, if needed, *letter spacing* (key 488). Place a test row, measure and adjust; the pieces are placed using that width, so a badly calibrated width leaves gaps or overlaps between pieces.

## The window (430x288, fits in GD's 320 screen height)

* **Left:** preview (140x140) and status: objects, approximate string size (`~KB`), quality in dB and time. *Settings* sits below it.
* **Right:** two **tabs**, *Main* (methods 1-8) and *Light* (styles 9-18); arrows to switch method, with its description; two **sliders** (*Objects*, 500 to 10,000 in steps of 100, and *Long side*, 150 to 6,000 u); and a **single color row** that changes with the tab (*Main*: Channels / Gray HSV / Gray channels, none = HSV color; *Light*: Colors in channels).
* **Buttons:** *Image*, *Place*, *i* (help, opens a small info box) and *Settings* (opens the mod's settings).
* The window remembers the last configuration. The calculation is redone 0.35 s after you release a slider.

### Interface diagnostics
If the window looks wrong, enable **"Save interface diagnostics (txt)"** in Settings. The mod then writes `ui-dump.txt` to its save folder; the exact path appears in a notification. It records a snapshot when the window opens, when you switch tabs, when you open the info box, when you open settings and when an image is calculated. The txt includes the version, screen size and scale factor, the full node tree of every open window (position/anchor/size/scale/skew/rotation/color/opacity and the **real on-screen box**), the literal texts with the glyphs the font does not have, the real spacing between lines, the **overlaps** between texts and buttons, what falls **outside the frame**, the color-channel state, the mod state and the last result data. Sending that file shows exactly how GD builds the window.

## Quality of the 8 main methods by object count

Measured with PSNR and SSIM (luma) against a common 512 px reference, on 4 images (photo, painting, flat illustration and logo) at 1,000 / 3,000 / 10,000 objects. PSNR in dB; in parentheses, before that version:

| Method | 1,000 | 3,000 | 10,000 |
|---|---|---|---|
| 3 BSP | 26.9 (26.7) | 34.2 (33.7) | 50.6 (50.3) |
| 4 Pyramid | 20.1 (19.7) | 23.8 (23.3) | 31.8 (30.8) |
| 7 Rotated rectangles | **30.4** (20.9) | **36.3** (23.3) | **44.9** (26.3) |
| 8 Hybrid | 30.0 (27.3) | 37.0 (32.9) | 51.3 (45.8) |

What changed:
* **Visibility recoloring:** in the layered methods (Pyramid, 7 and 8) each shape takes as its color the average of the pixels that are really visible, and shapes completely covered by others are **removed and their budget is reinvested** into more shapes (up to 3 rounds).
* **Rotated rectangles** now start from a coarse BSP base (6 %) and use the same internal resolution as the Hybrid; before, it started from a flat background and converged very slowly.
* **Hybrid:** 15 % BSP base (measured: anything between 10 % and 35 % gives the same; more base is worse). If BSP with the whole budget is already almost perfect (>= 38 dB: logos, illustrations), it returns the BSP.
* **BSP and Quadtree:** the priority looks two levels ahead (a poor cut that opens a very valuable one moves up the queue). A merge+split swap phase was also tried and **did not improve things**, so it was dropped: the greedy approach is already almost optimal in that family.
* **Strips:** picks rows or columns depending on what compresses better.
* **Search quality:** *High* by default (before, *Normal*), a new *Maximum* level, and with 3,000 objects or fewer it goes up one level by itself (there is time to spare). *High* gives ~+0.9 dB and +0.02 SSIM over *Normal* for ~1.7x the time; at 10,000 objects, 7 and 8 take up to ~5 s on a 512 px image.
* No noticeable changes: Grid (already the exact average), Regions.

## Fixes after the first test in the game

* **The Layer pyramid got stuck at a percentage forever** with transparent images and few objects (a sprite with a lot of border). The borders forced a minimum number of objects that did not depend on the threshold, and that minimum exceeded the budget; the safety loop raised the threshold endlessly. Now mixed cells (partly opaque, partly transparent) are handled by majority when the minimum does not fit, as BSP does, and all the safety loops have a cap. It was reproduced before the fix (hang with 1,500, 3,000 and 6,000 objects) and checked afterwards with 500, 1,500, 3,000 and 10,000 in the 4 color modes.
* **Safety notice:** if a calculation goes past 90 s it is canceled and the window says so.
* **Interface:** the description ran out of the window (now the text is measured and wrapped by hand), the tabs covered the title (they are smaller), and *Settings* was outside the frame.

## Structure

```
src/engine/   conversion engine WITHOUT Geode dependencies (core, partition, regions, geometrize, mono, palette, lightcolor, gdexport)
src/ui/       window (Popup): method, objects, size, preview, place; info box; UI diagnostics
src/main.cpp  button in the Edit tab (hook of EditorUI::createMoveMenu)
tests/        engine_test.cpp (validation) and bench.cpp (PSNR/SSIM per method and budget)
.github/      official geode-sdk/build-geode-mod workflow
```

## Building

**Easy option (recommended): GitHub Actions.** Upload the folder to a GitHub repo; the workflow builds for Windows, macOS, iOS and Android and leaves the combined `.geode` as an artifact ("Build Output").

**Local:** install the [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli) and the SDK, and run `geode build` in this folder.

`mod.json` points at **Geode 5.11.2 / GD 2.2081**. If your game/loader is different, change `geode` and `gd` in `mod.json` (your *Art importer* used 5.3.0, also with 2.2081).

## Testing the engine without Geode

```
g++ -O2 -std=c++20 -Iinclude -Isrc/engine tests/engine_test.cpp -o engine_test
./engine_test image.png 10000 output_folder all
```
For each method it checks: objects <= limit, quality (PSNR), and that the generated GD string, when reinterpreted, reproduces the same drawing.

## Measured results (10,000 objects)

PSNR (dB) against the image reduced to 512 px; on the painting (1000x1000) and the photo (1024x768):

| Method | Painting | Photo | Time |
|---|---|---|---|
| 1 Grid | 22.6 | 22.2 | ~15 ms |
| 2 Quadtree | 26.6 | 23.1 | ~40 ms |
| 3 BSP | 30.8 | 24.8 | ~55 ms |
| 4 Pyramid | 26.4 | 23.6 | ~65 ms |
| 5 Regions | 25.4 | 22.5 | ~170 ms |
| 6 Strips | 27.5 | 23.7 | ~250 ms |
| 7 Rotated | 26.4 | 24.4 | ~1.5 s |
| 8 Hybrid | **32.4** | **25.6** | ~1.2 s |

On flat illustration and pixel art the quality is nearly lossless with far fewer objects (e.g. a 900x600 flat image: Regions 1,796 objects, Hybrid 2,221; 64x64 pixel art: Regions 99 objects).
Times measured on 1 core; a 4000x3000 image takes <= 1.6 s in any method.

## What is NOT verified (read before using)

* **The Geode code (src/ui, src/main.cpp) has not been compiled or tested inside the game.** It was written against the real SDK 5.11.2 headers and the GD 2.2091 bindings, and its syntax was checked against a minimal mock, but the first real build may need a minor tweak.
* **White channel 1011:** the monochrome styles use channel 1010 (black, the same as Art importer) and 1011 as white; I could not check 1011 in the game. If the paper does not come out white, change the *White channel* setting.
* ASCII depends on the level's font and on the calibration described above.
* The rotation direction of the rotated rectangles (methods 7 and 8) follows the cocos2d convention (clockwise). If they come out rotated the wrong way, enable **"Flip rotation"**.
* Object 3092 with X/Y scale (keys 128/129) and the HSV color are taken from *Art importer*, which works; they have not been tested here in GD.
* The import **cannot be undone** (as in Art importer); save the level first. The objects are left selected so you can move or delete them.
