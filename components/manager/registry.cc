module manager;

import manager.pipeline;
import manager.draw_unit;

void mng::Manager::registerSubManagers() {
  auto camera_manager = new SubManager<CameraResource>{};
  auto pipeline_manager = new SubManager<PipelineResource>{};
  auto draw_unit_manager = new SubManager<DrawUnitResource>{};
  draw_unit_manager->registerRefManager(camera_manager);
  draw_unit_manager->registerRefManager(pipeline_manager);
  registerSubManager(camera_manager);
  registerSubManager(pipeline_manager);
  registerSubManager(draw_unit_manager);
}