"""Preprocess GLSL or compile explicit Vulkan layouts into SDL3 D3D12 bytecode."""

import argparse
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path


GLSL_TEXT = re.compile(
    r'"(?:\\.|[^"\\])*"|//(?:\\\n|[^\n])*|/\*.*?\*/|^[ \t]*#[ \t]*include\b',
    re.DOTALL | re.MULTILINE,
)
TARGET_DIRECTIVE = re.compile(r"/\*\s*(?:RENDERTARGETS|DRAWBUFFERS)\s*:")
TARGET_MARKER = "cata_iris_internal_target_"
BUFFER_FORMAT = re.compile(r"^\s*const\s+int\s+((?:colortex|shadowcolor)\d+)Format\s*=\s*([^;\r\n]+);", re.MULTILINE)
BUFFER_CLEAR = re.compile(r"^\s*const\s+bool\s+((?:colortex|shadowcolor)\d+)Clear\s*=\s*(true|false)\s*;", re.MULTILINE)
BUFFER_CLEAR_COLOR = re.compile(r"^\s*const\s+vec4\s+((?:colortex|shadowcolor)\d+)ClearColor\s*=\s*([^;]+);", re.MULTILINE)
PACK_SAMPLER = re.compile(
    r"^[ \t]*uniform[ \t]+(?:[iu]?sampler(?:2D|2DArray|3D|Cube)|sampler(?:2D|2DArray|Cube)Shadow)"
    r"[ \t]+([a-zA-Z_][a-zA-Z_0-9]*)[ \t]*;",
    re.MULTILINE,
)
PACK_UNIFORM = re.compile(
    r"^[ \t]*uniform[ \t]+((?:bool|float|int|uint|[iu]?vec[234]|mat[234]))"
    r"[ \t]+([a-zA-Z_][a-zA-Z_0-9]*)[ \t]*(?:=[ \t]*([^;\r\n]+))?;",
    re.MULTILINE,
)
GROUPED_UNIFORM = re.compile(
    r"^[ \t]*uniform[ \t]+(\w+)[ \t]+([a-zA-Z_]\w*(?:[ \t]*,[ \t]*[a-zA-Z_]\w*)+)[ \t]*;",
    re.MULTILINE,
)
EXPLICIT_VARYING_LOCATION = re.compile(
    r"layout\s*\([^)]*\blocation\s*=[^)]*\)\s*(?:\w+\s+)*?(in|out)\s+"
    r"(?:\w+\s+)+?(\w+)\s*(?:\[[^\]]*\]\s*)*;"
)
HLSL_VARYING_MATRIX = re.compile(
    r"(\b)(float[2-4]x[2-4]\s+\w+(?:\[\d+\])*\s*:\s*TEXCOORD\d+\s*;)"
)
PACK_ATTRIBUTE = re.compile(
    r"^[ \t]*(?:attribute|in)[ \t]+((?:float|int|uint|[iu]?vec[234]))"
    r"[ \t]+([a-zA-Z_][a-zA-Z_0-9]*)[ \t]*;",
    re.MULTILINE,
)
LEGACY_VERTEX_ATTRIBUTES = {
    "gl_Vertex": "vec4", "gl_Normal": "vec3", "gl_Color": "vec4",
    "gl_MultiTexCoord0": "vec4", "gl_MultiTexCoord1": "vec4", "gl_MultiTexCoord2": "vec4",
}
NATIVE_WATER_INPUTS = {
    "cata_iris_position": "vec3", "cata_iris_local_uv": "vec2", "cata_iris_vertex_color": "vec4",
    "cata_iris_instance_offset": "vec4", "cata_iris_atlas_rect": "vec4",
    "cata_iris_instance_tint": "vec4", "cata_iris_normal": "vec3",
}
NATIVE_TERRAIN_INPUTS = {**NATIVE_WATER_INPUTS, "cata_iris_terrain_light": "uint"}
NATIVE_WATER_UNIFORMS = {
    "cata_iris_anchor": "vec3", "cata_iris_lightmap": "vec2", "cata_iris_material": "float",
}
NATIVE_WATER_VALUES = {
    "vaColor": "cata_iris_vertex_color * cata_iris_instance_tint",
    "gl_Vertex": "vec4(cata_iris_position + cata_iris_instance_offset.xyz + cata_iris_anchor, 1.0)",
    "gl_Normal": "cata_iris_normal",
    "gl_Color": "cata_iris_vertex_color * cata_iris_instance_tint",
    "gl_MultiTexCoord0": "vec4(mix(cata_iris_atlas_rect.xy, cata_iris_atlas_rect.zw, cata_iris_local_uv), 0, 1)",
    "gl_MultiTexCoord1": "vec4(cata_iris_lightmap, 0, 1)",
    "gl_MultiTexCoord2": "vec4(cata_iris_lightmap, 0, 1)",
    "at_tangent": "vec4(1, 0, 0, 1)", "mc_Entity": "vec3(cata_iris_material, 0, 0)",
    "mc_midTexCoord": "(cata_iris_atlas_rect.xy + cata_iris_atlas_rect.zw) * 0.5",
}
# WorldRenderingPhase ordinals used by Iris StandardMacros.
IRIS_RENDER_STAGES = (
    "NONE", "SKY", "SUNSET", "CUSTOM_SKY", "SUN", "MOON", "STARS", "VOID",
    "TERRAIN_SOLID", "TERRAIN_CUTOUT_MIPPED", "TERRAIN_CUTOUT", "ENTITIES",
    "BLOCK_ENTITIES", "DESTROY", "OUTLINE", "DEBUG", "HAND_SOLID",
    "TERRAIN_TRANSLUCENT", "TRIPWIRE", "PARTICLES", "CLOUDS", "RAIN_SNOW",
    "WORLD_BORDER", "HAND_TRANSLUCENT",
)
FULLSCREEN_VALUES = {
    "gl_Vertex": "vec4(cata_iris_quad, 0, 1)",
    "gl_MultiTexCoord0": "vec4(cata_iris_quad, 0, 1)",
    "gl_Color": "vec4(1)",
}


