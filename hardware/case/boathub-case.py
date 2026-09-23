# -*- coding: utf-8 -*-
"""Enclosure for the BoatHub main board - a parametric FreeCAD model.

Run it, do not draw it. Every dimension below either comes from the KiCad
project in ../main-board or is a parameter with its reason written next to it,
so a change to the board is a change to a number here rather than a redraw.

    freecadcmd hardware/case/boathub-case.py

or, inside FreeCAD: View -> Panels -> Python console, then

    exec(open(r"<path>/boathub-case.py").read())

It writes boathub-case.FCStd and two STL files into ./export next to itself.

CABLES COME THROUGH GLANDS, NOT THROUGH SLOTS
---------------------------------------------
Every wall opening is a round hole for an M12 cable gland. The cable passes
through the wall and is screwed into its terminal from the inside, which is how
docs/MATERIAL.md already specifies the entries for phase C. One hole diameter
throughout: the gland decides what cable fits, not the print.

    LAPP 53111000  SKINTOP ST-M, M12 x 1.5, clamps 3.5 - 7 mm, IP69K, PA
    LAPP 53119000  matching lock nut, M12 x 1.5, SW 17, PA

Two of their dimensions set parameters here. The thread is 8 mm long, so a wall
thicker than about 4 mm leaves the nut nothing to catch. And the nut is SW 17
and sits against the inside face, so the board has to stand far enough back for
the nut to clear the terminal bodies - that is what CLEAR pays for.

WHAT THIS IS NOT
----------------
Not the enclosure the documentation specifies. docs/MATERIAL.md calls for a
bought ABS box rated IP65/IP67, because on a boat the enclosure is the corrosion
measure and an FDM print leaks along its layer lines. Treat this as a bench
housing, or as the starting point for something moulded.
"""

import os
import sys

try:
    import FreeCAD as App
    import Part
    from FreeCAD import Vector
except ImportError:
    sys.stderr.write("This has to run inside FreeCAD.\n"
                     "  freecadcmd hardware/case/boathub-case.py\n")
    raise

# ---------------------------------------------------------------- the board --

BOARD_W = 134.62          # Edge.Cuts, X
BOARD_D = 91.44           # Edge.Cuts, Y
PCB_T = 1.6

HOLES = [(7.62, 7.62), (127.00, 7.62), (7.62, 83.82), (127.00, 83.82)]

# Terminals, by the board edge they face and the span their pads cover, read
# out of BoatHub.kicad_pcb.
TERMINALS = [
    ("J2",  "left",   17.78, 33.02),   # SHT31
    ("J4",  "left",   40.64, 66.04),   # IMU
    ("J16", "right",  15.24, 30.48),   # MOTOR-T
    ("J15", "right",  38.10, 53.34),   # BILGE-T
    ("J14", "right",  60.96, 76.20),   # FRIDGE-T
    ("J12", "bottom", 17.78, 22.86),   # bilge level sender
    ("J9",  "top",    15.24, 20.32),   # 12 V in
    ("J17", "top",    68.58, 83.82),   # SeaTalk
]

# The IMU lives inside the box - B-002 terminates it on a screw terminal like
# the probes outside, but its cable never leaves. So it gets no entry.
NO_GLAND = ("J4",)

# What goes through each gland, engraved beside it. The names are the ones the
# design documents use, so the box and the schematic say the same thing.
LABELS = {"J2": "SHT31", "J9": "12V", "J12": "BILGE LVL", "J14": "FRIDGE-T",
          "J15": "BILGE-T", "J16": "MOTOR-T", "J17": "SEATALK"}

# No opening reaches the DevKit USB sockets, on purpose: in service the board
# runs off 12 V on J9, and USB is for the bench and for reflashing. Both mean
# taking the lid off.

# --------------------------------------------------------------- parameters --

WALL = 3.0                # 4.0 is the ceiling: the gland thread is 8 mm long
                          # and the nut needs what is left of it
FLOOR = 3.0
CLEAR = 8.0               # board edge to the inside of the wall. The SW 17 nut
                          # stands about 5 mm proud of that face and must not
                          # meet a terminal body; the rest is cable bend

STANDOFF_H = 6.0          # room for trimmed through-hole leads underneath
STANDOFF_OD = 8.0
STANDOFF_PILOT = 2.5      # M3 thread-forming. A heat-set insert wants 4.0

HEADROOM = 24.0           # above the board: the DevKit in its sockets needs
                          # about 14, the electrolytics and terminals the rest

