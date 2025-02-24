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

#include <memory>   // for unique_ptr
#include <stddef.h> // for size_t
#include <vector>   // for vector

#include "commontypes.hpp" // for read_type
#include "fastq.hpp"       // for fastq_pair_vec
#include "fastq_io.hpp"    // for output_chunk_ptr
#include "scheduler.hpp"   // for chunk_vec, analytical_step, threadstate
#include "statistics.hpp"  // for trimming_statistics

class output_sample_files;
class userconfig;

typedef std::unique_ptr<trimming_statistics> statistics_ptr;

/** Helper class used to generate per file-type chunks for processed reads . */
class trimmed_reads
{
public:
  trimmed_reads(const output_sample_files& map, const bool eof);

  /**
   * Adds a read of the given type.
   *
   * @param read A read to be distributed in the pipeline; may be modified.
   * @param type The read type to store the read as.
   */
  void add(fastq& read, const read_type type);

  /** Returns a chunk for each generated type of proccessed reads. */
  chunk_vec finalize();

private:
  const output_sample_files& m_map;

  //! A set output chunks being created; typically fewer than read_type::max.
  std::vector<output_chunk_ptr> m_chunks;
};

class reads_processor : public analytical_step
{
public:
  reads_processor(const userconfig& config,
                  const output_sample_files& output,
                  size_t nth);

  statistics_ptr get_final_statistics();

protected:
  const userconfig& m_config;
  const fastq_pair_vec m_adapters;
  threadstate<trimming_statistics> m_stats;
  const output_sample_files& m_output;
  const size_t m_nth;
};

class se_reads_processor : public reads_processor
{
public:
  se_reads_processor(const userconfig& config,
                     const output_sample_files& output,
                     size_t nth);

  chunk_vec process(analytical_chunk* chunk);
};

class pe_reads_processor : public reads_processor
{
public:
  pe_reads_processor(const userconfig& config,
                     const output_sample_files& output,
                     size_t nth);

  chunk_vec process(analytical_chunk* chunk);
};
