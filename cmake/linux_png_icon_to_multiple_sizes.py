#!/usr/bin/env python3
"""
Generate multiple icon sizes for Linux from a single PNG source.
Similar to hello_imgui's icon generation for other platforms.
"""

import os
import sys
from PIL import Image


def create_linux_icons(input_png: str, output_dir: str):
    """Create icon files at multiple resolutions for Linux desktop environments."""
    # Standard icon sizes for Linux/FreeDesktop specification
    sizes = [16, 24, 32, 48, 64, 96, 128, 256, 512]

    # Create output directory if it doesn't exist
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Open the source image
    try:
        img = Image.open(input_png)
    except Exception as e:
        print(f"Error: Failed to open input image '{input_png}': {e}", file=sys.stderr)
        sys.exit(1)

    # Convert to RGBA if needed
    if img.mode != "RGBA":
        img = img.convert("RGBA")

    # Generate each size
    for size in sizes:
        try:
            # Resize using high-quality Lanczos resampling
            resized = img.resize((size, size), Image.Resampling.LANCZOS)

            # Save to output directory
            output_path = os.path.join(output_dir, f"icon-{size}.png")
            resized.save(output_path, "PNG", optimize=True)

            print(f"Generated: {output_path} ({size}x{size})")
        except Exception as e:
            print(
                f"Warning: Failed to generate {size}x{size} icon: {e}", file=sys.stderr
            )

    print(f"Successfully generated {len(sizes)} icon sizes in {output_dir}")


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate multiple icon sizes for Linux from a PNG image"
    )
    parser.add_argument(
        "input_png",
        help="Input PNG image (should be high resolution, e.g., 512x512 or larger)",
    )
    parser.add_argument("output_dir", help="Output directory for generated icons")

    args = parser.parse_args()

    # Validate input file exists
    if not os.path.exists(args.input_png):
        print(f"Error: Input file '{args.input_png}' does not exist", file=sys.stderr)
        sys.exit(1)

    create_linux_icons(args.input_png, args.output_dir)


if __name__ == "__main__":
    main()
