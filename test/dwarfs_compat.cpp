/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>

// TODO: this test should be autogenerated somehow...

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <sys/statvfs.h>

#include <folly/json.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs/filesystem_writer.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/progress.h"
#include "dwarfs/worker_group.h"

#include "mmap_mock.h"

namespace {

char const* reference = R"(
{
  "root": {
    "entries": [
      {
        "inode": 11,
        "mode": 33188,
        "modestring": "----rw-r--r--",
        "name": "bench.sh",
        "size": 1517,
        "type": "file"
      },
      {
        "entries": [],
        "inode": 1,
        "mode": 16877,
        "modestring": "---drwxr-xr-x",
        "name": "dev",
        "type": "directory"
      },
      {
        "entries": [
          {
            "entries": [],
            "inode": 3,
            "mode": 16877,
            "modestring": "---drwxr-xr-x",
            "name": "alsoempty",
            "type": "directory"
          }
        ],
        "inode": 2,
        "mode": 16877,
        "modestring": "---drwxr-xr-x",
        "name": "empty",
        "type": "directory"
      },
      {
        "entries": [
          {
            "inode": 5,
            "mode": 41471,
            "modestring": "---lrwxrwxrwx",
            "name": "bad",
            "target": "../foo",
            "type": "link"
          },
          {
            "inode": 7,
            "mode": 33188,
            "modestring": "----rw-r--r--",
            "name": "bar",
            "size": 0,
            "type": "file"
          },
          {
            "inode": 11,
            "mode": 33188,
            "modestring": "----rw-r--r--",
            "name": "bla.sh",
            "size": 1517,
            "type": "file"
          }
        ],
        "inode": 4,
        "mode": 16877,
        "modestring": "---drwxr-xr-x",
        "name": "foo",
        "type": "directory"
      },
      {
        "inode": 6,
        "mode": 41471,
        "modestring": "---lrwxrwxrwx",
        "name": "foobar",
        "target": "foo/bar",
        "type": "link"
      },
      {
        "inode": 8,
        "mode": 33261,
        "modestring": "----rwxr-xr-x",
        "name": "format.sh",
        "size": 94,
        "type": "file"
      },
      {
        "inode": 10,
        "mode": 33188,
        "modestring": "----rw-r--r--",
        "name": "perl-exec.sh",
        "size": 87,
        "type": "file"
      },
      {
        "inode": 9,
        "mode": 33188,
        "modestring": "----rw-r--r--",
        "name": "test.py",
        "size": 1012,
        "type": "file"
      }
    ],
    "inode": 0,
    "mode": 16877,
    "modestring": "---drwxr-xr-x",
    "type": "directory"
  },
  "statvfs": {
    "f_blocks": 4240,
    "f_bsize": 1048576,
    "f_files": 12
  }
}
)";

std::vector<std::string> versions{
    "0.2.0",
    "0.2.3",
    "0.3.0",
};

std::string format_sh = R"(#!/bin/bash
find test/ src/ include/ -type f -name '*.[ch]*' | xargs -d $'\n' clang-format -i
)";

struct ::stat make_stat(::mode_t mode, ::off_t size) {
  struct ::stat st;
  std::memset(&st, 0, sizeof(st));
  st.st_mode = mode;
  st.st_size = size;
  return st;
}

} // namespace

using namespace dwarfs;

class compat_metadata : public testing::TestWithParam<std::string> {};

TEST_P(compat_metadata, backwards_compat) {
  std::ostringstream oss;
  stream_logger lgr(oss);
  auto filename =
      std::string(TEST_DATA_DIR "/compat-v") + GetParam() + ".dwarfs";
  filesystem_v2 fs(lgr, std::make_shared<mmap>(filename));
  auto meta = fs.metadata_as_dynamic();
  auto ref = folly::parseJson(reference);
  EXPECT_EQ(ref, meta);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, compat_metadata,
                         ::testing::ValuesIn(versions));

class compat_filesystem
    : public testing::TestWithParam<std::tuple<std::string, bool>> {};

