# Enclosure

A parametric FreeCAD model of a housing for the main board, kept as a script
rather than as a drawing so that a change to the board is a change to a number
here and not a redraw.

**This is not the enclosure the project specifies.** `docs/MATERIAL.md` calls
for a bought ABS box rated IP65/IP67, because on a boat the enclosure is the
corrosion measure, and an FDM print leaks along its layer lines. What follows is
a bench housing and a way of checking reach and clearance in three dimensions
before anything is bought.

## Running it

```
freecadcmd hardware/case/boathub-case.py
```

Or from FreeCAD's own Python console, *View → Panels → Python console*:

```python
exec(open(r"...\hardware\case\boathub-case.py").read())
```

Started that way the script has no `__file__`, so it writes into the current
working directory instead of next to itself. Run it from the command line if
that matters.

It produces:

| | |
|---|---|
| `boathub-case.FCStd` | the document, with `Base` and `Lid` as separate solids |
| `export/boathub-case-base.stl` | printed open side up |
| `export/boathub-case-lid.stl` | already turned over, so it lies flat on the bed |

## What the numbers mean

Everything under `# the board` was read out of `../main-board/BoatHub.kicad_pcb`
and should only change when the layout does. Everything under `# parameters` is
a decision, each with its reason beside it.

Board coordinates have their origin at the lower left corner of `Edge.Cuts`. The
case adds `WALL + CLEAR` on both axes, so board `(0, 0)` is case `(6, 6)`.

## The two coordinate systems

KiCad measures Y **downwards** from the top left of the board. FreeCAD measures
it **upwards**. The board goes into the case component side up, so X carries
over unchanged and Y turns over - `bx()` and `by()` do that once, and every
position in the model goes through them.

This is worth stating because getting it wrong produces a case that is entirely
self-consistent and still useless. A mirror reverses one axis only, so it cannot
be corrected by turning the case round: that would swap left and right as well.
The board would have to go in upside down for the terminals to meet their
openings.

Nothing in the arithmetic can catch it either - a mirrored model checks out
perfectly against itself. What catches it is holding the board next to the
screen, which is why the summary the script prints names each wall by where it
is rather than by an axis.

## Cable entries

Every wall opening is a round hole of one diameter for an M12 cable gland. The
cable passes through the wall and is screwed into its terminal from the inside,
which is how `docs/MATERIAL.md` already specifies the entries for phase C. The
gland decides what cable fits, not the print - so the 3 to 4 mm probe cables,
the 12 V feed and the SeaTalk run all use the same hole.

| | | |
|---|---|---|
| `LAPP 53111000` | SKINTOP ST-M, M12 x 1.5, clamps 3.5 - 7 mm, IP69K, PA | 0,68 EUR |
| `LAPP 53119000` | matching lock nut, M12 x 1.5, SW 17, PA glass filled | 0,25 EUR |

Seven entries, so 6,51 EUR. Plastic rather than brass on purpose: a boat has
enough dissimilar metals already.

Two of the gland's dimensions are constraints here rather than preferences.

**The thread is 8 mm long**, and the nut has to catch what the wall leaves. At
`WALL = 3.0` and a 4 mm nut that is 1 mm of engagement to spare; a 5 mm nut
leaves none. Measure the nut before raising `WALL`.

**The nut is SW 17** and sits against the inside face, standing about 5 mm
proud of it. `CLEAR = 8.0` is what keeps it off the terminal bodies, and it is
why the box is 10 mm wider and deeper than the board needs.

On the right hand wall the three probe terminals sit 22.86 mm apart, which
leaves **3.2 mm between the corners of neighbouring nuts**. Enough to turn them
by hand, not enough for a spanner. Fit that wall before the board goes in.

## Engraving

Each entry carries its name on the outside wall - `12V`, `SEATALK`, `MOTOR-T`
and so on, in the wording the design documents use, so the box and the schematic
agree. The lid carries `BoatHub` and an engraved border.

The text is cut with `Part.makeWireString`, the call underneath ShapeString,
which needs neither a document nor a GUI and so works under `freecadcmd`. It
needs a font file: `FONTS` lists candidates and the first one present wins. If
none is found, or a glyph will not build, the model is still produced and the
script prints a `WARNING` line rather than failing - engraving is decoration,
and decoration must not cost you the part.

## One thing worth knowing before printing

**The lid is the way in.** Nothing opens onto the DevKit USB sockets, on
purpose: in service the board runs off 12 V on J9, and USB is for the bench and
for reflashing. Both of those mean taking the lid off, so the lid is a service
part rather than something closed once.

Two things follow. The lid screws go into **heat-set brass inserts**
(`LID_INSERT`), because a thread formed straight into PLA or PETG survives a
handful of cycles and then spins. And before a USB cable goes anywhere near the
board, **the 12 V has to come off**: B-001 section 8 and
[A-002](../../docs/design/A-002-bench-setup-usb.md) both say USB and 12 V are
alternative supply paths, never parallel ones. The lid being the only way to the
USB socket is what makes that rule easy to keep - you cannot reach the socket
without having the terminals in front of you.
