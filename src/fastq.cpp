/*************************************************************************\
 * AdapterRemoval - cleaning next-generation sequencing reads            *
 *                                                                       *
 * Copyright (C) 2011 by Stinus Lindgreen - stinus@binf.ku.dk            *
 * Copyright (C) 2014 by Mikkel Schubert - mikkelsch@gmail.com           *
 *                                                                       *
 * If you use the program, please cite the paper:                        *
 * S. Lindgreen (2012): AdapterRemoval: Easy Cleaning of Next Generation *
 * Sequencing Reads, BMC Research Notes, 5:337                           *
 * http://www.biomedcentral.com/1756-0500/5/337/                         *
 *                                                                       *
 * This program is free software: you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation, either version 3 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. *
\*************************************************************************/
#include <algorithm> // for reverse, count, max, min
#include <cmath>     // for log10
#include <iostream>  // for operator<<, basic_ostream, stringstream
#include <numeric>   // for accumulate
#include <sstream>   // for stringstream

#include "debug.hpp" // for AR_DEBUG_ASSERT, AR_DEBUG_FAIL
#include "fastq.hpp"
#include "linereader.hpp" // for line_reader_base

enum class read_mate
{
  unknown,
  mate_1,
  mate_2,
};

struct mate_info
{
  mate_info()
    : name()
    , mate(read_mate::unknown)
  {}

  std::string desc() const
  {
    switch (mate) {
      case read_mate::unknown:
        return "unknown";
      case read_mate::mate_1:
        return "mate 1";
      case read_mate::mate_2:
        return "mate 2";
      default:
        AR_DEBUG_FAIL("Invalid mate in mate_info::desc");
    }
  }

  std::string name;
  read_mate mate;
};

inline mate_info
get_and_fix_mate_info(fastq& read, char mate_separator)
{
  mate_info info;
  std::string& header = read.m_header;

  size_t pos = header.find_first_of(' ');
  if (pos == std::string::npos) {
    pos = header.length();
  }

  if (pos >= 2 && header.at(pos - 2) == mate_separator) {
    const char digit = header.at(pos - 1);

    if (digit == '1') {
      header[pos - 2] = MATE_SEPARATOR;
      info.mate = read_mate::mate_1;
      pos -= 2;
    } else if (digit == '2') {
      header[pos - 2] = MATE_SEPARATOR;
      info.mate = read_mate::mate_2;
      pos -= 2;
    }
  }

  info.name = header.substr(0, pos);
  return info;
}

///////////////////////////////////////////////////////////////////////////////
// fastq

fastq::fastq()
  : m_header()
  , m_sequence()
  , m_qualities()
{}

fastq::fastq(const std::string& header,
             const std::string& sequence,
             const std::string& qualities,
             const fastq_encoding& encoding)
  : m_header(header)
  , m_sequence(sequence)
  , m_qualities(qualities)
{
  post_process(encoding);
}

fastq::fastq(const std::string& header, const std::string& sequence)
  : m_header(header)
  , m_sequence(sequence)
  , m_qualities(std::string(sequence.length(), '!'))
{
  post_process(FASTQ_ENCODING_33);
}

bool
fastq::operator==(const fastq& other) const
{
  return (m_header == other.m_header) && (m_sequence == other.m_sequence) &&
         (m_qualities == other.m_qualities);
}

size_t
fastq::count_ns() const
{
  return static_cast<size_t>(
    std::count(m_sequence.begin(), m_sequence.end(), 'N'));
}

fastq::ntrimmed
fastq::trim_trailing_bases(const bool trim_ns,
                           char low_quality,
                           const bool preserve5p)
{
  low_quality += PHRED_OFFSET_33;
  auto is_quality_base = [&](size_t i) {
    return m_qualities.at(i) > low_quality &&
           (!trim_ns || m_sequence.at(i) != 'N');
  };

  size_t right_exclusive = 0;
  for (size_t i = m_sequence.length(); i; --i) {
    if (is_quality_base(i - 1)) {
      right_exclusive = i;
      break;
    }
  }

  size_t left_inclusive = 0;
  for (size_t i = 0; !preserve5p && i < right_exclusive; ++i) {
    if (is_quality_base(i)) {
      left_inclusive = i;
      break;
    }
  }

  return trim_sequence_and_qualities(left_inclusive, right_exclusive);
}

