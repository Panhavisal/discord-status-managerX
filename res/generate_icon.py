#!/usr/bin/env python3
"""
Generate a Discord-themed icon for the Discord Presence AutoUpdater app.
Design: Discord blurple background with a refresh/update arrow and status indicator.
"""

import struct
import zlib
from pathlib import Path
import math

# Discord colors
DISCORD_BLURPLE = (88, 101, 242)
DISCORD_BLURPLE_DARK = (68, 78, 200)
DISCORD_BLURPLE_LIGHT = (114, 137, 218)
WHITE = (255, 255, 255)
DISCORD_GREEN = (67, 181, 129)
DISCORD_GRAY = (54, 57, 63)

def create_png(width, height, pixels):
    """Create a PNG file from pixel data (list of RGBA tuples)."""

    def png_chunk(chunk_type, data):
        chunk_len = struct.pack('>I', len(data))
        chunk_crc = struct.pack('>I', zlib.crc32(chunk_type + data) & 0xffffffff)
        return chunk_len + chunk_type + data + chunk_crc

    signature = b'\x89PNG\r\n\x1a\n'

    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    ihdr = png_chunk(b'IHDR', ihdr_data)

    raw_data = b''
    for y in range(height):
        raw_data += b'\x00'
        for x in range(width):
            pixel = pixels[y][x]
            raw_data += bytes(pixel)

    compressed = zlib.compress(raw_data, 9)
    idat = png_chunk(b'IDAT', compressed)

    iend = png_chunk(b'IEND', b'')

    return signature + ihdr + idat + iend

def draw_rounded_rect(pixels, x1, y1, x2, y2, radius, color):
    """Draw a rounded rectangle."""
    width = x2 - x1
    height = y2 - y1
    center_x = x1 + width // 2
    center_y = y1 + height // 2

    for y in range(len(pixels)):
        for x in range(len(pixels[0])):
            # Check corners
            in_rect = (x1 + radius <= x <= x2 - radius) and (y1 <= y <= y2)
            in_rect |= (y1 + radius <= y <= y2 - radius) and (x1 <= x <= x2)

            # Check corner circles
            corners = [
                (x1 + radius, y1 + radius),
                (x2 - radius, y1 + radius),
                (x1 + radius, y2 - radius),
                (x2 - radius, y2 - radius)
            ]

            for cx, cy in corners:
                if (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2:
                    in_rect = True

            if in_rect:
                pixels[y][x] = color

def draw_circle(pixels, cx, cy, radius, color):
    """Draw a filled circle."""
    size = len(pixels)
    for y in range(size):
        for x in range(size):
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy <= radius * radius:
                if 0 <= y < size and 0 <= x < size:
                    pixels[y][x] = color

def draw_arrow(pixels, cx, cy, size, color, rotation=0):
    """Draw a curved arrow (refresh/update symbol)."""
    # Draw arrow body (curved)
    start_angle = math.radians(225 + rotation)
    end_angle = math.radians(315 + rotation)

    for angle in range(int(start_angle * 10), int(end_angle * 10)):
        a = angle / 10
        r = size * 0.6
        x = int(cx + r * math.cos(a))
        y = int(cy + r * math.sin(a))
        if 0 <= y < len(pixels) and 0 <= x < len(pixels[0]):
            draw_circle(pixels, x, y, max(1, size // 10), color)

    # Arrow head
    head_x = int(cx + size * 0.7 * math.cos(math.radians(315 + rotation)))
    head_y = int(cy + size * 0.7 * math.sin(math.radians(315 + rotation)))

    # Draw arrowhead triangle
    for dy in range(-size // 3, size // 3 + 1):
        for dx in range(-size // 6, size // 2 + 1):
            px = head_x + dx
            py = head_y + dy + dx
            if 0 <= py < len(pixels) and 0 <= px < len(pixels[0]):
                if dx * 0.5 + abs(dy) < size // 3:
                    pixels[py][px] = color

def create_icon_pixels(size):
    """Create pixel data for the icon."""
    pixels = [[DISCORD_GRAY for _ in range(size)] for _ in range(size)]

    scale = size / 256.0

    # Draw rounded square background (Discord blurple gradient)
    margin = int(8 * scale)
    radius = int(40 * scale)

    for y in range(size):
        for x in range(size):
            dx = abs(x - size // 2)
            dy = abs(y - size // 2)
            dist = (dx ** 2 + dy ** 2) ** 0.5

            # Check if inside rounded square
            corner_radius = radius
            in_rect = (dx <= size // 2 - corner_radius) or (dy <= size // 2 - corner_radius)
            in_corner = dist <= size // 2 - margin

            if in_rect or in_corner:
                # Subtle gradient
                gradient = 1.0 - (dist / (size // 2)) * 0.2
                r = int(DISCORD_BLURPLE[0] * gradient)
                g = int(DISCORD_BLURPLE[1] * gradient)
                b = int(DISCORD_BLURPLE[2] * gradient)
                pixels[y][x] = (r, g, b, 255)

    # Draw status indicator elements
    center = size // 2
    icon_size = int(100 * scale)

    # Draw circular arrow (update/refresh symbol)
    arrow_color = WHITE
    draw_arrow(pixels, center, center, icon_size // 2, arrow_color, rotation=-45)

    # Draw status dot (green, online status)
    dot_radius = int(20 * scale)
    dot_x = center + int(icon_size * 0.3)
    dot_y = center + int(icon_size * 0.3)
    draw_circle(pixels, dot_x, dot_y, dot_radius, DISCORD_GREEN)

    # Add border to dot for better visibility
    if size >= 32:
        for angle in range(0, 360, 15):
            rad = math.radians(angle)
            bx = int(dot_x + (dot_radius + 2 * scale) * math.cos(rad))
            by = int(dot_y + (dot_radius + 2 * scale) * math.sin(rad))
            if 0 <= by < size and 0 <= bx < size:
                # Check if pixel is background (gray or transparent)
                px = pixels[by][bx]
                if px == DISCORD_GRAY or (len(px) == 4 and px[3] == 0):
                    pixels[by][bx] = DISCORD_BLURPLE

    return pixels

def create_ico_file(png_data_list, output_path):
    """Create an ICO file from multiple PNG images."""
    ico_header = struct.pack('<HHH', 0, 1, len(png_data_list))

    directory = b''
    offset = len(ico_header) + len(png_data_list) * 16

    png_datas = []
    for size, png_data in png_data_list:
        width, height = size, size
        directory += struct.pack('<BBBBHHII',
            width if width < 256 else 0,
            height if height < 256 else 0,
            0,
            0,
            1,
            32,
            len(png_data),
            offset
        )
        png_datas.append(png_data)
        offset += len(png_data)

    with open(output_path, 'wb') as f:
        f.write(ico_header)
        f.write(directory)
        for png_data in png_datas:
            f.write(png_data)

def main():
    output_path = Path(__file__).parent / 'app.ico'

    sizes = [256, 128, 64, 48, 32, 16]
    png_data_list = []

    for size in sizes:
        pixels = create_icon_pixels(size)
        png_data = create_png(size, size, pixels)
        png_data_list.append((size, png_data))
        print(f"Generated {size}x{size} PNG ({len(png_data)} bytes)")

    create_ico_file(png_data_list, output_path)
    print(f"\nIcon saved to: {output_path}")

    total_size = sum(len(data) for _, data in png_data_list)
    print(f"Total icon data: {total_size} bytes")

if __name__ == '__main__':
    main()
