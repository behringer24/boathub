# Generates the KiCad netlist for the BoatHub main board.
#
# The component and net tables here are the single source of truth; the tables
# in docs/design/B-002-main-board.md restate them for a human reader. The
# generator validates before writing: every reference unique, every pin either
# on exactly one net or explicitly declared a no-connect, no net with fewer
# than two nodes. A netlist that assembles cleanly is not necessarily right,
# but one that does not assemble is certainly wrong, and the check is free.

import io
import sys

R_FP = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"
C_FP = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
CP8 = "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"
CP10 = "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
D_FP = "Diode_THT:D_DO-201AD_P15.24mm_Horizontal"


def HDR(n):
    return "Connector_PinHeader_2.54mm:PinHeader_1x%02d_P2.54mm_Vertical" % n


def SOCK(n):
    return "Connector_PinSocket_2.54mm:PinSocket_1x%02d_P2.54mm_Vertical" % n


# ref, value, footprint, library, part, pin count, description
COMPONENTS = [
    # DevKit socket, replaces the Heemol carrier
    ("J1", "DevKit col A", SOCK(22), "Connector_Generic", "Conn_01x22", 22,
     "ESP32-S3-DevKitC-1, column A"),
    ("J2", "DevKit col B", SOCK(22), "Connector_Generic", "Conn_01x22", 22,
     "ESP32-S3-DevKitC-1, column B"),
    # 12 V entry, protection and conversion, from B-001
    ("J3", "12V IN", HDR(2), "Connector_Generic", "Conn_01x02", 2,
     "12 V house supply, fused externally"),
    ("D1", "1.5KE20A", D_FP, "Device", "D_Zener", 2,
     "transient clamp, unidirectional - a zener symbol, so pin 1 is the cathode"),
    ("C1", "100n/50V", C_FP, "Device", "C", 2, "HF bypass at the input"),
    ("R1", "100k 0.1%", R_FP, "Device", "R", 2, "battery divider, top leg"),
    ("R2", "10k 0.1%", R_FP, "Device", "R", 2, "battery divider, bottom leg"),
    ("R3", "1k", R_FP, "Device", "R", 2, "series into ADS1115 A0"),
    ("C2", "100n/50V", C_FP, "Device", "C", 2, "at A0 to GND"),
    ("D2", "1N5822", D_FP, "Device", "D_Schottky", 2, "reverse polarity"),
    ("C3", "100u/35V", CP8, "Device", "CP", 2, "bulk, DC/DC input, 105 C"),
    # SIP-3, three pins at 2.54 mm: 1 = +VIN, 2 = GND, 3 = +VOUT. The pin header
    # footprint is geometrically right and always resolves; KiCad's own RECOM
    # footprint is the better choice where the installed libraries carry one.
    ("U1", "R-78K5.0-1.0", HDR(3), "Connector_Generic", "Conn_01x03", 3,
     "RECOM switching regulator, 6.5-36 V in, 5 V 1 A out, SIP-3"),
    ("C4", "470u/16V", CP10, "Device", "CP", 2, "bulk, 5 V output, 105 C"),
    ("C5", "100n/50V", C_FP, "Device", "C", 2, "HF bypass at the output"),
    # 1-Wire, one channel per probe, from A-003
    ("R4", "2k0", R_FP, "Device", "R", 2, "1-Wire pull-up, engine bay"),
    ("R7", "100R", R_FP, "Device", "R", 2, "1-Wire series, engine bay"),
    ("J4", "MOTOR-T", HDR(3), "Connector_Generic", "Conn_01x03", 3, "probe, engine bay"),
    ("R5", "2k0", R_FP, "Device", "R", 2, "1-Wire pull-up, bilge"),
    ("R8", "100R", R_FP, "Device", "R", 2, "1-Wire series, bilge"),
    ("J5", "BILGE-T", HDR(3), "Connector_Generic", "Conn_01x03", 3, "probe, bilge water"),
    ("R6", "2k0", R_FP, "Device", "R", 2, "1-Wire pull-up, fridge"),
    ("R9", "100R", R_FP, "Device", "R", 2, "1-Wire series, fridge"),
    ("J6", "FRIDGE-T", HDR(3), "Connector_Generic", "Conn_01x03", 3, "probe, fridge"),
    # I2C devices
    ("J7", "SHT31", HDR(4), "Connector_Generic", "Conn_01x04", 4,
     "SHT31 outside the enclosure"),
    ("U2", "ADS1115", SOCK(10), "Connector_Generic", "Conn_01x10", 10,
     "ADS1115 breakout, ADDR to GND = 0x48"),
    ("J8", "ADC IN", HDR(4), "Connector_Generic", "Conn_01x04", 4, "ADS1115 A1-A3 and GND"),
    ("J9", "IMU", HDR(5), "Connector_Generic", "Conn_01x05", 5,
     "IMU breakout with interrupt, A-009"),
    ("J10", "I2C EXP", HDR(4), "Connector_Generic", "Conn_01x04", 4,
     "I2C for the further ADS1115"),
    # Breakout of reserved and spare pins
    ("J11", "RESERVED", HDR(8), "Connector_Generic", "Conn_01x08", 8,
     "SeaTalk, TWAI, fourth probe, buzzer"),
    ("J12", "SPARE", HDR(14), "Connector_Generic", "Conn_01x14", 14, "unallocated GPIO"),
]

