#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "app/library_index.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
// Library of a few real photos (copied, so files can be added/removed safely).
struct Lib {
  pbtest::TempDir photos, cache;
  explicit Lib(std::initializer_list<const char*> names) {
    for (auto n : names) fs::copy_file(pbtest::photo(n), photos.path / n);
  }
  fs::path libDir() const { return cache.path / "lib"; }
};

pb::LibraryState indexNow(const Lib& lib, pb::JobSystem& jobs) {
  pb::ThumbCache thumbs(lib.libDir());
  pb::Indexer ix(lib.photos.path, lib.libDir(), jobs, thumbs);
  ix.start();
  for (int i = 0; i < 2000 && !ix.finished(); ++i) std::this_thread::sleep_for(5ms);
  REQUIRE(ix.finished());
  CHECK(ix.done() == ix.total());
  return ix.take();
}
}  // namespace

TEST_CASE("library: cold index sorts by date and writes cache") {
  Lib lib({"PXL_20260321_134351678.jpg", "PXL_20260321_003010085.jpg", "PXL_20260321_012354015~2.jpg"});
  pb::JobSystem jobs(4);
  auto st = indexNow(lib, jobs);
  REQUIRE(st.catalog.photos.size() == 3);
  REQUIRE(st.catalog.order.size() == 3);
  CHECK(st.catalog.photos[st.catalog.order[0]].relPath == "PXL_20260321_003010085.jpg");
  CHECK(st.catalog.photos[st.catalog.order[2]].relPath == "PXL_20260321_134351678.jpg");
  CHECK(st.l0.size() == 3);  // includes the edited copy via the decode fallback
  for (const auto& p : st.catalog.photos) {
    CHECK((p.flags & pb::kNeedsDecodeL1) == 0);
    CHECK(fs::exists(pb::ThumbCache(lib.libDir()).levelPath(1, p.key)));
  }
  CHECK(fs::exists(lib.libDir() / "catalog.bin"));
  CHECK(fs::exists(lib.libDir() / "l0.pack"));
}

TEST_CASE("library: warm load returns the saved state") {
  Lib lib({"PXL_20260321_003010085.jpg", "PXL_20260321_010024264.jpg"});
  CHECK_FALSE(pb::loadWarm(lib.libDir()));
  pb::JobSystem jobs(2);
  auto cold = indexNow(lib, jobs);
  auto warm = pb::loadWarm(lib.libDir());
  REQUIRE(warm);
  CHECK(warm->catalog.order == cold.catalog.order);
  CHECK(warm->l0.size() == 2);
}

TEST_CASE("library: broken files are kept out of the layout") {
  Lib lib({"PXL_20260321_003010085.jpg"});
  pbtest::writeFile(lib.photos.path / "bad.jpg", "nope");
  pb::JobSystem jobs(2);
  auto st = indexNow(lib, jobs);
  CHECK(st.catalog.photos.size() == 2);
  CHECK(st.catalog.order.size() == 1);
}

TEST_CASE("library: empty folder indexes to nothing") {
  Lib lib({});
  pb::JobSystem jobs(2);
  auto st = indexNow(lib, jobs);
  CHECK(st.catalog.photos.empty());
}

TEST_CASE("library: refresh applies added/removed files for the next launch") {
  Lib lib({"PXL_20260321_003010085.jpg", "PXL_20260321_010024264.jpg"});
  pb::JobSystem jobs(2);
  auto st = indexNow(lib, jobs);
  pb::ThumbCache thumbs(lib.libDir());
  CHECK_FALSE(pb::refreshLibrary(lib.photos.path, lib.libDir(), st, thumbs));  // nothing changed
  fs::copy_file(pbtest::photo("PXL_20260321_134351678.jpg"), lib.photos.path / "new.jpg");
  fs::remove(lib.photos.path / "PXL_20260321_010024264.jpg");
  CHECK(pb::refreshLibrary(lib.photos.path, lib.libDir(), st, thumbs));
  auto next = pb::loadWarm(lib.libDir());
  REQUIRE(next);
  REQUIRE(next->catalog.photos.size() == 2);
  std::vector<std::string> names;
  for (auto id : next->catalog.order) names.push_back(next->catalog.photos[id].relPath);
  CHECK(names == std::vector<std::string>{"PXL_20260321_003010085.jpg", "new.jpg"});
  CHECK(next->l0.size() == 2);
}

TEST_CASE("library: abandoning an indexer skips its queued work") {
  Lib lib({"PXL_20260321_003010085.jpg", "PXL_20260321_010024264.jpg"});
  pb::JobSystem jobs(1);
  std::atomic<bool> release{false};
  jobs.submit(-1, [&] {
    while (!release) std::this_thread::sleep_for(1ms);
  });
  pb::ThumbCache thumbs(lib.libDir());
  std::thread releaser([&] {
    std::this_thread::sleep_for(50ms);
    release = true;
  });
  {
    pb::Indexer ix(lib.photos.path, lib.libDir(), jobs, thumbs);
    ix.start();
  }  // destroyed while its scan job is still queued
  releaser.join();
  CHECK_FALSE(fs::exists(lib.libDir() / "catalog.bin"));
}

TEST_CASE("library: refresh stops early when asked") {
  Lib lib({"PXL_20260321_003010085.jpg"});
  pb::JobSystem jobs(2);
  auto st = indexNow(lib, jobs);
  pb::ThumbCache thumbs(lib.libDir());
  fs::copy_file(pbtest::photo("PXL_20260321_134351678.jpg"), lib.photos.path / "new.jpg");
  std::atomic<bool> stop{true};
  CHECK_FALSE(pb::refreshLibrary(lib.photos.path, lib.libDir(), st, thumbs, &stop));
  CHECK(pb::loadWarm(lib.libDir())->catalog.photos.size() == 1);  // nothing saved
}

TEST_CASE("library: recursive setting is part of the cache; changing it re-indexes") {
  Lib lib({"PXL_20260321_003010085.jpg"});
  fs::create_directories(lib.photos.path / "sub");
  fs::copy_file(pbtest::photo("PXL_20260321_010024264.jpg"), lib.photos.path / "sub" / "x.jpg");
  pb::JobSystem jobs(2);
  pb::ThumbCache thumbs(lib.libDir());
  {
    pb::Indexer ix(lib.photos.path, lib.libDir(), jobs, thumbs, pb::MediaFormats::none(), false);
    ix.start();
    for (int i = 0; i < 2000 && !ix.finished(); ++i) std::this_thread::sleep_for(5ms);
    REQUIRE(ix.finished());
    auto st = ix.take();
    CHECK(st.catalog.order.size() == 1);  // the subfolder photo is left out
    CHECK_FALSE(st.catalog.recursive);
  }
  CHECK(pb::loadWarm(lib.libDir(), pb::MediaFormats::none(), false));
  CHECK_FALSE(pb::loadWarm(lib.libDir(), pb::MediaFormats::none(), true));
}
