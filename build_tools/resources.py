from dataclasses import asdict, dataclass, field, fields
from os import path
from pathlib import Path
import pickle
from typing import Any, Callable, ClassVar, Type, get_args
from dacite import from_dict
import dacite
import yaml
from cache import CacheCtx, cached
from public import Root


@dataclass
class Resource:
    file: str

    __auto_match_name: ClassVar[str | None] = None

    @classmethod
    def get_auto_match_name(cls):
        if cls.__auto_match_name is not None:
            return cls.__auto_match_name
        members = [x for x in fields(cls)]
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
            if len(args) == 1:
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

    def serialize(self):
        value = asdict(self)
        value.pop("file")
        remain_member_size = len(value)
        value = {k: v for k, v in value.items() if v is not None}
        if remain_member_size == 0:
            return self.file
        elif remain_member_size == 1:
            return {self.file: list(value.values())[0]}
        else:
            for k in list(value.keys()):
                if value[k] is None:
                    value.pop(k)
            return {self.file: value}


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
class Target(Resource):
    name: str


@dataclass
class Source(Resource):
    # the target cc file abspath list (if None, applied to all cc files)
    targets: list[str] | None = None
    macros: list[tuple[str, str]] = field(default_factory=list)

    def needed_by(self, target: str | Target) -> bool:
        if self.targets is None:
            return True
        return [path.normpath(x) for x in self.targets].count(
            path.normpath(target if isinstance(target, str) else target.file)
        ) > 0


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
    def get_id(self):
        return "_".join(Path(path.splitext(Root.relpath(self.file))[0]).parts)


str2resource: dict[str, Any] = {
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

resource2str = {v: k for k, v in str2resource.items()}


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

    def get_by_type(self, type: Type[Resource]) -> list[Any]:
        field_infos = fields(Resources)
        for field in field_infos:
            if list[type] == field.type:
                return getattr(self, field.name)
        assert False

    def get_by_str(self, resource_name: str) -> list[Any]:
        type = str2resource[resource_name]
        return self.get_by_type(type)

    def __str__(self) -> str:
        return str(self.__dict__)

    def get_all_resources(self) -> dict[Type[Resource], list[Resource]]:
        field_infos = fields(Resources)
        field_dict = {
            get_args(field.type)[0]: getattr(self, field.name) for field in field_infos
        }
        return field_dict

    def merge(self, other: "Resources"):
        other_dict = other.get_all_resources()
        for k, v in self.get_all_resources().items():
            v.extend(other_dict[k])

    @staticmethod
    def from_yaml(content: str) -> "Resources":
        config_content = yaml.safe_load(content)
        ret = Resources()
        if config_content is None:
            return ret
        assert isinstance(config_content, dict)
        for typestr, resources in config_content.items():
            Type = str2resource[typestr]
            member = ret.get_by_str(typestr)
            if isinstance(resources, list):
                member.extend([Type.create(x) for x in resources])
            elif isinstance(resources, dict):
                member.extend(Type.create({k: v}) for k, v in resources.items())
            else:
                raise RuntimeError(f"invalid resources: {resources}")
        return ret

    @staticmethod
    def from_file(file: str) -> "Resources":
        if not path.exists(file):
            return Resources()
        with open(file, "rt") as f:
            return Resources.from_yaml(f.read())

    def to_yaml(self) -> str:
        obj_dict = {
            resource2str[k]: [x.serialize() for x in v]
            for k, v in self.get_all_resources().items()
            if len(v) != 0
        }
        if len(obj_dict) == 0:
            return ""
        return yaml.dump(obj_dict)

    def to_file(self, file: str):
        with open(file, "wt") as f:
            f.write(self.to_yaml())


def get_config_file(dir: str) -> str:
    return path.join(dir, "resource.yml")


@cached
def get_file_resources(cache_dep_files: list[str] = []) -> Resources:
    resources_dict = Resources()

    # all resource files
    resource_files = []
    # 记录要递归处理的目录的绝对路径
    dir_stack = [Root.dir]
    while len(dir_stack) != 0:
        current_dir = dir_stack.pop()
        resource_file = get_config_file(current_dir)
        resource_files.append(resource_file)
        try:
            resources = Resources.from_file(resource_file)
            for r in [r for rs in resources.get_all_resources().values() for r in rs]:
                r.file = path.abspath(path.join(current_dir, r.file))
                if not path.exists(r.file):
                    raise RuntimeError(f"resource file not exists: {r.file}")
            for r in resources.sub_dirs:
                dir_stack.append(r.file)
            resources_dict.merge(resources)
        except Exception as e:
            e.add_note(f"when handling {resource_file}")
            raise
    cache_dep_files.extend(resource_files)
    return resources_dict


@cached
def save_resources(resources: Resources):
    pass


def load_resources() -> Resources:
    return pickle.load(open(CacheCtx(save_resources.__wrapped__).param_file(), "rb"))["resources"]  # type: ignore


def touch_config_file(dir: str) -> str:
    config_file = get_config_file(dir)
    ret = config_file
    if path.exists(config_file):
        return ret
    open(config_file, "w").close()
    if path.normpath(dir) == path.normpath(Root.dir):
        return ret
    dir, subdir = path.dirname(dir), path.basename(dir)
    touch_config_file(dir)
    config_file = get_config_file(dir)
    resources = Resources.from_file(config_file)
    resources.sub_dirs.append(SubDir(subdir))
    resources.to_file(config_file)
    return ret
