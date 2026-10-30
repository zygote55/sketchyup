#!/usr/bin/env python3
"""R107.b1 (ADR 0181 §8 V3 and V7): generates the synthetic GeoTIFF fixtures the reader tests use.

Run as: python3 -I make_geotiff_fixtures.py <output-directory>
(the directory defaults to this script's own directory). The output is deterministic for a given
zlib version; the tests check decoded values, not the compressed bytes.

Positive fixtures are small single-band rasters on a 1 arc-second grid near 51.6 N, 0.2 W, with
analytic values that are exact in float32 and int16:
  plane:      z = 10 + 2*col + 3*row        (bilinear interpolation reproduces it exactly)
  paraboloid: z = 50 + (col - 24)**2 + (row - 20)**2
where col and row are pixel indexes from the upper-left post. Each file's tie is the upper-left
corner (pixel-is-area) unless noted.

Refusal fixtures each carry one feature of ADR 0181 §7.3 that the reader must name.

R107.b1.1 (ADR 0181 §7.2): LZW (compression 5) is written by lzw_encode, a TIFF 6.0 encoder
written here from the specification, and checked against tifffile (see the notes in the
R107.b1.1 implementation record). The corrupt LZW streams are built with lzw_pack, which writes
codes the way the decoder reads them, so each one is wrong in exactly one way.

R107.b1.2 (ADR 0181 §7.2 and §7.3): a spec's 'overviews' are further images after its own, written
in order, each with NewSubfileType 1 unless it says otherwise (a reduced-resolution overview). The
'loop' key links the last image back to the first. 'subfile' sets NewSubfileType on an image.
"""
import math
import os
import struct
import sys
import zlib

WIDTH, HEIGHT = 48, 40
TIE_LON, TIE_LAT = -0.2, 51.6
SCALE = 1.0 / 3600.0  # one arc-second, both ways
NODATA = -9999.0
LZW_CLEAR, LZW_END, LZW_FIRST = 256, 257, 258


def plane(col, row):
    return 10 + 2 * col + 3 * row


def plane_at(scale):
    """The plane sampled at every `scale`-th post: an overview of the same surface."""
    return lambda col, row: plane(col * scale, row * scale)


def paraboloid(col, row):
    return 50 + (col - 24) ** 2 + (row - 20) ** 2


def noise(col, row):
    """A pseudo-random int16 surface: a 32-bit integer hash of the position. Its predictor-2
    differences are random, so LZW's table fills and clears on it and every code width is used."""
    h = (col * 0x9E3779B1 + row * 0x85EBCA77 + 0x27D4EB2F) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & 0xFFFFFFFF
    h ^= h >> 12
    h = (h * 0x297A2D39) & 0xFFFFFFFF
    h ^= h >> 15
    return (h & 0xFFFF) - 32768


class MsbBits:
    """Packs codes most significant bit first, as TIFF LZW reads them."""

    def __init__(self):
        self.value = 0
        self.count = 0
        self.out = bytearray()

    def put(self, code, width):
        self.value = (self.value << width) | code
        self.count += width
        while self.count >= 8:
            self.count -= 8
            self.out.append((self.value >> self.count) & 0xFF)
        self.value &= (1 << self.count) - 1

    def finish(self):
        if self.count:
            self.out.append((self.value << (8 - self.count)) & 0xFF)
            self.count = 0
            self.value = 0
        return bytes(self.out)


def lzw_width(next_code):
    """The code width when the decoder's next free code is next_code. The early change: 10 bits
    from code 511, 11 bits from 1023 and 12 bits from 2047 (TIFF 6.0, section 13)."""
    if next_code < 511:
        return 9
    if next_code < 1023:
        return 10
    if next_code < 2047:
        return 11
    return 12


