module;
#include <toy.h>
module engine.package;

namespace eg {

/**
 *
 * LinkedFile implements
 *
 */

LinkedFile::LinkedFile(fs::path const& path) {
  bool new_file = false;
  if (!fs::exists(path)) {
    if (path.has_parent_path()) {
      fs::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    toy::throwf(file.is_open(), "Failed to create file: {}", path.generic_string());
    new_file = true;
  }
  _file.open(path, std::ios::in | std::ios::out | std::ios::binary | std::ios::ate);
  _stream = FileStream(&_file);
  toy::throwf(_file.is_open(), "Failed to open file: {}", path.generic_string());
  auto file_size = _file.tellg();
  _file.seekg(0);
  if (!new_file) {
    TOY_ASSERT(readMagicNumber() == _magic_number && file_size % _page_size == 0);
  } else {
    TOY_ASSERT(file_size == 0);
    appendPage();
    writeNextPage(0, 0);
    writeMagicNumber(_magic_number);
    writeAllocatedPages({});
  }
}

void LinkedFile::readBytes(std::span<std::byte> b) {
  _read_size += b.size();
  auto iter = b.begin();
  while (iter != b.end()) {
    auto read_size = std::min(size_t(b.end() - iter), getPageRemainSize());
    // toy::debugf("read size: {}, current ptr: {}", read_size, (size_t)_file.tellg());
    _file.read(reinterpret_cast<char*>(iter.base()), read_size);
    iter += read_size;
    if (getPageRemainSize() == _page_size) {
      auto next_page = readNextPage(getNowPage() - 1);
      TOY_ASSERT(next_page != 0);
      seekPage(next_page);
    }
  }
}

void LinkedFile::writeBytes(std::span<std::byte const> b) {
  _write_size += b.size();
  auto iter = b.begin();
  while (iter != b.end()) {
    auto write_size = std::min(size_t(b.end() - iter), getPageRemainSize());
    // toy::debugf("write size: {}, current ptr: {}", write_size, (size_t)_file.tellg());
    _file.write(reinterpret_cast<char const*>(iter.base()), write_size);
    iter += write_size;
    if (getPageRemainSize() == _page_size) {
      auto now_page = getNowPage() - 1;
      auto next_page = readNextPage(now_page);
      if (next_page == 0) {
        next_page = allocatePage();
        writeNextPage(now_page, next_page);
      }
      seekPage(next_page);
    }
  }
}

void LinkedFile::skip(size_t size, bool enable_append) {
  _write_size += size;
  _read_size += size;
  auto page_remain_size = getPageRemainSize();
  auto remain_size = size;
  while (remain_size != 0) {
    TOY_ASSERT(page_remain_size > 0);
    if (page_remain_size > remain_size) {
      _file.seekg(remain_size, std::ios::cur);
      return;
    } else {
      remain_size -= page_remain_size;
      auto now_page = getNowPage();
      auto next_page = readNextPage(now_page);
      if (next_page == 0) {
        toy::throwf(enable_append, "skip out of range");
        next_page = allocatePage();
        writeNextPage(now_page, next_page);
      }
      seekPage(next_page);
      page_remain_size = getPageRemainSize();
    }
  }
}

/**
 * @brief call after all write done to release unused tail pages
 */
void LinkedFile::releaseTail() {
  // first_page: first page which can be released
  auto first_page = getNowPage();
  if (getCurrentPageSize() != 0) {
    first_page = readNextPage(first_page);
    if (first_page == 0) {
      return;
    }
  }
  auto last_page = toLastPage(first_page);
  writeNextPage(last_page, readFreePage());
  writeFreePage(first_page);
}

void LinkedFile::setCurrent(PageNumber id) {
  // toy::debugf("set current page: {}", id);
  _read_size = 0;
  _write_size = 0;
  seekPage(id);
}

auto LinkedFile::allocate() -> PageNumber {
  auto new_page = allocatePage();
  auto allocated_pages = readAllocatedPages();
  allocated_pages.push_back(new_page);
  writeAllocatedPages(allocated_pages);
  return new_page;
}

void LinkedFile::release(PageNumber id) {
  // todo: when free page is too much, how to reduce file size
  auto allocated_pages = readAllocatedPages();
  auto iter = std::find(allocated_pages.begin(), allocated_pages.end(), id);
  TOY_ASSERT(iter != allocated_pages.end());
  allocated_pages.erase(iter);
  writeAllocatedPages(allocated_pages);
  auto first_page = id;
  auto last_page = toLastPage(first_page);
  writeNextPage(last_page, readFreePage());
  writeFreePage(first_page);
}

auto LinkedFile::toLastPage(PageNumber page) -> PageNumber {
  // todo: low performance
  while (auto next_page = readNextPage(page)) {
    page = next_page;
  }
  return page;
}

/**
 * @brief allocate a new page and set next_page to 0
 */
auto LinkedFile::allocatePage() -> PageNumber {
  auto recover = snapshot();
  auto free_page = readFreePage();
  if (free_page == 0) {
    // allocate a new page
    auto new_page = appendPage();
    writeNextPage(new_page, 0);
    return new_page;
  } else {
    auto new_free_page = readNextPage(free_page);
    writeFreePage(new_free_page);
    writeNextPage(free_page, 0);
    return free_page;
  }
}

auto LinkedFile::appendPage() -> PageNumber {
  _file.seekg(_page_size - 1, std::ios::end);
  // append file length
  _file.put(0);
  TOY_ASSERT(_file.tellg() % _page_size == 0, (size_t)_file.tellg());
  return _file.tellg() / _page_size - 1;
}

auto LinkedFile::readMagicNumber() -> MagicNumber {
  _file.seekg(sizeof(PageNumber));
  return _stream.read<MagicNumber>();
}

void LinkedFile::writeMagicNumber(MagicNumber magic_number) {
  _file.seekg(sizeof(PageNumber));
  _stream.write<MagicNumber>(magic_number);
}

auto LinkedFile::readFreePage() -> PageNumber {
  // next page + magic number
  _file.seekg(sizeof(PageNumber) + sizeof(MagicNumber));
  return _stream.read<PageNumber>();
}

void LinkedFile::writeFreePage(PageNumber free_page) {
  // next page + magic number
  _file.seekg(sizeof(PageNumber) + sizeof(MagicNumber));
  _stream.write<PageNumber>(free_page);
}

void LinkedFile::writeAllocatedPages(std::vector<PageNumber> const& allocated_pages) {
  setCurrent(0);
  // next page + magic number + free page
  skip(sizeof(PageNumber) + sizeof(MagicNumber) + sizeof(PageNumber));
  write<std::vector<PageNumber>>(allocated_pages);
}

auto LinkedFile::readAllocatedPages() -> std::vector<PageNumber> {
  setCurrent(0);
  // next page + magic number + free page
  skip(sizeof(PageNumber) + sizeof(MagicNumber) + sizeof(PageNumber));
  return read<std::vector<PageNumber>>();
}

void LinkedFile::seekPage(PageNumber page) { _file.seekg(page * _page_size + sizeof(PageNumber)); }

// next page tag: 0 means end, other means next page
auto LinkedFile::readNextPage(PageNumber page) -> PageNumber {
  _file.seekg(page * _page_size);
  return _stream.read<PageNumber>();
}

void LinkedFile::writeNextPage(PageNumber page, PageNumber next_page) {
  _file.seekg(page * _page_size);
  _stream.write<PageNumber>(next_page);
}

LinkedFile::PtrRecover::PtrRecover(LinkedFile* file) {
  _file.reset(file);
  _pos = file->_file.tellg();
  _read_size = file->_read_size;
  _write_size = file->_write_size;
}

void LinkedFile::PtrRecover::recover() {
  TOY_ASSERT(_file);
  _file->_file.seekg(_pos);
  _file->_read_size = _read_size;
  _file->_write_size = _write_size;
  _file = nullptr;
}

LinkedFile::PtrRecover::~PtrRecover() {
  if (_file) {
    recover();
  }
}

/**
 * Asset implements
 */

auto Asset::getAssetGuid() -> Guid {
  if (!_guid.valid()) {
    _guid = Guid::generate();
  }
  return _guid;
}
Asset::~Asset() {
  if (_package) {
    Package::beforeDestroyAsset(this);
  }
}
void Asset::setOwnedPackage(Package* package) { Package::setAssetOwner(this, package); }
void Asset::resetOwnedPackage() { Package::resetAssetOwner(this); }
void Asset::setOwnedPackageShared(std::shared_ptr<Asset> shared, Package* package) {
  Package::setAssetOwnerShared(shared, package);
}
void Asset::saveAsset() {
  TOY_ASSERT(_package);
  _package->saveAsset(*this, true);
}
void Asset::saveAsset(Package* package) {
  setOwnedPackage(package);
  saveAsset();
}
void Asset::setAssetName(std::string name) {
  TOY_ASSERT(!name.empty());
  if (_name != name) {
    if (_package) {
      TOY_ASSERT(!_package->_meta_info.name2guid.contains(name));
      if (!_name.empty()) {
        _package->_meta_info.name2guid.erase(_name);
        _package->_meta_info.guid2name.erase(_guid);
      }
      _package->_meta_info.name2guid[name] = _guid;
      _package->_meta_info.guid2name[_guid] = name;
      _package->saveGuid2Page();
    }
    _name = std::move(name);
  }
}

void Asset::resetAssetName() {
  if (_package && !_name.empty()) {
    _package->_meta_info.name2guid.erase(_name);
    _package->_meta_info.guid2name.erase(_guid);
    _package->saveGuid2Page();
  }
  _name.clear();
}

/**
 * Package implements
 *
 */

auto Package::get(fs::path const& path) -> std::shared_ptr<Package> {
  if (_packages.contains(path)) {
    return _packages[path]->shared_from_this();
  }
  struct MakeSharedEnabler : public Package {
    MakeSharedEnabler(fs::path const& path) : Package(path) {}
  };
  return std::make_shared<MakeSharedEnabler>(path);
}

void Package::loadAsset(Asset& asset, Guid const& guid) {
  TOY_ASSERT(!asset._guid.valid());
  loadAsset(guid, [&](std::string const& type_name) -> Asset& {
    TOY_ASSERT(type_name == typeid(asset).name(), type_name, typeid(asset).name());
    return asset;
  });
}

void Package::loadAsset(Asset& asset, std::string const& name) {
  TOY_ASSERT(_meta_info.name2guid.contains(name));
  loadAsset(asset, _meta_info.name2guid[name]);
}

Package::Package(fs::path const& path) : _path(path), _file(path) {
  TOY_ASSERT(!_packages.contains(_path));
  _packages[_path] = this;
  if (!_file.getAll().empty()) {
    _file.setCurrent(1);
    _meta_info = _file.read<MetaInfo>();
  } else {
    auto id = _file.allocate();
    TOY_ASSERT(id == 1);
    saveGuid2Page();
  }
}

Package::~Package() { _packages.erase(_path); }

void Package::saveGuid2Page() {
  _file.setCurrent(1);
  _file.write<MetaInfo>(_meta_info);
}

void Package::setAssetOwner(
  Asset* asset, Package* owner, std::optional<std::weak_ptr<Asset>> weak
) {
  if (asset->_package.get() == owner) {
    return;
  }
  if (!asset->_name.empty() && owner->_meta_info.name2guid.contains(asset->_name)) {
    toy::throwf(
      "Asset name {} is already used in package {}", asset->_name, owner->_path.generic_string()
    );
  }
  auto guid = asset->getAssetGuid();
  auto origin = asset->_package;
  asset->_package = owner->shared_from_this();
  auto owner_page = owner->_file.allocate();
  owner->_meta_info.guid2page[guid] = owner_page;
  if (!asset->_name.empty()) {
    owner->_meta_info.name2guid[asset->_name] = guid;
    owner->_meta_info.guid2name[guid] = asset->_name;
  }
  owner->saveGuid2Page();
  if (origin) {
    // move data from old package to new package and release old package
    auto old_data = std::vector<std::byte>{};
    auto origin_page = origin->_meta_info.guid2page.extract(guid).mapped();
    if (!asset->_name.empty()) {
      origin->_meta_info.name2guid.erase(asset->_name);
      origin->_meta_info.guid2name.erase(guid);
    }
    origin->saveGuid2Page();
    origin->_file.setCurrent(origin_page);
    old_data.resize(origin->_file.read<size_t>());
    origin->_file.read(std::span{ old_data });
    origin->_file.release(origin_page);
    owner->_file.setCurrent(owner_page);
    owner->_file.write<size_t>(old_data.size());
    owner->_file.write(std::span{ old_data });
  } else {
    // mark new page has no data
    owner->saveAsset(*asset, false);
    Package::_assets[guid] = std::move(weak);
  }
}

void Package::setAssetOwnerShared(std::shared_ptr<Asset> asset, Package* owner) {
  setAssetOwner(asset.get(), owner, std::weak_ptr{ asset });
}

void Package::resetAssetOwner(Asset* asset) {
  if (auto origin = asset->_package) {
    auto page = origin->_meta_info.guid2page.extract(asset->getAssetGuid()).mapped();
    if (!asset->_name.empty()) {
      origin->_meta_info.name2guid.erase(asset->_name);
      origin->_meta_info.guid2name.erase(asset->getAssetGuid());
    }
    origin->saveGuid2Page();
    Package::_assets.erase(asset->getAssetGuid());
    origin->_file.release(page);
  }
  asset->_package = nullptr;
}

void Package::beforeDestroyAsset(Asset* asset) {
  if (auto origin = asset->_package) {
    Package::_assets.erase(asset->getAssetGuid());
  }
}

void Package::loadAsset(Guid const& guid, std::function<Asset&(std::string const&)> asset_getter) {
  TOY_ASSERT(guid.valid());
  toy::throwf(_meta_info.guid2page.contains(guid), "Asset not found in this package");
  _file.setCurrent(_meta_info.guid2page[guid]);
  _file.skip(sizeof(size_t)); // skip data size
  auto  serialize_data = _file.read<bool>();
  auto  type_name = _file.read<std::string>();
  auto& asset = asset_getter(type_name);
  asset._guid = guid;
  asset._package = this->shared_from_this();
  if (auto iter = _meta_info.guid2name.find(guid); iter != _meta_info.guid2name.end()) {
    asset._name = iter->second;
  }
  if (serialize_data) {
    auto unpacker = AssetUnpacker{ this };
    asset.assetDeserialize(unpacker);
  }
}

auto Package::getAssetSharedImpl(Guid const& guid) -> std::shared_ptr<Asset> {
  if (auto iter = _assets.find(guid); iter != _assets.end()) {
    toy::throwf(bool(iter->second), "Asset is already loaded in memory but not shared ownership");
    return iter->second->lock();
  }
  auto asset = std::shared_ptr<Asset>{};
  loadAsset(guid, [&](std::string const& type_name) -> Asset& {
    asset = getDefaultAssetShared(type_name);
    return *asset.get();
  });
  _assets[guid] = asset;
  return asset;
}

auto Package::getAssetUniqueImpl(Guid const& guid) -> std::unique_ptr<Asset> {
  toy::throwf(!_assets.contains(guid), "Asset is already loaded in memory");
  auto asset = std::unique_ptr<Asset>{};
  loadAsset(guid, [&](std::string const& type_name) -> Asset& {
    asset = getDefaultAssetUnique(type_name);
    return *asset.get();
  });
  _assets[guid] = std::nullopt;
  return asset;
}

void Package::saveAsset(Asset& asset, bool serialize_data) {
  toy::throwf((bool)asset._package, "Asset must have a owned package");
  auto& package = *asset._package.get();
  auto  guid = asset.getAssetGuid();

  auto defer = toy::Defer{};
  // could call setOwnedPackage first(saveAsset with serialize_data=false) then call saveAsset()
  // so we just consider the call that serialize_data = true
  if (serialize_data) {
    if (!_is_inner_save) {
      _is_inner_save = true;
      defer = []() {
        _is_inner_save = false;
        _guids_in_this_save.clear();
      };
    } else if (_guids_in_this_save.contains(guid)) {
      return;
    }
    _guids_in_this_save.insert(guid);
  }

  toy::throwf(package._meta_info.guid2page.contains(guid), "Asset not found in this package");
  package._file.setCurrent(package._meta_info.guid2page[guid]);
  package._file.skip(sizeof(size_t)); // later will write real data size
  package._file.write<bool>(serialize_data);
  package._file.write<std::string>(typeid(asset).name());
  if (serialize_data) {
    auto packager = AssetPackager{ &package };
    asset.assetSerialize(packager);
  }
  package._file.releaseTail();
  auto data_size = package._file.getWriteSize() - sizeof(size_t);
  package._file.setCurrent(package._meta_info.guid2page[guid]);
  package._file.write<size_t>(data_size);
}

/**
 * @brief 下面是 Package::getDefaultAssetShared 和 Package::getDefaultAssetUnique 的具体实现
 */

auto Package::getDefaultAssetShared(std::string const& type_name) -> std::shared_ptr<Asset> {
  return toy::getDefaultObjectShared<Asset>(type_name);
}

auto Package::getDefaultAssetUnique(std::string const& type_name) -> std::unique_ptr<Asset> {
  return toy::getDefaultObjectUnique<Asset>(type_name);
}

void Package::MetaInfo::serialize(toy::OutputStream& io, MetaInfo const& t) {
  io.write(t.guid2page);
  io.write(t.name2guid);
  io.write(t.guid2name);
}

auto Package::MetaInfo::deserialize(toy::InputStream& io) -> MetaInfo {
  auto guid2page = io.read<GuidPageMap>();
  auto name2guid = io.read<NameGuidMap>();
  auto guid2name = io.read<GuidNameMap>();
  return { std::move(guid2page), std::move(name2guid), std::move(guid2name) };
}

} // namespace eg