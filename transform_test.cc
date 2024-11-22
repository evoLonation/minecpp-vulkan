import toy;
// #include <test.h>
import std;
import transform;
import glm;
#include <test.h>
#include <toy.h>

using namespace trans;

TEST(transform) {
  auto view = view::create({ 1, 0, 0 }, { 1, 1, 0 }, { 0, -1, 1 });
  // TOY_ASSERT(eq(view * glm::vec4{ 1, 0, 1, 1 }, glm::vec4{ 0, 0, 1, 1 }));
  TOY_ASSERT(eq(view * glm::vec4{ 0, 0, 1, 1 }, glm::vec4{ 0, 1, 1, 1 }));
  TOY_ASSERT(eq(view * glm::vec4{ 2, 0, 1, 1 }, glm::vec4{ 0, -1, 1, 1 }));
  TOY_ASSERT(eq(view * glm::vec4{ 1, 1, 1, 1 }, glm::vec4{ 1, 0, 1, 1 }));
  TOY_ASSERT(eq(view * glm::vec4{ 1, -1, 1, 1 }, glm::vec4{ -1, 0, 1, 1 }));
  auto transform = translate(glm::vec3{ 7, 8, 9 });
  auto I = transform * inverse(transform);
  TOY_ASSERT(eq(glm::vec4{ I[0][0], I[1][1], I[2][2], I[3][3] }, glm::vec4{ 1.0f }));
  transform *= rotate(glm::vec3{ 1, 2, 3 }, 56);
  I = transform * inverse(transform);
  TOY_ASSERT(eq(glm::vec4{ I[0][0], I[1][1], I[2][2], I[3][3] }, glm::vec4{ 1.0f }));
  transform *= scale(glm::vec3{ 4, 5, 6 });
  I = transform * inverse(transform);
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      if (i == j) {
        TOY_ASSERT(eq(I[i][j], 1));
      } else {
        TOY_ASSERT(eq(I[i][j], 0), I[i][j]);
      }
    }
  }
  // intersection test
  auto inter = intersection(
    {
      .direction = glm::vec3{ glm::sqrt(3.0f), -1.0f, 0.0f },
      .point = glm::vec3{ 0.0f, 1.0f, 1.0f },
    },
    {
      .normal = glm::vec3{ -1.0f, glm::sqrt(3.0f), 0.0f },
      .dot = -glm::sqrt(3.0f),
    }
  );
  TOY_ASSERT(inter.index() == 0);
  TOY_ASSERT(eq(std::get<0>(inter), glm::vec3(glm::sqrt(3.0f), 0.0f, 1.0f)));

  // proj test
  auto near = 1.0f, far = 3.0f, left = 2.0f, right = 0.0f, bottom = 0.0f, top = 2.0f;
  // auto near = 1.0f, far = 3.0f, left = 2.0f, right = -2.0f, bottom = -2.0f, top = 2.0f;
  auto orthogonal = proj::orthogonal(near, far, left, right, bottom, top);
  TOY_ASSERT(eq(
    orthogonal * glm::vec4{ (near + far) / 2, (left + right) / 2, (bottom + top) / 2, 1.0f },
    glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f }
  ));
  TOY_ASSERT(
    eq(orthogonal * glm::vec4{ near, left, bottom, 1.0f }, glm::vec4{ -1.0f, 1.0f, -1.0f, 1.0f })
  );
  TOY_ASSERT(
    eq(orthogonal * glm::vec4{ far, right, top, 1.0f }, glm::vec4{ 1.0f, -1.0f, 1.0f, 1.0f })
  );
  TOY_ASSERT(
    eq(orthogonal * glm::vec4{ far, right, bottom, 1.0f }, glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f })
  );
  TOY_ASSERT(
    eq(orthogonal * glm::vec4{ near, left, top, 1.0f }, glm::vec4{ -1.0f, -1.0f, -1.0f, 1.0f })
  );
  auto perspective = proj::perspective(near, far, left, right, bottom, top);
  auto perspective_i = proj::perspectiveInverse(near, far, left, right, bottom, top);
  TOY_ASSERT(eq(perspective * perspective_i, glm::mat4{ 1.0f }));
  TOY_ASSERT(equivalence(
    perspective * glm::vec4{ near, left, bottom, 1.0f }, glm::vec4{ -1.0f, 1.0f, -1.0f, 1.0f }
  ));
  TOY_ASSERT(equivalence(
    perspective * glm::vec4{ near, right, top, 1.0f }, glm::vec4{ 1.0f, -1.0f, -1.0f, 1.0f }
  ));
  TOY_ASSERT(equivalence(
    perspective *
      glm::vec4{ far, (left + right) / 2 * far / near, (bottom + top) / 2 * far / near, 1.0f },
    glm::vec4{ 0.0f, 0.0f, 1.0f, 1.0f }
  ));
  TOY_ASSERT(equivalence(
    perspective * glm::vec4{ far, left * far / near, bottom * far / near, 1.0f },
    glm::vec4{ -1.0f, 1.0f, 1.0f, 1.0f }
  ));
  TOY_ASSERT(equivalence(
    perspective * glm::vec4{ far, right * far / near, top * far / near, 1.0f },
    glm::vec4{ 1.0f, -1.0f, 1.0f, 1.0f }
  ));
  perspective = proj::perspective({ 1920, 1080, 90.0f, 2.5f, 100.0f });
  perspective_i = proj::perspectiveInverse({ 1920, 1080, 90.0f, 2.5f, 100.0f });
  TOY_ASSERT(eq(perspective * perspective_i, glm::mat4{ 1.0f }));
  TOY_ASSERT(equivalence(
    perspective * glm::vec4{ 2.5f, 0.0f, 0.0f, 1.0f }, glm::vec4{ 0.0f, 0.0f, -1.0f, 1.0f }
  ));
  TOY_ASSERT(equivalence(
    perspective * glm::vec4{ 100.0f, 0.0f, 0.0f, 1.0f }, glm::vec4{ 0.0f, 0.0f, 1.0f, 1.0f }
  ));
  TOY_ASSERT(equivalence(
    perspective * glm::vec4{ 2.5f, 2.5f, 0.0f, 1.0f }, glm::vec4{ -1.0f, 0.0f, -1.0f, 1.0f }
  ));
  // euler angle
  TOY_ASSERT(eq(rotate(174, 0, 0), rotate<Axis::X>(174)));
  TOY_ASSERT(eq(rotate(0, 174, 0), rotate<Axis::Y>(174)));
  TOY_ASSERT(eq(rotate(0, 0, 174), rotate<Axis::Z>(174)));
  TOY_ASSERT(eq(rotate(174, 73, 0), rotate<Axis::Y>(73) * rotate<Axis::X>(174)));
  TOY_ASSERT(eq(rotate(174, 0, 73), rotate<Axis::Z>(73) * rotate<Axis::X>(174)));
  TOY_ASSERT(eq(rotate(0, 174, 73), rotate<Axis::Z>(73) * rotate<Axis::Y>(174)));
  TOY_ASSERT(
    eq(rotate(174, 73, 75), rotate<Axis::Z>(75) * rotate<Axis::Y>(73) * rotate<Axis::X>(174))
  );
  TOY_ASSERT(eq(rotate(73, 90, 0), rotate<Axis::Y>(90) * rotate<Axis::X>(73)));
  TOY_ASSERT(eq(rotate(-73, 90, 0), rotate<Axis::Z>(73) * rotate<Axis::Y>(90)));
  TOY_ASSERT(eq(rotate(0, 90, 73), rotate<Axis::Z>(73) * rotate<Axis::Y>(90)));
  TOY_ASSERT(eq(rotate(0, 90, -73), rotate<Axis::Y>(90) * rotate<Axis::X>(73)));
  TOY_ASSERT(eq(rotate(73, 90, 76), rotate(-3, 90, 0)));
  TOY_ASSERT(eq(rotate(43, -90, 30), rotate(0, -90, 73)));

  // getEulerAngle
  TOY_ASSERT(eq(getEulerAngle(rotate(74, 0, 0)), glm::vec3{ 74, 0, 0 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(0, 74, 0)), glm::vec3{ 0, 74, 0 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(0, 0, 74)), glm::vec3{ 0, 0, 74 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(73, 74, 0)), glm::vec3{ 73, 74, 0 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(73, 0, 74)), glm::vec3{ 73, 0, 74 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(0, 73, 74)), glm::vec3{ 0, 73, 74 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(71, 72, 73)), glm::vec3{ 71, 72, 73 }));
  // y_degree = 90
  TOY_ASSERT(eq(getEulerAngle(rotate(0, 90, 0)), glm::vec3{ 0, 90, 0 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(0, -90, 0)), glm::vec3{ 0, -90, 0 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(0, 90, 73)), glm::vec3{ -73, 90, 0 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(0, -90, -73)), glm::vec3{ -73, -90, 0 }));
  TOY_ASSERT(eq(getEulerAngle(rotate(73, 90, 0)), glm::vec3{ 73, 90, 0 }));

  // distance
  auto line1 = Line{ .direction = { 1, 0, 0 }, .point = { 0, 0, 0 } };
  auto line2 = Line{ .direction = { 0, 1, 0 }, .point = { 0, 0, 0 } };
  auto ret = distance(line1, line2);
  TOY_ASSERT(ret.distance == 0);
  TOY_ASSERT(eq(ret.point1, glm::vec3{ 0, 0, 0 }));
  TOY_ASSERT(eq(ret.point2, glm::vec3{ 0, 0, 0 }));
  line1 = Line{ .direction = { 1, 0, 0 }, .point = { 0, 0, 0 } };
  line2 = Line{ .direction = { 0, 1, 0 }, .point = { 0, 0, 1 } };
  ret = distance(line1, line2);
  TOY_ASSERT(ret.distance == 1);
  TOY_ASSERT(eq(ret.point1, glm::vec3{ 0, 0, 0 }));
  TOY_ASSERT(eq(ret.point2, glm::vec3{ 0, 0, 1 }));
  line1 = Line{ .direction = { 0, 1, 1 }, .point = { 0, 0, 0 } };
  line2 = Line{ .direction = { 0, 1, -1 }, .point = { 0, 0, 1 } };
  ret = distance(line1, line2);
  TOY_ASSERT(eq(ret.point2, glm::vec3{ 0, 0.5, 0.5 }), ret.point2);
  TOY_ASSERT(eq(ret.point1, glm::vec3{ 0, 0.5, 0.5 }), ret.point1);
  TOY_ASSERT(eq(ret.distance, 0), ret.distance);
  line1 = Line{ .direction = { 0, 1, 1 }, .point = { 0, 0, 0 } };
  line2 = Line{ .direction = { 0, 1, -1 }, .point = { 1, 0, 1 } };
  ret = distance(line1, line2);
  TOY_ASSERT(eq(ret.point2, glm::vec3{ 1, 0.5, 0.5 }), ret.point2);
  TOY_ASSERT(eq(ret.point1, glm::vec3{ 0, 0.5, 0.5 }), ret.point1);
  TOY_ASSERT(eq(ret.distance, 1), ret.distance);
  // parallel
  line1 = Line{ .direction = { 0, 1, 1 }, .point = { 0, 0, 0 } };
  line2 = Line{ .direction = { 0, 1, 1 }, .point = { 10, 123, 123 } };
  ret = distance(line1, line2);
  TOY_ASSERT(eq(ret.distance, 10), ret.distance);
}