def lzw_encode(data):
    """One TIFF LZW block: a ClearCode, the greedy longest-match codes, and an EndOfInformation
    code. The encoder clears its table (a ClearCode) before the 4,096 codes are used up. Its width
    is one code late against its own table, which is the decoder's early change."""
    bits = MsbBits()
    table = {}
    width, next_code = 9, LZW_FIRST
    bits.put(LZW_CLEAR, width)
    if not data:
        bits.put(LZW_END, width)
        return bits.finish()
    current = data[0]
    for byte in data[1:]:
        key = (current, byte)
        if key in table:
            current = table[key]
            continue
        bits.put(current, width)
        table[key] = next_code
        next_code += 1
        if next_code > (1 << width) - 1 and width < 12:
            width += 1
        if next_code >= 4094:
            bits.put(LZW_CLEAR, width)
            table = {}
            width, next_code = 9, LZW_FIRST
        current = byte
    bits.put(current, width)
    # The decoder adds one entry for this last code, so the width follows as if the encoder had.
    next_code += 1
    if next_code > (1 << width) - 1 and width < 12:
        width += 1
    bits.put(LZW_END, width)
    return bits.finish()


def lzw_pack(codes):
    """Packs codes as the decoder reads them: a ClearCode resets the table, and each code after the
    first in a run adds one entry. For streams that are wrong on purpose (refusal fixtures)."""
    bits = MsbBits()
    width, next_code, previous = 9, LZW_FIRST, None
    for code in codes:
        bits.put(code, width)
        if code == LZW_CLEAR:
            width, next_code, previous = 9, LZW_FIRST, None
        elif code == LZW_END:
            break
        else:
            if previous is not None:
                next_code += 1
            previous = code
            width = lzw_width(next_code)
    return bits.finish()


def predictor3_encode(row_values, big_endian_planes=True):
    """Floating-point predictor (TIFF Technical Note 3): byte planes, most significant first,
    then byte differencing across the whole row. row_values are Python floats."""
    width = len(row_values)
    raw = [0] * (4 * width)
    for j, value in enumerate(row_values):
        b = struct.pack('>f', value)
        for k in range(4):
            raw[k * width + j] = b[k]
    out = [raw[0]] + [(raw[k] - raw[k - 1]) & 255 for k in range(1, 4 * width)]
    return bytes(out)


def predictor2_encode(row_values, little):
    """Horizontal differencing of int16 samples, modulo 2**16, in file byte order."""
    prev = 0
    out = []
    for value in row_values:
        diff = (value - prev) & 0xFFFF
        out.append(diff)
        prev = value & 0xFFFF
    fmt = '<%dH' if little else '>%dH'
    return struct.pack(fmt % len(out), *out)


def encode_blocks(spec):
    """One image's block payloads, and its block shape: (payloads, block width, block height)."""
    little = spec.get('little', True)
    e = '<' if little else '>'
    w = spec.get('width', WIDTH)
    h = spec.get('height', HEIGHT)
    dtype = spec.get('dtype', 'f32')
    compression = spec.get('compression', 1)
    predictor = spec.get('predictor', 1)
    layout = spec.get('layout', 'strip')
    values = spec['values']
    if dtype == 'f32':
        pack = lambda v: struct.pack(e + 'f', v)
    else:
        pack = lambda v: struct.pack(e + 'h', v)

    if layout == 'tile':
        tw, th = spec.get('tile', (16, 16))
        across = (w + tw - 1) // tw
        down = (h + th - 1) // th
        blocks = []
        for by in range(down):
            for bx in range(across):
                rows = []
                for r in range(th):
                    row = []
                    for c in range(tw):
                        col = bx * tw + c
                        row_v = by * th + r
                        if col < w and row_v < h:
                            row.append(values(col, row_v))
                        else:
                            row.append(0)
                    rows.append(row)
                blocks.append((tw, th, rows))
        block_width, block_height = tw, th
    else:
        rps = spec.get('rows_per_strip', 8)
        blocks = []
        r0 = 0
        while r0 < h:
            rows_here = min(rps, h - r0)
            rows = [[values(c, r0 + r) for c in range(w)] for r in range(rows_here)]
            blocks.append((w, rows_here, rows))
            r0 += rows_here
        block_width, block_height = w, rps

    payloads = []
    for (bw, bh, rows) in blocks:
        out = bytearray()
        for row in rows:
            if predictor == 3 and dtype == 'f32':
                out += predictor3_encode(row)
            elif predictor == 2 and dtype == 'i16':
                out += predictor2_encode(row, little)
            else:
                for v in row:
                    out += pack(v)
        data = bytes(out)
        if compression == 8:
            data = zlib.compress(data, 9)
        elif compression == 5:
            data = lzw_encode(data)
        payloads.append(data)
    if spec.get('payloads') is not None:
        payloads = list(spec['payloads'])
    return payloads, block_width, block_height


