from PIL import Image, ImageChops
import os

src = r"C:/Users/Rtyu3221/.dsh/attachments/v1/objects/08/085a5385fce086c9d6b4c011dbca24c3da989681a30978a51d047f10f51113fd"
dst_dir = r"D:/QtWorkspace/AutoRun_Codex/resources"
os.makedirs(dst_dir, exist_ok=True)
dst = os.path.join(dst_dir, "openai-logo-white.png")

img = Image.open(src).convert("RGBA")
print("source size:", img.size, "mode:", img.mode)
gray = img.convert("L")
dark_alpha = gray.point(lambda p: 255 - p)
r, g, b, base_alpha = img.split()
alpha = ImageChops.multiply(base_alpha, dark_alpha)
white = Image.new("RGBA", img.size, (255, 255, 255, 0))
white.putalpha(alpha)
bbox = alpha.getbbox()
if bbox:
    white = white.crop(bbox)
    side = max(white.size)
    square = Image.new("RGBA", (side, side), (255, 255, 255, 0))
    square.paste(white, ((side - white.size[0]) // 2, (side - white.size[1]) // 2))
    white = square
white.save(dst)
print("saved:", dst, white.size)
