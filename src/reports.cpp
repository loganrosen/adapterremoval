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
#include <cstring>  // for size_t, strerror
#include <errno.h>  // for errno
#include <fstream>  // for ofstream
#include <iostream> // for ofstream, operator<<, basic_ostream, endl
#include <string>   // for operator+, string, operator<<
#include <vector>   // for vector

#include "adapterset.hpp" // for adapter_set
#include "counts.hpp"     // for counts, counts_tmpl
#include "fastq.hpp"      // for fastq_pair_vec, IDX_TO_ACGT, fastq
#include "json.hpp"       // for json_writer, json_section
#include "main.hpp"       // for NAME, VERSION
#include "statistics.hpp" // for fastq_statistics, trimming_statistics, ar_...
#include "strutils.hpp"   // for cli_formatter
#include "userconfig.hpp" // for userconfig, ar_command, ar_command::demult...

#define WITH_SECTION(writer, key)                                              \
  if (const auto section##__LINE__ = (writer).start(key))

void
write_report_meta(const userconfig& config, json_writer& writer)
{
  WITH_SECTION(writer, "meta")
  {
    writer.write("version", NAME + " " + VERSION);
    writer.write("command", config.args);
    writer.write_float("runtime", config.runtime());
  }
}

void
write_report_summary_stats(json_writer& writer,
                           const std::vector<const fastq_statistics*>& stats)
{
  size_t n_reads = 0;
  size_t n_reads_s = 0;
  size_t n_bases = 0;
  size_t n_a = 0;
  size_t n_c = 0;
  size_t n_g = 0;
  size_t n_t = 0;
  size_t n_n = 0;
  size_t n_q20 = 0;
  size_t n_q30 = 0;

  for (const auto& it : stats) {
    n_reads += it->number_of_input_reads();
    n_reads_s += it->number_of_sampled_reads();
    n_bases += it->length_dist().product();
    // The following stats are all (potentially) based on a subset of reads
    n_a += it->nucleotides_pos('A').sum();
    n_c += it->nucleotides_pos('C').sum();
    n_g += it->nucleotides_pos('G').sum();
    n_t += it->nucleotides_pos('T').sum();
    n_n += it->uncalled_pos().sum();
    n_q20 += it->quality_dist().sum(20);
    n_q30 += it->quality_dist().sum(30);
  }

  const auto n_bases_s = n_a + n_c + n_g + n_t + n_n;

  writer.write_int("reads", n_reads);
  writer.write_int("bases", n_bases);
  writer.write_float("mean_length", static_cast<double>(n_bases) / n_reads);
  writer.write_int("reads_sampled", n_reads_s);
  writer.write_float("q20_rate", static_cast<double>(n_q20) / n_bases_s);
  writer.write_float("q30_rate", static_cast<double>(n_q30) / n_bases_s);
  writer.write_float("uncalled_rate", static_cast<double>(n_n) / n_bases_s);
  writer.write_float("gc_content", static_cast<double>(n_g + n_c) / n_bases_s);
}

void
write_report_trimming(const userconfig& config,
                      json_writer& writer,
                      const trimming_statistics& totals,
                      const fastq_pair_vec& adapters)
{
  if (config.run_type == ar_command::demultiplex_sequences) {
    writer.write_null("trimming_and_filtering");
    return;
  }

  WITH_SECTION(writer, "trimming_and_filtering")
  {
    writer.write_int("adapter_trimmed_reads",
                     totals.adapter_trimmed_reads.sum());
    writer.write_int("adapter_trimmed_bases",
                     totals.adapter_trimmed_bases.sum());

    if (adapters.size() == 1) {
      writer.write("adapter_sequence_1", adapters.front().first.sequence());
      writer.write("adapter_sequence_2", adapters.front().second.sequence());

      writer.write_null("adapter_sequences");
    } else {
      writer.write_null("adapter_sequence_1");
      writer.write_null("adapter_sequence_2");

      writer.start_list("adapter_sequences");

      for (size_t i = 0; i < adapters.size(); ++i) {
        const auto _section = writer.start();

        writer.write("adapter_sequence_1", adapters.at(i).first.sequence());
        writer.write("adapter_sequence_2", adapters.at(i).second.sequence());
        writer.write_int("adapter_trimmed_reads",
                         totals.adapter_trimmed_reads.get(i));
        writer.write_int("adapter_trimmed_bases",
                         totals.adapter_trimmed_bases.get(i));
      }

      writer.end_list();
    }

    writer.write_int("overlapping_reads_merged",
                     totals.overlapping_reads_merged);
    writer.write_int("terminal_bases_trimmed", totals.terminal_bases_trimmed);
    writer.write_int("low_quality_trimmed_reads",
                     totals.low_quality_trimmed_reads);
    writer.write_int("low_quality_trimmed_bases",
                     totals.low_quality_trimmed_bases);
    writer.write_int("filtered_min_length_reads",
                     totals.filtered_min_length_reads);
    writer.write_int("filtered_min_length_bases",
                     totals.filtered_min_length_bases);
    writer.write_int("filtered_max_length_reads",
                     totals.filtered_max_length_reads);
    writer.write_int("filtered_max_length_bases",
                     totals.filtered_max_length_bases);
    writer.write_int("filtered_ambiguous_reads",
                     totals.filtered_ambiguous_reads);
    writer.write_int("filtered_ambiguous_bases",
                     totals.filtered_ambiguous_bases);
  }
}

void
write_read_length(json_writer& writer,
                  const std::string& label,
                  size_t reads,
                  size_t bases)
{
  writer.write_int(label + "_reads", reads);
  writer.write_float(label + "_mean_length",
                     static_cast<double>(bases) / reads);
}

void
write_report_summary(const userconfig& config,
                     json_writer& writer,
                     const ar_statistics& stats)
{
  const bool demux_only = config.run_type == ar_command::demultiplex_sequences;

  WITH_SECTION(writer, "summary")
  {
    WITH_SECTION(writer, "input")
    {
      std::vector<const fastq_statistics*> input = { &stats.input_1,
                                                     &stats.input_2 };
      write_report_summary_stats(writer, input);
    }

    if (config.adapters.barcode_count()) {
      WITH_SECTION(writer, "demultiplexing")
      {
        const auto& demux = stats.demultiplexing;
        const auto total = demux.total();

        writer.write_int("total_reads", total);
        writer.write_int("assigned_reads",
                         total - demux.unidentified - demux.ambiguous);
        writer.write_int("ambiguous_reads", demux.ambiguous);
        writer.write_int("unassigned_reads", demux.unidentified);

        WITH_SECTION(writer, "samples")
        {
          for (size_t i = 0; i < demux.barcodes.size(); ++i) {
            writer.write_int(config.adapters.get_sample_name(i),
                             demux.barcodes.at(i));
          }
        }
      }
    } else {
      writer.write_null("demultiplexing");
    }

    {
      trimming_statistics totals;
      for (const auto& it : stats.trimming) {
        totals += it;
      }

      write_report_trimming(
        config, writer, totals, config.adapters.get_raw_adapters());
    }

    WITH_SECTION(writer, "output")
    {
      WITH_SECTION(writer, "passed")
      {
        std::vector<const fastq_statistics*> output;
        for (const auto& it : stats.trimming) {
          output.push_back(&it.read_1);
          output.push_back(&it.read_2);
          output.push_back(&it.merged);
        }

        write_report_summary_stats(writer, output);
      }

      if (config.adapters.barcode_count()) {
        WITH_SECTION(writer, "unidentified_1")
        {
          write_report_summary_stats(
            writer, { &stats.demultiplexing.unidentified_stats_1 });
        }

        if (config.paired_ended_mode) {
          WITH_SECTION(writer, "unidentified_2")
          {
            write_report_summary_stats(
              writer, { &stats.demultiplexing.unidentified_stats_2 });
          }
        } else {
          writer.write_null("unidentified_2");
        }
      } else {
        writer.write_null("unidentified_1");
        writer.write_null("unidentified_2");
      }

      if (demux_only) {
        writer.write_null("discarded");
      } else {
        WITH_SECTION(writer, "discarded")
        {
          std::vector<const fastq_statistics*> discarded;
          for (const auto& it : stats.trimming) {
            discarded.push_back(&it.discarded);
          }

          write_report_summary_stats(writer, discarded);
        }
      }
    }
  }
}

void
write_io_section(const std::string& key,
                 json_writer& writer,
                 const fastq_statistics& stats)
{
  WITH_SECTION(writer, key)
  {
    const std::vector<const fastq_statistics*> read_1 = { &stats };

    writer.write_int("reads", stats.number_of_input_reads());
    writer.write_int("reads_sampled", stats.number_of_sampled_reads());
    writer.write("lengths", stats.length_dist());

    auto total_bases = stats.uncalled_pos();
    auto total_quality = stats.uncalled_quality_pos();

    WITH_SECTION(writer, "quality_curves")
    {
      for (size_t nuc_i = 0; nuc_i < 4; ++nuc_i) {
        const auto nuc = IDX_TO_ACGT(nuc_i);
        const auto& nucleotides = stats.nucleotides_pos(nuc);
        const auto& quality = stats.qualities_pos(nuc);

        total_bases += nucleotides;
        total_quality += quality;

        writer.write(std::string(1, nuc), quality / nucleotides);
      }

      writer.write("Mean", total_quality / total_bases);
    }

    WITH_SECTION(writer, "content_curves")
    {
      for (size_t nuc_i = 0; nuc_i < 4; ++nuc_i) {
        const auto nuc = IDX_TO_ACGT(nuc_i);
        const auto& bases = stats.nucleotides_pos(nuc);

        writer.write(std::string(1, nuc), bases / total_bases);
      }

      writer.write("N", stats.uncalled_quality_pos() / total_bases);
      writer.write("GC",
                   (stats.nucleotides_pos('G') + stats.nucleotides_pos('C')) /
                     total_bases);
    }
  }
}

void
write_report_input(const userconfig& config,
                   json_writer& writer,
                   const ar_statistics& stats)
{
  WITH_SECTION(writer, "input")
  {
    write_io_section("read1", writer, stats.input_1);

    if (config.paired_ended_mode) {
      write_io_section("read2", writer, stats.input_2);
    } else {
      writer.write_null("read2");
    }
  }
}

void
write_report_demultiplexing(const userconfig& config,
                            json_writer& writer,
                            const ar_statistics& stats)
{
  const bool demux_only = config.run_type == ar_command::demultiplex_sequences;

  if (config.adapters.barcode_count()) {
    WITH_SECTION(writer, "demultiplexing")
    {
      size_t assigned_reads = 0;
      const auto& demux = stats.demultiplexing;
      for (size_t it : demux.barcodes) {
        assigned_reads += it;
      }

      writer.write_int("assigned_reads", assigned_reads);
      writer.write_int("ambiguous_reads", demux.ambiguous);
      writer.write_int("unassigned_reads", demux.unidentified);

      WITH_SECTION(writer, "samples")
      {
        for (size_t i = 0; i < demux.barcodes.size(); ++i) {
          WITH_SECTION(writer, config.adapters.get_sample_name(i))
          {
            const auto& sample_stats = stats.trimming.at(i);

            writer.write_int("reads", demux.barcodes.at(i));

            write_report_trimming(
              config, writer, sample_stats, config.adapters.get_adapter_set(i));

            WITH_SECTION(writer, "output")
            {
              write_io_section("read1", writer, sample_stats.read_1);

              if (config.paired_ended_mode) {
                write_io_section("read2", writer, sample_stats.read_2);

                if (config.merge && !demux_only) {
                  write_io_section("merged", writer, sample_stats.merged);
                } else {
                  writer.write_null("merged");
                }
              } else {
                writer.write_null("read2");
                writer.write_null("merged");
              }

              if (demux_only) {
                writer.write_null("discarded");
              } else {
                write_io_section("discarded", writer, sample_stats.discarded);
              }
            }
          }
        }
      }
    }
  } else {
    writer.write_null("demultiplexing");
  }
}

void
write_report_output(const userconfig& config,
                    json_writer& writer,
                    const ar_statistics& stats)
{
  fastq_statistics output_1;
  fastq_statistics output_2;
  fastq_statistics merged;
  fastq_statistics discarded;

  for (const auto& it : stats.trimming) {
    output_1 += it.read_1;
    output_2 += it.read_2;
    merged += it.merged;
    discarded += it.discarded;
  }

  const bool demux_only = config.run_type == ar_command::demultiplex_sequences;

  WITH_SECTION(writer, "output")
  {
    write_io_section("read1", writer, output_1);

    if (config.paired_ended_mode) {
      write_io_section("read2", writer, output_2);

      if (config.merge && !demux_only) {
        write_io_section("merged", writer, merged);
      } else {
        writer.write_null("merged");
      }
    } else {
      writer.write_null("read2");
      writer.write_null("merged");
    }

    if (config.adapters.barcode_count()) {
      write_io_section(
        "unidentified_1", writer, stats.demultiplexing.unidentified_stats_1);

      if (config.paired_ended_mode) {
        write_io_section(
          "unidentified_2", writer, stats.demultiplexing.unidentified_stats_2);
      } else {
        writer.write_null("unidentified_2");
      }
    } else {
      writer.write_null("unidentified_1");
      writer.write_null("unidentified_2");
    }

    if (demux_only) {
      writer.write_null("discarded");
    } else {
      write_io_section("discarded", writer, discarded);
    }
  }
}

bool
write_report(const userconfig& config,
             const ar_statistics& stats,
             const std::string& filename)
{
  try {
    std::ofstream output(filename, std::ofstream::out);
    if (!output.is_open()) {
      throw std::ofstream::failure(std::strerror(errno));
    }

    output.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    {
      json_writer writer(output);
      write_report_meta(config, writer);
      write_report_summary(config, writer, stats);
      write_report_input(config, writer, stats);
      write_report_demultiplexing(config, writer, stats);
      write_report_output(config, writer, stats);
    }

    output << std::endl;
  } catch (const std::ios_base::failure& error) {
    std::cerr << "Error writing JSON report to '" << filename << "':\n"
              << cli_formatter::fmt(error.what()) << std::endl;
    return false;
  }

  return true;
}
