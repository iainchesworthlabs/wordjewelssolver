# lightprobe

Makes `src/app/prototype/shake/qml/lightprobe/sky.ktx`, the board scene's light
probe (see [the shake prototype](../../docs/design/shake-prototype.md)).

## What the file is

`BoardScene.qml` lights the dice from a sky: an image-based light, which Qt
Quick 3D wants as a *prefiltered* cube map, one mip level for each roughness.
Given a plain image it makes that itself, on the GPU, on the first frame of
every `View3D` - 512 x 512 faces, six levels, 1024 samples a texel, whatever
the size of the image. Where there is a GPU that is not noticed; where there
is not (a virtual machine, a remote desktop, a weak device) it is seconds of
CPU every time the scene is made.

Given a KTX file that carries the key `QT_IBL_BAKER_VERSION`, Qt skips all of
that and uses the cube map as it is. `sky.ktx` is that file, made from the sky
the scene would otherwise make at run time: Qt's `ProceduralSkyTextureData`
with its defaults. It is the same file Cubeulator ships, made by the same tool.

## Making it

`bake_probe` needs Qt's *private* Quick 3D modules and the IBL baker, which the
application does not link, so it is a project of its own and not part of the
build:

```sh
cmake -S tools/lightprobe -B build/lightprobe -DCMAKE_PREFIX_PATH=<Qt prefix>
cmake --build build/lightprobe --config Release
build/lightprobe/bake_probe src/app/prototype/shake/qml/lightprobe/sky.ktx 128
```

It needs a Qt with `Quick3DIblBaker` (the Windows, macOS and Linux desktop
kits have it) and a way to draw: the baker renders with Qt's RHI.

The second argument is the number of texels across a face of the first mip
level, 16 to 512, a power of two (the default is 128, 1 MB). What it does is:

1. generates the sky and writes it as an OpenEXR image, rows in reverse order;
2. runs `QSSGIblBaker` over it - the code the run-time path is - which gives
   512 x 512 faces and six levels in RGBA16F;
3. averages every level down to the asked-for size. Each level keeps its
   roughness; only the number of texels across drops.
