#!/usr/bin/env python3
"""PNG를 정사각형 유지한 채 여러 크기로 리사이즈해 .ico 생성 (찌그러짐 방지)."""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow 필요: pip install Pillow")
    sys.exit(1)

SIZES = [16, 22, 32, 48, 64, 128, 256]
SCRIPT_DIR = Path(__file__).resolve().parent

def main():
    # 소스: watch_time/assets 또는 icons/256x256/apps
    src = (SCRIPT_DIR / ".." / ".." / "assets" / "mrwatchmaker_icon_256.png").resolve()
    if not src.exists():
        src = SCRIPT_DIR / "256x256" / "apps" / "tg-timer.png"
    if not src.exists():
        print("소스 이미지를 찾을 수 없습니다:", src)
        sys.exit(1)

    img = Image.open(src).convert("RGBA")
    w, h = img.size
    if w != h:
        # 정사각형으로 크롭
        s = min(w, h)
        x, y = (w - s) // 2, (h - s) // 2
        img = img.crop((x, y, x + s, y + s))
    elif w != 256:
        img = img.resize((256, 256), Image.Resampling.LANCZOS)

    icons = []
    for size in SIZES:
        icons.append(img.resize((size, size), Image.Resampling.LANCZOS))

    out = SCRIPT_DIR / "tg-timer.ico"
    icons[0].save(out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=icons[1:])
    print("생성:", out)

    # 창/작업표시줄 런타임용 PNG도 동일 아이콘으로 덮어쓰기
    for size, icon in zip(SIZES, icons):
        png_dir = SCRIPT_DIR / f"{size}x{size}" / "apps"
        png_dir.mkdir(parents=True, exist_ok=True)
        icon.save(png_dir / "tg-timer.png")
    print("PNG 갱신: %dx%d/apps/tg-timer.png" % (SIZES[0], SIZES[0]), "...", "%dx%d/apps/tg-timer.png" % (SIZES[-1], SIZES[-1]))

if __name__ == "__main__":
    main()
