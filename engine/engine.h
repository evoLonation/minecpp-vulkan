#ifndef ENGINE_H
#define ENGINE_H

#define REGISTER_ASSET(cls)                                                                        \
  static inline bool __asset_default_getter_register = []() {                                      \
    AssetManager::registerDefaultAsset<cls>();                                                     \
    return true;                                                                                   \
  }()

#endif // ENGINE_H