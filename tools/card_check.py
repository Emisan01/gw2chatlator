r"""The helper for NVIDIA cards on this PC: does it fall back as it should, and how much faster than the CPU is it?

    GCT_BENCH_DATA=<folder with translate\> python tools/card_check.py [--model nllb|m2m] [--beam 4] [--lines 6]

1. Card visible: the helper says it runs on the card and translates.
2. No usable card (CUDA_VISIBLE_DEVICES=-1, what an old or foreign card looks like): it says "card" and still translates on
   the CPU.
3. Module files missing (the exe alone, no CUDA libraries): it ends at once, so the add-on starts the CPU helper instead.
4. Speed: the same invented lines, the same model and beam, on the card helper and on the CPU helper; ms per line.
Only made-up lines go in, only numbers and states come out. Needs the builds build-localmt-gpu2 and build-localmt-omp.
"""
import argparse
import ctypes
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REL = os.path.join("addons", "nexus-keyboard", "Release", "GW2Keyboard", "gw2kb_translate.exe")
GPU = os.path.join(ROOT, "build-localmt-gpu2", REL)
CPU = os.path.join(ROOT, "build-localmt-omp", REL)
CUDA = os.path.join(os.environ.get("CUDA_PATH", r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"), "bin")
LINES = [
    "would someone let me teleport to the valley please",
    "lfg fractals tier four, need a healer and two dps",
    "the commander is waiting at the north waypoint",
    "thanks for the help, that boss was really hard",
    "does anyone know where to buy the newbie mount",
    "meet at the bank in ten minutes, bring your best gear",
]


class Helper:
    def __init__(self, exe, data, env_extra=None, cuda_path=True):
        env = dict(os.environ)
        env.update(env_extra or {})
        if cuda_path:
            env["PATH"] = CUDA + ";" + env["PATH"]
        else:  # a player's PC: no CUDA toolkit on the path (this PC may have one)
            env["PATH"] = ";".join(p for p in env["PATH"].split(";") if "cuda" not in p.lower() and "nvidia gpu" not in p.lower())
        self.p = subprocess.Popen([exe, "--data", data], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
        self.q = queue.Queue()
        threading.Thread(target=lambda: [self.q.put(l) for l in self.p.stdout], daemon=True).start()
        self.device = None
        self.next_id = 1

    def ask(self, text, model, beam, timeout=180):
        rid = self.next_id
        self.next_id += 1
        start = time.time()
        try:
            self.p.stdin.write((json.dumps({"cmd": "translate", "id": rid, "src": "en", "tgt": "de", "text": text, "beam": beam, "model": model}) + "\n").encode())
            self.p.stdin.flush()
        except OSError:
            return None, 0.0
        while time.time() - start < timeout:
            try:
                m = json.loads(self.q.get(timeout=2).decode("utf-8"))
            except queue.Empty:
                if self.p.poll() is not None:
                    return None, 0.0
                continue
            if m.get("evt") == "device":
                self.device = m
            elif m.get("evt") in ("result", "error"):
                return m, (time.time() - start) * 1000.0
        return None, 0.0

    def close(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait(10)
        return self.p.poll()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--model", default="nllb", choices=("nllb", "m2m"))
    ap.add_argument("--beam", type=int, default=4)
    ap.add_argument("--lines", type=int, default=len(LINES))
    a = ap.parse_args()
    data = os.environ.get("GCT_BENCH_DATA", "")
    if not data or not os.path.isdir(os.path.join(data, "translate")):
        sys.exit("GCT_BENCH_DATA must name a folder with translate\\ (the models)")
    for exe in (GPU, CPU):
        if not os.path.isfile(exe):
            sys.exit(f"missing: {exe}")
    ok = True
    ctypes.windll.kernel32.SetErrorMode(0x8003)  # a helper that cannot start ends quietly, no Windows message box here

    def verdict(name, good, detail):
        nonlocal ok
        ok = ok and good
        print(f"[{'PASS' if good else 'FAIL'}] {name}: {detail}")

    h = Helper(GPU, data)
    r, _ = h.ask(LINES[0], a.model, a.beam)
    on = (h.device or {}).get("on")
    verdict("card visible", bool(r) and r.get("evt") == "result" and on == "card", f"device {on}, {(h.device or {}).get('detail', '')}"[:120])
    card_ms = [h.ask(t, a.model, a.beam)[1] for t in LINES[:a.lines]]
    h.close()

    h = Helper(GPU, data, {"CUDA_VISIBLE_DEVICES": "-1"})
    r, _ = h.ask(LINES[0], a.model, a.beam)
    d = h.device or {}
    verdict("no usable card", bool(r) and r.get("evt") == "result" and d.get("on") != "card" and d.get("why") == "card", f"device {d.get('on')}, why {d.get('why')}")
    h.close()

    folder = tempfile.mkdtemp(prefix="gct_card_")
    alone = os.path.join(folder, "gw2kb_translate_gpu.exe")
    shutil.copy2(GPU, alone)
    h = Helper(alone, data, cuda_path=False)
    r, _ = h.ask(LINES[0], a.model, a.beam, timeout=20)
    code = h.close()
    shutil.rmtree(folder, ignore_errors=True)
    verdict("module files missing", r is None and code is not None and code != 0, f"ended with {hex(code & 0xFFFFFFFF) if code is not None else 'nothing'}")

    h = Helper(CPU, data, cuda_path=False)
    h.ask(LINES[0], a.model, a.beam)
    cpu_ms = [h.ask(t, a.model, a.beam)[1] for t in LINES[:a.lines]]
    h.close()
    card, cpu = sum(card_ms) / len(card_ms), sum(cpu_ms) / len(cpu_ms)
    print(f"[INFO] speed {a.model} beam {a.beam}, {len(card_ms)} lines: card {card:.0f} ms per line, CPU {cpu:.0f} ms per line, "
          f"the card {cpu / card:.1f}x as fast" if card > 0 else "[INFO] no speed: the card answered nothing")
    print("ALL PASS" if ok else "SOMETHING FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
