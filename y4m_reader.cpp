#include "y4m_reader.h"
#include <future>
#include <iostream>

YUVFrame::YUVFrame(unsigned width, unsigned height)
    : m_width(width), m_height(height) {
  m_data.resize(width * height);
}

YUV &YUVFrame::pixel(unsigned x, unsigned y) {
  if (x >= m_width || y >= m_height)
    throw std::runtime_error("out of range");
  return m_data[y * m_width + x];
}

YUV const &YUVFrame::pixel(unsigned x, unsigned y) const {
  if (x >= m_width || y >= m_height)
    throw std::runtime_error("out of range");
  return m_data[y * m_width + x];
}

namespace {

YUVFrame decode_frame(std::span<char> rawdata, unsigned width,
                      unsigned height) {
  YUVFrame frame{width, height};
  auto u_offset = width * height;
  auto v_offset = u_offset + (width * height) / 4;
  for (auto j = 0ull; j < height; ++j)
    for (auto k = 0ull; k < width; ++k) {
      YUV pixel;
      // Read pixel from y u v planes.
      pixel.y = rawdata[j * width + k];
      pixel.u = rawdata[u_offset + (j / 2) * width / 2 + (k / 2)];
      pixel.v = rawdata[v_offset + (j / 2) * width / 2 + (k / 2)];
      frame.pixel(k, j) = pixel;
    }

  return frame;
}

} // namespace
YUVFile::YUVFile(std::vector<char> &&rawdata, unsigned width, unsigned height)
    : m_width(width), m_height(height) {
  unsigned long long frame_size = (width * height * 3) / 2;
  if (rawdata.size() % frame_size != 0)
    throw std::runtime_error("YUVFile data size mismatch");
  auto frames = rawdata.size() / frame_size;
  auto u_offset = width * height;
  auto v_offset = u_offset + (width * height) / 4;
  // Decode frame asyncronously.
  std::vector<std::future<YUVFrame>> decodedFrames;
  for (auto i = 0ull; i < frames; i++) {
    auto frame_offset = i * frame_size;
    decodedFrames.emplace_back(
        std::async(std::launch::async, &decode_frame,
                   std::span<char>{rawdata.data() + frame_offset, frame_size},
                   width, height));
  }

  for (auto &decoded : decodedFrames) {
    m_data.emplace_back(decoded.get());
  }
}

namespace {

void encode_planes(YUVFrame const &frame, std::span<char> buffer) {
  if (buffer.size() != (frame.width() * frame.height() * 3) / 2)
    throw std::runtime_error("encode_planes() got wrong buffer size");

  auto u_offset = frame.width() * frame.height();
  auto v_offset = u_offset + (frame.width() * frame.height()) / 4;

  for (int i = 0u; i < frame.height(); ++i)
    for (int j = 0u; j < frame.width(); ++j) {
      auto pix = frame.pixel(j, i);
      // encode pixel to planes.
      // u and v are subsampled.
      // For simplicity only one sample of 4 uv samples
      // is considered.
      buffer[i * frame.width() + j] = pix.y;
      buffer[u_offset + (i / 2) * frame.width() / 2 + j / 2] = pix.u;
      buffer[v_offset + (i / 2) * frame.width() / 2 + j / 2] = pix.v;
    }
}

} // namespace
std::vector<char> YUVFrame::encode_planes() const {
  std::vector<char> encoded;
  encoded.resize((width() * height() * 3) / 2);

  auto u_offset = width() * height();
  auto v_offset = u_offset + (width() * height()) / 4;

  for (int i = 0u; i < height(); ++i)
    for (int j = 0u; j < width(); ++j) {
      auto pix = pixel(j, i);
      // encode pixel to planes.
      // u and v are subsampled.
      // For simplicity only one sample of 4 uv samples
      // is considered.
      encoded.at(i * width() + j) = pix.y;
      encoded.at(u_offset + (i / 2) * width() / 2 + j / 2) = pix.u;
      encoded.at(v_offset + (i / 2) * width() / 2 + j / 2) = pix.v;
    }
  return encoded;
}

YUVFile::YUVFile(unsigned width, unsigned height)
    : m_width(width), m_height(height) {}

