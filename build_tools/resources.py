from dataclasses import dataclass, field, fields
from enum import Enum
from os import path
import pickle
from typing import Any, Literal
from dacite import from_dict
import dacite
import yaml
from cache import CacheCtx, cached
from public import PathCtx


@dataclass
class Resource:
    file: str


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


@cached("get_file_resources")
def get_file_resources(cache_dep_files: list[str] = []) -> Resources:
    resources_dict = Resources()

    # all resource files
    resource_files = []
    # 记录要递归处理的目录的绝对路径
    dir_stack = [PathCtx.root_dir]
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
                    members = fields(Type)
                    auto_match_name = None
                    if len(members) == 2:
                        auto_match_name = next(
                            filter(
                                lambda x: x.name != fields(Resource)[0].name, members
                            )
                        ).name

                    def add_resource(args_dict: dict):
                        if "auto_match" in args_dict:
                            value = args_dict.pop("auto_match")
                            args_dict[auto_match_name] = value
                        resource = from_dict(
                            Type, args_dict, dacite.Config(strict=True)
                        )
                        assert isinstance(resource, Resource)
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
                            if isinstance(resource_, str):
                                add_resource({"file": resource_})
                            elif isinstance(resource_, dict):
                                if len(resource_) == 1:
                                    for key in resource_.keys():
                                        file = key
                                    assert isinstance(file, str)  # type: ignore
                                    if isinstance(resource_[file], dict):
                                        add_resource({"file": file, **resource_[file]})
                                    else:
                                        add_resource(
                                            {
                                                "file": file,
                                                "auto_match": resource_[file],
                                            }
                                        )
                                else:
                                    add_resource(resource_)
                            else:
                                raise RuntimeError(f"invalid resource: {resource_}")
                    elif isinstance(resources, dict):
                        for resource_name, resource_info in resources.items():
                            assert isinstance(resource_info, dict)
                            add_resource({"file": resource_name, **resource_info})
                    else:
                        raise RuntimeError(f"invalid resources: {resources}")
                except Exception as e:
                    raise RuntimeError(
                        e,
                        f"when handling {resource_file}, resource_type {resource_type}",
                    )
    cache_dep_files.extend(resource_files)

    return resources_dict


@cached("save_resources")
def save_resources(resources: Resources):
    pass


def load_resources() -> Resources:
    return pickle.load(open(CacheCtx("save_resources").param_file(), "rb"))["resources"]