GLAND_HOLE_D = 12.5       # M12 x 1.5 through a 3 mm wall
GLAND_RISE = 5.0          # above the board's top face, level with the wire
                          # entry of a CTB0509

CORNER_R = 6.0
FLANGE_R = 9.0            # the four screw ears, which are also the feet
FLANGE_OFF = 3.0
SCREW_CLEAR = 3.4
SCREW_PILOT = 2.5

LID_INSERT = True         # The lid is the way to the USB sockets, so it comes
LID_INSERT_D = 4.2        # off and goes back on repeatedly. A thread formed in
LID_INSERT_H = 6.0        # PLA or PETG survives a handful of cycles; a heat-set
                          # brass insert survives the boat.

LID_T = 3.0
LIP_H = 3.0
LIP_W = 2.5               # a ring, not a slab - a slab would be a second floor
LIP_CLEAR = 0.3

VENT_D = 6.0              # pressure equalisation, membrane glued over the
VENT_RECESS_D = 14.0      # recess on the outside. Fitted pointing down
VENT_RECESS_T = 1.0

ENGRAVE_D = 0.6           # deep enough to read, shallow enough not to weaken
LABEL_SIZE = 3.5          # cap height of the gland labels
LABEL_GAP = 2.5           # from the top of a gland hole to the text baseline
TITLE = "BoatHub"
TITLE_SIZE = 18.0
TITLE_DEPTH = 0.8
FRAME_INSET = 14.0        # an engraved border on the lid, for looks
FRAME_W = 1.2

