#include "ScrollableNode.hpp"

#include <algorithm>
#include <ftxui/dom/node.hpp>
#include <utility>

using namespace ftxui;

namespace ally::components {

auto make_scrollable(Element child, int* scroll_y) -> Element {
  class Impl : public Node {
   public:
    Impl(Element child, int* scroll_y) : Node({std::move(child)}), scroll_y_(scroll_y) {}
    void ComputeRequirement() override {
      children_[0]->ComputeRequirement();
      requirement_ = children_[0]->requirement();
      content_req_h_ = requirement_.min_y;
      requirement_.min_y = 0;
    }
    void SetBox(Box box) override {
      Node::SetBox(box);
      const int viewport_h = box.y_max - box.y_min + 1;

      int content_h = std::max(content_req_h_, viewport_h);

      const int max_scroll = std::max(0, content_h - viewport_h);
      *scroll_y_ = std::clamp(*scroll_y_, 0, max_scroll);

      Box child_box = box;
      child_box.y_min = box.y_min - *scroll_y_;
      child_box.y_max = child_box.y_min + content_h - 1;
      children_[0]->SetBox(child_box);
    }
    void Render(Screen& screen) override {
      Box old_stencil = screen.stencil;
      screen.stencil = Box::Intersection(box_, screen.stencil);
      children_[0]->Render(screen);
      screen.stencil = old_stencil;
    }

   private:
    int* scroll_y_;
    int content_req_h_ = 0;
  };
  return std::make_shared<Impl>(std::move(child), scroll_y);
}

auto cap_height(Element child) -> Element {
  class Impl : public Node {
   public:
    explicit Impl(Element elem) : Node({std::move(elem)}) {}
    void ComputeRequirement() override {
      children_[0]->ComputeRequirement();
      requirement_ = children_[0]->requirement();
      requirement_.min_y = 0;
    }
    void SetBox(Box box) override {
      Node::SetBox(box);
      children_[0]->SetBox(box);
    }
    void Render(Screen& screen) override { children_[0]->Render(screen); }
  };
  return std::make_shared<Impl>(std::move(child));
}

}  // namespace ally::components
