#!/usr/bin/env python3
"""R107.b2.1 (ADR 0181, R107.b2 design "Inputs"): generates the synthetic orthophoto fixtures the
reference-image reader and importer tests use.

Run as: python3 -I make_reference_fixtures.py <output-directory>
(the directory defaults to this script's own directory). The output is deterministic for a given
zlib version; the tests check decoded pixels and georeferences, not compressed bytes.

Every positive image is north-up in WGS84 UTM zone 30N unless noted, with its top-left corner at
easting 499800 m and northing 5710000 m, so the corners lie well inside the zone's 6° band. Pixel
values come from rgb() and rgba() below; the tests repeat those formulas.

  north-up.png, north-up.pgw     an 8 by 6 RGB PNG and its world file (2 m pixels)
  north-up.jpg, north-up.jgw     the same placement as a JPEG (written by cjpeg when it is installed)
  ambiguous.png, ambiguous.pngw, ambiguous.pgw
                                 the first matching world file (.pngw) is used; .pgw is ignored
  refuse-rotated.png, refuse-rotated.pngw
                                 a world file with a non-zero rotation term (world-rotated)
  utm-rgb-strip-none.tif         8-bit RGB, uncompressed strips, pixel-is-area
  utm-rgb-be-strip-lzw.tif       big-endian, LZW, predictor 2, three-row strips
  utm-rgba-tile-deflate.tif      RGBA with unassociated alpha, 4 by 4 tiles, Deflate (zlib)
  utm-rgba-tile-lzw-pred2.tif    RGBA, 4 by 4 tiles, LZW, predictor 2
  utm-rgb-point-deflate.tif      RGB, pixel-is-point, Adobe Deflate (32946), one strip
  utm-rgb-nad83-26910.tif        RGB in NAD83 UTM zone 10N (EPSG:26910), around San Francisco
  utm-rgb-south.tif              RGB in WGS84 UTM zone 56S (EPSG:32756), around Sydney
  utm-rgb-transform.tif          RGB with a ModelTransformation tag and no tie or scale
  refuse-ortho-*.tif             one refusal each; refuse-ortho-zone-band.tif has a corner outside the band
  not-tiff.tif                   text in a .tif name (image-type)

The JPEG-compressed GeoTIFF (refuse-ortho-jpeg.tif) carries no real JPEG data: the reader names
the compression from its tag and never decodes it.
"""
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

WIDTH, HEIGHT = 8, 6
E0, N0 = 499800.0, 5710000.0   # the top-left corner of the zone 30N images
PIXEL = 2.0                    # metres per pixel for the GeoTIFFs and north-up.png
ZONE30_KEY = 32630
SHORT, LONG, ASCII, DOUBLE = 3, 4, 2, 12


def rgb(col, row):
    return ((col * 37 + row * 11) & 255, (col * 5 + row * 29 + 40) & 255, (255 - col * 13 - row * 7) & 255)


def rgba(col, row):
    r, g, b = rgb(col, row)
    return (r, g, b, (255 - 20 * row) & 255)


class BitWriter:
    """MSB-first code writer for LZW."""

    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, code, width):
        self.acc = (self.acc << width) | code
        self.n += width
        while self.n >= 8:
            self.n -= 8
            self.out.append((self.acc >> self.n) & 255)
        self.acc &= (1 << self.n) - 1

    def finish(self):
        if self.n:
            self.out.append((self.acc << (8 - self.n)) & 255)
        return bytes(self.out)


def lzw_encode(data):
    """A TIFF LZW stream for data of a few thousand bytes (no table reset): a ClearCode, greedy
    codes, and an EndOfInformation code. The width follows the decoder's early change (TIFF 6.0,
    section 13): it grows one code before the table needs it."""
    bits = BitWriter()
    table, width, next_code = {}, 9, 258
    bits.put(256, width)
    if not data:
        bits.put(257, width)
        return bits.finish()
    current = data[0]
    for byte in data[1:]:
        if (current, byte) in table:
            current = table[(current, byte)]
            continue
        bits.put(current, width)
        table[(current, byte)] = next_code
        next_code += 1
        if next_code > (1 << width) - 1 and width < 12:
            width += 1
        current = byte
    bits.put(current, width)
    next_code += 1
    if next_code > (1 << width) - 1 and width < 12:
        width += 1
    bits.put(257, width)
    return bits.finish()


def predictor2(samples, spp, row_length):
    """Horizontal differencing of 8-bit samples, per channel, along each row of row_length pixels."""
    out = bytearray(samples)
    for start in range(0, len(samples), row_length * spp):
        for j in range(row_length - 1, 0, -1):
            for k in range(spp):
                here = start + j * spp + k
                out[here] = (samples[here] - samples[here - spp]) & 255
    return bytes(out)