FONTS = ["C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf",
         "C:/Windows/Fonts/arial.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]

# ------------------------------------------------------------------ derived --

OFF = WALL + CLEAR
W = BOARD_W + 2 * OFF
D = BOARD_D + 2 * OFF
H = FLOOR + STANDOFF_H + PCB_T + HEADROOM
Z_BOARD_TOP = FLOOR + STANDOFF_H + PCB_T
GLAND_Z = Z_BOARD_TOP + GLAND_RISE

FLANGES = [(-FLANGE_OFF, -FLANGE_OFF), (W + FLANGE_OFF, -FLANGE_OFF),
           (-FLANGE_OFF, D + FLANGE_OFF), (W + FLANGE_OFF, D + FLANGE_OFF)]

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else os.getcwd()
OUT = os.path.join(HERE, "export")
FONT = next((f for f in FONTS if os.path.isfile(f)), None)
warnings = []

# ------------------------------------------------------------- coordinates --


def bx(v):
    """Board X to case X. Both axes point the same way, so this is an offset."""
    return OFF + v


def by(v):
    """Board Y to case Y - and it turns over on the way.

    KiCad measures Y downwards from the top left of the board, FreeCAD measures
    it upwards. The board goes in component side up, so X carries over unchanged
    and Y does not. Getting this wrong is not a rotation that can be corrected
    on the bench: a mirror reverses one axis only, so turning the case round
    swaps left and right as well and the terminals still miss their openings.
    """
    return OFF + (BOARD_D - v)


def _flip(edge, lo, hi):
    """The same turn, applied to a terminal's edge and span."""
    if edge in ("left", "right"):
        return edge, BOARD_D - hi, BOARD_D - lo
    return ("bottom" if edge == "top" else "top"), lo, hi


def glands():
    """(wall, position along that wall, designator, label) per cable entry.

    The position is a finished case coordinate. _flip has already turned the
    axis over, so bx()/by() must not be applied again - doing it twice is the
    same as not doing it at all, and the result looks entirely plausible.
    """
    out = []
    for ref, edge, lo, hi in TERMINALS:
        if ref in NO_GLAND:
            continue
        e, a, b = _flip(edge, lo, hi)
        out.append((e, OFF + (a + b) / 2.0, ref, LABELS.get(ref, ref)))
    return out


# -------------------------------------------------------------- primitives --


def _vertical(edge):
    vs = edge.Vertexes
    if len(vs) != 2:
        return False
    a, b = vs[0].Point, vs[1].Point
    return abs(a.x - b.x) < 1e-7 and abs(a.y - b.y) < 1e-7 and abs(a.z - b.z) > 1e-7


def rounded_box(l, w, h, r, at=None):
    box = Part.makeBox(l, w, h, at or Vector(0, 0, 0))
    if r <= 0.01:
        return box
    return box.makeFillet(r, [e for e in box.Edges if _vertical(e)])


def tube(x, y, z, d, h, direction=None):
    return Part.makeCylinder(d / 2.0, h, Vector(x, y, z), direction or Vector(0, 0, 1))


def text_solid(s, size, thickness):
    """Flat text lying in XY, extruded up, centred on the origin in X.

    Part.makeWireString is the low level call behind ShapeString. It needs no
    document and no GUI, which is what makes this work under freecadcmd.
    """
    if FONT is None:
        raise RuntimeError("no font found")
    faces = []
    for char in Part.makeWireString(s, FONT, size, 0):
        made = []
        for wire in char:
            try:
                made.append(Part.Face(wire))
            except Exception:
                pass
        if not made:
            continue
        # the largest wire is the glyph, the rest are its counters
        made.sort(key=lambda f: f.Area, reverse=True)
        face = made[0]
        for hole in made[1:]:
            face = face.cut(hole)
        faces.append(face)
    if not faces:
        raise RuntimeError("nothing to engrave for %r" % s)
    flat = faces[0]
    for f in faces[1:]:
        flat = flat.fuse(f)
    solid = flat.extrude(Vector(0, 0, thickness))
    bb = solid.BoundBox
    solid.translate(Vector(-(bb.XMin + bb.XLength / 2.0), 0, 0))
    return solid


def engrave_wall(body, wall, pos, text):
    """Cut <text> into the outside of <wall>, centred on <pos>, above the hole."""
    solid = text_solid(text, LABEL_SIZE, ENGRAVE_D + 1.0)
    z = GLAND_Z + GLAND_HOLE_D / 2.0 + LABEL_GAP
    solid.rotate(Vector(0, 0, 0), Vector(1, 0, 0), 90)      # stand it upright
    if wall == "bottom":                                    # faces -Y
        solid.translate(Vector(pos, ENGRAVE_D, z))
    elif wall == "top":                                     # faces +Y
        solid.rotate(Vector(0, 0, 0), Vector(0, 0, 1), 180)
        solid.translate(Vector(pos, D - ENGRAVE_D, z))
    elif wall == "left":                                    # faces -X
        solid.rotate(Vector(0, 0, 0), Vector(0, 0, 1), -90)
        solid.translate(Vector(ENGRAVE_D, pos, z))
    else:                                                   # right, faces +X
        solid.rotate(Vector(0, 0, 0), Vector(0, 0, 1), 90)
        solid.translate(Vector(W - ENGRAVE_D, pos, z))
    return body.cut(solid)


# ------------------------------------------------------------------- base --


def build_base():
    body = rounded_box(W, D, H, CORNER_R)

    for x, y in FLANGES:
        body = body.fuse(tube(x, y, 0, 2 * FLANGE_R, H))

    body = body.cut(rounded_box(W - 2 * WALL, D - 2 * WALL, H + 1,
                                CORNER_R - WALL, Vector(WALL, WALL, FLOOR)))

    for hx, hy in HOLES:
        body = body.fuse(tube(bx(hx), by(hy), FLOOR, STANDOFF_OD, STANDOFF_H))
    for hx, hy in HOLES:
        body = body.cut(tube(bx(hx), by(hy), FLOOR, STANDOFF_PILOT, STANDOFF_H + 1.0))

    # the cable entries: one diameter everywhere, the gland decides the cable
    through = WALL + 2.0
    for wall, pos, ref, text in glands():
        if wall == "bottom":
            body = body.cut(tube(pos, -1.0, GLAND_Z, GLAND_HOLE_D, through,
                                 Vector(0, 1, 0)))
        elif wall == "top":
            body = body.cut(tube(pos, D - WALL - 1.0, GLAND_Z, GLAND_HOLE_D,
                                 through, Vector(0, 1, 0)))
        elif wall == "left":
            body = body.cut(tube(-1.0, pos, GLAND_Z, GLAND_HOLE_D, through,
                                 Vector(1, 0, 0)))
        else:
            body = body.cut(tube(W - WALL - 1.0, pos, GLAND_Z, GLAND_HOLE_D,
                                 through, Vector(1, 0, 0)))

    for wall, pos, ref, text in glands():
        try:
            body = engrave_wall(body, wall, pos, text)
        except Exception as e:
            warnings.append("label %s: %s" % (text, e))

    for x, y in FLANGES:
        if LID_INSERT:
            body = body.cut(tube(x, y, H - LID_INSERT_H, LID_INSERT_D,
                                 LID_INSERT_H + 1.0))
            body = body.cut(tube(x, y, H - LID_INSERT_H - 8.0, SCREW_CLEAR, 8.1))
        else:
            body = body.cut(tube(x, y, H - 14.0, SCREW_PILOT, 15.0))

    vx, vy = W / 2.0, WALL + 10.0
    body = body.cut(tube(vx, vy, -1.0, VENT_D, FLOOR + 2.0))
    body = body.cut(tube(vx, vy, -0.01, VENT_RECESS_D, VENT_RECESS_T))
    return body


# -------------------------------------------------------------------- lid --


def build_lid():
    """Built where it sits on the base, so the assembly reads correctly."""
    lid = rounded_box(W, D, LID_T, CORNER_R, Vector(0, 0, H))
    for x, y in FLANGES:
        lid = lid.fuse(tube(x, y, H, 2 * FLANGE_R, LID_T))

    lo = WALL + LIP_CLEAR
    lip_r = CORNER_R - WALL - LIP_CLEAR
    lip = rounded_box(W - 2 * lo, D - 2 * lo, LIP_H, lip_r, Vector(lo, lo, H - LIP_H))
    lip = lip.cut(rounded_box(W - 2 * lo - 2 * LIP_W, D - 2 * lo - 2 * LIP_W,
                              LIP_H + 2.0, max(lip_r - LIP_W, 0.0),
                              Vector(lo + LIP_W, lo + LIP_W, H - LIP_H - 1.0)))
    lid = lid.fuse(lip)

    for x, y in FLANGES:
        lid = lid.cut(tube(x, y, H - 1.0, SCREW_CLEAR, LID_T + 2.0))

    # an engraved border, so the top reads as a face rather than a slab
    top = H + LID_T
    frame = rounded_box(W - 2 * FRAME_INSET, D - 2 * FRAME_INSET, FRAME_W + 1.0,
                        CORNER_R, Vector(FRAME_INSET, FRAME_INSET, top - ENGRAVE_D))
    frame = frame.cut(rounded_box(W - 2 * FRAME_INSET - 2 * FRAME_W,
                                  D - 2 * FRAME_INSET - 2 * FRAME_W,
                                  FRAME_W + 3.0, max(CORNER_R - FRAME_W, 0.0),
                                  Vector(FRAME_INSET + FRAME_W, FRAME_INSET + FRAME_W,
                                         top - ENGRAVE_D - 1.0)))
    lid = lid.cut(frame)

    try:
        name = text_solid(TITLE, TITLE_SIZE, TITLE_DEPTH + 1.0)
        bb = name.BoundBox
        name.translate(Vector(W / 2.0, D / 2.0 - bb.YLength / 2.0, top - TITLE_DEPTH))
        lid = lid.cut(name)
    except Exception as e:
        warnings.append("title: %s" % e)
    return lid


# ------------------------------------------------------------------ output --


def export_stl(shape, path):
    try:
        import MeshPart
        MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.05,
                               AngularDeflection=0.15, Relative=False).write(path)
    except ImportError:
        shape.exportStl(path)


