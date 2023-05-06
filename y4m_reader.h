#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

class YUVFile;

struct YUV {
  char y;
  char u;
  char v;
};

class YUVFrame {
public:
  // Class holds a matrix of YUV pixels.

  // Constructs YUVFrame with [width, height] dimensions.
  YUVFrame(unsigned width, unsigned height);

  unsigned width() const { return m_width; }

  unsigned height() const { return m_height; }

  // Encode frame using (4:2:0) format
  // Y plane goes first
  // U plane second
  // V third
  std::vector<char> encode_planes() const;

  YUV &pixel(unsigned x, unsigned y);

  YUV const &pixel(unsigned x, unsigned y) const;

private:
  std::vector<YUV> m_data;
  unsigned m_width;
  unsigned m_height;
};

class YUVFile {
public:
  // Class holds instances YUVFrames with same width and height.
  // This sequence represents a video coded in YUV.

  // Constructs frames from (4:2:0) planes.
  YUVFile(std::vector<char> &&rawdata, unsigned width, unsigned height);

  // Creates empty frame with expected width and height.
  YUVFile(unsigned width, unsigned height);

  // Adds extra frame to end of file.
  // If new frame has incompatible width or height
  // exception is thrown.
  void add_frame(YUVFrame &&frame);

  auto width() const { return m_width; }

  auto height() const { return m_height; }

  auto frames() const { return m_data.size(); }

  YUVFrame &operator[](unsigned id) { return m_data.at(id); }

  YUVFrame const &operator[](unsigned id) const { return m_data.at(id); }

private:
  std::vector<YUVFrame> m_data;
  unsigned m_width;
  unsigned m_height;
};

class Y4MReader final {
public:
  Y4MReader();
  ~Y4MReader();

  // Constructs YUVFile from Y4M file.
  YUVFile read(std::ifstream &file);
  // Opens 'filename' as file and calls read(file)
  YUVFile read(std::filesystem::path const &filename);

  // Saves YUVFile to disk.
  void save(YUVFile &video, std::filesystem::path filename);
};
