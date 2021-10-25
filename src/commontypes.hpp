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
#pragma once

#include <string>
#include <vector>

class fastq;

typedef std::vector<std::string> string_vec;
typedef string_vec::const_iterator string_vec_citer;
typedef std::pair<std::string, std::string> string_pair;
typedef std::vector<string_pair> string_pair_vec;

typedef std::vector<fastq> fastq_vec;
typedef fastq_vec::iterator fastq_vec_iter;

/** Different file-types read / generated by AdapterRemoval. */
enum class read_type
{
  /** Mate 1 reads, either read or written by AR. */
  mate_1 = 0,
  /** Mate 2 reads, either read or written by AR. */
  mate_2,
  /** PE reads for which the mate has been discarded. */
  singleton,
  /** Overlapping PE reads merged into a single sequence. */
  collapsed,
  /** Discarded reads; e.g. too short reads. */
  discarded,
  //! End value; not to be used as an argument.
  max
};

/** Unique IDs for analytical steps. */
enum analyses_id
{
  //! Step for reading of SE or PE reads
  ai_read_fastq = 0,

  //! Step for demultiplexing SE or PE reads
  ai_demultiplex = 1,

  //! Step for writing mate 1 reads which were not identified
  ai_write_unidentified_1 = 2,
  //! Step for writing mate 2 reads which were not identified
  ai_write_unidentified_2 = 5,

  //! Offset for post-demultiplexing analytical steps
  //! If enabled, the demultiplexing step will forward reads to the
  //! (nth + 1) * ai_analyses_offset analytical step, corresponding to the
  //! barcode number.
  ai_analyses_offset = 16,

  //! Step for reading adapter identification
  ai_identify_adapters = 0,
  //! Step for trimming of PE reads
  ai_trim_pe = 0,
  //! Step for trimming of SE reads
  ai_trim_se = 0,

  //! Steps for writing of trimmed reads
  ai_write_mate_1 = 1,
  ai_write_mate_2 = 4,
  ai_write_singleton = 7,
  ai_write_collapsed = 10,
  ai_write_discarded = 13,

  //! Offset added to write steps when splitting
  ai_split_offset = 1,

  //! Offset added to write steps when zipping
  ai_zip_offset = 2,
};