def image_tags(spec, offsets, counts, block_width, block_height):
    """One image's directory entries, by tag: (type, count, raw value bytes)."""
    little = spec.get('little', True)
    e = '<' if little else '>'
    w = spec.get('width', WIDTH)
    h = spec.get('height', HEIGHT)
    dtype = spec.get('dtype', 'f32')
    bits = spec.get('bits', 32 if dtype == 'f32' else 16)
    sample_format = spec.get('sample_format', 3 if dtype == 'f32' else 2)
    samples = spec.get('samples', 1)
    compression = spec.get('compression', 1)
    predictor = spec.get('predictor', 1)
    layout = spec.get('layout', 'strip')

    tags = {}  # tag -> (type, count, bytes)
    def add(tag, typ, count, raw):
        tags[tag] = (typ, count, raw)
    SHORT, LONG, ASCII, DOUBLE = 3, 4, 2, 12
    add(256, LONG, 1, struct.pack(e + 'I', w))
    add(257, LONG, 1, struct.pack(e + 'I', h))
    add(258, SHORT, 1, struct.pack(e + 'H', bits))
    add(259, SHORT, 1, struct.pack(e + 'H', compression))
    add(262, SHORT, 1, struct.pack(e + 'H', 1))
    if spec.get('subfile') is not None:
        add(254, LONG, 1, struct.pack(e + 'I', spec['subfile']))
    if layout == 'tile':
        add(322, SHORT, 1, struct.pack(e + 'H', block_width))
        add(323, SHORT, 1, struct.pack(e + 'H', block_height))
        add(324, LONG, len(offsets), struct.pack(e + '%dI' % len(offsets), *offsets))
        add(325, LONG, len(counts), struct.pack(e + '%dI' % len(counts), *counts))
    else:
        add(273, LONG, len(offsets), struct.pack(e + '%dI' % len(offsets), *offsets))
        add(278, SHORT, 1, struct.pack(e + 'H', block_height))
        add(279, LONG, len(counts), struct.pack(e + '%dI' % len(counts), *counts))
    add(277, SHORT, 1, struct.pack(e + 'H', samples))
    if predictor != 1:
        add(317, SHORT, 1, struct.pack(e + 'H', predictor))
    add(339, SHORT, 1, struct.pack(e + 'H', sample_format))
    if spec.get('transform'):
        add(34264, DOUBLE, 16, struct.pack(e + '16d', *([1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0])))
    else:
        scale_x = spec.get('scale_x', SCALE)
        scale_y = spec.get('scale_y', SCALE)
        add(33550, DOUBLE, 3, struct.pack(e + '3d', scale_x, scale_y, 0.0))
        add(33922, DOUBLE, 6, struct.pack(e + '6d', 0.0, 0.0, 0.0, spec.get('tie_lon', TIE_LON), spec.get('tie_lat', TIE_LAT), 0.0))
    keys = []
    model = 2
    if spec.get('projected') is not None:
        model = 1
    keys.append((1024, 0, 1, model))
    keys.append((1025, 0, 1, spec.get('raster_type', 1)))
    if spec.get('projected') is not None:
        keys.append((3072, 0, 1, spec['projected']))
    else:
        keys.append((2048, 0, 1, spec.get('geo_key', 4326)))
    if spec.get('vertical') is not None:
        keys.append((4096, 0, 1, spec['vertical']))
    keys.sort()
    directory = [1, 1, 0, len(keys)]
    for key in keys:
        directory += list(key)
    add(34735, SHORT, len(directory), struct.pack(e + '%dH' % len(directory), *directory))
    if spec.get('nodata') is not None:
        text = (spec['nodata'] + '\x00').encode('ascii')
        if len(text) % 2:
            text += b'\x00'
        add(42113, ASCII, len(text), text)
    return tags


