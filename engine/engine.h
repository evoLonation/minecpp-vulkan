#ifndef ENGINE_H
#define ENGINE_H

#define REGISTER_ASSET_PATH(pathname)                                                              \
  static inline fs::path asset_path = pathname;                                                    \
  auto getAssetPath() -> fs::path override { return asset_path; }

#endif // ENGINE_H