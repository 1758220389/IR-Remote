"""Host tests and exact 128x64 OLED bitmap contact sheet.
Run from any directory: python tests/view/run_tests.py
Requires gcc on PATH; Pillow is optional (only used for the PNG contact sheet).
All executables and images go to the explicit Hermes scratch directory.
The vendor OLED_Data.c has flat array initializers; only its existing
-Wmissing-braces warning is disabled. All other warnings are errors.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = Path("C:/Users/null/AppData/Local/hermes/profiles/stm32/cache/scratch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMP=str(output), TEMP=str(output), TMPDIR=str(output))
    common = ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-missing-braces",
              "-Itests/view/mocks", "-Icode/Main", "-Icode/Library"]
    cases = {
        "test_ui_view": ["tests/view/test_ui_view.c", "code/Main/ui_view.c",
                         "code/Main/ui_font.c", "code/Main/ui_presenter.c", "code/Main/ui.c",
                         "code/Library/OLED_Data.c"],
        "test_board_keys": ["tests/view/test_board_keys.c", "code/Main/board_keys.c"],
        "test_oled_pins": ["tests/view/test_oled_pins.c", "code/Library/OLED.c",
                           "code/Library/OLED_Data.c", "-lm"],
    }
    for name, sources in cases.items():
        executable = output / (name + ".exe")
        subprocess.run(common + sources + ["-o", str(executable)],
                       cwd=ROOT, env=env, check=True)
        extra = [str(output)] if name == "test_ui_view" else []
        subprocess.run([str(executable)] + extra, cwd=ROOT, env=env, check=True)
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("Pillow unavailable; PBM layout proofs are still exported.")
    else:
        if Path("C:/Windows/Fonts/simsun.ttc").exists():
            regenerated = output / "ui_font_regenerated.c"
            subprocess.run([sys.executable, "tools/generate_ui_font.py", "--output", str(regenerated)],
                           cwd=ROOT, env=env, check=True)
            assert regenerated.read_bytes() == (ROOT / "code/Main/ui_font.c").read_bytes()
            print("installed-source font regeneration is byte-identical: PASS")
        files = sorted(output.glob("ui_page_*.pbm"))
        assert len(files) == 11
        sheet = Image.new("RGB", (3 * 536, 4 * 292), "#18212c")
        draw = ImageDraw.Draw(sheet)
        for index, path in enumerate(files):
            # PBM black bits are lit pixels on OLED: invert for white-on-black.
            image = Image.open(path).convert("L").point(lambda p: 255-p)
            image = image.resize((512, 256), Image.Resampling.NEAREST)
            x, y = (index % 3) * 536 + 12, (index // 3) * 292 + 24
            sheet.paste(image, (x, y))
            draw.text((x, y-18), path.stem, fill="white")
        proof = output / "ui_view_pages.png"
        sheet.save(proof)
        print(f"11 framebuffer layout proofs: {proof}")
    print("ALL VIEW/GPIO TESTS PASSED")


if __name__ == "__main__":
    main()
