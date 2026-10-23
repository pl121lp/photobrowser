#include <doctest/doctest.h>

#include <vector>

#include "app/options.h"

namespace {
std::variant<pb::Options, std::string> parse(std::vector<const char*> args) {
  args.insert(args.begin(), "photobrowse");
  return pb::parseOptions(int(args.size()), const_cast<char**>(args.data()));
}
}  // namespace

TEST_CASE("options: defaults") {
  auto r = parse({});
  REQUIRE(std::holds_alternative<pb::Options>(r));
  auto& o = std::get<pb::Options>(r);
  CHECK_FALSE(o.photoDir);
  CHECK_FALSE(o.cacheDir);
  CHECK_FALSE(o.rebuildCache);
  CHECK(o.layout == "square-crop");
  CHECK_FALSE(o.bench);
  CHECK_FALSE(o.help);
}

TEST_CASE("options: folder and flags") {
  auto r = parse({"/data/pics", "--cache-dir", "/tmp/c", "--rebuild-cache", "--layout", "square-crop", "--bench"});
  REQUIRE(std::holds_alternative<pb::Options>(r));
  auto& o = std::get<pb::Options>(r);
  CHECK(*o.photoDir == "/data/pics");
  CHECK(*o.cacheDir == "/tmp/c");
  CHECK(o.rebuildCache);
  CHECK(o.bench);
}

TEST_CASE("options: help") {
  auto r = parse({"--help"});
  REQUIRE(std::holds_alternative<pb::Options>(r));
  CHECK(std::get<pb::Options>(r).help);
  CHECK(pb::usage().find("--cache-dir") != std::string::npos);
}

TEST_CASE("options: unknown flag errors") {
  auto r = parse({"--frobnicate"});
  REQUIRE(std::holds_alternative<std::string>(r));
  CHECK(std::get<std::string>(r).find("--frobnicate") != std::string::npos);
  CHECK(std::holds_alternative<std::string>(parse({"--cache-dir"})));  // missing value
  CHECK(std::holds_alternative<std::string>(parse({"a", "b"})));       // two folders
}

TEST_CASE("options: unknown layout errors") {
  auto r = parse({"--layout", "spiral"});
  REQUIRE(std::holds_alternative<std::string>(r));
  CHECK(std::get<std::string>(r).find("spiral") != std::string::npos);
}

TEST_CASE("options: snapshot") {
  auto r = parse({"--snapshot", "out.jpg"});
  REQUIRE(std::holds_alternative<pb::Options>(r));
  CHECK(*std::get<pb::Options>(r).snapshot == "out.jpg");
  CHECK(std::holds_alternative<std::string>(parse({"--snapshot"})));
}

TEST_CASE("options: stats starts with the F3 display on") {
  CHECK_FALSE(std::get<pb::Options>(parse({})).stats);
  CHECK(std::get<pb::Options>(parse({"--stats"})).stats);
}

TEST_CASE("options: windowed overrides the fullscreen setting") {
  CHECK_FALSE(std::get<pb::Options>(parse({})).windowed);
  CHECK(std::get<pb::Options>(parse({"--windowed"})).windowed);
}

TEST_CASE("options: config file and no default folder") {
  auto o = std::get<pb::Options>(parse({"--config", "/tmp/x.ini"}));
  CHECK(*o.configFile == "/tmp/x.ini");
  CHECK_FALSE(o.photoDir);  // the folder comes from the config unless given
}

TEST_CASE("options: plugins") {
  auto r = parse({"--plugin", "video", "--plugin", "/x/libpb_other.so"});
  REQUIRE(std::holds_alternative<pb::Options>(r));
  CHECK(std::get<pb::Options>(r).plugins == std::vector<std::string>{"video", "/x/libpb_other.so"});
  CHECK(std::holds_alternative<std::string>(parse({"--plugin"})));
  CHECK(std::get<pb::Options>(parse({})).plugins.empty());
}
