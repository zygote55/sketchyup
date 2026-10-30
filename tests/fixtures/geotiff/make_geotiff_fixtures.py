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
"""
import os
import struct
import sys
import zlib

WIDTH, HEIGHT = 48, 40
TIE_LON, TIE_LAT = -0.2, 51.6
SCALE = 1.0 / 3600.0  # one arc-second, both ways
NODATA = -9999.0


def plane(col, row):
    return 10 + 2 * col + 3 * row


def paraboloid(col, row):
    return 50 + (col - 24) ** 2 + (row - 20) ** 2


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


def build(path, spec):
    """spec keys: values(col,row)->number, dtype 'f32'|'i16', little (bool), layout 'strip'|'tile',
    compression 1|8, predictor 1|2|3, tie_lon, tie_lat, scale_x, scale_y, width, height,
    nodata (str or None), raster_type 1|2, geo_key 4326|4269|4258|4277 (default 4326),
    vertical (int or None), samples (1), bits (override), sample_format (override),
    extra_ifd (bool), projected (int or None), transform (bool), lzw (bool)."""
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
    values = spec['values']
    if dtype == 'f32':
        pack = lambda v: struct.pack(e + 'f', v)
        bps = 4
    else:
        pack = lambda v: struct.pack(e + 'h', v)
        bps = 2

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
        payloads.append(data)

    if spec.get('lzw'):
        payloads = [bytes(len(p)) for p in payloads]
        compression = 5

    # Layout: header, block data, then the tags' values and the directory.
    header_len = 8
    body = bytearray()
    offsets, counts = [], []
    cursor = header_len
    for data in payloads:
        offsets.append(cursor)
        counts.append(len(data))
        body += data
        cursor += len(data)
    if len(body) % 2:
        body += b'\x00'
        cursor += 1

    tags = {}  # tag -> (type, count, bytes)
    def add(tag, typ, count, raw):
        tags[tag] = (typ, count, raw)
    SHORT, LONG, ASCII, DOUBLE = 3, 4, 2, 12
    add(256, LONG, 1, struct.pack(e + 'I', w))
    add(257, LONG, 1, struct.pack(e + 'I', h))
    if spec.get('transform'):
        pass
    add(258, SHORT, 1, struct.pack(e + 'H', bits))
    add(259, SHORT, 1, struct.pack(e + 'H', compression))
    add(262, SHORT, 1, struct.pack(e + 'H', 1))
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

    # Values wider than four bytes go after the directory's header; keep the layout simple:
    # every tag value is written after the blocks, each at an even offset.
    extras = bytearray()
    entries = []
    base = cursor
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
    next_ifd = 0
    if spec.get('extra_ifd'):
        next_ifd = ifd_offset + 2 + 12 * len(entries) + 4
    directory_bytes = struct.pack(e + 'H', len(entries))
    for tag, typ, count, value in entries:
        directory_bytes += struct.pack(e + 'HHI', tag, typ, count) + value
    directory_bytes += struct.pack(e + 'I', next_ifd)
    if spec.get('extra_ifd'):
        # A second, empty image directory: the reader must refuse the file for it.
        directory_bytes += struct.pack(e + 'H', 1) + struct.pack(e + 'HHI', 256, 4, 1) + struct.pack(e + 'I', 0) + struct.pack(e + 'I', 0)

    order = b'II' if little else b'MM'
    magic = 42 if not spec.get('bigtiff') else 43
    head = order + struct.pack(e + 'H', magic) + struct.pack(e + 'I', ifd_offset)
    if spec.get('bigtiff'):
        head = order + struct.pack(e + 'H', 43) + struct.pack(e + 'H', 8) + struct.pack(e + 'H', 0) + struct.pack(e + 'Q', 16)
        data = head + bytes(16)
    else:
        data = head + bytes(body) + bytes(extras) + directory_bytes
    with open(path, 'wb') as handle:
        handle.write(data)


def f(name, fn):
    return fn


def main(argv):
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
        'with-vertical-navd88-f32.tif': dict(values=plane, dtype='f32', little=True, vertical=5703),
        # Refusal fixtures (ADR 0181 §7.3).
        'refuse-multiband.tif': dict(values=plane, dtype='f32', little=True, samples=3),
        'refuse-float64.tif': dict(values=plane, dtype='f32', little=True, bits=64, sample_format=3),
        'refuse-lzw.tif': dict(values=plane, dtype='f32', little=True, layout='tile', compression=8, predictor=3, lzw=True),
        'refuse-two-images.tif': dict(values=plane, dtype='f32', little=True, extra_ifd=True),
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
