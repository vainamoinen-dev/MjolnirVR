# SMAA

This directory vendors the official SMAA implementation and lookup-table
headers from <https://github.com/iryoku/smaa> at commit
`71c806a838bdd7d517df19192a20f0c61b3ca29d`.

Unmodified upstream files:

- `SMAA.hlsl`
- `Textures/AreaTex.h`
- `Textures/SearchTex.h`
- `LICENSE.txt`

Halo MCC VR's D3D11 bindings and entry-point wrappers live in
`src/dll/core/smaa_runtime.hlsl.in`. CMake compiles its three vertex and three pixel
entry points to strict Shader Model 5 bytecode and embeds them in the DLL; the
installed mod has no loose SMAA shader or lookup-texture dependency.
