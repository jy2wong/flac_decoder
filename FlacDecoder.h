#ifndef FLAC_DECODER_H_
#define FLAC_DECODER_H_

#include <iostream>
#include <memory>

#include "Flac.h"

enum Verbosity {
  WARNINGS,
  METADATA_INFO,
  FRAME_INFO
};

class FlacDecoder {
  std::shared_ptr<Flac> flac;
  Verbosity verbosity;

  bool parse_metadata(std::istream &in, std::ostream &log);
  bool parse_frame(std::istream &in, std::ostream &out, std::ostream &log);
 public:
  FlacDecoder(Verbosity v);
  FlacDecoder(Verbosity v, std::shared_ptr<Flac> f);
  void decode(std::istream &in, std::ostream &out);
};

#endif
