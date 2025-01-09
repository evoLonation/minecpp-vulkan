module;
#include <toy.h>
module engine.node;

namespace eg {

ModelController::ModelController() {
  _location.setName(std::format("Location {}", (void*)this));
  _rotate_euler.setName(std::format("Rotate Euler {}", (void*)this));
  _scale_factor.setName(std::format("Scale Factor {}", (void*)this));
}

void ModelController::setName(std::string name) {
  _location.setName(std::format("Location {}", name));
  _rotate_euler.setName(std::format("Rotate Euler {}", name));
  _scale_factor.setName(std::format("Scale Factor {}", name));
}

ModelController::ModelController(Reval<glm::mat4>* model, SettingMode mode) : ModelController{} {
  setModelTransform(model, mode);
}

void ModelController::setModelTransformInvalid() { _model = nullptr; }

void ModelController::resetModelTransform() {
  if (_model) {
    _unbinder_model.unbind();
    _unbinder_vec.unbind();
  }
  _model = nullptr;
}

void ModelController::setModelTransform(Reval<glm::mat4>* model, SettingMode mode) {
  if (_model) {
    _unbinder_model.unbind();
    _unbinder_vec.unbind();
  }
  _model = model;
  auto computer_model = [](auto& location, auto& rotate_euler, auto& scale_factor) {
    auto model = mt::translate(location) * mt::rotate(rotate_euler) * mt::scale(scale_factor);
    // TOY_DEBUG(location, rotate_euler, scale_factor, model);
    return model;
  };
  auto computer_vec = [](glm::mat4 model) {
    auto location = glm::vec3{ model[3] };
    model[3] = glm::vec4{ 0, 0, 0, 1 };
    auto scale_factor = glm::vec3{
      glm::length(model[0]),
      glm::length(model[1]),
      glm::length(model[2]),
    };
    model[0] = glm::normalize(model[0]);
    model[1] = glm::normalize(model[1]);
    model[2] = glm::normalize(model[2]);
    auto rotate_euler = mt::getEulerAngle(model);
    return std::tuple{ location, rotate_euler, scale_factor };
  };
  if (mode == CHANGE_CONTROLLER) {
    _unbinder_model = _model->bind(computer_model, &_location, &_rotate_euler, &_scale_factor);
    _unbinder_vec =
      MultiBinder{ &_location, &_rotate_euler, &_scale_factor }.bindNow(computer_vec, _model);
  } else if (mode == CHANGE_MODEL) {
    _unbinder_model = _model->bindNow(computer_model, &_location, &_rotate_euler, &_scale_factor);
    _unbinder_vec =
      MultiBinder{ &_location, &_rotate_euler, &_scale_factor }.bind(computer_vec, _model);
  }
}

void ModelController::translate(glm::vec3 const& delta) {
  if (_mode == WORLD) {
    _location += delta;
  } else {
    auto r_2 = mt::rotate(_rotate_euler) * mt::translate(delta);
    // r_2 = t_new * r_new
    // t_new = t_2[3][0-3], r_new = t_2[0-2][0-2]
    _location += glm::vec3{ r_2[3] };
    r_2[3] = glm::vec4{ 0, 0, 0, 1 };
    _rotate_euler = mt::getEulerAngle(r_2);
  }
}

void ModelController::translate(mt::Axis axis, float delta) {
  using enum mt::Axis;
  switch (axis) {
  case X:
    translate(glm::vec3{ delta, 0, 0 });
    break;
  case Y:
    translate(glm::vec3{ 0, delta, 0 });
    break;
  case Z:
    translate(glm::vec3{ 0, 0, delta });
    break;
  }
}

void ModelController::rotate(mt::Axis axis, float degree) {
  auto r_new = glm::mat4{};
  if (_mode == WORLD) {
    r_new = mt::rotate(axis, degree) * mt::rotate(_rotate_euler);
  } else {
    r_new = mt::rotate(_rotate_euler) * mt::rotate(axis, degree);
  }
  _rotate_euler = mt::getEulerAngle(r_new);
}

void ModelController::rotate(glm::vec3 const& axis, float degree) {
  auto r_new = glm::mat4{};
  if (_mode == WORLD) {
    r_new = mt::rotate(axis, degree) * mt::rotate(_rotate_euler);
  } else {
    r_new = mt::rotate(_rotate_euler) * mt::rotate(axis, degree);
  }
  _rotate_euler = mt::getEulerAngle(r_new);
}

/**
 * @brief Always on local mode
 */
void ModelController::scale(glm::vec3 const& factor) {
  _scale_factor.set([&](glm::vec3& x) { x *= factor; });
}

void ModelController::scale(mt::Axis axis, float factor) {
  using enum mt::Axis;
  switch (axis) {
  case X:
    _scale_factor.set([&](glm::vec3& x) { x[0] *= factor; });
    break;
  case Y:
    _scale_factor.set([&](glm::vec3& x) { x[1] *= factor; });
    break;
  case Z:
    _scale_factor.set([&](glm::vec3& x) { x[2] *= factor; });
    break;
  }
}

void ModelController::setMode(Mode mode) { _mode = mode; }

Node::Node() {
  _to_world.setName(std::format("To World {}", (void*)this));
  _to_parent.setName(std::format("To Parent {}", (void*)this));
  _controller.setModelTransform(&_to_parent, ModelController::CHANGE_MODEL);
  _unbinder = _to_world.bindNow([](glm::mat4 const& to_parent) { return to_parent; }, &_to_parent);
  _root = this;
}

void Node::setName(std::string name) {
  _to_world.setName(std::format("To World {}", name));
  _to_parent.setName(std::format("To Parent {}", name));
  getController().setName(name);
}

Node::~Node() {
  // can not directly use _children, because it will be modified in detach
  auto children = _children;
  for (auto& child : children) {
    child->detach();
  }
  // can ensure _parent is nullptr (otherwise this will not destroyed)
}

void Node::attachTo(Node* node) {
  if (_parent) {
    detach();
  }
  setParent(node);
  doRecursively([node](Node& child) { child._root = node->_root; });
}

void Node::detach() {
  TOY_ASSERT(_parent);
  resetParent();
  doRecursively([this](Node& child) { child._root = this; });
}

void Node::doRecursively(std::function<void(Node&)> const& dealer) {
  dealer(*this);
  for (auto& child : _children) {
    child->doRecursively(dealer);
  }
}

void Node::setParent(Node* parent) {
  // resetParent();
  _unbinder.unbind();
  _parent = parent;
  parent->_children.insert(getPtr());
  _unbinder = _to_world.bindNow(
    [](glm::mat4 const& parent, glm::mat4 const& to_parent) { return parent * to_parent; },
    &_parent->_to_world,
    &_to_parent
  );
}

void Node::resetParent() {
  _unbinder.unbind();
  _unbinder = _to_world.bindNow([](glm::mat4 const& to_parent) { return to_parent; }, &_to_parent);
  if (_parent) {
    _parent->_children.erase(getPtr());
    _parent = nullptr;
  }
}

auto Node::assetSerialize() -> AssetPackager {
  auto children_data = std::vector<AssetRef>{};
  for (auto& child : _children) {
    children_data.push_back(AssetRef{ child });
  }
  return { std::tuple{ _to_parent.get(), children_data }, Asset::assetSerialize() };
}

void Node::assetDeserialize(AssetUnpacker unpacker) {
  Asset::assetDeserialize(unpacker.extractInherited());
  auto [to_parent, children_data] = unpacker.extract<glm::mat4, std::vector<AssetRef>>();
  _to_parent = to_parent;
  for (auto& child_data : children_data) {
    auto child = child_data.consume<Node>();
    child->attachTo(this);
  }
}

} // namespace eg