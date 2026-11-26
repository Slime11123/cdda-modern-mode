"""Preprocess GLSL or compile explicit Vulkan layouts into SDL3 D3D12 bytecode."""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path


GLSL_TEXT = re.compile(
    r'"(?:\\.|[^"\\])*"|//(?:\\\n|[^\n])*|/\*.*?\*/|^[ \t]*#[ \t]*include\b',
    re.DOTALL | re.MULTILINE,
)
TARGET_DIRECTIVE = re.compile(r"/\*\s*(?:RENDERTARGETS|DRAWBUFFERS)\s*:")
TARGET_MARKER = "cata_iris_internal_target_"
PACK_SAMPLER = re.compile(
    r"^[ \t]*uniform[ \t]+[iu]?sampler2D[ \t]+([a-zA-Z_][a-zA-Z_0-9]*)[ \t]*;",
    re.MULTILINE,
)
PACK_UNIFORM = re.compile(
    r"^[ \t]*uniform[ \t]+((?:float|int|uint|[iu]?vec[234]|mat[234]))"
    r"[ \t]+([a-zA-Z_][a-zA-Z_0-9]*)[ \t]*;",
    re.MULTILINE,
)
PACK_ATTRIBUTE = re.compile(
    r"^[ \t]*(?:attribute|in)[ \t]+((?:float|int|uint|[iu]?vec[234]))"
    r"[ \t]+([a-zA-Z_][a-zA-Z_0-9]*)[ \t]*;",
    re.MULTILINE,
)
LEGACY_VERTEX_ATTRIBUTES = {
    "gl_Vertex": "vec4", "gl_Normal": "vec3", "gl_Color": "vec4",
    "gl_MultiTexCoord0": "vec4", "gl_MultiTexCoord1": "vec4",
}


def preprocess_pack_source(glslang, source, temp_dir):
    """Keep attachment metadata in the same macro branches as the shader."""
    text = source.read_text(encoding="utf-8-sig")
    if TARGET_MARKER in text:
        raise ValueError("Shader source contains the reserved Iris render target marker.")
    comments = {}

    def mark(match):
        comment = match.group()
        if comment.lstrip().startswith("#"):
            raise ValueError("Expand shader pack includes before preprocessing attachment metadata.")
        if not TARGET_DIRECTIVE.match(comment):
            return comment
        marker = f"{TARGET_MARKER}{len(comments)}_"
        comments[marker] = comment.replace("\n", " ")
        return f" {marker} " + "\n" * comment.count("\n")

    marked = temp_dir / "metadata.frag"
    marked.write_text(GLSL_TEXT.sub(mark, text), encoding="utf-8")
    result = subprocess.run(
        [*glslang, "-E", str(marked)], cwd=source.parent,
        check=True, stdout=subprocess.PIPE, encoding="utf-8",
    ).stdout
    return re.sub(rf"\b{TARGET_MARKER}[0-9]+_\b",
                  lambda match: comments.get(match.group(), match.group()), result)


