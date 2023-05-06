#include "y4m_reader.h"
#include <cassert>
#include <future>
#include <iostream>

YUVFrame get_transformed_frame(YUVFile const &orig, unsigned index) {
  auto new_width = orig.frames();
  if (new_width % 2)
    new_width -= 1;
  auto new_height = orig.height();

  YUVFrame frame{new_width, new_height};
  for (auto j = 0u; j < new_height; j++)
    for (auto k = 0u; k < new_width; k++)
      frame.pixel(k, j) = orig[k].pixel(index, j);

  return frame;
}

// Swaps time and horizantal dimesions of
// yuv coded videofile.
YUVFile swap_dims(YUVFile const &file) {
  auto new_width = file.frames();
  if (new_width % 2)
    new_width -= 1;
  auto new_height = file.height();
  auto new_frames = file.width();

  YUVFile ret{new_width, new_height};

  std::vector<std::future<YUVFrame>> frameTransforms;

  for (auto i = 0u; i < new_frames; ++i) {
    frameTransforms.emplace_back(std::async(
        std::launch::async, &get_transformed_frame, std::ref(file), i));
  }

  for (auto i = 0u; i < new_frames; ++i) {
    ret.add_frame(std::move(frameTransforms.at(i).get()));
  }

  return ret;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " + std::string(argv[0]) + " <filename>" << std::endl;
    return 1;
  }

  auto input_filename = std::filesystem::path(argv[1]);

  Y4MReader reader;

  auto yuv = reader.read(input_filename);

  assert(yuv.frames() > 0);

  std::cout << "File read complete" << std::endl;

  auto transformed = swap_dims(yuv);

  std::cout << "File transform complete" << std::endl;

  // Output to <input filename>.1
  auto output_filename = std::filesystem::path(std::string(argv[1]) + ".1");
  reader.save(transformed, output_filename);

  std::cout << "Done!" << std::endl;
}
