# v1.0.0
- First version: 8 conversion methods, hard limit of 10,000 objects, preview and placement in the editor.

# v1.1.0
- 9 monochrome styles (halftone, engraving, cross-hatch, stippling, Bayer, ink, strokes, spiral, 10 PRINT) with no per-object HSV.
- Approximate level-string size indicator (~KB).
- Exact hue 0: it no longer shifts by 1 degree (less color error on pure reds).

# v1.1.1
- Methods 7 and 8: rotated rectangles no longer stick out past the image border (GD cannot clip them, so they left leftovers outside the frame). The image is now a clean rectangle; quality (PSNR) barely changes.
- New setting *Save interface diagnostics (txt)*: writes ui-dump.txt with the window's node tree (position, size, scale, real on-screen box), the overlaps and what falls outside the frame, plus the mod and result state.

# v1.1.2
- Fixed the Windows compile error (a variable named "small" clashed with a Windows macro) and the localtime warning.

# v1.1.3
- Interface re-laid out from ui-dump.txt: Settings no longer falls outside the frame or covers the "Light" tab; the tabs no longer overlap the title; the slider knobs no longer cover their labels (Objects / Long side); lower buttons, a slightly taller window (288), and the description/note anchored at the top so 3 lines do not collide with what is below.

# v1.1.4
- Texts are plain ASCII again: the GD font has no accents or special letters, and those characters disappeared.
- Method description and status note: each line is now its own label with fixed spacing. The huge gap between the first and second line is gone.
- The Main / Light tabs keep the active one blue (the button flipped its state after the click and both turned gray). The Channels / Gray HSV / Gray channels checks had the same bug and are now in sync too.
- Settings moves to the bottom left, on the same row and with the same size as Image and Place.
- The info button opens a custom box (400x250, no scroll): hand-wrapped text, left aligned, with a color per paragraph and the OK button always visible. It no longer runs off the screen or clips lines.
- UI diagnostics (setting "Save interface diagnostics"): it now dumps EVERYTHING. The whole scene and the full tree of every open window (mod, info, Geode settings, alerts), with position, anchor, size, scale, skew, rotation, color, opacity, children and real on-screen box; literal texts with their bytes, glyphs the font does not have, line count and real line spacing; text and button summaries; overlaps and overflows. It saves a snapshot when the window opens, on tab change, when opening info, when opening settings and on calculate, all in the same ui-dump.txt.

# v1.1.5
- Color channels: when channels 2-999 run out it no longer falls back to HSV; the allocation continues at 1020 and goes up to 9999, skipping 1000-1019 (special GD channels: BG, G1, G2, black, white...). Up to 9,978 channels are usable instead of 998.
- Channels: the mod remembers the channels it already handed out in the open editor and adds them to the ones detected in the level (objects, defined colors and channels copied by another channel), also in the 1020+ range. A new image always skips occupied channels and uses the next free one, so colors do not mix with those of a previous image.
- The UI diagnostics now include a COLOR CHANNELS section (occupied detected, occupied >= 1000, free, reserved) and the log shows the assigned range when placing.

# v1.1.6
- The whole mod is now in English: interface, info box, notifications, settings, diagnostics, documentation and test output. The "Calculation quality" setting now uses Fast / Normal / High / Maximum (old saved Spanish values still work).
