# Height maps

`waves.png` is the height map sequence that comes with Project 4 (Water Surface): the course's `HeightMap\000.png` to `199.png` (also `WaterSurface\Images\waves5\` in the course's sample), 200 grey 512 x 512 pictures of a patch of sea that tile seamlessly and loop.

To keep the repository small it holds every second frame (000, 002, ... 198), each shrunk to 256 x 256 by averaging 2 x 2 texels, laid out ten to a row in a 2560 x 2560 grey PNG (about 2.2 MB instead of 14 MB). The 3D texture interpolates between the frames, so the motion stays smooth.

The program uses the full sequence instead when a `HeightMap\` folder holding `000.png`, `001.png`, ... sits beside the exe (copy the course's folder there). See `src/WaterWaves.h` and ADR 0021.
