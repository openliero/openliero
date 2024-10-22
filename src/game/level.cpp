#include "level.hpp"
#include <cstring>
#include <random>
#include "filesystem.hpp"
#include "game.hpp"
#include "gfx.hpp"
#include "gfx/color.hpp"

void Level::generateDirtPattern(Common& common, std::mt19937& rand) {
  resize(504, 350);

  // segfault occurs when these are placed before resize() call above,
  // because at that point, both height/width are zero.
  std::uniform_int_distribution<int> distHeight(0, height - 1);
  std::uniform_int_distribution<int> distWidth(0, width - 1);

  setPixel(0, 0, std::uniform_int_distribution<int>(12, 18)(rand), common);

  for (int y = 1; y < height; ++y)
    setPixel(
        0, y,
        (std::uniform_int_distribution<int>(12, 18)(rand) + pixel(0, y - 1)) >>
            1,
        common);

  for (int x = 1; x < width; ++x)
    setPixel(
        x, 0,
        (std::uniform_int_distribution<int>(12, 18)(rand) + pixel(x - 1, 0)) >>
            1,
        common);

  for (int y = 1; y < height; ++y)
    for (int x = 1; x < width; ++x) {
      setPixel(
          x, y,
          (pixel(x - 1, y) + pixel(x, y - 1) +
           std::uniform_int_distribution<int>(12, 19)(rand)) /
              3,
          common);
    }

  // TODO: Optimize the following

  int count = std::uniform_int_distribution<int>(0, 99)(rand);

  for (int i = 0; i < count; ++i) {
    int x = distWidth(rand) - 8;
    int y = distHeight(rand) - 8;

    int temp = std::uniform_int_distribution<int>(69, 72)(rand);

    const PalIdx* image = common.largeSprites.spritePtr(temp);

    for (int cy = 0; cy < 16; ++cy) {
      int my = cy + y;
      if (my >= height)
        break;

      if (my < 0)
        continue;

      for (int cx = 0; cx < 16; ++cx) {
        int mx = cx + x;
        if (mx >= width)
          break;

        if (mx < 0)
          continue;

        PalIdx srcPix = image[(cy << 4) + cx];
        if (srcPix > 0) {
          PalIdx pix = pixel(mx, my);
          if (pix > 176 && pix < 180)
            setPixel(mx, my, (srcPix + pix) / 2, common);
          else
            setPixel(mx, my, srcPix, common);
        }
      }
    }
  }

  count = std::uniform_int_distribution<int>(0, 14)(rand);

  for (int i = 0; i < count; ++i) {
    int x = distWidth(rand) - 8;
    int y = distHeight(rand) - 8;

    int which = std::uniform_int_distribution<int>(56, 59)(rand);

    blitStone(common, *this, false, common.largeSprites.spritePtr(which), x, y);
  }
}

bool isNoRock(const Common& common, Level& level, int size, int x, int y) {
  gvl::rect rect(x, y, x + size + 1, y + size + 1);

  rect.intersect(gvl::rect(0, 0, level.width, level.height));

  for (int y = rect.y1; y < rect.y2; ++y)
    for (int x = rect.x1; x < rect.x2; ++x) {
      if (level.mat(x, y).rock())
        return false;
    }

  return true;
}

void Level::generateRandom(
    Common& common,
    Settings const& settings,
    std::mt19937& rand) {
  origpal.resetPalette(common.exepal, settings);

  generateDirtPattern(common, rand);

  std::uniform_int_distribution<int> distHeight(0, height - 1);
  std::uniform_int_distribution<int> distWidth(0, width - 1);

  int count = std::uniform_int_distribution<int>(5, 49)(rand);

  for (int i = 0; i < count; ++i) {
    int cx = distWidth(rand) - 8;
    int cy = distHeight(rand) - 8;

    int dx = std::uniform_int_distribution<int>(-5, 5)(rand);
    int dy = std::uniform_int_distribution<int>(-2, 2)(rand);

    int count2 = std::uniform_int_distribution<int>(0, 11)(rand);

    for (int j = 0; j < count2; ++j) {
      int count3 = std::uniform_int_distribution<int>(0, 4)(rand);

      for (int k = 0; k < count3; ++k) {
        cx += dx;
        cy += dy;
        drawDirtEffect(
            common, rand, *this, 1, cx,
            cy);  // TODO: Check if it really should be dirt effect 1
      }

      cx -=
          (count3 + 1) * dx;  // TODO: Check if it really should be (count3 + 1)
      cy -=
          (count3 + 1) * dy;  // TODO: Check if it really should be (count3 + 1)

      cx += std::uniform_int_distribution<int>(-3, 3)(rand);
      cy += std::uniform_int_distribution<int>(-7, 7)(rand);
    }
  }

  count = std::uniform_int_distribution<int>(5, 19)(rand);
  for (int i = 0; i < count; ++i) {
    int cx, cy;
    do {
      cx = distWidth(rand) - 16;

      if (std::uniform_int_distribution<int>(0, 3)(rand) == 0)
        cy = height - 1 - std::uniform_int_distribution<int>(0, 19)(rand);
      else
        cy = distHeight(rand) - 16;
    } while (!isNoRock(common, *this, 32, cx, cy));

    int rock = std::uniform_int_distribution<int>(0, 2)(rand);

    blitStone(
        common, *this, false, common.largeSprites.spritePtr(stoneTab[rock][0]),
        cx, cy);
    blitStone(
        common, *this, false, common.largeSprites.spritePtr(stoneTab[rock][1]),
        cx + 16, cy);
    blitStone(
        common, *this, false, common.largeSprites.spritePtr(stoneTab[rock][2]),
        cx, cy + 16);
    blitStone(
        common, *this, false, common.largeSprites.spritePtr(stoneTab[rock][3]),
        cx + 16, cy + 16);
  }

  count = std::uniform_int_distribution<int>(5, 29)(rand);

  for (int i = 0; i < count; ++i) {
    int cx, cy;
    do {
      cx = distWidth(rand) - 8;

      if (std::uniform_int_distribution<int>(0, 4)(rand) == 0)
        cy = height - 1 - std::uniform_int_distribution<int>(0, 12)(rand);
      else
        cy = distHeight(rand) - 8;
    } while (!isNoRock(common, *this, 15, cx, cy));

    blitStone(
        common, *this, false,
        common.largeSprites.spritePtr(
            std::uniform_int_distribution<int>(3, 8)(rand)),
        cx, cy);
  }
}