# Pins deliberately left unconnected, with the reason.
NO_CONNECT = {
    ("J1", 9): "IO46 strapping",
    ("J1", 10): "IO3 strapping",
    ("J1", 20): "RST, the DevKit has its own button",
    ("J2", 3): "IO19 USB",
    ("J2", 4): "IO20 USB",
    ("J2", 7): "IO48 DevKit RGB LED",
    ("J2", 8): "IO45 strapping",
    ("J2", 9): "IO0 DevKit BOOT button",
    ("J2", 10): "IO35 octal PSRAM",
    ("J2", 11): "IO36 octal PSRAM",
    ("J2", 12): "IO37 octal PSRAM",
    ("J2", 20): "RX debug UART",
    ("J2", 21): "TX debug UART",
    ("U2", 6): "ALRT unused",
}

NETS = [
    ("GND", [("J1", 1), ("J2", 1), ("J2", 2), ("J2", 22), ("J3", 2), ("D1", 2), ("C1", 2),
             ("R2", 2), ("C2", 2), ("C3", 2), ("U1", 2), ("C4", 2), ("C5", 2),
             ("J4", 3), ("J5", 3), ("J6", 3), ("J7", 4), ("U2", 2), ("U2", 5), ("J8", 4),
             ("J9", 2), ("J10", 2), ("J11", 8), ("J12", 14)]),
    ("+5V", [("U1", 3), ("C4", 1), ("C5", 1), ("J1", 2)]),
    ("+3V3", [("J1", 21), ("J1", 22), ("R4", 1), ("R5", 1), ("R6", 1),
              ("J4", 1), ("J5", 1), ("J6", 1), ("J7", 1), ("U2", 1), ("J9", 1), ("J10", 1),
              ("J11", 7), ("J12", 13)]),
    ("+12V_FUSED", [("J3", 1), ("D1", 1), ("C1", 1), ("R1", 1), ("D2", 2)]),
    ("+12V_PROT", [("D2", 1), ("C3", 1), ("U1", 1)]),
    ("VBAT_SENSE", [("R1", 2), ("R2", 1), ("R3", 1)]),
    ("ADS_A0", [("R3", 2), ("C2", 1), ("U2", 7)]),
    ("SDA", [("J1", 11), ("J7", 2), ("U2", 4), ("J9", 3), ("J10", 3)]),
    ("SCL", [("J1", 8), ("J7", 3), ("U2", 3), ("J9", 4), ("J10", 4)]),
    ("IMU_INT", [("J2", 18), ("J9", 5)]),
    ("OW1_BUS", [("R4", 2), ("R7", 1), ("J4", 2)]),
    ("OW1_GPIO", [("R7", 2), ("J1", 19)]),
    ("OW2_BUS", [("R5", 2), ("R8", 1), ("J5", 2)]),
    ("OW2_GPIO", [("R8", 2), ("J1", 18)]),
    ("OW3_BUS", [("R6", 2), ("R9", 1), ("J6", 2)]),
    ("OW3_GPIO", [("R9", 2), ("J1", 17)]),
    ("ADS1_A1", [("U2", 8), ("J8", 1)]),
    ("ADS1_A2", [("U2", 9), ("J8", 2)]),
    ("ADS1_A3", [("U2", 10), ("J8", 3)]),
    # Reserved for later stages, brought out on J11
    ("IO7", [("J1", 16), ("J11", 1)]),
    ("IO15", [("J1", 15), ("J11", 2)]),
    ("IO16", [("J1", 14), ("J11", 3)]),
    ("IO17", [("J1", 13), ("J11", 4)]),
    ("IO18", [("J1", 12), ("J11", 5)]),
    ("IO21", [("J2", 5), ("J11", 6)]),
    # Unallocated, brought out on J12
    ("IO1", [("J2", 19), ("J12", 1)]),
    ("IO10", [("J1", 7), ("J12", 2)]),
    ("IO11", [("J1", 6), ("J12", 3)]),
    ("IO12", [("J1", 5), ("J12", 4)]),
    ("IO13", [("J1", 4), ("J12", 5)]),
    ("IO14", [("J1", 3), ("J12", 6)]),
    ("IO38", [("J2", 13), ("J12", 7)]),
    ("IO39", [("J2", 14), ("J12", 8)]),
    ("IO40", [("J2", 15), ("J12", 9)]),
    ("IO41", [("J2", 16), ("J12", 10)]),
    ("IO42", [("J2", 17), ("J12", 11)]),
    ("IO47", [("J2", 6), ("J12", 12)]),
]


def validate():
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
            if not 1 <= pin <= pins[ref]:
                errors.append("net %s: %s has no pin %d" % (name, ref, pin))
            if (ref, pin) in NO_CONNECT:
                errors.append("%s pin %d is on net %s but declared no-connect" % (ref, pin, name))
            if (ref, pin) in seen:
                errors.append("%s pin %d appears in both %s and %s"
                              % (ref, pin, seen[(ref, pin)], name))
            seen[(ref, pin)] = name

    for ref, pin in NO_CONNECT:
        if ref not in pins or not 1 <= pin <= pins[ref]:
            errors.append("no-connect names %s pin %d, which does not exist" % (ref, pin))

    for ref, count in pins.items():
        for p in range(1, count + 1):
            if (ref, p) not in seen and (ref, p) not in NO_CONNECT:
                errors.append("%s pin %d is neither on a net nor declared no-connect" % (ref, p))

    return errors, seen


def emit():
    out = io.StringIO()
    w = out.write
    w('(export (version "E")\n')
    w('  (design\n')
    w('    (source "boathub-main")\n')
    w('    (tool "BoatHub repository, hardware/kicad/generate-netlist.py")\n')
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
    return out.getvalue()


errors, seen = validate()
if errors:
    for e in errors:
        print("ERROR: %s" % e)
    sys.exit(1)

with io.open("hardware/kicad/boathub-main.net", "w", encoding="utf-8", newline="\n") as f:
    f.write(emit())

print("ok: %d components, %d nets, %d pins on nets, %d no-connect"
      % (len(COMPONENTS), len(NETS), len(seen), len(NO_CONNECT)))
