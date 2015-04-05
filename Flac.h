#ifndef FLAC_H_
#define FLAC_H_

#include <cstdint>
#include <vector>

class Flac {
public:
  // streaminfo metadata block. Should this go into its own class?
  uint16_t min_block_length;
  uint16_t max_block_length;
  uint32_t min_frame_size;  // 24 bits
  uint32_t max_frame_size;  // 24 bits
  bool variable_blocksize;

  uint32_t sample_rate;  // 20 bits
  uint8_t n_channels;
  uint8_t bits_per_sample;
  uint64_t n_samples;
  uint64_t md5sum_a;
  uint64_t md5sum_b;

  uint32_t application_id;
  // seek table
  // comments
  // cuesheet
  // picture

  class Frame {
    enum BlockSize {
      RESERVED,
      _192,
      _576,
      GET_8BIT_FROM_HEADER,
      GET_16BIT_FROM_HEADER,
      _256
    };

   public:
    static const bool FIXED_BLOCKSIZE = false;
    static const bool VARIABLE_BLOCKSIZE = true;

    uint16_t sync_code;  // 14 bit
    bool reserve_bit1;  // mandatory 0
    bool blocking_strategy;
    uint8_t blocksize_code;  // in inter-channel samples
    uint8_t sample_rate_code;
    uint8_t channel_assignment;
    uint8_t sample_size_code;  // in bits
    bool reserve_bit2; // mandatory 0

    union {
      uint64_t sample_number;
      uint32_t frame_number;
    } sf;

    uint8_t crc8;

    uint16_t blocksize;
    uint32_t sample_rate;
    uint16_t n_channels;
    void print_header(std::ostream &log);
  };

  Frame frame;
  //std::vector<Frame> frames;
  void reset_metadata();
};

#endif
