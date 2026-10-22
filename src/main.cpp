#include <cstdio>
#include <variant>

#include "app/app.h"
#include "app/options.h"

int main(int argc, char** argv) {
  auto parsed = pb::parseOptions(argc, argv);
  if (auto* err = std::get_if<std::string>(&parsed)) {
    std::fprintf(stderr, "photobrowse: %s\n", err->c_str());
    return 2;
  }
  const auto& options = std::get<pb::Options>(parsed);
  if (options.help) {
    std::fputs(pb::usage().c_str(), stdout);
    return 0;
  }
  return pb::App().run(options);
}
