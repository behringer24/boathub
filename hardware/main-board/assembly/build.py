# -*- coding: utf-8 -*-
"""Interactive assembly guide for the BoatHub main board, drawn from the layout.

    python hardware/main-board/assembly/build.py

reads ../BoatHub.kicad_pcb and writes index.html next to itself: template.html
with the board drawing inserted. Open index.html in any browser - it needs no
server and nothing installed.

WHAT COMES FROM THE LAYOUT
--------------------------
The drawing is the layout itself: board outline, every footprint's silkscreen
and pads, the board texts and the hatched module areas, all read from the PCB
file. The orientation marks are derived from the footprints as well - a
diode's band from its pad 1, an electrolytic's minus half from its pad 2, the
converter's marked face from the side its pins sit closest to, a terminal's
wire entry from the board edge it faces - so they follow a part that moves.

WHAT DOES NOT
-------------
The words. template.html says "band to the left" and "opening at the bottom"
for this layout. A change that turns a part needs its sentence read again, and
so do the few layout-specific tables below: the pin labels beside the module
sockets, and the silkscreen legends whose tokens are placed on the pads they
name.

Standard library only.
"""

import html
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, '..', 'BoatHub.kicad_pcb')
TEMPLATE = os.path.join(HERE, 'template.html')
OUTPUT = os.path.join(HERE, 'index.html')

# Fitted on purpose as bare pads; drawn hollow.
EMPTY = {'C7', 'C8', 'C9', 'C10', 'C11', 'C12', 'JP2'}

# What pin 1 of each module socket carries, and where its label sits relative
# to the pad (mm, text anchor). Placed on the side the module body covers, or
# outside the DevKit, where the drawing has room.
PIN1_LABELS = {
    'J6': ('VDD', (0.0, -2.2), 'middle'),
    'J10': ('VDD', (-2.3, 0.5), 'end'),
    'J7': ('3V3', (-1.9, 0.5), 'end'),
    'J11': ('GND', (1.9, 0.5), 'start'),
}

# The hatched silkscreen areas mark where a module stands over the board. Each
# belongs to the socket it touches; the rest lie under the DevKit.
MODULE_SOCKETS = {'J6': 'ADS1', 'J10': 'ADS2'}

# Silkscreen legends that name individual pins. The board aligns them with
# spaces in KiCad's font; a browser font has other widths, so each token is put
# on the pad it names instead. Pad positions may be fractional to sit between
# pads.
PIN_LEGENDS = {
    'SDA SCL GND VIN': ('J2', [('VIN', 1), ('GND', 2), ('SCL', 3), ('SDA', 4)]),
    'INT SCL SDA GND VIN': ('J4', [('VIN', 1), ('GND', 2), ('SDA', 3), ('SCL', 4), ('INT', 5)]),
    'AD1 | AD2': ('J1', [('AD1', 2.5), ('|', 3.5), ('AD2', 5.5)]),
    '+ 2 3 0 1 2 3 GND': ('J1', [('+', 1), ('2', 2), ('3', 3), ('0', 4), ('1', 5), ('2', 6),
                                 ('3', 7), ('GND', 8)]),
    '+ -': ('J9', [('+', 2), ('\u2212', 1)]),
}

BAND = 1.75          # width of the drawn cathode band, mm
ARROW_OUT = 4.1      # wire-entry arrows start this far outside the board edge
ARROW_IN = 0.45      # and end this far outside it


# ---------------------------------------------------------------- s-expressions

