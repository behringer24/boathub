# Generates the KiCad netlist for the BoatHub carrier perfboard.
# Single source of truth for the component and net tables; validates that every
# declared pin lands in exactly one net before writing anything.

import io, sys

R_FP  = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"
C_FP  = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
CP8   = "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"
CP10  = "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
D_FP  = "Diode_THT:D_DO-201AD_P15.24mm_Horizontal"
def HDR(n): return "Connector_PinHeader_2.54mm:PinHeader_1x%02d_P2.54mm_Vertical" % n

# ref, value, footprint, lib, part, pin count, description
COMPONENTS = [
    ("J1", "12V IN",      HDR(2), "Connector_Generic", "Conn_01x02", 2, "12 V house supply, fused externally"),
    ("D1", "1.5KE20A",    D_FP,   "Device", "D_TVS",   2, "transient clamp, unidirectional"),
    ("C1", "100n/50V",    C_FP,   "Device", "C",       2, "HF bypass at the input"),
    ("R1", "82k 0.1%",    R_FP,   "Device", "R",       2, "battery divider, top leg"),
    ("R2", "10k 0.1%",    R_FP,   "Device", "R",       2, "battery divider, bottom leg"),
    ("R3", "1k",          R_FP,   "Device", "R",       2, "series into ADS1115 A0"),
    ("C2", "100n/50V",    C_FP,   "Device", "C",       2, "at A0 to GND"),
    ("J8", "ADS A0",      HDR(2), "Connector_Generic", "Conn_01x02", 2, "divider output to ADS1115 A0"),
    ("D2", "1N5822",      D_FP,   "Device", "D_Schottky", 2, "reverse polarity"),
    ("C3", "100u/35V",    CP8,    "Device", "CP",      2, "bulk, DC/DC input, 105 C"),
    ("U1", "DCDC 12V-5V", HDR(4), "Connector_Generic", "Conn_01x04", 4, "9-36 V to 5 V module, min 3 A"),
    ("C4", "470u/16V",    CP10,   "Device", "CP",      2, "bulk, 5 V output, 105 C"),
    ("C5", "100n/50V",    C_FP,   "Device", "C",       2, "HF bypass at the output"),
    ("J2", "5V OUT",      HDR(2), "Connector_Generic", "Conn_01x02", 2, "5 V to the carrier 5V terminal"),
    ("J3", "3V3 IN",      HDR(2), "Connector_Generic", "Conn_01x02", 2, "sensor rail from the carrier"),
    ("R4", "2k0", R_FP, "Device", "R", 2, "1-Wire pull-up, engine bay"),
    ("R7", "100R", R_FP, "Device", "R", 2, "1-Wire series, engine bay"),
    ("J4", "MOTOR-T", HDR(3), "Connector_Generic", "Conn_01x03", 3, "probe, engine bay"),
    ("R5", "2k0", R_FP, "Device", "R", 2, "1-Wire pull-up, bilge"),
    ("R8", "100R", R_FP, "Device", "R", 2, "1-Wire series, bilge"),
    ("J5", "BILGE-T", HDR(3), "Connector_Generic", "Conn_01x03", 3, "probe, bilge water"),
    ("R6", "2k0", R_FP, "Device", "R", 2, "1-Wire pull-up, fridge"),
    ("R9", "100R", R_FP, "Device", "R", 2, "1-Wire series, fridge"),
    ("J6", "FRIDGE-T", HDR(3), "Connector_Generic", "Conn_01x03", 3, "probe, fridge"),
    ("J7", "1-Wire OUT", HDR(4), "Connector_Generic", "Conn_01x04", 4, "DATA to carrier IO4/IO5/IO6 plus GND"),
]

NETS = [
    ("GND", [("J1",2),("D1",2),("C1",2),("R2",2),("C2",2),("J8",2),("C3",2),
             ("U1",2),("U1",4),("C4",2),("C5",2),("J2",2),("J3",2),
             ("J4",3),("J5",3),("J6",3),("J7",4)]),
    ("+12V_FUSED", [("J1",1),("D1",1),("C1",1),("R1",1),("D2",2)]),
    ("VBAT_SENSE", [("R1",2),("R2",1),("R3",1)]),
    ("ADS_A0",     [("R3",2),("C2",1),("J8",1)]),
    ("+12V_PROT",  [("D2",1),("C3",1),("U1",1)]),
    ("+5V",        [("U1",3),("C4",1),("C5",1),("J2",1)]),
    ("+3V3",       [("J3",1),("R4",1),("R5",1),("R6",1),("J4",1),("J5",1),("J6",1)]),
    ("OW1_BUS",  [("R4",2),("R7",1),("J4",2)]),
    ("OW1_GPIO", [("R7",2),("J7",1)]),
    ("OW2_BUS",  [("R5",2),("R8",1),("J5",2)]),
    ("OW2_GPIO", [("R8",2),("J7",2)]),
    ("OW3_BUS",  [("R6",2),("R9",1),("J6",2)]),
    ("OW3_GPIO", [("R9",2),("J7",3)]),
]

# --- validation -------------------------------------------------------------
errors = []
refs = [c[0] for c in COMPONENTS]
if len(refs) != len(set(refs)):
    errors.append("duplicate reference designator")
pins = {c[0]: c[5] for c in COMPONENTS}

seen = {}
for name, nodes in NETS:
    if len(nodes) < 2:
        errors.append("net %s has fewer than two nodes" % name)
    for ref, pin in nodes:
        if ref not in pins:
            errors.append("net %s references unknown component %s" % (name, ref))
            continue
        if not (1 <= pin <= pins[ref]):
            errors.append("net %s: %s has no pin %d" % (name, ref, pin))
        key = (ref, pin)
        if key in seen:
            errors.append("%s pin %d appears in both %s and %s" % (ref, pin, seen[key], name))
        seen[key] = name

for ref, count in pins.items():
    for p in range(1, count + 1):
        if (ref, p) not in seen:
            errors.append("%s pin %d is not on any net" % (ref, p))

if errors:
    for e in errors:
        print("ERROR:", e)
    sys.exit(1)

# --- emit -------------------------------------------------------------------
out = io.StringIO()
w = out.write
w('(export (version "E")\n')
w('  (design\n')
w('    (source "boathub-carrier")\n')
w('    (tool "BoatHub repository, hardware/kicad")\n')
w('    (sheet (number "1") (name "/") (tstamps "/")))\n')
w('  (components\n')
for i, (ref, val, fp, lib, part, _n, desc) in enumerate(COMPONENTS, 1):
    w('    (comp (ref "%s")\n' % ref)
    w('      (value "%s")\n' % val)
    w('      (footprint "%s")\n' % fp)
    w('      (description "%s")\n' % desc)
    w('      (libsource (lib "%s") (part "%s") (description "%s"))\n' % (lib, part, desc))
    w('      (sheetpath (names "/") (tstamps "/"))\n')
    w('      (tstamps "00000000-0000-0000-0000-%012d"))\n' % i)
w('  )\n')
w('  (nets\n')
for code, (name, nodes) in enumerate(NETS, 1):
    w('    (net (code "%d") (name "%s")\n' % (code, name))
    for ref, pin in nodes:
        w('      (node (ref "%s") (pin "%d") (pintype "passive"))\n' % (ref, pin))
    w('    )\n')
w('  )\n')
w(')\n')

path = 'hardware/kicad/boathub-carrier.net'
with io.open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(out.getvalue())

print("ok: %d components, %d nets, %d pins all assigned" % (len(COMPONENTS), len(NETS), len(seen)))
for name, nodes in NETS:
    print("  %-12s %2d nodes" % (name, len(nodes)))
