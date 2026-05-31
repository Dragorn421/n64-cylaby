# SPDX-FileCopyrightText: 2026 Dragorn421
# SPDX-License-Identifier: CC0-1.0

# Model Compiler

import argparse
from pathlib import Path

import numpy as np
from pygltflib import GLTF2


def accessor_to_np(gltf: GLTF2, accessor_index: int):
    accessor = gltf.accessors[accessor_index]

    n = accessor.count

    # https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#accessor-data-types

    component_type_size, component_dtype = {
        5120: (1, "<b"),  # signed byte
        5121: (1, "<B"),  # unsigned byte
        5122: (2, "<h"),  # signed short
        5123: (2, "<H"),  # unsigned short
        5125: (4, "<I"),  # unsigned int
        5126: (4, "<f"),  # float
    }[accessor.componentType]

    n_components = {
        "SCALAR": 1,
        "VEC2": 2,
        "VEC3": 3,
        "VEC4": 4,
        "MAT2": 4,
        "MAT3": 9,
        "MAT4": 16,
    }[accessor.type]

    assert accessor.bufferView is not None
    bufferView = gltf.bufferViews[accessor.bufferView]

    byteOffset = 0
    if accessor.byteOffset is not None:
        byteOffset += accessor.byteOffset
    if bufferView.byteOffset is not None:
        byteOffset += bufferView.byteOffset

    if bufferView.byteStride is None:
        stride = n_components * component_type_size
    else:
        stride = bufferView.byteStride

    buffer = gltf.buffers[bufferView.buffer]
    data = gltf.get_data_from_buffer_uri(buffer.uri)
    x = np.frombuffer(
        memoryview(data),  # type: ignore
        dtype=component_dtype,
        count=n * stride // component_type_size,
        offset=byteOffset,
    )
    x = np.lib.stride_tricks.as_strided(
        x,
        shape=(n, n_components),
        strides=(stride, component_type_size),
    )
    return x


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("gltf")
    parser.add_argument("c")
    parser.add_argument("h")
    parser.add_argument("--scale", type=float, default=1.0)
    args = parser.parse_args()

    gltf = GLTF2.load(args.gltf)
    assert gltf is not None

    declarations: list[str] = []

    with Path(args.c).open("w") as f:
        f.write("#include <stdint.h>\n")
        f.write("\n")
        f.write('#include "model.h"\n')
        f.write("\n")
        for i_mesh, mesh in enumerate(gltf.meshes):
            # Blender gltf export names the gltf meshes per the blender mesh datablock
            # name and the object name (which we want to use) is used as node name
            node_names = [
                _n.name
                for _n in gltf.nodes
                if _n.mesh == i_mesh and _n.name is not None
            ]
            if node_names:
                mesh_prefix = node_names[0]
            else:
                mesh_prefix = mesh.name
            f.write(f"// Mesh {mesh_prefix}\n")
            primitives_symbols: list[str] = []
            for i_primitive, primitive in enumerate(mesh.primitives):
                primitive_prefix = f"{mesh_prefix}_{i_primitive}"
                material_name = (
                    gltf.materials[primitive.material].name
                    if primitive.material is not None
                    else ""
                )
                f.write(
                    f" // Primitive material {primitive.material} {material_name}\n"
                )

                assert isinstance(primitive.attributes.POSITION, int)
                positions = accessor_to_np(gltf, primitive.attributes.POSITION)
                positions = positions.astype(float)
                positions *= args.scale

                assert isinstance(primitive.attributes.NORMAL, int)
                normals = accessor_to_np(gltf, primitive.attributes.NORMAL)

                if primitive.mode is None:
                    mode = 4
                else:
                    mode = primitive.mode
                # https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_mesh_primitive_mode
                assert mode == 4  # TRIANGLES

                assert primitive.indices is not None
                indices = accessor_to_np(gltf, primitive.indices)

                n_vertices = positions.shape[0]
                assert normals.shape[0] == n_vertices
                assert np.max(indices) < n_vertices

                f.write(f" struct vertex {primitive_prefix}_vertices[] = " "{\n")
                declarations.append(f"struct vertex {primitive_prefix}_vertices[];")
                for i_vertex in range(n_vertices):
                    pos = positions[i_vertex]
                    norm = normals[i_vertex]
                    f.write(
                        "  { "
                        "{"
                        f"{round(pos[0])}, {round(pos[1])}, {round(pos[2])}"
                        "}, "
                        f"MGFX_NRMF({norm[0]}, {norm[1]}, {norm[2]})"
                        " },\n"
                    )
                f.write(" };\n")

                f.write(f" uint16_t {primitive_prefix}_indices[] = " "{")
                declarations.append(f"uint16_t {primitive_prefix}_indices[];")
                for i_index in range(indices.shape[0]):
                    if i_index % 30 == 0:
                        f.write("\n ")
                    f.write(f" {indices[i_index][0]},")
                f.write("\n")
                f.write(" };\n")

                f.write(f" struct primitive {primitive_prefix} = " "{\n")
                declarations.append(f"struct primitive {primitive_prefix};")
                if primitive.material is None:
                    f.write(f"  -1,\n")
                else:
                    f.write(f"  {primitive.material},\n")
                f.write(f"  {primitive_prefix}_vertices,\n")
                f.write(f"  {primitive_prefix}_indices,\n")
                f.write(f"  {indices.shape[0]},\n")
                f.write(" };\n")

                primitives_symbols.append(primitive_prefix)

            f.write(f"struct primitive *{mesh_prefix}_primitives[] = " "{\n")
            declarations.append(f"struct primitive *{mesh_prefix}_primitives[];")
            for prim_sym in primitives_symbols:
                f.write(f" &{prim_sym},\n")
            f.write("};\n")
            f.write(f"struct mesh {mesh_prefix} = " "{\n")
            declarations.append(f"struct mesh {mesh_prefix};")
            f.write(f" {mesh_prefix}_primitives,\n")
            f.write(f" {len(primitives_symbols)},\n")
            f.write("};\n")

    with Path(args.h).open("w") as f:
        f.write("#include <stdint.h>\n")
        f.write("\n")
        f.write('#include "model.h"\n')
        f.write("\n")
        f.write("".join(f"extern {decl}\n" for decl in declarations))


if __name__ == "__main__":
    main()
