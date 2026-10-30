r"""
Ghost keychain: draws the board outline on Edge.Cuts and places the parts.

HOW TO RUN (KiCad PCB Editor):
  1. Do "Update PCB from Schematic" first so all footprints exist.
  2. Tools > Scripting Console, then paste:
       exec(open(r"C:\path\to\ghost_layout.py").read())
  3. Run it ONCE on a fresh board. Running twice draws the outline twice
     (undo with Ctrl+Z, or set CLEAR_OLD_EDGE = True).

Coordinates are millimetres, front view, x right, y down, origin = top-left
corner of the board's bounding box. Back-side parts use the same x/y as seen
through the board from the front.
"""
import math
import pcbnew
from pcbnew import FromMM

# ---------------- settings you may change ----------------
OX, OY = 100.0, 80.0        # where the board's top-left lands on the sheet (mm)
CLEAR_OLD_EDGE = False      # True = delete everything on Edge.Cuts first
EDGE_W = 0.1                # outline line width (mm)

# ref: (centre x, centre y, rotation degrees, on back?)
# Rotation is counter-clockwise in KiCad. If a part ends up facing the wrong
# way, change its rotation by 90/180 and rerun on a fresh board.
PARTS = {
    "DS1": (26.0, 30.0, 0,   False),   # OLED 1.3"
    "SW1": (14.0, 52.5, 0,   False),   # buttons under the screen
    "SW2": (26.0, 52.5, 0,   False),
    "SW3": (38.0, 52.5, 0,   False),
    "U2":  (26.0, 28.5, 0,   True),    # MPU6050, top of the back
    "U3":  (14.5, 47.0, -90, True),    # TP4056, USB-C towards the left edge
    "U1":  (40.5, 47.0, -90, True),    # ESP32-C3, USB towards the right edge
    "J1":  (26.0, 59.5, 0,   True),    # battery connector, bottom strip
}

# ---------------- helpers ----------------
board = pcbnew.GetBoard()
EDGE = pcbnew.Edge_Cuts
SEG = getattr(pcbnew, "SHAPE_T_SEGMENT", None)
CIR = getattr(pcbnew, "SHAPE_T_CIRCLE", None)
if SEG is None:                      # KiCad 6 names
    SEG, CIR = pcbnew.S_SEGMENT, pcbnew.S_CIRCLE


def vec(x_iu, y_iu):
    try:
        return pcbnew.VECTOR2I(int(x_iu), int(y_iu))
    except AttributeError:
        return pcbnew.wxPoint(int(x_iu), int(y_iu))


def P(x, y):
    return vec(FromMM(OX + x), FromMM(OY + y))


def add_segment(a, b):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(SEG)
    s.SetLayer(EDGE)
    s.SetWidth(FromMM(EDGE_W))
    s.SetStart(P(*a))
    s.SetEnd(P(*b))
    board.Add(s)


def add_circle(cx, cy, r):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(CIR)
    s.SetLayer(EDGE)
    s.SetWidth(FromMM(EDGE_W))
    s.SetCenter(P(cx, cy))
    s.SetEnd(P(cx + r, cy))
    board.Add(s)


def arc_points(cx, cy, r, a_from, a_to, n):
    pts = []
    for i in range(n + 1):
        a = math.radians(a_from + (a_to - a_from) * i / n)
        pts.append((cx + r * math.cos(a), cy - r * math.sin(a)))  # y is down
    return pts


def quad_points(p0, c, p2, n):
    pts = []
    for i in range(n + 1):
        t = i / n
        x = (1 - t) ** 2 * p0[0] + 2 * t * (1 - t) * c[0] + t ** 2 * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * t * (1 - t) * c[1] + t ** 2 * p2[1]
        pts.append((x, y))
    return pts


# ---------------- outline ----------------
if CLEAR_OLD_EDGE:
    for d in list(board.GetDrawings()):
        if d.GetLayer() == EDGE:
            board.Remove(d)

W, R, SIDE_Y, BASE_Y = 52.0, 26.0, 63.0, 63.0
SW = W / 3.0                                   # scallop width

outline = arc_points(R, R, R, 180, 0, 72)      # top semicircle, left -> right
outline.append((W, SIDE_Y))                    # down the right side
outline += quad_points((W, BASE_Y), (W - SW / 2, 68), (W - SW, BASE_Y), 16)[1:]
outline += quad_points((W - SW, BASE_Y), (W - 1.5 * SW, 58), (W - 2 * SW, BASE_Y), 16)[1:]
outline += quad_points((W - 2 * SW, BASE_Y), (SW / 2, 68), (0, BASE_Y), 16)[1:]
# last point (0, 63) closes back up the left side to the start (0, 26)

for a, b in zip(outline, outline[1:] + [outline[0]]):
    add_segment(a, b)

add_circle(26.0, 5.5, 1.7)                     # keyring hole


# ---------------- part placement ----------------
def flip_to_other_side(fp):
    c = fp.GetPosition()
    for arg in (True, getattr(pcbnew, "FLIP_DIRECTION_LEFT_RIGHT", None)):
        if arg is None:
            continue
        try:
            fp.Flip(c, arg)
            return
        except Exception:
            pass
    fp.Flip(c)


def bbox_centre(fp):
    try:
        bb = fp.GetBoundingBox(False, False)
    except TypeError:
        bb = fp.GetBoundingBox(False)
    return bb.GetCenter()


def place(ref, x, y, rot, back):
    fp = board.FindFootprintByReference(ref)
    if fp is None:
        print("skipped (not on board): " + ref)
        return
    on_back = fp.GetLayer() == pcbnew.B_Cu
    if back != on_back:
        flip_to_other_side(fp)
    fp.SetOrientationDegrees(rot)
    c, t = bbox_centre(fp), P(x, y)
    fp.Move(vec(t.x - c.x, t.y - c.y))
    print("placed %s at (%.1f, %.1f) rot %s %s" % (ref, x, y, rot, "back" if back else "front"))


for ref, (x, y, rot, back) in PARTS.items():
    place(ref, x, y, rot, back)

pcbnew.Refresh()
print("Done. Check: DRC, 3D viewer, and that USB ends face the left/right edges.")