TEST_P(compat_filesystem, backwards_compat) {
  auto [version, enable_nlink] = GetParam();

  std::ostringstream oss;
  stream_logger lgr(oss);
  auto filename = std::string(TEST_DATA_DIR "/compat-v") + version + ".dwarfs";

  filesystem_options opts;
  opts.metadata.enable_nlink = enable_nlink;

  filesystem_v2 fs(lgr, std::make_shared<mmap>(filename), opts);

  struct ::statvfs vfsbuf;
  fs.statvfs(&vfsbuf);

  EXPECT_EQ(1048576, vfsbuf.f_bsize);
  EXPECT_EQ(1, vfsbuf.f_frsize);
  EXPECT_EQ(4240, vfsbuf.f_blocks);
  EXPECT_EQ(12, vfsbuf.f_files);
  EXPECT_EQ(ST_RDONLY, vfsbuf.f_flag);
  EXPECT_GT(vfsbuf.f_namemax, 0);

  auto json = fs.serialize_metadata_as_json(true);
  EXPECT_GT(json.size(), 1000) << json;

  std::ostringstream dumpss;
  fs.dump(dumpss, 9);
  EXPECT_GT(dumpss.str().size(), 1000) << dumpss.str();

  auto entry = fs.find("/format.sh");
  struct ::stat st;

  ASSERT_TRUE(entry);
  EXPECT_EQ(0, fs.getattr(*entry, &st));
  EXPECT_EQ(94, st.st_size);
  EXPECT_EQ(S_IFREG | 0755, st.st_mode);
  EXPECT_EQ(1000, st.st_uid);
  EXPECT_EQ(100, st.st_gid);
  EXPECT_EQ(1606161908 + 1007022, st.st_atime);
  EXPECT_EQ(1606161908 + 94137, st.st_mtime);
  EXPECT_EQ(1606161908 + 94137, st.st_ctime);

  EXPECT_EQ(0, fs.access(*entry, R_OK, 1000, 0));

  auto inode = fs.open(*entry);
  EXPECT_GE(inode, 0);

  std::vector<char> buf(st.st_size);
  auto rv = fs.read(inode, &buf[0], st.st_size, 0);
  EXPECT_EQ(rv, st.st_size);
  EXPECT_EQ(format_sh, std::string(buf.begin(), buf.end()));

  entry = fs.find("/foo/bad");
  ASSERT_TRUE(entry);
  std::string link;
  EXPECT_EQ(fs.readlink(*entry, &link), 0);
  EXPECT_EQ(link, "../foo");

  entry = fs.find(0, "foo");
  ASSERT_TRUE(entry);

  auto dir = fs.opendir(*entry);
  ASSERT_TRUE(dir);
  EXPECT_EQ(5, fs.dirsize(*dir));

  std::vector<std::string> names;
  for (size_t i = 0; i < fs.dirsize(*dir); ++i) {
    auto r = fs.readdir(*dir, i);
    ASSERT_TRUE(r);
    auto [view, name] = *r;
    names.emplace_back(name);
  }

  std::vector<std::string> expected{
      ".", "..", "bad", "bar", "bla.sh",
  };

  EXPECT_EQ(expected, names);

  std::map<std::string, struct ::stat> ref_entries{
      {"", make_stat(S_IFDIR | 0755, 8)},
      {"bench.sh", make_stat(S_IFREG | 0644, 1517)},
      {"dev", make_stat(S_IFDIR | 0755, 0)},
      {"empty", make_stat(S_IFDIR | 0755, 1)},
      {"empty/alsoempty", make_stat(S_IFDIR | 0755, 0)},
      {"foo", make_stat(S_IFDIR | 0755, 3)},
      {"foo/bad", make_stat(S_IFLNK | 0777, 6)},
      {"foo/bar", make_stat(S_IFREG | 0644, 0)},
      {"foo/bla.sh", make_stat(S_IFREG | 0644, 1517)},
      {"foobar", make_stat(S_IFLNK | 0777, 7)},
      {"format.sh", make_stat(S_IFREG | 0755, 94)},
      {"perl-exec.sh", make_stat(S_IFREG | 0644, 87)},
      {"test.py", make_stat(S_IFREG | 0644, 1012)},
  };

  for (auto mp : {&filesystem_v2::walk, &filesystem_v2::walk_inode_order}) {
    std::map<std::string, struct ::stat> entries;
    std::vector<int> inodes;

    (fs.*mp)([&](dir_entry_view e) {
      struct ::stat stbuf;
      ASSERT_EQ(0, fs.getattr(e.inode(), &stbuf));
      inodes.push_back(stbuf.st_ino);
      EXPECT_TRUE(entries.emplace(e.path(), stbuf).second);
    });

    EXPECT_EQ(entries.size(), ref_entries.size());

    for (auto const& [p, st] : entries) {
      auto it = ref_entries.find(p);
      EXPECT_TRUE(it != ref_entries.end()) << p;
      if (it != ref_entries.end()) {
        EXPECT_EQ(it->second.st_mode, st.st_mode) << p;
        EXPECT_EQ(1000, st.st_uid) << p;
        EXPECT_EQ(100, st.st_gid) << p;
        EXPECT_EQ(it->second.st_size, st.st_size) << p;
      }
    }

    if (mp == &filesystem_v2::walk_inode_order) {
      EXPECT_TRUE(std::is_sorted(inodes.begin(), inodes.end()));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(dwarfs, compat_filesystem,
                         ::testing::Combine(::testing::ValuesIn(versions),
                                            ::testing::Bool()));

class rewrite
    : public testing::TestWithParam<std::tuple<std::string, bool, bool>> {};

TEST_P(rewrite, filesystem_rewrite) {
  auto [version, recompress_block, recompress_metadata] = GetParam();

  std::ostringstream oss;
  stream_logger lgr(oss);
  auto filename = std::string(TEST_DATA_DIR "/compat-v") + version + ".dwarfs";

  rewrite_options opts;
  opts.recompress_block = recompress_block;
  opts.recompress_metadata = recompress_metadata;

  worker_group wg("rewriter", 2);
  block_compressor bc("null");
  progress prog([](const progress&, bool) {}, 1000);
  std::ostringstream rewritten, idss;
  filesystem_writer fsw(rewritten, lgr, wg, prog, bc, 64 << 20);
  filesystem_v2::identify(lgr, std::make_shared<mmap>(filename), idss);
  filesystem_v2::rewrite(lgr, prog, std::make_shared<mmap>(filename), fsw,
                         opts);

  filesystem_v2::identify(
      lgr, std::make_shared<test::mmap_mock>(rewritten.str()), idss);
  filesystem_v2 fs(lgr, std::make_shared<test::mmap_mock>(rewritten.str()));
  auto meta = fs.metadata_as_dynamic();
  auto ref = folly::parseJson(reference);
  EXPECT_EQ(ref, meta);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, rewrite,
                         ::testing::Combine(::testing::ValuesIn(versions),
                                            ::testing::Bool(),
                                            ::testing::Bool()));
