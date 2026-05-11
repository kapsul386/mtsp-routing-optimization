"""
Builds mTSPLib (Necula, Breaban, Raschip) instances from TSPLIB sources:
  - eil51, berlin52, eil76, rat99 (4 base TSPs)
  - m in {2, 3, 5, 7} (4 multiplicities)
  = 16 mTSP instances total.

Each output file is in our project's mTSP format:
  Line 1: <n> <m>
  Lines 2..n+1: x y     (vertex 0 = depot, rest = customers)

The TSPLIB depot is NOT marked in the file — by Necula et al. convention,
vertex 1 in the TSP is the depot for the mTSP (matches our 0-indexed depot=0).

Output: data/mtsp/mtsplib/
  eil51_m2.txt, eil51_m3.txt, ..., rat99_m7.txt
"""
import os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TSPLIB_DIR = os.environ.get("TSPLIB_DATA_DIR", os.path.join(ROOT, "external", "NeuroLKH", "tsplib_data"))
OUT_DIR = os.path.join(ROOT, "data", "mtsp", "mtsplib")

BASES = ["eil51", "berlin52", "eil76", "rat99"]
M_VALUES = [2, 3, 5, 7]


def parse_tsplib(path):
    """Parse TSPLIB-format file, return (name, coords) where coords[0] is the depot."""
    coords = []
    name = ""
    with open(path) as f:
        in_coords = False
        for line in f:
            line = line.strip()
            if line.startswith("NAME"):
                name = line.split(":")[1].strip()
            elif line == "NODE_COORD_SECTION":
                in_coords = True
                continue
            elif line == "EOF":
                break
            elif in_coords:
                parts = line.split()
                if len(parts) >= 3:
                    # idx, x, y
                    coords.append((float(parts[1]), float(parts[2])))
    return name, coords


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    written = 0
    for base in BASES:
        src = os.path.join(TSPLIB_DIR, f"{base}.tsp")
        if not os.path.exists(src):
            print(f"  MISSING: {src}", file=sys.stderr)
            continue
        name, coords = parse_tsplib(src)
        for m in M_VALUES:
            n = len(coords)
            if n - 1 < m:
                print(f"  SKIP: {base} m={m} (n-1={n-1} < m)")
                continue
            out_path = os.path.join(OUT_DIR, f"{base}_m{m}.txt")
            with open(out_path, "w") as f:
                f.write(f"{n} {m}\n")
                for x, y in coords:
                    f.write(f"{x:.3f} {y:.3f}\n")
            written += 1
            print(f"  wrote {out_path} (n={n}, m={m})")
    print(f"\nDone. {written} mTSPLib instances in {OUT_DIR}")


if __name__ == "__main__":
    main()