def preprocess_pack_source(glslang, source, temp_dir, macro_names=()):
    """Keep attachment metadata in the same macro branches as the shader."""
    text = source.read_text(encoding="utf-8-sig")
    if TARGET_MARKER in text:
        raise ValueError("Shader source contains the reserved Iris render target marker.")
    comments = {}
    attachment_comments = []

    def mark(match):
        comment = match.group()
        if comment.lstrip().startswith("#"):
            raise ValueError("Expand shader pack includes before preprocessing attachment metadata.")
        targets = TARGET_DIRECTIVE.match(comment)
        body = comment[2:-2] if comment.startswith("/*") else ""
        if not targets and not any(pattern.search(body)
                                   for pattern in (BUFFER_FORMAT, BUFFER_CLEAR, BUFFER_CLEAR_COLOR)):
            return comment
        marker = f"{TARGET_MARKER}{len(comments)}_"
        comments[marker] = (comment.replace("\n", " "), None) if targets else ("", body)
        return f" {marker} " + "\n" * comment.count("\n")

    marked = temp_dir / "metadata.frag"
    markers = ""
    if macro_names:
        if "cata_iris_property_macro_" in text:
            raise ValueError("Shader source contains a reserved Iris property macro marker.")
        markers = "\n" + "\n".join(
            f"#ifdef {name}\ncata_iris_property_macro_{index} {name}\n#endif"
            for index, name in enumerate(macro_names)
        ) + "\n"
    marked.write_text(GLSL_TEXT.sub(mark, text) + markers, encoding="utf-8")
    result = subprocess.run(
        [*glslang, "-E", str(marked)], cwd=source.parent,
        check=True, stdout=subprocess.PIPE, encoding="utf-8",
    ).stdout
    def restore(match):
        code, metadata = comments.get(match.group(), (match.group(), None))
        if metadata is not None:
            attachment_comments.append(metadata)
        return code

    source = re.sub(rf"\b{TARGET_MARKER}[0-9]+_\b", restore, result)
    return source, "\n".join(attachment_comments)


def disable_unmapped_fragment_outputs(source):
    directives = [match.group() for match in GLSL_TEXT.finditer(source)
                  if TARGET_DIRECTIVE.match(match.group())]
    if not directives:
        return source
    directive = re.fullmatch(r"/\*\s*(RENDERTARGETS|DRAWBUFFERS)\s*:\s*([\d,\s]*)\*/",
                             directives[-1])
    if not directive:
        return source  # Keep invalid metadata for the graph's normal diagnostics.
    targets = directive[2].strip()
    if not targets:
        return source
    count = len(targets) if directive[1] == "DRAWBUFFERS" else len(targets.split(","))
    output = re.compile(
        r"(?P<qualifier>\blayout\s*\(\s*location\s*=\s*(?P<location>\d+)\s*\)\s*out\s+)"
        r"(?P<declaration>\w+\s+\w+\s*;)"
    )

    def lower(match):
        if int(match["location"]) < count:
            return match[0]
        # Preserve reads/writes in the shader while removing the unmapped
        # hardware output. Keep preprocessed line numbers for diagnostics.
        return "\n" * match["qualifier"].count("\n") + match["declaration"]

    return output.sub(lower, source)


