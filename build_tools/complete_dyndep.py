# generate dyndep, output is a module A phony, inputs are all module phony whose dependent on A
import argparse

from public import CompleteDepNinja, Root, open_ninja

parser = argparse.ArgumentParser()
parser.add_argument("root_dir", type=str)
parser.add_argument("phony", type=str, help="the phony target name")
parser.add_argument(
    "--module", type=str, help="the module name if resource is a module", required=False
)
parser.add_argument("inputs", type=str, nargs="+", help="all module dep file path")
parser.add_argument("output", type=str, help="the dyndep file path")
args = parser.parse_args()
Root.set_dir(args.root_dir)

dep_modules = []
for input in args.inputs:
    with open(input, "rt") as f:
        for line in f:
            dep_modules.append(line.strip())
if args.module != None:
    dep_modules = set(dep_modules) - set([args.module])
with open_ninja(args.output) as writer:
    writer.variable("ninja_dyndep_version", "1")
    writer.build(
        outputs=args.phony,
        rule="dyndep",
        implicit=[CompleteDepNinja.Phony.module(module) for module in dep_modules],
    )
