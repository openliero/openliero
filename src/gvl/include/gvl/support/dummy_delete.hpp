#pragma once

namespace gvl {

  struct dummy_delete {
    template <typename T>
    void operator()(T const&) const {
      // Do nothing
    }
  };
}