void Level::makeShadow(const Common& common) {
  for (int x = 0; x < width - 3; ++x)
    for (int y = 3; y < height; ++y) {
      if (mat(x, y).seeShadow() && mat(x + 3, y - 3).dirtRock()) {
        setPixel(x, y, pixel(x, y) + 4, common);
      }

      if (pixel(x, y) >= 12 && pixel(x, y) <= 18 && mat(x + 3, y - 3).rock()) {
        setPixel(x, y, pixel(x, y) - 2, common);
        if (pixel(x, y) < 12)
          setPixel(x, y, 12, common);
      }
    }

  for (int x = 0; x < width; ++x) {
    if (mat(x, height - 1).background()) {
      setPixel(x, height - 1, 13, common);
    }
  }
}

void Level::resize(int width_new, int height_new) {
  width = width_new;
  height = height_new;
  data.resize(width * height);
  materials.resize(width * height);
}

bool Level::load(
    const Common& common,
    Settings const& settings,
    gvl::octet_reader r) {
  resize(504, 350);

  // std::size_t len = f.len;
  bool resetPalette = true;

  r.get(reinterpret_cast<uint8_t*>(&data[0]), width * height);

  if (/*len >= 504*350 + 10 + 256*3
   &&*/
      (settings.extensions && settings.loadPowerlevelPalette)) {
    uint8_t buf[10] = {};
    if (r.try_get(buf, 10)) {
      if (!std::memcmp("POWERLEVEL", buf, 10)) {
        Palette pal;
        pal.read(r);
        origpal.resetPalette(pal, settings);

        resetPalette = false;
      }
    }
  }

  for (std::size_t i = 0; i < data.size(); ++i)
    materials[i] = common.materials[data[i]];

  if (resetPalette)
    origpal.resetPalette(common.exepal, settings);

  return true;
}

void Level::generateFromSettings(
    Common& common,
    Settings const& settings,
    std::mt19937& rand) {
  if (settings.randomLevel) {
    generateRandom(common, settings, rand);
  } else {
    std::string path = settings.levelFile;
    if (path.find('.', 0) == std::string::npos)
      path += ".LEV";

    bool loaded = false;
    try {
      loaded = load(common, settings, FsNode(path).toOctetReader());
    } catch (std::runtime_error&) {
      // Ignore
    }

    if (!loaded)
      generateRandom(common, settings, rand);
  }

  oldRandomLevel = settings.randomLevel;
  oldLevelFile = settings.levelFile;

  if (settings.shadow) {
    makeShadow(common);
  }
}

using std::vector;

inline bool free(Material m) {
  return m.background() || m.anyDirt();
}

bool Level::selectSpawn(
    std::mt19937& rand,
    int w,
    int h,
    gvl::ivec2& selected) {
  vector<int> vruns(width - w + 1);
  vector<int> vdists(width - w + 1);

  Material* m = &materials[0];

  uint32_t i = 0;

  for (int y = 0; y < height; ++y) {
    int hrun = 0;
    int filled = 0;

    for (int x = 0; x < width; ++x) {
      if (free(*m)) {
        ++hrun;
      } else {
        hrun = 0;
        ++filled;
      }
      ++m;

      int cx = x - (w - 1);
      if (cx < 0)
        continue;

      int& vrun = vruns[cx];
      int& vdist = vdists[cx];

      if (hrun >= w) {
        if (vdist > 0) {
          vrun = 0;
          vdist = 0;
        }
        ++vrun;
      } else {
        if (vrun >= h && vdist <= 8 && filled > w / 4) {
          // We have a supported square at (x + 1 - w, y - h)
          ++i;
          if (std::uniform_int_distribution<int>(0, i - 1)(rand) < 1) {
            selected.x = cx;
            selected.y = y - h;
          }
        }
        ++vdist;
      }

      filled -= !free(m[-w]);
    }
  }

  return i > 0;
}

void Level::drawMiniature(Bitmap& dest, int mapX, int mapY, int step) {
  int my = step / 2;

  int mapEndY = mapY + ((height + step / 2) / step);
  int mapEndX = mapX + ((width + step / 2) / step);

  for (int y = mapY; y < mapEndY; ++y) {
    int mx = step / 2;
    for (int x = mapX; x < mapEndX; ++x) {
      dest.getPixel(x, y) = checkedPixelWrap(mx, my);
      mx += step;
    }
    my += step;
  }
}
