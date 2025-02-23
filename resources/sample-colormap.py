#!/usr/bin/env python3

import sys
import argparse
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

def srgb_to_linear(srgb):
    linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    return linear

def linearize(values, do_linearize):
    return srgb_to_linear(values) if do_linearize else values

def get_colormap(name):
    try:
        return sns.color_palette(name, as_cmap=True)
    except ValueError:
        raise ValueError(f"Colormap '{name}' not found in Matplotlib or Seaborn.")

def main(arguments):
    parser = argparse.ArgumentParser(description="Sample a colormap at regularly spaced intervals.")
    parser.add_argument("name", type=str, help="The name of the matplotlib or seaborn colormap to sample.")
    parser.add_argument("-n", "--num-samples", type=int, default=256, help="The number of samples to generate.")
    parser.add_argument("-a", "--include-alpha", action="store_true", help="Include the alpha channel in the output.")
    parser.add_argument("-l", "--linearize", action="store_true", help="Output linear (instead of sRGB encoded) values.")

    args = parser.parse_args(arguments)

    xs = np.linspace(0, 1, args.num_samples)
    print(xs)

    cmap = get_colormap(args.name)
    rgba_values = cmap(xs)

    rgb = rgba_values[:, :3]
    alpha = rgba_values[:, 3] if rgba_values.shape[1] == 4 else np.ones_like(xs)
    values = linearize(rgb, args.linearize)
    if args.include_alpha:
        values = np.column_stack((values, alpha))

    values = ",\n    ".join(["{" + ", ".join([str(y) + "f" for y in x]) + "}" for x in values]) + ","
    print(f"static const std::vector<float4> data = {{\n    {values}\n}};")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))