def pixels_bytes(fn, spp, cols, rows, col0=0, row0=0):
    out = bytearray()
    for row in range(row0, row0 + rows):
        for col in range(col0, col0 + cols):
            if col < WIDTH and row < HEIGHT:
                out += bytes(fn(col, row))
            else:
                out += bytes([0] * spp)
    return bytes(out)


def strip_blocks(fn, spp, rows_per_strip, width=WIDTH, height=HEIGHT):
    blocks = []
    for row in range(0, height, rows_per_strip):
        rows = min(rows_per_strip, height - row)
        blocks.append(pixels_bytes(fn, spp, width, rows, 0, row))
    return blocks


def tile_blocks(fn, spp, tile):
    blocks = []
    for ty in range(0, HEIGHT, tile):
        for tx in range(0, WIDTH, tile):
            blocks.append(pixels_bytes(fn, spp, tile, tile, tx, ty))
    return blocks


def entry(e, tag, typ, values):
    fmt = {SHORT: 'H', LONG: 'I', DOUBLE: 'd'}[typ]
    return (tag, typ, len(values), struct.pack(e + '%d%s' % (len(values), fmt), *values))


def write_tiff(path, spec):
    """One classic TIFF image. spec keys: little, blocks (bytes per strip or tile), tiled (tile size or
    None), width, height, spp, bits, photometric, compression, predictor, planar, extra (ExtraSamples),
    sample_format, raster_type, model, projected, linear_units, geographic, scale, tie, transform,
    rows_per_strip, plus 'keys' to replace the GeoKey list."""
    little = spec.get('little', True)
    e = '<' if little else '>'
    width = spec.get('width', WIDTH)
    height = spec.get('height', HEIGHT)
    spp = spec.get('spp', 3)
    body = bytearray()
    offsets, counts = [], []
    for block in spec['blocks']:
        offsets.append(8 + len(body))
        counts.append(len(block))
        body += block
    if len(body) % 2:
        body += b'\x00'
    entries = [
        entry(e, 256, LONG, [width]),
        entry(e, 257, LONG, [height]),
        entry(e, 258, SHORT, [spec.get('bits', 8)] * spp),
        entry(e, 259, SHORT, [spec.get('compression', 1)]),
        entry(e, 262, SHORT, [spec.get('photometric', 2)]),
        entry(e, 277, SHORT, [spp]),
        entry(e, 284, SHORT, [spec.get('planar', 1)]),
        entry(e, 339, SHORT, [spec.get('sample_format', 1)] * spp),
    ]
    if spec.get('predictor', 1) != 1:
        entries.append(entry(e, 317, SHORT, [spec['predictor']]))
    if spec.get('extra') is not None:
        entries.append(entry(e, 338, SHORT, [spec['extra']]))
    if spec.get('tiled'):
        tile = spec['tiled']
        entries += [entry(e, 322, SHORT, [tile]), entry(e, 323, SHORT, [tile]),
                    entry(e, 324, LONG, offsets), entry(e, 325, LONG, counts)]
    else:
        entries += [entry(e, 273, LONG, offsets), entry(e, 278, SHORT, [spec.get('rows_per_strip', 1)]),
                    entry(e, 279, LONG, counts)]
    if spec.get('scale') is not None:
        entries.append(entry(e, 33550, DOUBLE, list(spec['scale']) + [0.0]))
    if spec.get('tie') is not None:
        tie = list(spec['tie'])  # I, J, X, Y: the tag holds I, J, K, X, Y, Z
        entries.append(entry(e, 33922, DOUBLE, tie[:2] + [0.0] + tie[2:] + [0.0]))
    if spec.get('transform') is not None:
        entries.append(entry(e, 34264, DOUBLE, list(spec['transform'])))
    if spec.get('keys') is not None:
        keys = spec['keys']
    else:
        keys = [(1024, 0, 1, spec.get('model', 1)), (1025, 0, 1, spec.get('raster_type', 1))]
        if spec.get('projected') is not None:
            keys.append((3072, 0, 1, spec['projected']))
        if spec.get('geographic') is not None:
            keys.append((2048, 0, 1, spec['geographic']))
        if spec.get('linear_units') is not None:
            keys.append((3076, 0, 1, spec['linear_units']))
        keys.sort()
    if keys and spec.get('geokeys', True):
        directory = [1, 1, 0, len(keys)]
        for key in keys:
            directory += list(key)
        entries.append(entry(e, 34735, SHORT, directory))
    entries.sort()
    start = 8 + len(body)
    extras = bytearray()
    records = []
    for tag, typ, count, raw in entries:
        if len(raw) <= 4:
            value = raw.ljust(4, b'\x00')
        else:
            if (start + len(extras)) % 2:
                extras += b'\x00'
            value = struct.pack(e + 'I', start + len(extras))
            extras += raw
        records.append(struct.pack(e + 'HHI', tag, typ, count) + value)
    if (start + len(extras)) % 2:
        extras += b'\x00'
    ifd = start + len(extras)
    directory = struct.pack(e + 'H', len(records)) + b''.join(records) + struct.pack(e + 'I', 0)
    head = (b'II' if little else b'MM') + struct.pack(e + 'H', 42) + struct.pack(e + 'I', ifd)
    with open(path, 'wb') as handle:
        handle.write(head + bytes(body) + bytes(extras) + directory)


