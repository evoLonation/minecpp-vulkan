from os import path
import os
import re
import subprocess as sp
import argparse
import json
import tempfile
from public import Compiler, DepCtx, Root, NinjaCtx

parser = argparse.ArgumentParser()
parser.add_argument(
    "-c", type=str, dest="source", required=True, help="the abspath of source"
)
parser.add_argument(
    "--provide",
    dest="provide",
    type=str,
    help="the provided module name",
    required=False,
)
parser.add_argument(
    "--implement",
    dest="implement",
    type=str,
    help="the implemented module name",
    required=False,
)
parser.add_argument("--includes", type=str, dest="includes", nargs="+", required=True)
parser.add_argument("--root_dir", type=str, dest="root_dir", required=True)
args = parser.parse_args()
if args.provide and args.implement:
    raise RuntimeError("provide and implement cannot be specified at the same time")
Root.set_dir(args.root_dir)

is_module = bool(args.provide) or bool(args.implement)

with open(args.source, "rt", encoding="utf-8") as f:
    source = f.read()
pattern1 = re.compile(r"import\s+<([^>]+)>;")
# pattern2 = re.compile(r'import\s+"([^"]+)";')
includes = []
for match in pattern1.findall(source):
    includes.append(match)
source = pattern1.sub("", source)
# source = pattern2.sub("", source)
source = (
    ("module;\n" if is_module else "")
    + "\n".join([f"#include<{x}>" for x in includes])
    + "\n\n"
    + source
)

with tempfile.NamedTemporaryFile(
    delete=True,
    mode="w+",
    encoding="utf-8",
    dir=path.dirname(args.source),
    suffix="." + path.basename(args.source).split(".")[-1],
) as temp_file:
    temp_file_path = temp_file.name
    temp_file.write(source)
    temp_file.flush()
    # print(f"temp file path: {temp_file_path}")
    # print(f"content: \n{source}")
    command = Compiler.compile(
        args.includes, None, temp_file_path, Compiler.obj_file(args.source)
    )
    try:
        result = sp.run(
            f"clang-scan-deps -format=p1689 -- {command}", stdout=sp.PIPE, check=True
        )
    except Exception as e:
        raise RuntimeError(e, f"the source file is {args.source}")

rule = json.loads(result.stdout)["rules"][0]
provide = None
if "provides" in rule:
    provide = rule["provides"][0]["logical-name"]

if provide != args.provide:
    raise RuntimeError(
        f'The provided module searched is not match with config ("{provide}" vs "{args.provide}"'
    )
implement = None
if args.implement:
    match = re.search(rf"module\s+{args.implement}\s*;", source)
    # print("match", match)
    if not match:
        raise Exception(f"The file {source} does not contain a module declaration")
    implement = args.implement

required_modules = list(map(lambda x: x["logical-name"], rule.get("requires", [])))

with open(DepCtx.module_dep_file(args.source), "wt") as f:
    f.write("\n".join(required_modules))

# write header config file
with open(DepCtx.header_dep_config_file(args.source), "wt") as f:
    f.write(
        "\n".join(
            [
                "-fmodule-file=" + Compiler.header_pcm_file(x).replace("\\", "\\\\")
                for x in includes
            ]
        )
    )


with NinjaCtx.open_ninja(DepCtx.dyndep_file(args.source)) as ninja_writer:
    ninja_writer.variable("ninja_dyndep_version", "1")

    def build_dyndep(output):
        ninja_writer.build(
            outputs=output,
            rule="dyndep",
            implicit=[Compiler.pcm_file(module) for module in required_modules]
            + [Compiler.header_pcm_file(include) for include in includes],
        )

    build_dyndep(Compiler.obj_file(args.source))
    if provide != None:
        build_dyndep(Compiler.pcm_file(provide))
