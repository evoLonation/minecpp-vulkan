from dataclasses import dataclass, field, fields
from os import path
import pickle
from typing import Any
from dacite import from_dict
import dacite
import yaml
from cache import CacheCtx, cached
from public import Root


@dataclass
class Resource:
    file: str

    __auto_match_name: str | None = field(init=False, default=None)

    @classmethod
    def get_auto_match_name(cls):
        if cls.__auto_match_name is not None:
            return cls.__auto_match_name
        members = [x for x in fields(cls) if x.init]
        if len(members) == 2:
            cls.__auto_match_name = next(
                filter(lambda x: x.name != fields(Resource)[0].name, members)
            ).name
        else:
            cls.__auto_match_name = ""
        return cls.__auto_match_name

    @classmethod
    def create(cls, args):
        if isinstance(args, dict):
            if len(args.items()) == 1:
                k, v = list(args.items())[0]
                if cls.get_auto_match_name() != "":
                    args = {"file": k, cls.get_auto_match_name(): v}
                else:
                    args = {"file": k, **v}
        elif isinstance(args, str):
            args = {"file": args}
        else:
            raise RuntimeError(f"unknown args type {type(args)}")
        return from_dict(cls, args, dacite.Config(strict=True))


@dataclass
class Module(Resource):
    provide: str | None = None
    implement: str | None = None

    def __post_init__(self):
        if bool(self.provide) == bool(self.implement):
            raise RuntimeError(f"module {self.file} must provide or implement a module")


@dataclass
class SubDir(Resource):
    pass


@dataclass
class Source(Resource):
    pass


@dataclass
class IncludeDir(Resource):
    pass


@dataclass
class HeaderUnit(Resource):
    pass


@dataclass
class LibFile(Resource):
    pass


@dataclass
class DylibFile(Resource):
    pass


@dataclass
class Shader(Resource):
    pass


@dataclass
class Test(Resource):
    pass


@dataclass
class Target(Resource):
    name: str


resource_name_map: dict[str, Any] = {
    "module": Module,
    "sub_dir": SubDir,
    "source": Source,
    "include_dir": IncludeDir,
    "header_unit": HeaderUnit,
    "lib": LibFile,
    "dylib": DylibFile,
    "shader": Shader,
    "test": Test,
    "target": Target,
}


@dataclass
class Resources:
    modules: list[Module] = field(default_factory=list)
    sources: list[Source] = field(default_factory=list)
    sub_dirs: list[SubDir] = field(default_factory=list)
    include_dirs: list[IncludeDir] = field(default_factory=list)
    header_units: list[HeaderUnit] = field(default_factory=list)
    lib_files: list[LibFile] = field(default_factory=list)
    dylib_files: list[DylibFile] = field(default_factory=list)
    shaders: list[Shader] = field(default_factory=list)
    tests: list[Test] = field(default_factory=list)
    targets: list[Target] = field(default_factory=list)

    def get_by_str(self, resource_name: str) -> list[Any]:
        field_infos = fields(Resources)
        type = resource_name_map[resource_name]
        for field in field_infos:
            if list[type] == field.type:
                return getattr(self, field.name)
        assert False

    def __str__(self) -> str:
        return str(self.__dict__)


@cached
def get_file_resources(cache_dep_files: list[str] = []) -> Resources:
    resources_dict = Resources()

    # all resource files
    resource_files = []
    # 记录要递归处理的目录的绝对路径
    dir_stack = [Root.dir]
    while len(dir_stack) != 0:
        current_dir = dir_stack.pop()
        resource_file = path.join(current_dir, "resource.yml")
        resource_files.append(resource_file)
        with open(resource_file, "rt") as f:
            config_content: dict = yaml.safe_load(f)
            if config_content is None:
                continue
            assert isinstance(config_content, dict)
            for resource_type, resources in config_content.items():
                try:
                    Type = resource_name_map[resource_type]

                    def add_resource(resource: Resource):
                        resource.file = path.abspath(
                            path.join(current_dir, resource.file)
                        )
                        if not path.exists(resource.file):
                            raise RuntimeError(
                                f"resource file not exists: {resource.file}"
                            )
                        resources_dict.get_by_str(resource_type).append(resource)
                        if Type == SubDir:
                            dir_stack.append(path.join(current_dir, resource.file))

                    if isinstance(resources, list):
                        for resource_ in resources:
                            add_resource(Type.create(resource_))
                    elif isinstance(resources, dict):
                        for k, v in resources.items():
                            add_resource(Type.create({k: v}))
                    else:
                        raise RuntimeError(f"invalid resources: {resources}")
                except Exception as e:
                    e.add_note(
                        f"when handling {resource_file}, resource_type {resource_type}"
                    )
                    raise
    cache_dep_files.extend(resource_files)
    return resources_dict


@cached
def save_resources(resources: Resources):
    pass


def load_resources() -> Resources:
    return pickle.load(open(CacheCtx(save_resources.__wrapped__).param_file(), "rb"))["resources"]  # type: ignore