def lower_pack_source(source, stage, samplers, uniforms, attributes):
    """Assign SDL3 registers and convert the supported compatibility vertex inputs."""
    prefix = []
    if stage == "vert":
        # Atlas coordinates are supplied directly by the draw, in texture space.
        source = re.sub(r"\bgl_TextureMatrix\s*\[\s*0\s*\]", "mat4(1.0)", source)
        for old, new in (("gl_ModelViewMatrix", "gbufferModelView"),
                         ("gl_ProjectionMatrix", "gbufferProjection"),
                         ("gl_NormalMatrix", "transpose(mat3(gbufferModelViewInverse))")):
            source = re.sub(rf"\b{old}\b", new, source)
        inputs = {name: index for index, name in enumerate(attributes)}
        if len(inputs) != len(attributes):
            raise ValueError("A shader pack vertex attribute appears more than once in the binding list.")
        declared_inputs = set()

        def vertex_input(kind, name):
            if name not in inputs:
                raise ValueError(f"Shader pack vertex attribute {name} has no SDL3 input location.")
            declared_inputs.add(name)
            lowered = f"cata_iris_{name[3:]}" if name in LEGACY_VERTEX_ATTRIBUTES else name
            return f"layout(location = {inputs[name]}) in {kind} {lowered};"

        source = PACK_ATTRIBUTE.sub(lambda match: vertex_input(*match.groups()), source)
        for name, kind in LEGACY_VERTEX_ATTRIBUTES.items():
            if re.search(rf"\b{name}\b", source):
                prefix.append(vertex_input(kind, name))
                source = re.sub(rf"\b{name}\b", f"cata_iris_{name[3:]}", source)
        if missing := inputs.keys() - declared_inputs:
            raise ValueError(f"Missing vertex attributes: {', '.join(sorted(missing))}.")
        source, entries = re.subn(r"\bvoid\s+main\s*\(", "void cata_iris_main(", source)
        if entries != 1:
            raise ValueError("Expected one shader pack vertex entry point.")
        source += "\nvoid main() {\n    cata_iris_main();\n    gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);\n}\n"

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
        raise ValueError(f"Missing standalone sampler2D declaration: {', '.join(sorted(missing))}.")
    fields = {}

    def collect_uniform(match):
        kind, name = match.groups()
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--stage", choices=("vert", "frag", "comp"), required=True)
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--include", type=Path, action="append", default=[])
    parser.add_argument("--preprocess-only", action="store_true",
                        help="preprocess include-expanded graphics GLSL and keep attachment directives")
    parser.add_argument("--pack-sampler", action="append", default=[], metavar="NAME",
                        help="lower a pack stage with these sampled inputs in binding order")
    parser.add_argument("--pack-uniform", action="append", default=[], metavar="NAME",
                        help="lower a pack stage with these fields in its std140 frame block")
    parser.add_argument("--pack-attribute", action="append", default=[], metavar="NAME",
                        help="lower a pack vertex stage with these input locations in order")
    parser.add_argument("--glslang", default="glslangValidator")
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--fxc", default="fxc")
    args = parser.parse_args()
    lower_pack = args.pack_sampler or args.pack_uniform or args.pack_attribute
    pack_source = args.preprocess_only or lower_pack
    if pack_source and (args.stage == "comp" or args.include):
        parser.error("Pack preprocessing requires a graphics stage and include-expanded source")
    if args.pack_attribute and args.stage != "vert":
        parser.error("--pack-attribute requires --stage vert")
    if args.preprocess_only and lower_pack:
        parser.error("--preprocess-only cannot be combined with pack bindings")
    profiles = {"vert": "vs_5_1", "frag": "ps_5_1", "comp": "cs_5_1"}
    glslang = [
        args.glslang, "-S", args.stage,
        *(f"-D{definition}" for definition in args.define),
        *(f"-I{directory.resolve()}" for directory in args.include),
    ]
    output = args.output.resolve()
    with tempfile.TemporaryDirectory(prefix="iris-", dir=output.parent) as folder:
        source = args.source.resolve()
        if pack_source:
            prepared = preprocess_pack_source(glslang, source, Path(folder))
        if args.preprocess_only:
            preprocessed = Path(folder) / "source.glsl"
            preprocessed.write_text(prepared, encoding="utf-8")
            preprocessed.replace(output)
            return
        if lower_pack:
            source = Path(folder) / f"lowered.{args.stage}"
            source.write_text(lower_pack_source(prepared, args.stage, args.pack_sampler,
                                                args.pack_uniform, args.pack_attribute),
                              encoding="utf-8")
            glslang = [args.glslang, "-S", args.stage, "--auto-map-locations"]
        spirv = Path(folder) / "source.spv"
        hlsl = Path(folder) / "source.hlsl"
        binary = Path(folder) / "source.dxbc"
        subprocess.run([
            *glslang, "-V", "--target-env", "vulkan1.1",
            "-o", str(spirv), str(source),
        ], check=True)
        subprocess.run([args.spirv_val, "--target-env", "vulkan1.1", str(spirv)], check=True)
        subprocess.run([
            args.spirv_cross, str(spirv), "--hlsl", "--shader-model", "51",
            "--output", str(hlsl),
        ], check=True)
        subprocess.run([
            args.fxc, "/nologo", "/T", profiles[args.stage], "/E", "main",
            "/Fo", str(binary), str(hlsl),
        ], check=True)
        binary.replace(output)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(f"{Path(error.cmd[0]).name} failed with exit code {error.returncode}.")
    except (OSError, ValueError) as error:
        sys.exit(str(error))