def write_ifd(tags, little, base, next_ifd):
    """One image's directory laid out at file offset base: its tag values that do not fit in an
    entry, then the directory. Returns the bytes, and the offset of the directory within them. Their
    size does not depend on base or next_ifd, so a file's layout can be planned before its links are
    written. base is even, so the alignment below is the same at any offset."""
    e = '<' if little else '>'
    extras = bytearray()
    entries = []
    # Values wider than four bytes go after the blocks, each at an even offset.
    for tag in sorted(tags):
        typ, count, raw = tags[tag]
        if len(raw) <= 4:
            value = raw.ljust(4, b'\x00')
        else:
            if len(extras) % 2:
                extras += b'\x00'
            value = struct.pack(e + 'I', base + len(extras))
            extras += raw
        entries.append((tag, typ, count, value))
    ifd_offset = base + len(extras)
    if ifd_offset % 2:
        extras += b'\x00'
        ifd_offset += 1
    directory = struct.pack(e + 'H', len(entries))
    for tag, typ, count, value in entries:
        directory += struct.pack(e + 'HHI', tag, typ, count) + value
    directory += struct.pack(e + 'I', next_ifd)
    return bytes(extras) + directory, ifd_offset - base


def build(path, spec):
    """Writes one file: the spec's image, then its 'overviews' in order, then the bare directory of
    'extra_ifd' if set. A 'loop' spec links the last image back to the first. Each image's keys are
    those of encode_blocks and image_tags, and 'payloads' overrides the encoded blocks."""
    little = spec.get('little', True)
    e = '<' if little else '>'
    images = [spec] + [dict(ov, little=ov.get('little', little), subfile=ov.get('subfile', 1))
                       for ov in spec.get('overviews', [])]

    # The block data of every image in order, then one pad byte if the total is odd.
    body = bytearray()
    cursor = 8  # the header
    shapes = []
    for img in images:
        payloads, block_width, block_height = encode_blocks(img)
        offsets, counts = [], []
        for data in payloads:
            offsets.append(cursor)
            counts.append(len(data))
            body += data
            cursor += len(data)
        shapes.append((offsets, counts, block_width, block_height))
    if len(body) % 2:
        body += b'\x00'
        cursor += 1
    tag_sets = [image_tags(img, offsets, counts, bw, bh)
                for img, (offsets, counts, bw, bh) in zip(images, shapes)]

    # Each image's tag values and directory follow the block data, in order. Their sizes are planned
    # first, so that each link can name the directory after it.
    plan = [write_ifd(tags, little, 0, 0) for tags in tag_sets]
    starts, ifd_addresses = [], []
    base = cursor
    for blob, ifd_rel in plan:
        starts.append(base)
        ifd_addresses.append(base + ifd_rel)
        base += len(blob)
    bare = b''
    if spec.get('extra_ifd'):
        # A second, empty image directory: the reader must refuse the file for it.
        bare = struct.pack(e + 'H', 1) + struct.pack(e + 'HHI', 256, 4, 1) + struct.pack(e + 'I', 0) + struct.pack(e + 'I', 0)
    blobs = []
    for i, tags in enumerate(tag_sets):
        if i + 1 < len(tag_sets):
            link = ifd_addresses[i + 1]
        elif spec.get('loop'):
            link = ifd_addresses[0]
        elif spec.get('extra_ifd'):
            link = base
        else:
            link = 0
        blobs.append(write_ifd(tags, little, starts[i], link)[0])

    order = b'II' if little else b'MM'
    if spec.get('bigtiff'):
        head = order + struct.pack(e + 'H', 43) + struct.pack(e + 'H', 8) + struct.pack(e + 'H', 0) + struct.pack(e + 'Q', 16)
        data = head + bytes(16)
    else:
        head = order + struct.pack(e + 'H', 42) + struct.pack(e + 'I', ifd_addresses[0])
        data = head + bytes(body) + b''.join(blobs) + bare
    with open(path, 'wb') as handle:
        handle.write(data)


