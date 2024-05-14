module manager;

import manager.pipeline;

void mng::Manager::registerSubManagers() {
  _sub_managers.emplace_back(new SubManager<PipelineResource>{});
}