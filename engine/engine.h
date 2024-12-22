#ifndef ENGINE_H
#define ENGINE_H

#define REGISTER_ASSET_ID(pathname)                                                                \
  static inline fs::path asset_id = pathname;                                                      \
  auto getAssetPath() -> fs::path override { return asset_id; }

#endif // ENGINE_H