def utm_tiff(path, spp=3, fn=rgb, **kwargs):
    """A UTM image in zone 30N unless projected says otherwise. kwargs override write_tiff's spec."""
    spec = dict(spp=spp, width=WIDTH, height=HEIGHT, projected=ZONE30_KEY, model=1, linear_units=9001,
                scale=(PIXEL, PIXEL), tie=(0.0, 0.0, E0, N0), rows_per_strip=3,
                blocks=strip_blocks(fn, spp, 3))
    spec.update(kwargs)
    write_tiff(path, spec)


def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(path, fn, colour):
    """An 8-bit PNG, colour 2 (RGB) or 6 (RGBA), of WIDTH by HEIGHT pixels."""
    spp = 3 if colour == 2 else 4
    raw = bytearray()
    for row in range(HEIGHT):
        raw.append(0)
        for col in range(WIDTH):
            raw += bytes(fn(col, row))
    head = struct.pack('>IIBBBBB', WIDTH, HEIGHT, 8, colour, 0, 0, 0)
    data = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', head) + png_chunk(b'IDAT', zlib.compress(bytes(raw), 9)) \
        + png_chunk(b'IEND', b'')
    with open(path, 'wb') as handle:
        handle.write(data)
    assert spp in (3, 4)


def world_file(path, corner_e, corner_n, pixel_w=PIXEL, pixel_h=PIXEL, rotation=(0.0, 0.0)):
    """Six lines in the order A, D, B, E, C, F, where C and F are the centre of the upper-left pixel."""
    a, e = pixel_w, -pixel_h
    c, f = corner_e + a / 2, corner_n + e / 2
    lines = [a, rotation[0], rotation[1], e, c, f]
    with open(path, 'w') as handle:
        handle.write(''.join('%.6f\n' % value for value in lines))


def write_jpeg(path, fn):
    """A JPEG through cjpeg, when it is installed. Returns False (and writes nothing) otherwise."""
    cjpeg = shutil.which('cjpeg')
    if cjpeg is None:
        return False
    with tempfile.TemporaryDirectory() as scratch:
        ppm = os.path.join(scratch, 'source.ppm')
        with open(ppm, 'wb') as handle:
            handle.write(b'P6\n%d %d\n255\n' % (WIDTH, HEIGHT))
            for row in range(HEIGHT):
                for col in range(WIDTH):
                    handle.write(bytes(fn(col, row)))
        subprocess.run([cjpeg, '-quality', '92', '-outfile', path, ppm], check=True)
    return True


