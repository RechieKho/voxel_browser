"""A tiny PNG reader (8-bit RGB/RGBA, non-interlaced) so tests can look inside screenshots
without pulling in Pillow."""
import struct
import zlib


def read_png(path):
    """Returns (width, height, channels, rows) where each row is `bytes` of width*channels."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    pos, idat, width = 8, [], None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace != 0:
                raise ValueError("unsupported PNG (depth=%d type=%d interlace=%d)" % (depth, ctype, interlace))
            channels = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, prev = [], bytearray(stride)
    for y in range(height):
        base = y * (stride + 1)
        ftype, line = raw[base], bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 255
            elif ftype == 2:
                line[i] = (line[i] + b) & 255
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        rows.append(bytes(line))
        prev = line
    return width, height, channels, rows


def stats(path):
    """{'width','height','distinct_colors','mean_rgb'}: enough to tell a drawn frame from a blank one."""
    width, height, channels, rows = read_png(path)
    colors, total = set(), [0, 0, 0]
    for row in rows:
        for x in range(0, width * channels, channels):
            colors.add(row[x:x + 3])
            total[0] += row[x]
            total[1] += row[x + 1]
            total[2] += row[x + 2]
    n = width * height
    return {"width": width, "height": height, "distinct_colors": len(colors),
            "mean_rgb": tuple(t / n for t in total)}