//! Calculates the size of the sliding window for quality trimming given a
//! read length and a user-defined window-size (fraction or whole number).
size_t
calculate_winlen(const size_t read_length, const double window_size)
{
  size_t winlen;
  if (window_size >= 1.0) {
    winlen = static_cast<size_t>(window_size);
  } else {
    winlen = static_cast<size_t>(window_size * read_length);
  }

  if (winlen == 0 || winlen > read_length) {
    winlen = read_length;
  }

  return winlen;
}

fastq::ntrimmed
fastq::trim_windowed_bases(const bool trim_ns,
                           char low_quality,
                           const double window_size,
                           const bool preserve5p)
{
  AR_DEBUG_ASSERT(window_size >= 0.0);
  if (m_sequence.empty()) {
    return ntrimmed();
  }

  low_quality += PHRED_OFFSET_33;
  auto is_quality_base = [&](size_t i) {
    return m_qualities.at(i) > low_quality &&
           (!trim_ns || m_sequence.at(i) != 'N');
  };

  const size_t winlen = calculate_winlen(length(), window_size);
  long running_sum =
    std::accumulate(m_qualities.begin(), m_qualities.begin() + winlen, 0);

  size_t left_inclusive = std::string::npos;
  size_t right_exclusive = std::string::npos;
  for (size_t offset = 0; offset + winlen <= length(); ++offset) {
    const long running_avg = running_sum / static_cast<long>(winlen);

    // We trim away low quality bases and Ns from the start of reads,
    // **before** we consider windows.
    if (left_inclusive == std::string::npos && is_quality_base(offset) &&
        running_avg > low_quality) {
      left_inclusive = offset;
    }

    if (left_inclusive != std::string::npos &&
        (running_avg <= low_quality || offset + winlen == length())) {
      right_exclusive = offset;
      while (right_exclusive < length() && is_quality_base(right_exclusive)) {
        right_exclusive++;
      }

      break;
    }

    running_sum -= m_qualities.at(offset);
    if (offset + winlen < length()) {
      running_sum += m_qualities.at(offset + winlen);
    }
  }

  if (left_inclusive == std::string::npos) {
    // No starting window found. Trim all bases starting from start.
    return trim_sequence_and_qualities(length(), length());
  } else if (preserve5p) {
    left_inclusive = 0;
  }

  AR_DEBUG_ASSERT(right_exclusive != std::string::npos);
  return trim_sequence_and_qualities(left_inclusive, right_exclusive);
}

void
fastq::truncate(size_t pos, size_t len)
{
  AR_DEBUG_ASSERT(pos == 0 || pos <= length());

  if (pos || len < length()) {
    m_sequence = m_sequence.substr(pos, len);
    m_qualities = m_qualities.substr(pos, len);
  }
}

void
fastq::reverse_complement()
{
  std::reverse(m_sequence.begin(), m_sequence.end());
  std::reverse(m_qualities.begin(), m_qualities.end());

  // Lookup table for complementary bases based only on the last 4 bits
  static const char complements[] = "-T-GA--C------N-";
  for (auto& nuc : m_sequence) {
    nuc = complements[nuc & 0xf];
  }
}

bool
fastq::read_unsafe(line_reader_base& reader)
{
  std::string line;
  do {
    if (!reader.getline(line)) {
      // End of file; terminate gracefully
      return false;
    }
  } while (line.empty());

  m_header = line.substr(1);
  if (m_header.empty() || line.at(0) != '@') {
    throw fastq_error("Malformed or empty FASTQ header");
  }

  if (!reader.getline(m_sequence)) {
    throw fastq_error("partial FASTQ record; cut off after header");
  } else if (m_sequence.empty()) {
    throw fastq_error("sequence is empty");
  }

  if (!reader.getline(line)) {
    throw fastq_error("partial FASTQ record; cut off after sequence");
  } else if (line.empty() || line.at(0) != '+') {
    throw fastq_error("FASTQ record lacks separator character (+)");
  }

  if (!reader.getline(m_qualities)) {
    throw fastq_error("partial FASTQ record; cut off after separator");
  } else if (m_qualities.empty()) {
    throw fastq_error("no qualities");
  } else if (m_qualities.length() != m_sequence.length()) {
    throw fastq_error("sequence/quality lengths do not match");
  }

  return true;
}

