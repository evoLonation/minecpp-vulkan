module manager;

import manager.pipeline;
import manager.draw_unit;

void mng::Manager::registerSubManagers() {
  auto camera_manager = new SubManager<CameraResource>{};
  auto pipeline_manager = new SubManager<PipelineResource>{};
  auto draw_unit_manager = new SubManager<DrawUnitResource>{};
  draw_unit_manager->registerRefManager(camera_manager);
  draw_unit_manager->registerRefManager(pipeline_manager);
  _sub_managers.emplace_back(camera_manager);
  _sub_managers.emplace_back(pipeline_manager);
  _sub_managers.emplace_back(draw_unit_manager);
}