def main(argv):
    out = argv[1] if len(argv) > 1 else HERE
    os.makedirs(out, exist_ok=True)

    def p(name):
        return os.path.join(out, name)

    write_png(p('north-up.png'), rgb, 2)
    world_file(p('north-up.pgw'), E0, N0)
    if not write_jpeg(p('north-up.jpg'), rgb):
        print('cjpeg not found: north-up.jpg was not written', file=sys.stderr)
    else:
        world_file(p('north-up.jgw'), E0 + 100.0, N0 + 200.0, pixel_w=1.5, pixel_h=1.5)

    write_png(p('ambiguous.png'), rgb, 2)
    world_file(p('ambiguous.pngw'), E0, N0)
    world_file(p('ambiguous.pgw'), E0 + 500.0, N0 - 500.0)

    write_png(p('refuse-rotated.png'), rgb, 2)
    world_file(p('refuse-rotated.pngw'), E0, N0, rotation=(0.5, 0.0))

    utm_tiff(p('utm-rgb-strip-none.tif'))
    utm_tiff(p('utm-rgb-be-strip-lzw.tif'), little=False, compression=5, predictor=2,
             blocks=[lzw_encode(predictor2(block, 3, WIDTH)) for block in strip_blocks(rgb, 3, 3)])
    utm_tiff(p('utm-rgba-tile-deflate.tif'), spp=4, fn=rgba, extra=2, tiled=4, compression=8,
             blocks=[zlib.compress(b, 9) for b in tile_blocks(rgba, 4, 4)])
    utm_tiff(p('utm-rgba-tile-lzw-pred2.tif'), spp=4, fn=rgba, extra=2, tiled=4, compression=5, predictor=2,
             blocks=[lzw_encode(predictor2(b, 4, 4)) for b in tile_blocks(rgba, 4, 4)])
    # Pixel-is-point: the tie is the centre of pixel (0, 0), half a pixel in from the corner.
    utm_tiff(p('utm-rgb-point-deflate.tif'), raster_type=2, compression=32946,
             tie=(0.0, 0.0, E0 + PIXEL / 2, N0 - PIXEL / 2), rows_per_strip=HEIGHT,
             blocks=[zlib.compress(strip_blocks(rgb, 3, HEIGHT)[0], 9)])
    # NAD83 UTM zone 10N (EPSG:26910), around San Francisco.
    utm_tiff(p('utm-rgb-nad83-26910.tif'), projected=26910, tie=(0.0, 0.0, 549000.0, 4180000.0))
    # WGS84 UTM zone 56S: the tie is in metres from the false northing of 10,000,000 m.
    utm_tiff(p('utm-rgb-south.tif'), projected=32756, tie=(0.0, 0.0, 330000.0, 6250000.0))
    # A ModelTransformation: a north-up matrix (row-major 4 by 4) and no tie or scale.
    matrix = [PIXEL, 0, 0, E0, 0, -PIXEL, 0, N0, 0, 0, 1, 0, 0, 0, 0, 1]
    utm_tiff(p('utm-rgb-transform.tif'), scale=None, tie=None, transform=matrix)

    # Refusals. Each file carries the one feature it names; the rest is a valid RGB image.
    utm_tiff(p('refuse-ortho-jpeg.tif'), compression=7, blocks=[b'\xff\xd8\xff\xe0' * 4])
    utm_tiff(p('refuse-ortho-grey.tif'), spp=1, photometric=1, fn=lambda c, r: (c * 30,),
             blocks=strip_blocks(lambda c, r: (c * 30,), 1, 3))
    utm_tiff(p('refuse-ortho-palette.tif'), spp=1, photometric=3, fn=lambda c, r: (c,),
             blocks=strip_blocks(lambda c, r: (c,), 1, 3))
    utm_tiff(p('refuse-ortho-16bit.tif'), bits=16, blocks=[bytes(WIDTH * 3 * 2 * 3)] * 3)
    utm_tiff(p('refuse-ortho-planar.tif'), planar=2)
    utm_tiff(p('refuse-ortho-associated.tif'), spp=4, fn=rgba, extra=1,
             blocks=strip_blocks(rgba, 4, 3))
    utm_tiff(p('refuse-ortho-predictor3.tif'), predictor=3)
    utm_tiff(p('refuse-ortho-raster-type.tif'), raster_type=3)
    utm_tiff(p('refuse-ortho-geographic.tif'), projected=None, model=2, geographic=4326,
             scale=(1.0 / 36000.0, 1.0 / 36000.0), tie=(0.0, 0.0, -3.0, 52.0), linear_units=None)
    utm_tiff(p('refuse-ortho-user-crs.tif'), projected=32767)
    utm_tiff(p('refuse-ortho-web-mercator.tif'), projected=3857)
    utm_tiff(p('refuse-ortho-feet.tif'), linear_units=9002)
    utm_tiff(p('refuse-ortho-both.tif'), transform=[PIXEL, 0, 0, E0, 0, -PIXEL, 0, N0, 0, 0, 1, 0, 0, 0, 0, 1])
    utm_tiff(p('refuse-ortho-rotated.tif'), scale=None, tie=None,
             transform=[PIXEL, 0.5, 0, E0, 0, -PIXEL, 0, N0, 0, 0, 1, 0, 0, 0, 0, 1])
    utm_tiff(p('refuse-ortho-untagged.tif'), scale=None, tie=None)
    utm_tiff(p('refuse-ortho-size.tif'), width=4097, height=1, rows_per_strip=1,
             blocks=[bytes(4097 * 3)])
    utm_tiff(p('refuse-ortho-zone-band.tif'), tie=(0.0, 0.0, 50000.0, N0))
    with open(p('not-tiff.tif'), 'wb') as handle:
        handle.write(b'This is text, not a TIFF file.\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
