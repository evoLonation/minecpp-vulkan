module;
#include <toy.h>
module engine.camera;

namespace eg {

Camera::Camera() {
  setStage(fw::Stage::CAMERA_CONTROLLER);
  auto extent = rd::Presentation::getInstance().getSwapchain().getExtent();
  updateProj();
  updateView();
}

void Camera::runLoop(fw::ActionContext const& ctx) {
  if (ctx.recreated) {
    updateProj();
  }
}

/**
 * @brief 以自身的坐标系移动 moved 的距离
 * 自身坐标系： z与世界坐标系相同，只有x和y是根据_rotate_z旋转后的结果
 */
void Camera::move(glm::vec3 moved) {
  // 将moved转换到世界坐标系的位移，然后更新_view_pos
  _view_pos += mt::transformDot(mt::rotate<mt::Axis::Z>(-_rotate_z), moved);
  updateView();
}

void Camera::rotate_right(float degree) {
  // degree 为正 -> 所有坐标轴沿z轴向左转 -> 摄像机向右转
  _rotate_z += degree;
  updateView();
}

void Camera::rotate_up(float degree) {
  // degree为正 -> 所有坐标沿y轴向下转 -> 摄像机向上转
  _rotate_y += degree;
  _rotate_y = std::clamp(_rotate_y.get(), -90.0f, 90.0f);
  updateView();
}

void Camera::updateView() {
  _view = mt::rotate<mt::Axis::Y>(_rotate_y) * mt::rotate<mt::Axis::Z>(_rotate_z) *
          mt::translate(-_view_pos);
}

void Camera::updateProj() {
  auto extent = rd::Presentation::getInstance().getSwapchain().getExtent();
  auto info = mt::proj::ProjectionInfo{
    .width = extent.width,
    .height = extent.height,
  };
  _proj = mt::proj::perspective(info);
  _proj_iv = mt::proj::perspectiveInverse(info);
}

CameraController::CameraController(Camera* camera) : _camera(camera) {
  setStage(fw::Stage::CAMERA_CONTROLLER);
}

void CameraController::runLoop(fw::ActionContext const& ctx) {
  auto delta = ctx.interval;
  using enum fw::Keyboard;
  auto& input = fw::InputProcessor::getInstance();
  if (input[KEY_S]) {
    _camera->move(glm::vec3{ -_move_speed * delta, 0.0f, 0.0f });
  }
  if (input[KEY_W]) {
    _camera->move(glm::vec3{ _move_speed * delta, 0.0f, 0.0f });
  }
  if (input[KEY_A]) {
    _camera->move(glm::vec3{ 0.0f, _move_speed * delta, 0.0f });
  }
  if (input[KEY_D]) {
    _camera->move(glm::vec3{ 0.0f, -_move_speed * delta, 0.0f });
  }
  if (input[KEY_Z]) {
    _camera->move(glm::vec3{ 0.0f, 0.0f, _move_speed * delta });
  }
  if (input[KEY_X]) {
    _camera->move(glm::vec3{ 0.0f, 0.0f, -_move_speed * delta });
  }
  if (auto res = input[ESCAPE]; res && res->state == fw::ButtonState::DOWN) {
    input.setCursorVisible(!input.getCursorState().visible);
  }
  // x: left, y: down
  auto cursor = input.getCursorState();
  if (!cursor.visible) {
    auto x = cursor.x_move;
    auto y = cursor.y_move;
    auto [width, height] = rd::Presentation::getInstance().getSwapchain().getExtent();
    _camera->rotate_right(x / static_cast<float>(width) * 360);
    _camera->rotate_up(-y / static_cast<float>(height) * 360);
  }
}

} // namespace eg