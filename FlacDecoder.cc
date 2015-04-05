#include <iomanip>
#include <bitset>
#include "FlacDecoder.h"

using namespace std;

#define ONES(n) ((1UL << (n))-1)

FlacDecoder::FlacDecoder(Verbosity v) : verbosity(v) {
  flac = make_shared<Flac>();
  flac->reset_metadata();
}

FlacDecoder::FlacDecoder(Verbosity v, shared_ptr<Flac> f) : flac(f) {
  flac->reset_metadata();
}

void Flac::reset_metadata() {
  min_block_length = 0;
  max_block_length = 0;
  variable_blocksize = false;
  min_frame_size = 0;
  max_frame_size = 0;

  sample_rate = 0;
  n_channels = 0;
  bits_per_sample = 0;
  n_samples = 0;
  md5sum_a = 0;
  md5sum_b = 0;

  application_id = 0;
  // seek table, comments, cuesheet, picture
};

template<class T>
static bool read_into_buf(istream &in, int n_bytes, T *buf) {
  char *buf_raw = reinterpret_cast<char*>(buf);
  for (int i=0; i<n_bytes; i++) {
    if (in.get(buf_raw[i]) == 0)
      return false;
  }
  return true;
}

// reads n_bytes worth of big-endian-encoded bytes; 8*n_bytes bits in total
template<class T>
static bool read_into(istream &in, int n_bytes, T &dest) {
  dest = 0;
  for (int i=0; i<n_bytes; i++) {  // read three bytes to form an int
    char c;
    if (in.get(c) == 0)
      return false;
    dest = (dest << 8) | (c & ONES(8));
  }
//#define BYTEREADER_VERBOSE
#ifdef BYTEREADER_VERBOSE
  cerr << "Read " << n_bytes << " bytes: ";
  cerr << setw(2*n_bytes) << setfill('0') << hex << +dest << endl;
#endif
  return true;
}

template<class T>
static bool read_into_utf8(istream &in, T &dest) {
  dest = 0;
  char c;
  int extra_bytes = 0;
  if (in.get(c) == 0) {
    return false;  // unexpected end of input
  }
  // 0b 0111 1111
  // 0b 1100 1111
  // 0b 1110 1111
  if (!(0x80 & c)) {  // only one byte
    dest |= c;
  } else {
    char pat;
    extra_bytes = 1;
    cout << "argh" << hex << setw(2) << ((+c) & 0xFF) << endl;
    //                                     this bit is the first zero
    for (pat=0xE0; ((pat<<1) ^ c) & pat; pat >>= 1) {
      extra_bytes++;
    }
    dest |= (c & ~pat);
    for (int b=0; b<extra_bytes; b++) {
      if (in.get(c) == 0)
        return false;
      dest = (dest << 6) | (0x3F & c);
    }
  }

#if 0
//#define BYTEREADER_VERBOSE
#ifdef BYTEREADER_VERBOSE
  cerr << "Read " << n_bytes << " bytes: ";
  cerr << setw(2*n_bytes) << setfill('0') << hex << +dest << endl;
#endif
#endif
  return true;
}

/*
 * streaminfo     mandatory; always first
 * application    32-bit identifier for flac encoder
 * padding
 * seektable      each seek point takes 18 bytes
 * vorbis_comment human-readable name/value pairs in UTF-8. For tags.
 *                http://xiph.org/vorbis/doc/v-comment.html
 * cuesheet       track and index points
 * picture        usually album art
 * */