def main():
    base = build_base()
    lid = build_lid()

    doc = App.newDocument("BoatHubCase")
    Part.show(base, "Base")
    Part.show(lid, "Lid")
    doc.recompute()

    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    doc.saveAs(os.path.join(HERE, "boathub-case.FCStd"))
    export_stl(base, os.path.join(OUT, "boathub-case-base.stl"))

    flat = lid.copy()
    flat.rotate(Vector(0, 0, 0), Vector(1, 0, 0), 180)
    flat.translate(Vector(0, 0, -flat.BoundBox.ZMin))
    export_stl(flat, os.path.join(OUT, "boathub-case-lid.stl"))

    ear = 2 * (FLANGE_OFF + FLANGE_R)
    side = {"bottom": "front  y=0", "top": "back   y=max",
            "left": "left   x=0", "right": "right  x=max"}
    print("")
    print("  body           %.2f x %.2f x %.2f mm" % (W, D, H + LID_T))
    print("  with the ears  %.2f x %.2f mm" % (W + ear, D + ear))
    print("  cavity         %.2f x %.2f x %.2f mm" % (W - 2 * WALL, D - 2 * WALL,
                                                      H - FLOOR))
    print("  clear above    %.2f mm, board top at z = %.2f" % (H - Z_BOARD_TOP,
                                                              Z_BOARD_TOP))
    print("  gland holes    %.1f mm, centre at z = %.2f" % (GLAND_HOLE_D, GLAND_Z))
    print("  font           %s" % (FONT or "NONE - nothing engraved"))
    print("")
    for wall, pos, ref, text in glands():
        print("  entry          %-12s %-4s %-10s at %.2f"
              % (side[wall], ref, text, pos))
    print("")
    print("  Hold the board the way KiCad draws it, component side up. The")
    print("  12 V terminal is at the near edge; so is its gland.")
    for w in warnings:
        print("  WARNING        %s" % w)
    print("")


main()