def lower_pack_source(source, stage, samplers, uniforms, attributes, native_water=False, fullscreen=False, native_terrain=False,
                      native_shadow=False):
    """Assign SDL3 registers and convert the supported compatibility vertex inputs."""
    # Legacy packs use 'sampler' as a local name. Vulkan GLSL reserves it.
    if re.search(r"\b[iu]?sampler\w+\s+sampler\b", source):
        source = re.sub(r"\bsampler\b", "cata_iris_legacy_sampler", source)
    prefix = []
    if stage == "vert":
        initializers = []
        native_values = NATIVE_WATER_VALUES if native_water or native_terrain or native_shadow else FULLSCREEN_VALUES if fullscreen else None
        if fullscreen:
            prefix.append("vec2 cata_iris_quad;")
            initializers.append("    cata_iris_quad = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);")
            source = re.sub(r"\bftransform\s*\(\s*\)",
                            "vec4(cata_iris_quad * 2.0 - 1.0, 0.0, 1.0)", source)
        # Atlas coordinates are supplied directly by the draw, in texture space.
        source = re.sub(r"\bgl_TextureMatrix\s*\[\s*0\s*\]", "mat4(1.0)", source)
        if native_water or native_terrain or native_shadow:
            lightmap_matrix = ("mat4(vec4(1.0/256.0,0,0,0),vec4(0,1.0/256.0,0,0),"
                               "vec4(0,0,1.0/256.0,0),vec4(1.0/32.0,1.0/32.0,0,1))")
            source = re.sub(r"\bgl_TextureMatrix\s*\[\s*[12]\s*\]", lightmap_matrix, source)
        matrix_prefix = "shadow" if native_shadow else "gbuffer"
        if native_shadow:
            source = re.sub(r"\bftransform\s*\(\s*\)",
                            "(shadowProjection * shadowModelView * gl_Vertex)", source)
        for old, suffix in (("gl_ModelViewMatrix", "ModelView"),
                            ("gl_ProjectionMatrix", "Projection"),
                            ("gl_ModelViewMatrixInverse", "ModelViewInverse"),
                            ("gl_ProjectionMatrixInverse", "ProjectionInverse")):
            source = re.sub(rf"\b{old}\b", matrix_prefix + suffix, source)
        source = re.sub(r"\bgl_NormalMatrix\b",
                        f"transpose(mat3({matrix_prefix}ModelViewInverse))", source)
        source = re.sub(r"\bgl_ModelViewProjectionMatrix\b",
                        f"({matrix_prefix}Projection * {matrix_prefix}ModelView)", source)
        for suffix in ("Projection", "ModelView", "ModelViewInverse", "ProjectionInverse"):
            field = matrix_prefix + suffix
            if re.search(rf"\b{field}\b", source) and field not in uniforms:
                source += f"\nuniform mat4 {field};"
                uniforms = [*uniforms, field]
        inputs = {name: index for index, name in enumerate(attributes)}
        if len(inputs) != len(attributes):
            raise ValueError("A shader pack vertex attribute appears more than once in the binding list.")
        declared_inputs = set()

        def vertex_input(kind, name):
            lowered = f"cata_iris_{name[3:]}" if name in LEGACY_VERTEX_ATTRIBUTES else name
            if native_values is not None:
                if name not in native_values:
                    raise ValueError(f"Iris vertex mode cannot supply attribute {name}.")
                value = native_values[name]
                if name == "mc_Entity" and kind == "vec2":
                    value = f"({value}).xy"
                initializers.append(f"    {lowered} = {value};")
                return f"{kind} {lowered};"
            if name not in inputs:
                raise ValueError(f"Shader pack vertex attribute {name} has no SDL3 input location.")
            declared_inputs.add(name)
            return f"layout(location = {inputs[name]}) in {kind} {lowered};"

        attributes_only = PACK_ATTRIBUTE.sub("", source) if native_values is not None else source

        def lower_attribute(match):
            kind, name = match.groups()
            # Some packs declare optional attributes in every variant. Require a
            # binding only if the preprocessed variant can actually reference it.
            if (native_values is not None and name not in native_values and
                    not re.search(rf"\b{re.escape(name)}\b", attributes_only)):
                return ""
            return vertex_input(kind, name)

        source = PACK_ATTRIBUTE.sub(lower_attribute, source)
        for name, kind in LEGACY_VERTEX_ATTRIBUTES.items():
            if re.search(rf"\b{name}\b", source):
                prefix.append(vertex_input(kind, name))
                source = re.sub(rf"\b{name}\b", f"cata_iris_{name[3:]}", source)
        if native_water or native_terrain or native_shadow:
            mesh_inputs = NATIVE_TERRAIN_INPUTS if native_terrain else NATIVE_WATER_INPUTS
            prefix.extend(f"layout(location = {location}) in {kind} {name};"
                          for location, (name, kind) in enumerate(mesh_inputs.items()))
            mesh_uniforms = dict(NATIVE_WATER_UNIFORMS)
            if native_terrain:
                del mesh_uniforms["cata_iris_lightmap"]
                prefix.append("vec2 cata_iris_lightmap;")
                initializers.insert(0, "    cata_iris_lightmap = 16.0 * vec2(cata_iris_terrain_light & 15u, "
                                    "(cata_iris_terrain_light >> 4u) & 15u);")
            uniforms = [*uniforms, *mesh_uniforms]
            source += "\n" + "\n".join(f"uniform {kind} {name};"
                                      for name, kind in mesh_uniforms.items())
        else:
            if missing := inputs.keys() - declared_inputs:
                raise ValueError(f"Missing vertex attributes: {', '.join(sorted(missing))}.")
        source, entries = re.subn(r"\bvoid\s+main\s*\(", "void cata_iris_main(", source)
        if entries != 1:
            raise ValueError("Expected one shader pack vertex entry point.")
        source += "\nvoid main() {\n" + "\n".join(initializers)
        source += "\n    cata_iris_main();\n"
        if fullscreen:
            source += "    gl_Position.y = -gl_Position.y;\n"
        source += "    gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);\n}\n"

    texture_set = 0 if stage == "vert" else 2
    bindings = {name: index for index, name in enumerate(samplers)}
    if len(bindings) != len(samplers):
        raise ValueError("A shader pack sampler appears more than once in the binding list.")
    declared = set()

    def bind(match):
        name = match.group(1)
        if name not in bindings:
            label = "vertex" if stage == "vert" else "fragment"
            raise ValueError(f"Shader pack sampler {name} has no SDL3 {label} binding.")
        declared.add(name)
        return f"layout(set = {texture_set}, binding = {bindings[name]}) " + match.group().lstrip()

    source = PACK_SAMPLER.sub(bind, source)
    if missing := bindings.keys() - declared:
        raise ValueError(f"Missing supported standalone sampler declaration: {', '.join(sorted(missing))}.")
    fields = {}

    def collect_uniform(match):
        kind, name, _ = match.groups()
        if name not in uniforms:
            raise ValueError(f"Shader pack uniform {name} has no SDL3 frame binding.")
        fields[name] = kind
        return ""

    source = PACK_UNIFORM.sub(collect_uniform, source)
    if len(set(uniforms)) != len(uniforms):
        raise ValueError("A shader pack uniform appears more than once in the binding list.")
    if missing := set(uniforms) - fields.keys():
        raise ValueError(f"Missing standalone uniform declaration: {', '.join(sorted(missing))}.")
    version = "#version 450"
    if fields:
        constants = "\n".join(f"    {fields[name]} {name};" for name in uniforms)
        prefix.append(
            f"layout(set = {texture_set + 1}, binding = 0, std140) uniform CataIrisFrame {{\n{constants}\n}};"
        )
    if prefix:
        # Keep #version/#extension before the generated declarations.
        insert_at = source.find("\n")
        # A uniform can be referenced by any following pack function.
        lines = source[insert_at + 1:].splitlines(keepends=True)
        for line in lines:
            if line.strip() and not line.lstrip().startswith("#"):
                break
            insert_at += len(line)
        source = source[:insert_at + 1] + "\n".join(prefix) + "\n" + source[insert_at + 1:]
    return re.sub(r"^#version[ \t]+\d+(?:[ \t]+(?:compatibility|core))?[ \t]*$",
                  version, source, count=1, flags=re.MULTILINE)