bool FlacDecoder::parse_metadata(istream &in, ostream &log) {
  bool last_metadata_block_flag = false;
  uint8_t metadata_block_header;
  uint8_t block_type;
  uint32_t block_length;
  uint64_t tmp;

  {  // streaminfo block is mandatory and must be first
    read_into(in, 1, metadata_block_header);

    last_metadata_block_flag = (1 << 7) & metadata_block_header;
    block_type = metadata_block_header & ONES(7);

    read_into(in, 3, block_length);

    if (block_type != 0) {
      log << "Expected STREAMINFO metadata block to be first. Invalid FLAC. "
              "Exiting..." << endl;
      return false;
    } else {
      if (verbosity) {
        log << "STREAMINFO metadata block with length " << block_length << endl;
      }
      read_into(in, 2, flac->min_block_length);
      read_into(in, 2, flac->max_block_length);

      if (flac->min_block_length != flac->max_block_length) {
        flac->variable_blocksize = true;
      }
      read_into(in, 3, flac->min_frame_size);
      read_into(in, 3, flac->max_frame_size);
      // 20 + 3 + 5 + 36
      // sample rate, channels-1, bits per sample-1, total samples
      read_into(in, 8, tmp);
      flac->sample_rate = (tmp >> (3+5+36)) & ONES(20);  // 20 bits
      if (flac->sample_rate == 0) {
        log << "Sample rate of 0 is invalid! Exiting..." << endl;
        return false;
      }
      flac->n_channels = ((tmp >> (5+36)) & ONES(3)) + 1;  // 3 bits
      flac->bits_per_sample = ((tmp >> (36)) & ONES(5)) + 1;  // 5 bits
      if (4 > flac->bits_per_sample || 32 < flac->bits_per_sample) {
        log << "Warning: Invalid number of bits per sample: ";
        log << +flac->bits_per_sample << endl;
      }
      flac->n_samples = tmp & ONES(36);  // 36 bits
      read_into(in, 8, flac->md5sum_a);
      read_into(in, 8, flac->md5sum_b);
    }
    if (verbosity) {
      log << "min/max block length (in samples): " << dec;
      log << flac->min_block_length << "/" << flac->max_block_length << endl;

      log << "min/max frame size: (in bytes)" << dec;
      log << flac->min_frame_size << "/" << flac->max_frame_size << endl;

      log << "sample rate (Hz): " << dec << flac->sample_rate << endl;
      log << "bits per sample: " << dec << +flac->bits_per_sample << endl;

      log << "samples in stream: " << dec << flac->n_samples << endl;

      log << "md5sum of decoded audio: " << hex;
      log << flac->md5sum_a << flac->md5sum_b << endl;
    }
  }

  while (last_metadata_block_flag == false) {
    read_into(in, 1, metadata_block_header);

    last_metadata_block_flag = (1 << 7) & metadata_block_header;
    block_type = metadata_block_header & ONES(7);
    read_into(in, 3, block_length);
    if (verbosity && last_metadata_block_flag)
      log << "The next metadata block is the last one." << endl;
    switch(block_type) {
      case 0: {  // double-copy of STREAMINFO
        if (verbosity) {
          log << "Warning: Input has two STREAMINFO metadata blocks. "
                  "Ignoring the second one..." << endl;
        }
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
      case 1: {  // padding
        if (verbosity)
          log << "Padding block of size " << dec << block_length << endl;
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
      case 2: {  // application
        read_into(in, 4, flac->application_id);
        if (verbosity) {
          log << "Application block (ID 0x" << hex << +flac->application_id;
          log << " of size " << dec << block_length << endl;
        }
        for (int i=0; i<(block_length-4); i++)
          in.get();
        break;
      }
      case 3: {  // seek table
        // TODO
        if (verbosity) {
          log << "Seek table block of size " << dec << block_length;
          log << " (" << block_length/18 << " seekpoints)" << endl;
        }
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
      case 4: {  // vorbis_comment
        // TODO
        if (verbosity)
          log << "Vorbis comment block of size " << dec << block_length << endl;
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
      case 5: {  // cuesheet
        // TODO
        if (verbosity)
          log << "Cuesheet block of size " << dec << block_length << endl;
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
      case 6: {  // picture
        // TODO
        if (verbosity)
          log << "Picture block of size " << dec << block_length << endl;
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
      case 127:
      {
        log << "Warning: Block type 127 is invalid. Skipping..." << endl;
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
      default:
      {
        log << "Warning: Did not recognize metadata block with type ";
        log << +block_type;
        log << ". Skipping next " << dec << block_length << " bytes..." << endl;
        for (int i=0; i<block_length; i++)
          in.get();
        break;
      }
    }
  }

  if (verbosity) {
    log << "Done parsing metadata." << endl;
  }
  return true;
}

static uint16_t get_channels(uint8_t channel_assignment) {
  if (channel_assignment <= 0x07) {
    return channel_assignment+1;
  } else if (channel_assignment <= 0x0A) {
    return 2;
  } else {
    return 0;
  }
}

static uint16_t get_blocksize(uint8_t blocksize_code, istream &in, ostream &log) {
  blocksize_code &= 0x0F;
  if (blocksize_code == 0x00) {
    log << "Warning: reserved blocksize?" << endl;
    return 0;
  } else if (blocksize_code == 0x01) {
    return 192;
  } else if (blocksize_code <= 0x05) {
    return 576 * (2^(blocksize_code-2));
  } else if (blocksize_code <= 0x08) {
    uint16_t ret;
    read_into(in, 1 << (blocksize_code & 0x01), ret);
    return ret;
  } else {
    return 256 * (2^(blocksize_code-8));
  }
}

bool FlacDecoder::parse_frame(istream &in, ostream &out, ostream &log) {
  if (verbosity >= FRAME_INFO) {
    log << "Parsing frame..." << endl;
  }
  uint32_t raw = 0;
  read_into(in, 4, raw);
  // 1111 1111 1111 1000 1100 1001 1010 1000
  flac->frame.sync_code = (raw >> 18) & ONES(14);  // 3ffe
  if (flac->frame.sync_code != 0x3FFE) {
    log << "Got " << setw(1) << setfill('0') << hex
        << flac->frame.sync_code
        << " instead of expected sync code 0x3FFE. "
        << "Bailing..." << endl;
    return false;
  }

  flac->frame.reserve_bit1 = (raw >> 17) & ONES(1);
  if (flac->frame.reserve_bit1)
    log << "Warning: this frame's first reserve bit is set to 1." << endl;
  flac->frame.blocking_strategy = !!((raw >> 16) & ONES(1));

  flac->frame.blocksize_code = (raw >> 8) & ONES(4);
  flac->frame.sample_rate_code = (raw >> 4) & ONES(4);

  flac->frame.channel_assignment = (raw >> 4) & ONES(4);
  flac->frame.n_channels = get_channels(flac->frame.channel_assignment);

  flac->frame.sample_size_code = (raw >> 1) & ONES(3);
  flac->frame.reserve_bit2 = (raw >> 0) & ONES(1);
  if (flac->frame.reserve_bit2)
    log << "Warning: this frame's second reserve bit is set to 1." << endl;

  if (flac->frame.blocking_strategy == Flac::Frame::VARIABLE_BLOCKSIZE) {
    read_into_utf8(in, flac->frame.sf.sample_number);
  } else {
    read_into_utf8(in, flac->frame.sf.frame_number);
  }

  flac->frame.blocksize = get_blocksize(flac->frame.blocksize_code, in, log);

  if ((flac->frame.sample_rate_code & 0x0C) == 0x0C) {
    // read sample rate from frame
    switch(flac->frame.sample_rate_code & 0x03) {
      case 0:
        // get 8 bit sample rate in Hz
        read_into(in, 1, flac->frame.sample_rate);
        break;
      case 1:
        // get 16 bit sample rate in Hz
        read_into(in, 2, flac->frame.sample_rate);
        break;
      case 2:
        // get 16 bit sample rate in 10s of Hz
        read_into(in, 2, flac->frame.sample_rate);
        flac->frame.sample_rate *= 10;
        break;
      default:
        // 3 is invalid.
        log << "Warning: invalid sample rate code (0b"
            << bitset<4>(flac->frame.sample_rate_code) << ")" << endl;
        break;
    }
  } else {  // take sample rate from header
    flac->frame.sample_rate = flac->sample_rate;
  }

  read_into(in, 1, flac->frame.crc8);

  if (verbosity >= FRAME_INFO) {
    flac->frame.print_header(log);
  }
  return true;
}

void Flac::Frame::print_header(ostream &log) {
  log << "sync_code: " << setw(4) << setfill('0') << hex << sync_code << endl;
  log << "reserve bit 1: " << reserve_bit1 << endl;
  log << "blocking strategy: "
      << (blocking_strategy ? "variable" : "fixed") << "-blocksize stream" << endl;

  log << "blocksize in interchannel samples: 0b"
      << setw(1) << setfill('0') << bitset<4>(blocksize_code)
      << " -> " << dec << blocksize << endl;

  log << "sample rate: 0b"
      << setw(1) << setfill('0') << bitset<4>(sample_rate_code)
      << " -> " << dec << sample_rate << "Hz" << endl;

  log << "Number of channels: " << dec << n_channels
      << " [" << bitset<4>(channel_assignment) << "]" << endl;

  // sample size in bits

  if (blocking_strategy == VARIABLE_BLOCKSIZE)
    log << "Sample number: " << dec << sf.sample_number << endl;
  else
    log << "Frame number: " << dec << sf.frame_number << endl;

  log << "CRC-8: 0x" << hex << ((+crc8) & 0xFF) << endl;
}

// output in bigendian (man 3 endian)
void FlacDecoder::decode(istream &in, ostream &out) {
  // read in fLaC marker
  uint32_t fLaC;
  read_into(in, 4, fLaC);
  if (fLaC != 0x664C6143) {  // "fLaC"
    cerr << "The input does not appear to be a valid flac file "
            "(missing fLaC marker at beginning). Exiting..." << endl;
    return;
  }

  // read metadata blocks
  if (parse_metadata(in, clog) == false) {
    cerr << "Failed to parse metadata. Exiting..." << endl;
    return;
  }

  //while (in.peek() != EOF) {
  for (int i=0; i<3; i++) {
    parse_frame(in, out, clog);
  }

}

// hexdump ~/music/06_Alpha.flac  -C | less
int main(int argc, char *argv[]) {
  FlacDecoder decoder(FRAME_INFO);
  decoder.decode(cin, cout);
  return 0;
}
