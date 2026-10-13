"""Cheap stdlib similarity metric: silhouette IoU between bm_core renders and reference images.

Renders are fitted to each model's bounding sphere (preview.View), so the metric is
scale- and position-invariant and orientation-sensitive; flat silhouettes only, colour ignored.

Sources:
- PNG reader: zlib (Python 3.11+) or stdlib zlib for decompression; filters per RFC 2083
- Paeth predictor: libpng (MIT license)
"""
import math
import struct
import zlib

from . import imageio, preview
from .errors import ValidationError


VIEWS = {
    "front": (0.0, 0.0),
    "side": (math.pi / 2, 0.0),
    "top": (0.0, math.pi / 2),
}
DEFAULT_MIN_IOU = 0.75
REF_MAX = 512


def read_png(data: bytes):
    """Read a PNG image from bytes. Returns (Image, alpha).

    - Image: the decoded pixels as 0xRRGGBB ints.
    - alpha: list of 0..255 per pixel for RGBA, None for RGB.

    Supports only:
    - Bit depth 8
    - Colour type 2 (RGB) or 6 (RGBA)
    - Interlace 0 (no interlacing)
    - Filters 0-4 (standard Paeth predictor)

    Raises ValidationError if unsupported or larger than 512x512.
    Source: libpng (MIT), RFC 2083
    """
    # Check PNG signature
    if len(data) < 8 or data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValidationError("not a PNG file", "png")

    pos = 8
    w, h, bit_depth, colour_type, compression, filter_method, interlace = None, None, None, None, None, None, None
    idat_chunks = []

    # Parse chunks
    while pos < len(data):
        if pos + 8 > len(data):
            raise ValidationError("truncated chunk header", "png")

        chunk_len = struct.unpack(">I", data[pos:pos+4])[0]
        chunk_type = data[pos+4:pos+8]
        chunk_data = data[pos+8:pos+8+chunk_len]
        chunk_crc = struct.unpack(">I", data[pos+8+chunk_len:pos+12+chunk_len])[0]

        pos += 12 + chunk_len

        if chunk_type == b'IHDR':
            if chunk_len != 13:
                raise ValidationError("IHDR chunk must be 13 bytes", "png")
            w, h, bit_depth, colour_type, compression, filter_method, interlace = struct.unpack(">IIBBBBB", chunk_data)

            if bit_depth != 8:
                raise ValidationError(f"unsupported PNG: bit depth {bit_depth}, only 8 supported", "png")
            if colour_type not in (2, 6):
                raise ValidationError(f"unsupported PNG: colour type {colour_type}, only 2 (RGB) and 6 (RGBA) supported", "png")
            if compression != 0:
                raise ValidationError(f"unsupported PNG: compression method {compression}", "png")
            if filter_method != 0:
                raise ValidationError(f"unsupported PNG: filter method {filter_method}", "png")
            if interlace != 0:
                raise ValidationError(f"unsupported PNG: interlace {interlace}, only 0 supported", "png")

            if w > REF_MAX or h > REF_MAX:
                raise ValidationError(f"reference PNG larger than {REF_MAX}x{REF_MAX}: downscale it", "png")

        elif chunk_type == b'IDAT':
            idat_chunks.append(chunk_data)

        elif chunk_type == b'IEND':
            break

    if w is None or h is None:
        raise ValidationError("no IHDR chunk found", "png")

    if not idat_chunks:
        raise ValidationError("no IDAT chunks found", "png")

    # Decompress image data
    idat_data = b''.join(idat_chunks)
    try:
        raw = zlib.decompress(idat_data)
    except zlib.error as e:
        raise ValidationError(f"zlib decompression failed: {e}", "png")

    # Decode pixels
    bytes_per_pixel = 4 if colour_type == 6 else 3
    bytes_per_row = 1 + w * bytes_per_pixel  # 1 filter byte + data
    expected_len = h * bytes_per_row

    if len(raw) != expected_len:
        raise ValidationError(f"image data size mismatch: expected {expected_len}, got {len(raw)}", "png")

    # Paeth predictor (libpng, MIT)
    def paeth(a, b, c):
        p = a + b - c
        pa = abs(p - a)
        pb = abs(p - b)
        pc = abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        elif pb <= pc:
            return b
        else:
            return c

    pixels = []
    alpha = [] if colour_type == 6 else None

    prev_row = [0] * (w * bytes_per_pixel)
    for y in range(h):
        row_offset = y * bytes_per_row
        filter_type = raw[row_offset]
        row_data = bytearray(raw[row_offset + 1:row_offset + 1 + w * bytes_per_pixel])

        if filter_type not in (0, 1, 2, 3, 4):
            raise ValidationError(f"unsupported PNG: filter type {filter_type}", "png")

        # Unfilter
        for x in range(w * bytes_per_pixel):
            left = row_data[x - bytes_per_pixel] if x >= bytes_per_pixel else 0
            up = prev_row[x]
            up_left = prev_row[x - bytes_per_pixel] if x >= bytes_per_pixel else 0

            if filter_type == 0:  # None
                pass
            elif filter_type == 1:  # Sub
                row_data[x] = (row_data[x] + left) & 0xFF
            elif filter_type == 2:  # Up
                row_data[x] = (row_data[x] + up) & 0xFF
            elif filter_type == 3:  # Average
                row_data[x] = (row_data[x] + (left + up) // 2) & 0xFF
            elif filter_type == 4:  # Paeth
                row_data[x] = (row_data[x] + paeth(left, up, up_left)) & 0xFF

        prev_row = list(row_data)

        # Convert to pixels
        for x in range(w):
            base = x * bytes_per_pixel
            r = row_data[base]
            g = row_data[base + 1]
            b = row_data[base + 2]
            pixel = (r << 16) | (g << 8) | b
            pixels.append(pixel)

            if colour_type == 6:
                alpha.append(row_data[base + 3])

    img = imageio.Image(w, h, pixels=pixels)
    return img, alpha


def model_mask(model, view="front", size=(96, 96)):
    """Render a model twice with black and white backgrounds, return silhouette mask.

    mask[i] = True where the model was drawn (foreground), False where background.

    Renders with ortho projection, fitted to the model's bounding sphere.
    Unknown view raises ValidationError("unsupported view: ...", "view").
    """
    if view not in VIEWS:
        raise ValidationError(f"unsupported view: {view}", "view")

    yaw, pitch = VIEWS[view]

    # Render with black background
    img_black = preview.render_mesh(model, size=size, yaw=yaw, pitch=pitch, ortho=True, background=0x000000)

    # Render with white background
    img_white = preview.render_mesh(model, size=size, yaw=yaw, pitch=pitch, ortho=True, background=0xFFFFFF)

    # Mask: True where pixels match (both backgrounds → rendered)
    mask = [a == b for a, b in zip(img_black.pixels, img_white.pixels)]
    return mask


def image_mask(img, alpha=None, background=None):
    """Create a binary mask from an image.

    - If alpha is provided: mask[i] = (alpha[i] >= 128)
    - Otherwise: mask[i] = (pixel != background), where background defaults to img.pixels[0]
    """
    if alpha is not None:
        return [a >= 128 for a in alpha]

    if background is None:
        background = img.pixels[0]

    return [p != background for p in img.pixels]


def mask_image(mask, w, h):
    """Create an Image from a binary mask.

    - True pixels: 0xFFFFFF (white)
    - False pixels: 0x000000 (black)
    """
    pixels = [0xFFFFFF if m else 0x000000 for m in mask]
    return imageio.Image(w, h, pixels=pixels)


def iou(a, b):
    """Intersection over union of two binary masks.

    - Different lengths: raises ValidationError("masks differ in size", "iou")
    - Both empty: returns 1.0
    - Otherwise: intersection / union
    """
    if len(a) != len(b):
        raise ValidationError("masks differ in size", "iou")

    intersection = sum(1 for x, y in zip(a, b) if x and y)
    union = sum(1 for x, y in zip(a, b) if x or y)

    if union == 0:
        return 1.0

    return intersection / union


def compare_models(model, ref, size=(96, 96), views=("front", "side", "top")):
    """Compare a model against a reference model across multiple views.

    Returns dict {view: iou, ..., "mean": arithmetic mean}.
    """
    results = {}
    for view in views:
        model_m = model_mask(model, view=view, size=size)
        ref_m = model_mask(ref, view=view, size=size)
        results[view] = iou(model_m, ref_m)

    results["mean"] = sum(results.values()) / len(results)
    return results


def compare_png(model, data, view="front"):
    """Compare a model against a reference PNG image.

    Returns the IoU of the model's silhouette against the PNG's foreground.
    - Reads the PNG at its own (w, h)
    - Creates masks
    - Computes IoU
    """
    if view not in VIEWS:
        raise ValidationError(f"unsupported view: {view}", "view")

    ref_img, ref_alpha = read_png(data)
    ref_mask = image_mask(ref_img, alpha=ref_alpha)

    yaw, pitch = VIEWS[view]
    model_img = preview.render_mesh(model, size=(ref_img.w, ref_img.h), yaw=yaw, pitch=pitch, ortho=True, background=0x000000)
    model_m = image_mask(model_img, background=0x000000)

    return iou(model_m, ref_mask)


__all__ = [
    "VIEWS", "DEFAULT_MIN_IOU", "REF_MAX",
    "read_png", "model_mask", "image_mask", "mask_image", "iou",
    "compare_models", "compare_png",
]