def uniform_defaults(prepared):
    defaults = {}
    for kind, name, initializer in PACK_UNIFORM.findall(prepared):
        if not initializer:
            continue
        value = initializer.strip()
        if kind == "bool" and value in ("true", "false"):
            defaults[name] = int(value == "true")
        elif kind == "float" and re.fullmatch(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?[fF]?", value):
            defaults[name] = float(value.rstrip("fF"))
        elif kind in ("int", "uint") and re.fullmatch(r"[+-]?\d+[uU]?", value):
            defaults[name] = int(value.rstrip("uU"))
        else:
            raise ValueError(f"Unsupported initializer for pack uniform {name}: {kind} {value}.")
    return defaults


def varying_interface(reflection, direction, prepared):
    explicit = {name for qualifier, name in EXPLICIT_VARYING_LOCATION.findall(prepared)
                if qualifier == direction}
    types = reflection.get("types", {})

    def shape(entry):
        kind = entry["type"]
        members = types.get(kind, {}).get("members")
        return [kind if members is None else [[member["name"], shape(member)] for member in members],
                entry.get("array", [])]

    return [{"name": entry["name"], "location": entry["location"], "shape": shape(entry),
             "explicit": entry["name"] in explicit}
            for entry in reflection.get("inputs" if direction == "in" else "outputs", [])]


def replace_spirv_input_locations(spirv, moves):
    if not moves:
        return
    data = spirv.read_bytes()
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    # SPIR-V uses word-count/opcode pairs after its five-word header.
    op_variable, op_decorate, storage_input, decoration_location = 59, 71, 1, 30
    inputs = set()
    locations = []
    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xffff
        if not count or offset + count > len(words):
            raise ValueError("Invalid SPIR-V instruction size during varying linkage.")
        if opcode == op_variable and count >= 4 and words[offset + 3] == storage_input:
            inputs.add(words[offset + 2])
        elif opcode == op_decorate and count == 4 and words[offset + 2] == decoration_location:
            locations.append((words[offset + 1], offset + 3))
        offset += count
    changed = set()
    for target, index in locations:
        start = words[index]
        if target in inputs and start in moves:
            words[index] = moves[start]
            changed.add(start)
    if changed != moves.keys():
        raise ValueError("Cannot relocate an Iris fragment interface block member.")
    spirv.write_bytes(struct.pack(f"<{len(words)}I", *words))


def compact_spirv_samplers(spirv, textures, sampler_set):
    if any(entry["set"] != sampler_set or "array" in entry for entry in textures):
        raise ValueError("Cannot compact Iris sampler arrays or unrelated descriptor sets.")
    ordered = sorted(textures, key=lambda entry: entry["binding"])
    slots = {entry["binding"]: dense for dense, entry in enumerate(ordered)}
    if len(slots) != len(ordered):
        raise ValueError("Duplicate Iris sampler bindings.")
    data = spirv.read_bytes()
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    op_decorate, decoration_binding, decoration_set = 71, 33, 34
    sets, bindings = {}, {}
    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xffff
        if not count or offset + count > len(words):
            raise ValueError("Invalid SPIR-V instruction size during sampler compaction.")
        if opcode == op_decorate and count == 4:
            target, decoration = words[offset + 1:offset + 3]
            if decoration == decoration_set:
                sets[target] = words[offset + 3]
            elif decoration == decoration_binding:
                bindings[target] = offset + 3
        offset += count
    changed = set()
    for target, index in bindings.items():
        original = words[index]
        if sets.get(target) == sampler_set and original in slots:
            words[index] = slots[original]
            changed.add(original)
    if changed != slots.keys():
        raise ValueError("Cannot relocate an Iris sampler descriptor.")
    spirv.write_bytes(struct.pack(f"<{len(words)}I", *words))
    for entry in ordered:
        entry["binding"] = slots[entry["binding"]]


def link_fragment_varyings(spirv, reflection, prepared, vertex_outputs):
    by_name = {entry["name"]: entry for entry in vertex_outputs}
    by_slot = {entry["location"]: entry for entry in vertex_outputs if entry["explicit"]}
    moves = {}
    used = set()
    for fragment in varying_interface(reflection, "in", prepared):
        vertex = by_name.get(fragment["name"])
        if fragment["explicit"]:
            vertex = by_slot.get(fragment["location"], vertex)
        if vertex is None:
            raise ValueError(f"Iris fragment input {fragment['name']} has no vertex output.")
        if fragment["shape"] != vertex["shape"]:
            raise ValueError(f"Iris varying types differ for {fragment['name']}.")
        start, end = fragment["location"], vertex["location"]
        if fragment["explicit"] and start != end:
            raise ValueError(f"Iris explicit fragment location disagrees for {fragment['name']}.")
        if end in used:
            raise ValueError(f"Multiple Iris fragment inputs select vertex location {end}.")
        used.add(end)
        if start != end:
            moves[start] = end
    replace_spirv_input_locations(spirv, moves)
    for entry in reflection.get("inputs", []):
        entry["location"] = moves.get(entry["location"], entry["location"])


def reflect_pack_layout(reflection, stage, attributes):
    sampler_set = 0 if stage == "vert" else 2
    textures = sorted(reflection.get("textures", []), key=lambda texture: texture["binding"])
    if any(texture["set"] != sampler_set or texture["binding"] != slot or "array" in texture
           for slot, texture in enumerate(textures)):
        raise ValueError("Shader pack reflection requires contiguous, non-array sampler bindings.")
    for key in ("images", "separate_images", "separate_samplers", "ssbos", "push_constants", "subpass_inputs"):
        if reflection.get(key):
            raise ValueError(f"Unsupported shader pack reflection resource: {key}.")
    blocks = reflection.get("ubos", [])
    if len(blocks) > 1 or any(block["name"] != "CataIrisFrame" or block["set"] != sampler_set + 1 or
                              block["binding"] != 0 or "array" in block for block in blocks):
        raise ValueError("Shader pack reflection requires the generated frame block at slot zero.")
    frame = None
    if blocks:
        block = blocks[0]
        frame = {"size": (block["block_size"] + 15) // 16 * 16,
                 "members": reflection["types"][block["type"]]["members"]}
    inputs = reflection.get("inputs", [])
    if stage == "vert":
        if any(not 0 <= entry.get("location", -1) < len(attributes) for entry in inputs):
            raise ValueError("Shader pack reflection found an undeclared vertex input location.")
        inputs = [{**entry, "name": attributes[entry["location"]]} for entry in inputs]
    return {
        "version": 1,
        "stage": stage,
        "samplers": [{"name": texture["name"], "type": texture["type"], "slot": texture["binding"]}
                     for texture in textures],
        "frame": frame,
        "inputs": sorted(inputs, key=lambda entry: entry["location"]),
        "outputs": sorted(reflection.get("outputs", []), key=lambda entry: entry["location"]),
    }


def lower_readonly_textures(hlsl, layout, sampler_limit):
    """Use D3D12 SRV slots for color buffers that never use a sampler state."""
    entries = layout["samplers"]
    if len(entries) <= sampler_limit:
        return hlsl
    declarations = {
        (match["kind"], int(match["slot"])): match
        for match in re.finditer(
            r"(?m)^(?:Texture\w+(?:<[^>]+>)?|Sampler(?:Comparison)?State)\s+"
            r"(?P<name>\w+)\s*:\s*register\((?P<kind>[ts])(?P<slot>\d+),\s*space2\);",
            hlsl)
    }
    identifiers = Counter(re.findall(r"\b[A-Za-z_]\w*\b", hlsl))
    if any(("t", slot) not in declarations for slot in range(len(entries))):
        return hlsl
    readonly = []
    unused = set()
    for entry in entries:
        slot = entry["slot"]
        state = declarations.get(("s", slot))
        name = declarations[("t", slot)]["name"]
        if identifiers[name] == 1 and (state is None or identifiers[state["name"]] == 1):
            unused.add(slot)
            continue
        loads = len(re.findall(rf"\b{re.escape(name)}\s*\.\s*(?:Load|GetDimensions)\s*\(", hlsl))
        if (entry["type"] == "sampler2D" and re.fullmatch(r"colortex\d+", entry["name"])
                and loads > 0 and identifiers[name] == loads + 1
                and (state is None or identifiers[state["name"]] == 1)):
            readonly.append(slot)
    needed = max(0, len(entries) - len(unused) - sampler_limit)
    if needed > min(8, len(readonly)):
        return hlsl
    # Prefer graph-owned images to the imported native scene color.
    readonly.sort(key=lambda slot: entries[slot]["name"] == "colortex0")
    readonly = set(readonly[:needed])
    order = sorted((slot for slot in range(len(entries)) if slot not in unused),
                   key=lambda slot: slot in readonly)
    replacements = [(*match.span(), "") for (_, slot), match in declarations.items() if slot in unused]
    for new_slot, old_slot in enumerate(order):
        for kind in ("t", "s"):
            match = declarations.get((kind, old_slot))
            if match is None:
                continue
            if kind == "s" and old_slot in readonly:
                replacements.append((*match.span(), ""))
            else:
                replacements.append((*match.span("slot"), str(new_slot)))
    for start, end, value in sorted(replacements, reverse=True):
        hlsl = hlsl[:start] + value + hlsl[end:]
    layout["samplers"] = [
        dict(entries[old], slot=new, **({"readonly_texture": True} if old in readonly else {}))
        for new, old in enumerate(order)
    ]
    return hlsl


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pack_cache_key(args, source, profile):
    tools = {}
    commands = {"glslang": args.glslang, "validator": args.spirv_val, "cross": args.spirv_cross,
                "hlsl": args.dxc if args.format == "dxil" else args.fxc}
    for role, command in commands.items():
        executable = shutil.which(command)
        if executable is None:
            raise ValueError(f"Cannot fingerprint missing Iris compiler tool: {command}")
        path = Path(executable)
        tools[role] = file_sha256(path)
        if role == "hlsl" and args.format == "dxil":
            for name in ("dxcompiler.dll", "dxil.dll"):
                companion = path.with_name(name)
                if companion.is_file():
                    tools[name] = file_sha256(companion)
    payload = {
        "compiler": file_sha256(Path(__file__)),
        "toolchain": tools,
        "source": source,
        "defines": args.define,
        "profile": profile,
        "pack": file_sha256(args.pack_archive) if args.pack_archive else None,
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode("utf-8")).hexdigest()


def verified_cache_hit(output, metadata, key, stage, format_name):
    try:
        layout = json.loads(metadata.read_text(encoding="utf-8"))
        return (isinstance(layout, dict) and layout.get("version") == 1
                and layout.get("stage") == stage and layout.get("format") == format_name
                and layout.get("cache_key") == key
                and file_sha256(output) == layout.get("bytecode_sha256"))
    except (OSError, ValueError):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--stage", choices=("vert", "frag", "comp"), required=True)
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--include", type=Path, action="append", default=[])
    parser.add_argument("--preprocess-only", action="store_true",
                        help="preprocess include-expanded graphics GLSL and keep attachment directives")
    parser.add_argument("--probe-macro", action="append", default=[], metavar="NAME",
                        help="with --preprocess-only, return the active values as a JSON object")
    parser.add_argument("--pack-sampler", action="append", default=[], metavar="NAME",
                        help="lower a pack stage with these sampled inputs in binding order")
    parser.add_argument("--pack-uniform", action="append", default=[], metavar="NAME",
                        help="lower a pack stage with these fields in its std140 frame block")
    parser.add_argument("--pack-attribute", action="append", default=[], metavar="NAME",
                        help="lower a pack vertex stage with these input locations in order")
    parser.add_argument("--auto-pack-bindings", action="store_true",
                        help="reflect supported sampler and uniform declarations after preprocessing")
    vertices = parser.add_mutually_exclusive_group()
    vertices.add_argument("--native-water-vertices", "--native-world-vertices", action="store_true",
                        help="use the native SDL3 water vertex and instance streams")
    vertices.add_argument("--native-terrain-vertices", action="store_true",
                        help="use the SDL3 terrain vertex and instance streams with per-instance light")
    vertices.add_argument("--native-shadow-vertices", action="store_true",
                        help="supply native geometry with shadow-space compatibility matrices")
    vertices.add_argument("--fullscreen-vertices", action="store_true",
                          help="supply compatibility quad inputs from an SDL3 fullscreen triangle")
    parser.add_argument("--glslang", default="glslangValidator")
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--fxc", default="fxc")
    parser.add_argument("--dxc", default="dxc")
    parser.add_argument("--format", choices=("dxbc", "dxil"), default="dxbc")
    parser.add_argument("--reflect", action="store_true",
                        help="write an Iris layout sidecar beside the compiled pack stage")
    parser.add_argument("--cache", action="store_true",
                        help="reuse a matching, verified bytecode and reflection pair")
    parser.add_argument("--link-vertex-layout", type=Path,
                        help="align fragment inputs to the verified vertex interface sidecar")
    parser.add_argument("--pack-archive", type=Path,
                        help="include the selected ZIP contents in the cache identity")
    args = parser.parse_args()
    native_vertices = (args.native_water_vertices or args.native_terrain_vertices or
                       args.native_shadow_vertices or args.fullscreen_vertices)
    lower_pack = args.pack_sampler or args.pack_uniform or args.pack_attribute or native_vertices or args.auto_pack_bindings
    pack_source = args.preprocess_only or lower_pack
    if pack_source and (args.stage == "comp" or args.include):
        parser.error("Pack preprocessing requires a graphics stage and include-expanded source")
    if args.pack_attribute and args.stage != "vert":
        parser.error("--pack-attribute requires --stage vert")
    if args.auto_pack_bindings and (args.pack_sampler or args.pack_uniform):
        parser.error("--auto-pack-bindings replaces --pack-sampler and --pack-uniform")
    if native_vertices and (args.stage != "vert" or args.pack_attribute):
        parser.error("The native vertex modes require --stage vert and replace --pack-attribute")
    if args.preprocess_only and lower_pack:
        parser.error("--preprocess-only cannot be combined with pack bindings")
    if args.probe_macro and (not args.preprocess_only or
                             any(not re.fullmatch(r"[A-Za-z_]\w*", name) for name in args.probe_macro)):
        parser.error("--probe-macro requires --preprocess-only and valid GLSL identifiers")
    if args.reflect and not lower_pack:
        parser.error("--reflect requires shader pack bindings")
    if args.cache and not args.reflect:
        parser.error("--cache requires --reflect")
    if args.link_vertex_layout and (args.stage != "frag" or not args.reflect):
        parser.error("--link-vertex-layout requires a reflected fragment stage")
    vertex_outputs = None
    if args.link_vertex_layout:
        vertex_layout = json.loads(args.link_vertex_layout.read_text(encoding="utf-8"))
        if (vertex_layout.get("version") != 1 or vertex_layout.get("stage") != "vert" or
                vertex_layout.get("format") != args.format or not isinstance(vertex_layout.get("varyings"), list)):
            raise ValueError("Incompatible Iris vertex interface sidecar.")
        vertex_outputs = vertex_layout["varyings"]
    if args.pack_archive and not args.cache:
        parser.error("--pack-archive requires --cache")
    if args.auto_pack_bindings:
        supplied = {definition.split("=", 1)[0] for definition in args.define}
        args.define.extend(
            f"MC_RENDER_STAGE_{stage}={value}"
            for value, stage in enumerate(IRIS_RENDER_STAGES)
            if f"MC_RENDER_STAGE_{stage}" not in supplied
        )
    shader_model = "60" if args.format == "dxil" else "51"
    profile_prefix = {"vert": "vs", "frag": "ps", "comp": "cs"}
    profile = f"{profile_prefix[args.stage]}_{shader_model[0]}_{shader_model[1]}"
    glslang = [
        args.glslang, "-S", args.stage,
        *(f"-D{definition}" for definition in args.define),
        *(f"-I{directory.resolve()}" for directory in args.include),
    ]
    output = args.output.resolve()
    metadata = output.with_suffix(output.suffix + ".json")
    cache_key = None
    with tempfile.TemporaryDirectory(prefix="iris-", dir=output.parent) as folder:
        source = args.source.resolve()
        attachment_comments = ""
        if pack_source:
            prepared, attachment_comments = preprocess_pack_source(glslang, source, Path(folder), args.probe_macro)
        if args.preprocess_only:
            preprocessed = Path(folder) / "source.glsl"
            if args.probe_macro:
                active = {
                    args.probe_macro[int(index)]: value.strip()
                    for index, value in re.findall(
                        r"(?m)^[ \t]*cata_iris_property_macro_(\d+)[ \t]*([^\n]*)$", prepared)
                }
                prepared = json.dumps(active)
            preprocessed.write_text(prepared, encoding="utf-8")
            preprocessed.replace(output)
            return
        defaults = {}
        if lower_pack:
            if args.stage == "frag" and args.auto_pack_bindings:
                prepared = disable_unmapped_fragment_outputs(prepared)
            prepared = GROUPED_UNIFORM.sub(
                lambda match: "\n".join(f"uniform {match[1]} {name.strip()};"
                                        for name in match[2].split(",")), prepared)
            defaults = uniform_defaults(prepared)
            if args.auto_pack_bindings:
                args.pack_sampler = list(dict.fromkeys(PACK_SAMPLER.findall(prepared)))
                args.pack_uniform = list(dict.fromkeys(name for _, name, _ in PACK_UNIFORM.findall(prepared)))
            source = Path(folder) / f"lowered.{args.stage}"
            lowered = lower_pack_source(prepared, args.stage, args.pack_sampler,
                                        args.pack_uniform, args.pack_attribute,
                                        args.native_water_vertices, args.fullscreen_vertices, args.native_terrain_vertices,
                                        args.native_shadow_vertices)
            if args.cache:
                cache_key = pack_cache_key(args, lowered + json.dumps([defaults, vertex_outputs, attachment_comments], sort_keys=True),
                                          profile)
                if verified_cache_hit(output, metadata, cache_key, args.stage, args.format):
                    print(f"Iris cache hit: {output.name}")
                    return
            source.write_text(lowered, encoding="utf-8")
            glslang = [args.glslang, "-S", args.stage, "--auto-map-locations"]
        spirv = Path(folder) / "source.spv"
        hlsl = Path(folder) / "source.hlsl"
        binary = Path(folder) / f"source.{args.format}"
        def compile_spirv(output_path, options=()):
            subprocess.run([
                *glslang, *options, "-V", "--target-env", "vulkan1.1",
                "-o", str(output_path), str(source),
            ], check=True)

        def reflect_spirv(path):
            return json.loads(subprocess.check_output(
                [args.spirv_cross, str(path), "--reflect"], encoding="utf-8",
            ))

        compile_spirv(spirv)
        if args.reflect:
            reflection = reflect_spirv(spirv)
            sampler_limit = 16
            if args.auto_pack_bindings and len(reflection.get("textures", [])) > sampler_limit:
                optimized = Path(folder) / "optimized.spv"
                compile_spirv(optimized, ("-Os",))
                candidate = reflect_spirv(optimized)
                textures = candidate.get("textures", [])
                if len(textures) <= sampler_limit:
                    compact_spirv_samplers(optimized, textures, 0 if args.stage == "vert" else 2)
                    optimized.replace(spirv)
                    reflection = candidate
            if vertex_outputs is not None:
                link_fragment_varyings(spirv, reflection, prepared, vertex_outputs)
        subprocess.run([args.spirv_val, "--target-env", "vulkan1.1", str(spirv)], check=True)
        if args.reflect:
            attributes = args.pack_attribute
            if args.native_terrain_vertices:
                attributes = list(NATIVE_TERRAIN_INPUTS)
            elif args.native_water_vertices or args.native_shadow_vertices:
                attributes = list(NATIVE_WATER_INPUTS)
            layout = reflect_pack_layout(reflection, args.stage, attributes)
            if args.stage == "vert":
                layout["varyings"] = varying_interface(reflection, "out", prepared)
            if layout["frame"]:
                for member in layout["frame"]["members"]:
                    if member["name"] in defaults:
                        member["default"] = defaults[member["name"]]
            resolutions = set(re.findall(r"\bconst\s+int\s+shadowMapResolution\s*=\s*(\d+)\s*;",
                                         prepared))
            if len(resolutions) > 1:
                raise ValueError("Conflicting shadowMapResolution constants.")
            if resolutions:
                layout["shadow_resolution"] = int(resolutions.pop())
            resolutions = set(re.findall(r"\bconst\s+int\s+noiseTextureResolution\s*=\s*(\d+)\s*;",
                                         prepared))
            if len(resolutions) > 1:
                raise ValueError("Conflicting noiseTextureResolution constants.")
            if resolutions:
                layout["noise_resolution"] = int(resolutions.pop())
            for name, key in (("wetnessHalflife", "wetness_half_life"),
                              ("drynessHalflife", "dryness_half_life")):
                matches = re.findall(rf"\bconst\s+float\s+{name}\s*=\s*([^;]+);", prepared)
                amounts = set()
                for match in matches:
                    value = float(re.sub(r"^([-+])\s+", r"\1", match.strip()).rstrip("fF"))
                    if not math.isfinite(value) or not 0 <= value <= 3.402823466e38:
                        raise ValueError(f"Invalid {name}.")
                    amounts.add(value)
                if len(amounts) > 1:
                    raise ValueError(f"Conflicting {name} constants.")
                if amounts:
                    layout[key] = amounts.pop()
            if args.stage == "frag":
                targets = [match.group() for match in GLSL_TEXT.finditer(prepared)
                           if TARGET_DIRECTIVE.match(match.group())]
                layout["target_directive"] = targets[-1] if targets else ""
                attachments = prepared + "\n" + attachment_comments
                layout["buffer_formats"] = [{"image": image, "format": value.strip()}
                                            for image, value in BUFFER_FORMAT.findall(attachments)]
                layout["buffer_clears"] = [{"image": image, "enabled": value == "true"}
                                           for image, value in BUFFER_CLEAR.findall(attachments)]
                clear_colors = []
                for image, expr in BUFFER_CLEAR_COLOR.findall(attachments):
                    match = re.fullmatch(r"vec4\s*\(([^()]*)\)", expr.strip())
                    if not match:
                        raise ValueError(f"Unsupported Iris clear color expression for {image}.")
                    color = [float(re.sub(r"^([-+])\s+", r"\1", component.strip()).rstrip("fF")) for component in match[1].split(",")]
                    if len(color) == 1:
                        color *= 4
                    if len(color) != 4 or not all(math.isfinite(value) for value in color):
                        raise ValueError(f"Invalid Iris clear color for {image}.")
                    clear_colors.append({"image": image, "color": color})
                layout["buffer_clear_colors"] = clear_colors
        subprocess.run([
            args.spirv_cross, str(spirv), "--hlsl", "--shader-model", shader_model,
            "--output", str(hlsl),
        ], check=True)
        if lower_pack:
            hlsl_source = hlsl.read_text(encoding="utf-8")
            # SPIRV-Cross represents each GLSL matrix column as an HLSL row.
            # Match the reserved locations for matrices in the stage signature.
            normalized = HLSL_VARYING_MATRIX.sub(r"\1row_major \2", hlsl_source)
            if args.reflect and args.auto_pack_bindings and args.stage == "frag":
                normalized = lower_readonly_textures(normalized, layout, sampler_limit)
            if re.search(r"\bnew\b", normalized):
                name = "cata_iris_hlsl_new"
                identifiers = set(re.findall(r"\b[a-zA-Z_]\w*\b", normalized))
                while name in identifiers:
                    name += "_"
                normalized = re.sub(r"\bnew\b", name, normalized)
            if normalized != hlsl_source:
                hlsl.write_text(normalized, encoding="utf-8")
        subprocess.run([
            args.dxc if args.format == "dxil" else args.fxc, "/nologo", "/T", profile, "/E", "main",
            "/Fo", str(binary), str(hlsl),
        ], check=True)
        if args.reflect:
            layout["format"] = args.format
            layout["bytecode_sha256"] = file_sha256(binary)
            if cache_key is not None:
                layout["cache_key"] = cache_key
            sidecar = Path(folder) / "layout.json"
            sidecar.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")
            sidecar.replace(metadata)
        binary.replace(output)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(f"{Path(error.cmd[0]).name} failed with exit code {error.returncode}.")
    except (OSError, ValueError) as error:
        sys.exit(str(error))
