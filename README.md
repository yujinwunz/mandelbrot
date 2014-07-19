MANDELBROT
==========

# Build

dependencies: 

* SDL 1.25 (not 2)
* MPIR

# Usage

launch application. Click to zoom by 8x. Right click to zoom out by 2x.

Left arrow to double bits of precision (use when detail gets pixelated/distorted)
Right arrow to halve bits of precision
Up arrow to double the number of iterations (use when detail starts disappearing)
Down arrow to halve the number of iterations

# Code

Multithreaded for speed, and uses the MPIR arbitrary-precision library to zoom in arbitrarily.
Uses divide and conquer to avoid unnecessary calculation by filling in boxes bounded by a solid border, taking advantage of the mandelbrot set's connected property. This optimization is almost always correct with rare minor exceptions.