void YUVFile::add_frame(YUVFrame &&frame) {
  if (frame.width() != m_width || frame.height() != m_height)
    throw std::runtime_error("New frame dims mismatch");
  m_data.emplace_back(std::move(frame));
}

YUVFile Y4MReader::read(std::ifstream &file) {
  // 1. Header processing.
  std::string header;
  // YUV4MPEG2
  file >> header;

  if (!file)
    throw std::runtime_error("Error while reading file");

  if (header != "YUV4MPEG2")
    throw std::runtime_error("Wrong file format. Expected y4m.");

  std::string width, height;

  // W<width>
  file >> width;
  if (!file)
    throw std::runtime_error("Error while reading file");
  if (width[0] != 'W')
    throw std::runtime_error("y4m header broken. Expected 'W'");
  width.erase(0, 1);
  auto width_int = std::stoi(width);

  // H<height>
  file >> height;
  if (!file)
    throw std::runtime_error("Error while reading file");
  if (height[0] != 'H')
    throw std::runtime_error("y4m header broken. Expected 'H'");
  height.erase(0, 1);
  auto height_int = std::stoi(height);

  // F<framerate>
  std::string framerate;
  file >> framerate;
  if (!file)
    throw std::runtime_error("Error while reading file");
  if (framerate[0] != 'F')
    throw std::runtime_error("y4m header broken. Expected 'F'");

  // C<colorspace>
  std::string colorspace;

  do {
    file >> colorspace;
    if (!file)
      throw std::runtime_error("Error while reading file");
  } while (colorspace[0] != 'C');

  if (colorspace != "C420mpeg2")
    throw std::runtime_error(
        "Unsupported colorspace: only supporting is C420mpeg2");

  std::string frame;

  // Look up first 'FRAME' token.
  do {
    file >> frame;
    if (!file)
      throw std::runtime_error("Error while reading file");
  } while (frame != "FRAME");

  size_t frame_size = (width_int * height_int * 3) / 2;

  std::vector<char> data;
  data.reserve(frame_size * 1000u);

  // 2. Read frames.
  do {
    file.seekg(1, std::ios::cur);
    auto current_end = data.size();
    data.resize(data.size() + frame_size);
    for (auto i = 0ul; i < frame_size; ++i)
      file.get(data.data()[current_end + i]);
    if (!file)
      throw std::runtime_error("Error while reading file");
    if (file.eof())
      break;
    file >> frame;
    if (file.bad())
      throw std::runtime_error("Error while reading file");
    if (frame != "FRAME")
      throw std::runtime_error("Error while reading file");
  } while (!file.eof() && !file.bad());

  if (file.bad())
    throw std::runtime_error("Error reading file");

  return YUVFile(std::move(data), width_int, height_int);
}

YUVFile Y4MReader::read(std::filesystem::path const &filename) {
  auto file = std::ifstream(filename);

  if (!file)
    throw std::runtime_error("Could not open " + std::string(filename));

  return read(file);
}

void Y4MReader::save(YUVFile &video, std::filesystem::path filename) {

  auto file = std::ofstream(filename);

  if (!file)
    throw std::runtime_error("Could not open file '" + std::string(filename) +
                             "' for writing.");

  // Encode header.
  file << "YUV4MPEG2 "
       << "W" << video.width() << " H" << video.height() << " F30:1"
       << " C420mpeg2" << std::endl;

  if (!file)
    throw std::runtime_error("Error outputing to file.");

  // Encode frames.
  std::vector<char> buffer;
  auto frame_size = (video.width() * video.height() * 3) / 2;
  buffer.resize(video.frames() * frame_size);
  std::vector<std::future<void>> codedFrames;
  for (auto i = 0u; i < video.frames(); ++i) {
    codedFrames.emplace_back(std::async(
        std::launch::async, &encode_planes, std::ref(video[i]),
        std::span<char>{buffer.data() + i * frame_size, frame_size}));
  }

  for (auto i = 0u; i < video.frames(); ++i) {
    file << "FRAME" << std::endl;
    codedFrames.at(i).get();
    file.write(buffer.data() + i * frame_size, frame_size);

    if (!file)
      throw std::runtime_error("Error outputing to file.");
  }
}

Y4MReader::Y4MReader() {}
Y4MReader::~Y4MReader() {}