TOKEN = re.compile(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()"]+')


def parse(text):
    stack = [[]]
    for m in TOKEN.finditer(text):
        t = m.group(0)
        if t == '(':
            stack.append([])
        elif t == ')':
            node = stack.pop()
            stack[-1].append(node)
        else:
            if t.startswith('"'):
                t = t[1:-1].replace('\\"', '"')
            stack[-1].append(t)
    return stack[0][0]


def find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def first(node, key):
    found = find(node, key)
    return found[0] if found else None


# ---------------------------------------------------------------- geometry

def fmt(v):
    return f'{v:.2f}'.rstrip('0').rstrip('.')


def esc(s):
    return html.escape(s, quote=True)


def to_board(fx, fy, rot, lx, ly):
    """Footprint-local to board coordinates. KiCad rotates counter-clockwise
    on a y-down canvas."""
    t = math.radians(rot)
    return (fx + lx * math.cos(t) + ly * math.sin(t), fy - lx * math.sin(t) + ly * math.cos(t))


def unit(dx, dy):
    n = math.hypot(dx, dy)
    return (dx / n, dy / n)


def arc_path(p1, pm, p2):
    """SVG arc through three points."""
    (x1, y1), (xm, ym), (x2, y2) = p1, pm, p2
    d = 2 * (x1 * (ym - y2) + xm * (y2 - y1) + x2 * (y1 - ym))
    ux = ((x1**2 + y1**2) * (ym - y2) + (xm**2 + ym**2) * (y2 - y1) + (x2**2 + y2**2) * (y1 - ym)) / d
    uy = ((x1**2 + y1**2) * (x2 - xm) + (xm**2 + ym**2) * (x1 - x2) + (x2**2 + y2**2) * (xm - x1)) / d
    r = math.hypot(x1 - ux, y1 - uy)
    sweep = 1 if (xm - x1) * (y2 - y1) - (ym - y1) * (x2 - x1) > 0 else 0   # clockwise on screen
    a1, a2 = math.atan2(y1 - uy, x1 - ux), math.atan2(y2 - uy, x2 - ux)
    span = (a2 - a1) % (2 * math.pi) if sweep else (a1 - a2) % (2 * math.pi)
    large = 1 if span > math.pi else 0
    return f'M{fmt(x1)} {fmt(y1)} A{fmt(r)} {fmt(r)} 0 {large} {sweep} {fmt(x2)} {fmt(y2)}'


def svg_text(x, y, s, size, cls='silk-txt', anchor='middle', angle=0):
    tr = f' transform="rotate({fmt(-angle)} {fmt(x)} {fmt(y)})"' if angle else ''
    return (f'<text class="{cls}" x="{fmt(x)}" y="{fmt(y)}" font-size="{fmt(size)}" '
            f'text-anchor="{anchor}"{tr}>{esc(s)}</text>')


def arrow(x1, y1, x2, y2):
    a = math.atan2(y2 - y1, x2 - x1)
    head, half = 1.3, 0.75
    bx, by = x2 - head * math.cos(a), y2 - head * math.sin(a)
    px, py = -math.sin(a) * half, math.cos(a) * half
    return (f'<line class="pol-line" x1="{fmt(x1)}" y1="{fmt(y1)}" x2="{fmt(bx)}" y2="{fmt(by)}"/>'
            f'<polygon class="pol-fill" points="{fmt(x2)},{fmt(y2)} {fmt(bx + px)},{fmt(by + py)} '
            f'{fmt(bx - px)},{fmt(by - py)}"/>')


# ---------------------------------------------------------------- footprints

class Footprint:
    def __init__(self, node):
        self.node = node
        self.lib = node[1].split(':')[-1]
        self.ref = next(p[2] for p in find(node, 'property') if p[1] == 'Reference')
        at = first(node, 'at')
        self.x, self.y = float(at[1]), float(at[2])
        self.rot = float(at[3]) if len(at) > 3 else 0.0
        self.pads = {}
        for p in find(node, 'pad'):
            pat = first(p, 'at')
            w, h = (float(v) for v in first(p, 'size')[1:3])
            angle = float(pat[3]) if len(pat) > 3 else 0.0
            if round(angle) % 180 == 90:
                w, h = h, w
            drill = first(p, 'drill')
            self.pads[p[1]] = {
                'pos': self.board(float(pat[1]), float(pat[2])),
                'w': w, 'h': h, 'shape': p[3],
                'drill': float(drill[1]) if drill else 0.0,
            }

    def board(self, lx, ly):
        return to_board(self.x, self.y, self.rot, lx, ly)

    def silk(self):
        """Silkscreen graphics in board coordinates, as (kind, data, width)."""
        out = []
        for g in self.node:
            if not isinstance(g, list) or g[0] not in ('fp_line', 'fp_rect', 'fp_circle', 'fp_arc', 'fp_poly'):
                continue
            if first(g, 'layer')[1] != 'F.SilkS':
                continue
            stroke = first(g, 'stroke')
            width = max(float(first(stroke, 'width')[1]) if stroke else 0.12, 0.15)
            pt = lambda k: self.board(float(first(g, k)[1]), float(first(g, k)[2]))
            if g[0] == 'fp_line':
                out.append(('line', (pt('start'), pt('end')), width))
            elif g[0] == 'fp_rect':
                out.append(('rect', (pt('start'), pt('end')), width))
            elif g[0] == 'fp_circle':
                c, e = pt('center'), pt('end')
                out.append(('circle', (c, math.hypot(e[0] - c[0], e[1] - c[1])), width))
            elif g[0] == 'fp_arc':
                out.append(('arc', (pt('start'), pt('mid'), pt('end')), width))
            else:
                pts = [self.board(float(p[1]), float(p[2])) for p in first(g, 'pts') if isinstance(p, list) and p[0] == 'xy']
                out.append(('poly', pts, width))
        return out

    def texts(self):
        """Visible silkscreen texts: the reference and any user text such as a diode's K."""
        out = []
        for g in find(self.node, 'fp_text') + [p for p in find(self.node, 'property') if p[1] == 'Reference']:
            layer = first(g, 'layer')
            effects = first(g, 'effects')
            if not layer or layer[1] != 'F.SilkS' or first(g, 'hide') or (effects and first(effects, 'hide')):
                continue
            at = first(g, 'at')
            x, y = self.board(float(at[1]), float(at[2]))
            angle = float(at[3]) % 360 if len(at) > 3 else 0.0
            if 90 < angle <= 270:          # KiCad keeps footprint text upright
                angle -= 180
            size = float(first(first(effects, 'font'), 'size')[1]) if effects else 1.0
            out.append((g[2], x, y, angle, size, g[0] == 'property'))
        return out


def pad_svg(pad, cls='pad'):
    x, y = pad['pos']
    w, h = pad['w'], pad['h']
    out = []
    if pad['shape'] == 'circle':
        out.append(f'<circle class="{cls}" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(w / 2)}"/>')
    else:
        rx = {'oval': min(w, h) / 2, 'roundrect': min(w, h) * 0.25}.get(pad['shape'], 0)
        out.append(f'<rect class="{cls}" x="{fmt(x - w / 2)}" y="{fmt(y - h / 2)}" width="{fmt(w)}" '
                   f'height="{fmt(h)}" rx="{fmt(rx)}"/>')
    if pad['drill']:
        out.append(f'<circle class="drill" cx="{fmt(x)}" cy="{fmt(y)}" r="{fmt(pad["drill"] / 2)}"/>')
    return ''.join(out)


def silk_svg(kind, data, width, cls='silk'):
    sw = fmt(width)
    if kind == 'line':
        (x1, y1), (x2, y2) = data
        return f'<line class="{cls}" x1="{fmt(x1)}" y1="{fmt(y1)}" x2="{fmt(x2)}" y2="{fmt(y2)}" stroke-width="{sw}"/>'
    if kind == 'rect':
        (x1, y1), (x2, y2) = data
        return (f'<rect class="{cls}" x="{fmt(min(x1, x2))}" y="{fmt(min(y1, y2))}" width="{fmt(abs(x2 - x1))}" '
                f'height="{fmt(abs(y2 - y1))}" stroke-width="{sw}"/>')
    if kind == 'circle':
        (cx, cy), r = data
        return f'<circle class="{cls}" cx="{fmt(cx)}" cy="{fmt(cy)}" r="{fmt(r)}" stroke-width="{sw}"/>'
    if kind == 'arc':
        return f'<path class="{cls}" d="{arc_path(*data)}" stroke-width="{sw}"/>'
    return '<polygon class="silk-fill" points="' + ' '.join(f'{fmt(x)},{fmt(y)}' for x, y in data) + '"/>'


def silk_points(kind, data):
    if kind in ('line', 'rect'):
        return list(data)
    if kind == 'circle':
        (cx, cy), r = data
        return [(cx - r, cy - r), (cx + r, cy + r)]
    return list(data)


# ---------------------------------------------------------------- orientation marks

def marks(fp, silk, edge):
    """Returns (orientation overlay, silk items to draw, each with its class)."""
    items = [(k, d, w, 'silk') for k, d, w in silk]
    over = []
    p1 = fp.pads.get('1')

    if fp.lib.startswith('D_'):
        # The band sits at the pad 1 end of the body.
        body = max((d for k, d, w in silk if k == 'rect'),
                   key=lambda d: abs(d[1][0] - d[0][0]) * abs(d[1][1] - d[0][1]))
        x0, x1 = sorted((body[0][0], body[1][0]))
        y0, y1 = sorted((body[0][1], body[1][1]))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if x1 - x0 > y1 - y0:
            bx = x0 if p1['pos'][0] < cx else x1 - BAND
            over.append(f'<rect class="pol-fill" x="{fmt(bx)}" y="{fmt(y0)}" width="{BAND}" height="{fmt(y1 - y0)}"/>')
        else:
            by = y0 if p1['pos'][1] < cy else y1 - BAND
            over.append(f'<rect class="pol-fill" x="{fmt(x0)}" y="{fmt(by)}" width="{fmt(x1 - x0)}" height="{BAND}"/>')

    elif fp.lib.startswith('CP_'):
        # The minus half faces pad 2. The footprint fills it with hatch lines;
        # those are replaced by the overlay.
        (c, r) = max((d for k, d, w in silk if k == 'circle'), key=lambda d: d[1])
        ux, uy = unit(fp.pads['2']['pos'][0] - c[0], fp.pads['2']['pos'][1] - c[1])
        px, py = -uy, ux
        start = (c[0] + px * r, c[1] + py * r)
        mid = (c[0] + ux * r, c[1] + uy * r)
        end = (c[0] - px * r, c[1] - py * r)
        over.append(f'<path class="pol-half" d="{arc_path(start, mid, end)} Z"/>')
        size = r * 0.6
        over.append(svg_text(c[0] + ux * r * 0.45, c[1] + uy * r * 0.45 + size * 0.35, '\u2212', size, 'pol-txt'))

        def in_minus_half(k, d):
            if k != 'line':
                return False
            (ax, ay), (bx, by) = d
            perpendicular = abs((bx - ax) * ux + (by - ay) * uy) < 0.01
            return perpendicular and all((x - c[0]) * ux + (y - c[1]) * uy > 0.05 for x, y in d)
        items = [it for it in items if not in_minus_half(it[0], it[1])]

    elif fp.lib.startswith('DIP-'):
        # Dot inside the body corner nearest pin 1; the notch arc in orange.
        pts = [p for k, d, w in silk if k == 'line' for p in d]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        cx = min(xs) if abs(p1['pos'][0] - min(xs)) < abs(p1['pos'][0] - max(xs)) else max(xs)
        cy = min(ys) if abs(p1['pos'][1] - min(ys)) < abs(p1['pos'][1] - max(ys)) else max(ys)
        dx = 0.9 if cx == min(xs) else -0.9
        dy = 0.9 if cy == min(ys) else -0.9
        over.append(f'<circle class="pol-fill" cx="{fmt(cx + dx)}" cy="{fmt(cy + dy)}" r="0.6"/>')
        items = [(k, d, w, 'pol-line' if k == 'arc' else c) for k, d, w, c in items]

    elif fp.lib.startswith('TO-92'):
        # The straight silkscreen line is the flat face.
        items = [(k, d, w, 'pol-thick' if k == 'line' else c) for k, d, w, c in items]

    elif fp.lib.startswith('Converter_DCDC'):
        # The marked face is the body edge nearest the pin row; the dot is at pin 1.
        p3 = fp.pads['3']['pos']
        rx, ry = unit(p3[0] - p1['pos'][0], p3[1] - p1['pos'][1])
        nx, ny = -ry, rx
        row_len = math.hypot(p3[0] - p1['pos'][0], p3[1] - p1['pos'][1])

        def offset(d):
            mx, my = (d[0][0] + d[1][0]) / 2, (d[0][1] + d[1][1]) / 2
            return (mx - p1['pos'][0]) * nx + (my - p1['pos'][1]) * ny
        faces = [d for k, d, w in silk if k == 'line'
                 and abs(((d[1][0] - d[0][0]) * nx + (d[1][1] - d[0][1]) * ny)) < 0.01
                 and math.hypot(d[1][0] - d[0][0], d[1][1] - d[0][1]) > 2 * row_len]
        face = min(faces, key=lambda d: abs(offset(d)))
        side = 1 if offset(face) > 0 else -1
        (ax, ay), (bx, by) = face
        over.append(f'<line class="pol-thick" x1="{fmt(ax)}" y1="{fmt(ay)}" x2="{fmt(bx)}" y2="{fmt(by)}"/>')
        dot = (p1['pos'][0] - rx * 2.3 + nx * side, p1['pos'][1] - ry * 2.3 + ny * side)
        over.append(f'<circle class="pol-fill" cx="{fmt(dot[0])}" cy="{fmt(dot[1])}" r="0.55"/>')

    elif fp.lib.startswith('TerminalBlock'):
        # One arrow per pole, from outside the nearest board edge.
        pads = [p['pos'] for p in fp.pads.values()]
        mx, my = sum(p[0] for p in pads) / len(pads), sum(p[1] for p in pads) / len(pads)
        x0, y0, x1, y1 = edge
        side = min((mx - x0, 'left'), (x1 - mx, 'right'), (my - y0, 'top'), (y1 - my, 'bottom'))[1]
        for px, py in pads:
            if side == 'left':
                over.append(arrow(x0 - ARROW_OUT, py, x0 - ARROW_IN, py))
            elif side == 'right':
                over.append(arrow(x1 + ARROW_OUT, py, x1 + ARROW_IN, py))
            elif side == 'top':
                over.append(arrow(px, y0 - ARROW_OUT, px, y0 - ARROW_IN))
            else:
                over.append(arrow(px, y1 + ARROW_OUT, px, y1 + ARROW_IN))

    if fp.ref in PIN1_LABELS:
        label, (ox, oy), anchor = PIN1_LABELS[fp.ref]
        x, y = p1['pos']
        over.append(f'<circle class="pol-ring" cx="{fmt(x)}" cy="{fmt(y)}" r="1.25"/>')
        over.append(svg_text(x + ox, y + oy, label, 1.5, 'pol-txt', anchor))

    return ''.join(over), items


# ---------------------------------------------------------------- board texts

def board_texts(pcb, footprints, revision):
    out = []
    for g in find(pcb, 'gr_text'):
        if first(g, 'layer')[1] != 'F.SilkS':
            continue
        text = g[1].replace('${REVISION}', revision)
        at = first(g, 'at')
        x, y = float(at[1]), float(at[2])
        angle = float(at[3]) if len(at) > 3 else 0.0
        effects = first(g, 'effects')
        size = float(first(first(effects, 'font'), 'size')[2])
        justify = first(effects, 'justify')
        justify = justify[1:] if justify else []
        anchor = 'start' if 'left' in justify else ('end' if 'right' in justify else 'middle')

        lines = text.split('\\n')
        step = size * 1.62
        n = len(lines)
        if 'bottom' in justify:
            offsets = [-(n - 1 - i) * step for i in range(n)]
        elif 'top' in justify:
            offsets = [size + i * step for i in range(n)]
        else:
            offsets = [-((n - 1) * step + size) / 2 + size + i * step for i in range(n)]

        t = math.radians(angle)
        for line, off in zip(lines, offsets):
            # the text's own downward direction, on the board
            bx, by = x + off * math.sin(t), y + off * math.cos(t)
            legend = PIN_LEGENDS.get(' '.join(line.split()))
            if legend:
                ref, tokens = legend
                fp = footprints[ref]
                pads = sorted(fp.pads.items(), key=lambda kv: int(kv[0]))
                horizontal = abs(pads[-1][1]['pos'][0] - pads[0][1]['pos'][0]) > abs(pads[-1][1]['pos'][1] - pads[0][1]['pos'][1])
                for token, pos in tokens:
                    lo = fp.pads[str(int(pos))]['pos']
                    hi = fp.pads[str(min(int(pos) + 1, len(pads)))]['pos']
                    frac = pos - int(pos)
                    px, py = lo[0] + (hi[0] - lo[0]) * frac, lo[1] + (hi[1] - lo[1]) * frac
                    out.append(svg_text(px if horizontal else bx, by if horizontal else py, token, size * 1.3,
                                        angle=angle))
            else:
                out.append(svg_text(bx, by, line.strip(), size * 1.3, anchor=anchor, angle=angle))
    return out


# ---------------------------------------------------------------- assembly

def build():
    with open(PCB, encoding='utf-8') as f:
        pcb = parse(f.read())
    revision = next(p[2] for p in find(pcb, 'property') if p[1] == 'REVISION')

    outline = next(g for g in find(pcb, 'gr_rect') if first(g, 'layer')[1] == 'Edge.Cuts')
    ex0, ey0 = float(first(outline, 'start')[1]), float(first(outline, 'start')[2])
    ex1, ey1 = float(first(outline, 'end')[1]), float(first(outline, 'end')[2])
    edge = (min(ex0, ex1), min(ey0, ey1), max(ex0, ex1), max(ey0, ey1))

    footprints = {}
    for node in find(pcb, 'footprint'):
        fp = Footprint(node)
        footprints[fp.ref] = fp

    holes, groups = [], []
    for ref, fp in footprints.items():
        if ref.startswith('H'):
            holes += [pad_svg(p) for p in fp.pads.values()]
            continue
        silk = fp.silk()
        overlay, items = marks(fp, silk, edge)
        pts = [p for k, d, w in silk for p in silk_points(k, d)]
        for p in fp.pads.values():
            (x, y), w, h = p['pos'], p['w'], p['h']
            pts += [(x - w / 2, y - h / 2), (x + w / 2, y + h / 2)]
        hx0, hy0 = min(p[0] for p in pts) - 0.5, min(p[1] for p in pts) - 0.5
        hx1, hy1 = max(p[0] for p in pts) + 0.5, max(p[1] for p in pts) + 0.5
        texts = [svg_text(x, y + size * 0.36, s, size * 1.25, 'ref' if is_ref else 'silk-txt', angle=angle)
                 for s, x, y, angle, size, is_ref in fp.texts()]
        cls = 'fp empty' if ref in EMPTY else 'fp'
        groups.append(
            f'<g class="{cls}" id="fp-{ref}" data-ref="{ref}">'
            f'<rect class="halo" x="{fmt(hx0)}" y="{fmt(hy0)}" width="{fmt(hx1 - hx0)}" height="{fmt(hy1 - hy0)}" rx="0.8"/>'
            + ''.join(silk_svg(k, d, w, c) for k, d, w, c in items)
            + ''.join(pad_svg(p) for p in fp.pads.values())
            + f'<g class="pol">{overlay}</g>' + ''.join(texts) + '</g>')

    # Hatched module areas, grouped so that each can be selected as a module.
    modules = {'ADS1': [], 'ADS2': [], 'DEVKIT': []}
    for g in find(pcb, 'gr_rect'):
        if first(g, 'layer')[1] != 'F.SilkS' or first(g, 'fill')[1] != 'hatch':
            continue
        x0, x1 = sorted((float(first(g, 'start')[1]), float(first(g, 'end')[1])))
        y0, y1 = sorted((float(first(g, 'start')[2]), float(first(g, 'end')[2])))
        owner = 'DEVKIT'
        for sock, module in MODULE_SOCKETS.items():
            if any(x0 - 1.5 <= p['pos'][0] <= x1 + 1.5 and y0 - 1.5 <= p['pos'][1] <= y1 + 1.5
                   for p in footprints[sock].pads.values()):
                owner = module
        modules[owner].append((x0, y0, x1, y1))

    # The DevKit itself is drawn only as an approximate dashed outline between
    # its two socket rows, from beyond pin 1 down to the USB sockets.
    j7, j11 = footprints['J7'].pads, footprints['J11'].pads
    dk = (j7['1']['pos'][0] - 2.26, j7['1']['pos'][1] - 7.92,
          j11['1']['pos'][0] + 2.24, max(r[3] for r in modules['DEVKIT']))

    mod_svg = []
    for name, rects in modules.items():
        boxes = list(rects) + ([dk] if name == 'DEVKIT' else [])
        hx0 = min(r[0] for r in boxes) - 0.5
        hy0 = min(r[1] for r in boxes) - 0.5
        hx1 = max(r[2] for r in boxes) + 0.5
        hy1 = max(r[3] for r in boxes) + 0.5
        body = ''.join(f'<rect class="hatch" x="{fmt(r[0])}" y="{fmt(r[1])}" width="{fmt(r[2] - r[0])}" '
                       f'height="{fmt(r[3] - r[1])}"/>' for r in rects)
        if name == 'DEVKIT':
            body = (f'<rect class="devkit" x="{fmt(dk[0])}" y="{fmt(dk[1])}" width="{fmt(dk[2] - dk[0])}" '
                    f'height="{fmt(dk[3] - dk[1])}" rx="1"/>' + body
                    + svg_text((dk[0] + dk[2]) / 2, dk[1] + 2.1, 'antenna \u2191 (approx.)', 1.3, 'devkit-txt'))
        mod_svg.append(f'<g class="fp mod" id="fp-{name}" data-ref="{name}">'
                       f'<rect class="halo" x="{fmt(hx0)}" y="{fmt(hy0)}" width="{fmt(hx1 - hx0)}" '
                       f'height="{fmt(hy1 - hy0)}" rx="0.8"/>{body}</g>')

    margin = ARROW_OUT + 1.0
    vb = (edge[0] - margin, edge[1] - margin, edge[2] - edge[0] + 2 * margin, edge[3] - edge[1] + 2 * margin)
    svg = (f'<svg id="board" viewBox="{" ".join(fmt(v) for v in vb)}" xmlns="http://www.w3.org/2000/svg" '
           'role="img" aria-label="Component side of the BoatHub main board with every part position">'
           '<defs><pattern id="hatch" width="1.1" height="1.1" patternUnits="userSpaceOnUse" '
           'patternTransform="rotate(45)"><line class="hatch-line" x1="0" y1="0" x2="0" y2="1.1"/></pattern></defs>'
           f'<rect class="board" x="{fmt(edge[0])}" y="{fmt(edge[1])}" width="{fmt(edge[2] - edge[0])}" '
           f'height="{fmt(edge[3] - edge[1])}" rx="0.4"/>'
           # one element per line, so that a layout change shows up as a readable diff
           + '\n' + '\n'.join(holes)
           + '\n<g id="silk-board">\n' + '\n'.join(board_texts(pcb, footprints, revision)) + '\n</g>\n'
           + '\n'.join(mod_svg) + '\n' + '\n'.join(groups) + '\n</svg>')

    with open(TEMPLATE, encoding='utf-8') as f:
        page = f.read()
    page = page.replace('<!--BOARD-->', svg).replace('{{REVISION}}', esc(revision))
    with open(OUTPUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(page)
    print(f'{os.path.relpath(OUTPUT)}: revision {revision}, {len(groups)} parts')


if __name__ == '__main__':
    build()
