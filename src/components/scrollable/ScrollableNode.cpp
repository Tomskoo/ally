#include "ScrollableNode.hpp"

#include <algorithm>
#include <ftxui/dom/node.hpp>
#include <utility>

using namespace ftxui;

namespace ally::components {

auto make_scrollable(Element child, int* scroll_y, int* viewport_height_out,
                     int* content_height_out) -> Element {
  class Impl : public Node {
   public:
    Impl(Element child, int* scroll_y, int* viewport_height_out, int* content_height_out)
        : Node({std::move(child)}), scroll_y_(scroll_y), viewport_height_out_(viewport_height_out),
          content_height_out_(content_height_out) {}
    void ComputeRequirement() override {
      children_[0]->ComputeRequirement();
      requirement_ = children_[0]->requirement();
      content_req_h_ = requirement_.min_y;
      requirement_.min_y = 0;
    }
    void SetBox(Box box) override {
      Node::SetBox(box);
      const int viewport_h = box.y_max - box.y_min + 1;

      if (viewport_height_out_ != nullptr) {
        *viewport_height_out_ = viewport_h;
      }

      // First pass: lay out child at the correct width so wrapping elements
      // (flexbox/paragraph) settle at their true wrapped height.
      int content_h = std::max(content_req_h_, viewport_h);
      {
        Box probe = box;
        probe.y_min = box.y_min;
        probe.y_max = probe.y_min + content_h - 1;
        children_[0]->SetBox(probe);
      }

      // Re-query: after SetBox, flexbox elements know their actual width and
      // ComputeRequirement now reports the correct wrapped min_y.
      children_[0]->ComputeRequirement();
      int measured_h = children_[0]->requirement().min_y;
      content_h = std::max({content_req_h_, measured_h, viewport_h});

      const int max_scroll = std::max(0, content_h - viewport_h);
      *scroll_y_ = std::clamp(*scroll_y_, 0, max_scroll);

      Box child_box = box;
      child_box.y_min = box.y_min - *scroll_y_;
      child_box.y_max = child_box.y_min + content_h - 1;
      children_[0]->SetBox(child_box);

      // Write content height after final layout so the value is stable.
      if (content_height_out_ != nullptr) {
        children_[0]->ComputeRequirement();
        *content_height_out_ = children_[0]->requirement().min_y;
      }
    }
    void Render(Screen& screen) override {
      Box old_stencil = screen.stencil;
      screen.stencil = Box::Intersection(box_, screen.stencil);
      children_[0]->Render(screen);
      screen.stencil = old_stencil;
    }

   private:
    int* scroll_y_;
    int* viewport_height_out_;
    int* content_height_out_;
    int content_req_h_ = 0;
  };
  return std::make_shared<Impl>(std::move(child), scroll_y, viewport_height_out, content_height_out);
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

auto reflect_layout(Box& box) -> Decorator {
  return [&box](Element child) -> Element {
    class Impl : public Node {
     public:
      Impl(Element elem, Box& target) : Node({std::move(elem)}), box_(target) {}
      void ComputeRequirement() override {
        children_[0]->ComputeRequirement();
        requirement_ = children_[0]->requirement();
      }
      void SetBox(Box box) override {
        box_ = box;
        Node::SetBox(box);
        children_[0]->SetBox(box);
      }
      void Render(Screen& screen) override { children_[0]->Render(screen); }

     private:
      Box& box_;
    };
    return std::make_shared<Impl>(std::move(child), box);
  };
}

}  // namespace ally::components