bool
fastq::read(line_reader_base& reader, const fastq_encoding& encoding)
{
  if (!read_unsafe(reader)) {
    return false;
  }

  post_process(encoding);

  return true;
}

void
fastq::into_string(std::string& dst) const
{
  dst.push_back('@');
  dst.append(m_header);
  dst.push_back('\n');
  dst.append(m_sequence);
  dst.append("\n+\n", 3);
  fastq_encoding::encode(m_qualities, dst);
  dst.push_back('\n');
}

///////////////////////////////////////////////////////////////////////////////
// Public helper functions

void
fastq::clean_sequence(std::string& sequence)
{
  for (char& nuc : sequence) {
    switch (nuc) {
      case 'A':
      case 'C':
      case 'G':
      case 'T':
      case 'N':
        break;

      case 'a':
      case 'c':
      case 'g':
      case 't':
      case 'n':
        nuc += 'A' - 'a';
        break;

      default:
        throw fastq_error("invalid character in FASTQ sequence; "
                          "only A, C, G, T and N are expected!");
    }
  }
}

char
fastq::p_to_phred_33(double p)
{
  // Lowest possible error rate representable is '~' (~5e-10)
  const auto min_p = std::max(5e-10, p);
  const auto raw_score = static_cast<int>(-10.0 * std::log10(min_p));
  return std::min<int>('~', raw_score + PHRED_OFFSET_33);
}

void
fastq::validate_paired_reads(fastq& mate1, fastq& mate2, char mate_separator)
{
  if (mate1.length() == 0 || mate2.length() == 0) {
    throw fastq_error("Pair contains empty reads");
  }

  const mate_info info1 = get_and_fix_mate_info(mate1, mate_separator);
  const mate_info info2 = get_and_fix_mate_info(mate2, mate_separator);

  if (info1.name != info2.name) {
    std::stringstream error;
    error << "Pair contains reads with mismatching names:\n"
          << " - '" << info1.name << "'\n"
          << " - '" << info2.name << "'";

    if (info1.mate == read_mate::unknown || info2.mate == read_mate::unknown) {
      error << "\n\nNote that AdapterRemoval by determines the mate "
               "numbers as the digit found at the end of the read name, "
               "if this is preceded by the character '"
            << mate_separator
            << "'; if these data makes use of a different character to "
               "separate the mate number from the read name, then you "
               "will need to set the --mate-separator command-line "
               "option to the appropriate character.";
    }

    throw fastq_error(error.str());
  }

  if (info1.mate != read_mate::unknown || info2.mate != read_mate::unknown) {
    if (info1.mate != read_mate::mate_1 || info2.mate != read_mate::mate_2) {
      std::stringstream error;
      error << "Inconsistent mate numbering; please verify data:\n"
            << "\nRead 1 identified as " << info1.desc() << ": " << mate1.name()
            << "\nRead 2 identified as " << info2.desc() << ": "
            << mate2.name();

      throw fastq_error(error.str());
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Private helper functions

void
fastq::post_process(const fastq_encoding& encoding)
{
  if (m_qualities.length() != m_sequence.length()) {
    throw fastq_error(
      "invalid FASTQ record; sequence/quality length does not match");
  }

  clean_sequence(m_sequence);
  encoding.decode(m_qualities);
}

fastq::ntrimmed
fastq::trim_sequence_and_qualities(const size_t left_inclusive,
                                   const size_t right_exclusive)
{
  const ntrimmed summary(left_inclusive, length() - right_exclusive);

  if (summary.first || summary.second) {
    const size_t retained = right_exclusive - left_inclusive;
    m_sequence = m_sequence.substr(left_inclusive, retained);
    m_qualities = m_qualities.substr(left_inclusive, retained);
  }

  return summary;
}
