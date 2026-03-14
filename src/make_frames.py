import os
from PIL import Image, ImageOps

def convert():
    # Input source: 344x76 (8 frames, 43px wide each)
    input_filename = "stalin_sheet.png"
    
    if not os.path.exists(input_filename):
        print(f"Error: {input_filename} not found!")
        return

    img = Image.open(input_filename).convert("RGBA")
    
    frame_w = 43
    frame_h = 76
    orig_frames = 8
    total_frames = 16  # 8 original + 8 mirrored
    total_w = frame_w * total_frames 
    
    # Create new canvas for 16 frames
    new_img = Image.new("RGBA", (total_w, frame_h))
    
    # 1. Copy original 8 frames (Walking right)
    new_img.paste(img, (0, 0))
    
    # 2. Create mirrored 8 frames (Walking left)
    for i in range(orig_frames):
        left = i * frame_w
        frame = img.crop((left, 0, left + frame_w, frame_h))
        flipped_frame = ImageOps.mirror(frame)
        new_img.paste(flipped_frame, ((orig_frames + i) * frame_w, 0))

    # Generate C source file for LVGL 9
    with open("stalin_sheet.c", "w") as f:
        f.write('#include "lvgl.h"\n\n')
        f.write('/* Sprite sheet data: 16 frames (95x170) */\n')
        f.write('const uint8_t stalin_sheet_map[] = {\n')
        
        for y in range(frame_h):
            f.write("  ")
            for x in range(total_w):
                r, g, b, a = new_img.getpixel((x, y))
                
                # Convert to RGB565 and swap bytes for ESP32 SPI
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                low = rgb565 & 0xFF
                high = (rgb565 >> 8) & 0xFF
                f.write(f'0x{low:02x}, 0x{high:02x}, ')
            f.write("\n")
            
        f.write("};\n\n")
        f.write('const lv_image_dsc_t stalin_sheet = {\n')
        f.write('  .header.w = %d,\n' % total_w)
        f.write('  .header.h = %d,\n' % frame_h)
        f.write('  .header.cf = LV_COLOR_FORMAT_RGB565,\n')
        f.write('  .header.magic = LV_IMAGE_HEADER_MAGIC,\n')
        f.write('  .data_size = %d,\n' % (total_w * frame_h * 2))
        f.write('  .data = stalin_sheet_map,\n')
        f.write('};\n')
        
    print(f"Success! Generated stalin_sheet.c with {total_frames} frames ({total_w}x{frame_h})")

if __name__ == "__main__":
    convert()
    