def f(name, fn):
    return fn


def budget_surface(col, row):
    # A smooth, non-linear terrain-like surface (float32), so the Deflate and predictor work is real.
    return 120.0 + 60.0 * math.sin(col / 180.0) + 35.0 * math.cos(row / 140.0) + 0.001 * col * row


# A small overview (R107.b1.2): a strip of 6 by 5 posts, the plane sampled 8 posts apart.
SMALL_OVERVIEW = dict(values=plane_at(8), dtype='f32', width=6, height=5, rows_per_strip=5)


def main(argv):
    if len(argv) == 3 and argv[1] == '--budget':
        # ADR 0181 §9 budget file: the size and layout of one GLO-30 tile (2400 by 3600 posts,
        # 1 arc-second north-south and 1.5 arc-seconds east-west, pixel-is-point, Deflate, predictor 3,
        # 1024 by 1024 tiles). Written to the given path only; never committed.
        build(argv[2], dict(values=budget_surface, dtype='f32', little=True, layout='tile', compression=8,
                            predictor=3, width=2400, height=3600, tile=(1024, 1024), raster_type=2,
                            scale_x=1.0 / 2400.0, scale_y=1.0 / 3600.0, tie_lon=TIE_LON, tie_lat=TIE_LAT))
        print('wrote budget fixture to %s' % argv[2])
        return 0
    out = argv[1] if len(argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    os.makedirs(out, exist_ok=True)
    specs = {
        # Positive fixtures.
        'plane-f32-strip-none-le.tif': dict(values=plane, dtype='f32', little=True),
        'plane-f32-strip-none-be.tif': dict(values=plane, dtype='f32', little=False),
        'plane-f32-tile-deflate-pred3-le.tif': dict(values=plane, dtype='f32', little=True, layout='tile', compression=8, predictor=3),
        'plane-f32-tile-deflate-pred3-be.tif': dict(values=plane, dtype='f32', little=False, layout='tile', compression=8, predictor=3),
        'plane-i16-strip-deflate-pred2-le.tif': dict(values=plane, dtype='i16', little=True, compression=8, predictor=2),
        'plane-i16-tile-none-be.tif': dict(values=plane, dtype='i16', little=False, layout='tile'),
        'paraboloid-f32-tile-deflate-pred3-le.tif': dict(values=paraboloid, dtype='f32', little=True, layout='tile', compression=8, predictor=3),
        'void-f32-tile-deflate-pred3-le.tif': dict(values=lambda c, r: NODATA if (c, r) == (20, 15) else plane(c, r), dtype='f32', little=True, layout='tile', compression=8, predictor=3, nodata='-9999'),
        # Large enough that a 12 km window at 1 arc-second exceeds the triangle limit (decimation).
        'plane-f32-tile-deflate-pred3-large-le.tif': dict(values=plane, dtype='f32', little=True, layout='tile', compression=8, predictor=3, width=700, height=700, tile=(256, 256)),
        # PixelIsPoint, NAD83, and a pixel scale of 1/3 arc-second, to match the product rules.
        'glo30-like-pixel-point-f32.tif': dict(values=plane, dtype='f32', little=True, layout='tile', compression=8, predictor=3, raster_type=2, scale_x=1.0 / 2400.0, scale_y=SCALE),
        'dep-like-nad83-f32.tif': dict(values=plane, dtype='f32', little=True, layout='strip', compression=1, geo_key=4269, nodata='-999999', scale_x=1.0 / 10800.0, scale_y=1.0 / 10800.0, width=60, height=50),
        # The 1/3 arc-second step as USGS writes it (R107.b1.1): 2e-9 from 1/10800, and accepted as 3DEP.
        'dep13-usgs-scale-f32.tif': dict(values=plane, dtype='f32', little=True, scale_x=9.25925927753796e-05, scale_y=9.25925927753796e-05, width=60, height=50),
        'with-vertical-navd88-f32.tif': dict(values=plane, dtype='f32', little=True, vertical=5703),
        # LZW (R107.b1.1, ADR 0181 §7.2): int16 with predictor 2 and float32 with predictor 3, stripped
        # and tiled, in both byte orders. The noise surface fills and clears the table, so every code
        # width (9 to 12 bits) and a ClearCode in the middle of a block are read.
        'plane-i16-strip-lzw-pred2-le.tif': dict(values=plane, dtype='i16', little=True, compression=5, predictor=2),
        'plane-i16-strip-lzw-pred2-be.tif': dict(values=plane, dtype='i16', little=False, compression=5, predictor=2),
        'plane-i16-tile-lzw-pred2-le.tif': dict(values=plane, dtype='i16', little=True, layout='tile', compression=5, predictor=2),
        'plane-i16-tile-lzw-pred2-be.tif': dict(values=plane, dtype='i16', little=False, layout='tile', compression=5, predictor=2),
        'plane-f32-strip-lzw-pred3-le.tif': dict(values=plane, dtype='f32', little=True, compression=5, predictor=3),
        'plane-f32-strip-lzw-pred3-be.tif': dict(values=plane, dtype='f32', little=False, compression=5, predictor=3),
        'plane-f32-tile-lzw-pred3-le.tif': dict(values=plane, dtype='f32', little=True, layout='tile', compression=5, predictor=3),
        'plane-f32-tile-lzw-pred3-be.tif': dict(values=plane, dtype='f32', little=False, layout='tile', compression=5, predictor=3),
        'noise-i16-strip-lzw-pred2-le.tif': dict(values=noise, dtype='i16', little=True, compression=5, predictor=2, width=96, height=96, rows_per_strip=96),
        # R107.b1.2 (ADR 0181 §7.2, §7.3): a 3DEP tile as USGS distributes it: the full image, then two
        # reduced-resolution overviews (NewSubfileType 1), each a tiled LZW image of the same surface.
        # The full image is the plane, and its pixel scale is one arc-second, not the overview's.
        'plane-f32-tile-lzw-pred3-overviews-le.tif': dict(values=plane, dtype='f32', little=True, layout='tile', compression=5, predictor=3, overviews=[
            dict(values=plane_at(2), dtype='f32', layout='tile', compression=5, predictor=3, width=24, height=20, scale_x=2 * SCALE, scale_y=2 * SCALE),
            dict(values=plane_at(4), dtype='f32', layout='tile', compression=5, predictor=3, width=12, height=10, scale_x=4 * SCALE, scale_y=4 * SCALE)]),
        # The same file, with the overviews' blocks made of undecodable LZW streams (0xFF codes, which
        # point past the table). The reader never decodes an overview, so the file is still read.
        'plane-f32-tile-lzw-pred3-overviews-undecodable-le.tif': dict(values=plane, dtype='f32', little=True, layout='tile', compression=5, predictor=3, overviews=[
            dict(values=plane_at(2), dtype='f32', layout='tile', compression=5, predictor=3, width=24, height=20, scale_x=2 * SCALE, scale_y=2 * SCALE, payloads=[b'\xff\xff\xff'] * 4),
            dict(values=plane_at(4), dtype='f32', layout='tile', compression=5, predictor=3, width=12, height=10, scale_x=4 * SCALE, scale_y=4 * SCALE, payloads=[b'\xff\xff\xff'])]),
        # Sixteen overviews: the most the reader walks (GeoTiffBounds::furtherImages).
        'plane-f32-strip-none-overviews16-le.tif': dict(values=plane, dtype='f32', little=True, overviews=[SMALL_OVERVIEW] * 16),
        # Refusal fixtures (ADR 0181 §7.3).
        'refuse-multiband.tif': dict(values=plane, dtype='f32', little=True, samples=3),
        'refuse-float64.tif': dict(values=plane, dtype='f32', little=True, bits=64, sample_format=3),
        'refuse-jpeg.tif': dict(values=plane, dtype='f32', little=True, compression=7),
        # Corrupt LZW streams, each one block: a 64 by 64 int16 tile (8,192 bytes of output) unless
        # noted. Each is wrong in one way, and the reader names it.
        'refuse-lzw-bad-code.tif': dict(values=plane, dtype='i16', little=True, layout='tile', tile=(64, 64), width=64, height=64, compression=5, predictor=2,
                                        payloads=[lzw_pack([LZW_CLEAR, 65, 400, LZW_END])]),
        'refuse-lzw-table-overflow.tif': dict(values=plane, dtype='i16', little=True, layout='tile', tile=(64, 64), width=64, height=64, compression=5, predictor=2,
                                              payloads=[lzw_pack([LZW_CLEAR] + [code % 256 for code in range(4200)] + [LZW_END])]),
        'refuse-lzw-truncated.tif': dict(values=plane, dtype='i16', little=True, layout='tile', tile=(64, 64), width=64, height=64, compression=5, predictor=2,
                                         payloads=[lzw_pack([LZW_CLEAR, 65, 66, 67])]),
        'refuse-lzw-short.tif': dict(values=plane, dtype='i16', little=True, layout='tile', tile=(64, 64), width=64, height=64, compression=5, predictor=2,
                                     payloads=[lzw_pack([LZW_CLEAR, 65, 66, LZW_END])]),
        # 2,100 literal codes write 2,100 bytes into a block of 32 by 32 int16 (2,048 bytes).
        'refuse-lzw-more-output.tif': dict(values=plane, dtype='i16', little=True, layout='tile', tile=(32, 32), width=32, height=32, compression=5, predictor=2,
                                           payloads=[lzw_pack([LZW_CLEAR] + [code % 256 for code in range(2100)] + [LZW_END])]),
        'refuse-lzw-old-style.tif': dict(values=plane, dtype='i16', little=True, layout='tile', tile=(64, 64), width=64, height=64, compression=5, predictor=2,
                                         payloads=[b'\x00\x01' + bytes(20)]),
        # A second image: an empty full-resolution directory (NewSubfileType absent, so 0) after the first.
        'refuse-two-images.tif': dict(values=plane, dtype='f32', little=True, extra_ifd=True),
        # R107.b1.2 (ADR 0181 §7.2, §7.3): the further images the reader refuses by name.
        # A transparency mask (NewSubfileType 4: bit 2 without bit 0) after the first image.
        'refuse-mask-image.tif': dict(values=plane, dtype='f32', little=True, overviews=[
            dict(values=plane_at(2), dtype='f32', width=24, height=20, subfile=4)]),
        # The first image's link leads back to it: the chain loops after one overview.
        'refuse-ifd-loop.tif': dict(values=plane, dtype='f32', little=True, loop=True, overviews=[
            dict(values=plane_at(2), dtype='f32', width=24, height=20, scale_x=2 * SCALE, scale_y=2 * SCALE)]),
        # Seventeen overviews: one more than the reader walks.
        'refuse-overviews-17.tif': dict(values=plane, dtype='f32', little=True, overviews=[SMALL_OVERVIEW] * 17),
        # The first image is itself an overview (bit 0), or a mask (bit 2): the reader reads neither.
        'refuse-reduced-first.tif': dict(values=plane, dtype='f32', little=True, subfile=1),
        'refuse-mask-first.tif': dict(values=plane, dtype='f32', little=True, subfile=4),
        'refuse-osgb36.tif': dict(values=plane, dtype='f32', little=True, geo_key=4277),
        'refuse-etrs89.tif': dict(values=plane, dtype='f32', little=True, geo_key=4258),
        'refuse-projected.tif': dict(values=plane, dtype='f32', little=True, projected=32630),
        'refuse-transform.tif': dict(values=plane, dtype='f32', little=True, transform=True),
        'refuse-vertical-code.tif': dict(values=plane, dtype='f32', little=True, vertical=5714),
        'refuse-bigtiff.tif': dict(values=plane, dtype='f32', little=True, bigtiff=True),
    }
    # The unlabelled-vertical fixture is the GLO-30-like file (no vertical key): the DEM layer
    # refuses it unless --dem-vertical is given.
    for name, spec in specs.items():
        build(os.path.join(out, name), spec)
    print('wrote %d fixtures to %s' % (len(specs), out))


if __name__ == '__main__':
    sys.exit(main(sys.argv))
