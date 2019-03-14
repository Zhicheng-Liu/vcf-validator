/**
 * Copyright 2018 EMBL - European Bioinformatics Institute
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/iostreams/copy.hpp>

#include "bioio/bioio.hpp"

#include "util/file_utils.hpp"
#include "util/logger.hpp"
#include "util/stream_utils.hpp"
#include "util/string_utils.hpp"
#include "vcf/compression.hpp"
#include "fasta/fasta.hpp"
#include "vcf/string_constants.hpp"

ebi::vcf::fasta::FileBasedFasta::FileBasedFasta(const std::string& fasta_path, const std::string& fasta_index_path)
{
    BOOST_LOG_TRIVIAL(info) << "Reading from input FASTA file...";
    ebi::util::open_file(fasta_input, fasta_path, std::ifstream::binary);

    BOOST_LOG_TRIVIAL(info) << "Reading from input FASTA index file...";
    std::ifstream fs;
    ebi::util::open_file(fs, fasta_index_path, std::ifstream::binary);
    fasta_index = bioio::read_fasta_index(fs);
}

std::string
ebi::vcf::fasta::FileBasedFasta::sequence(const std::string& contig, const size_t pos, const size_t length)
{
    if (fasta_index.find(contig) == fasta_index.cend()) {
        return "";
    }
    return bioio::read_fasta_contig(fasta_input, fasta_index.at(contig), pos, length);
}

size_t
ebi::vcf::fasta::FileBasedFasta::count(const std::string &contig) const
{
    return fasta_index.count(contig);
}

size_t
ebi::vcf::fasta::FileBasedFasta::len(const std::string &contig) const
{
    auto iter = fasta_index.find(contig);
    if (iter == fasta_index.cend()) {
      return 0;
    }
    return (iter->second).length;
}

class ebi::vcf::fasta::ContigFromENA {
public:
  ContigFromENA(const std::string& contigName)
  {
      contig_length = 0;
      contig_name = contigName;
      fasta_file.open(contig_name.c_str(), std::fstream::binary | std::fstream::in | std::fstream::out | std::fstream::trunc);
  }

  ~ContigFromENA()
  {
      fasta_file.close();
      boost::filesystem::remove(contig_name);
  }

  void write(const char* buffer, const size_t length)
  {
      fasta_file.write(buffer, length);
      contig_length += length;
  }

  std::string read(const size_t pos, const size_t length)
  {
      if (pos >= contig_length) {
        return "";
      }

      std::string result;
      return ebi::util::read_n(fasta_file, result, length, pos);
  }

  size_t length() const {
      return contig_length;
  }

private:
  size_t contig_length;
  std::string contig_name;
  std::fstream fasta_file;
};

std::string
ebi::vcf::fasta::RemoteContig::sequence(const std::string& contig, const size_t pos, const size_t length)
{
    if (contigs.count(contig) == 0) {
        contigs[contig].reset(new ebi::vcf::fasta::ContigFromENA(contig));

        // This contig is not downloaded, try download it from ENA.
        std::string url = ebi::vcf::ENA_API_FASTA_URL + contig;
        std::fstream response_stream;
        response_stream.open(contig+".tmp", std::ios::in | std::ios::out | std::ios::app);
        ebi::util::open_remote(response_stream, url);
        response_stream.seekg(0, std::ios::beg);

        std::string line;
        line.reserve(1024);
        if (ebi::util::readline(response_stream, line).size() != 0) {
            if (boost::starts_with(line, ">")) {
                while (ebi::util::readline(response_stream, line).size() != 0) {
                    ebi::util::remove_end_of_line(line); // ignore linebreak at the end of line
                    contigs[contig]->write(line.c_str(), line.size());
                }
            }
        }

        response_stream.close();
        boost::filesystem::remove(contig+".tmp");
    }

    return contigs[contig]->read(pos, length);
}

size_t
ebi::vcf::fasta::RemoteContig::count(const std::string &contig) const
{
  if (contigs.find(contig) == contigs.cend()) {
    return 0;
  }

  return 1;
}

size_t
ebi::vcf::fasta::RemoteContig::len(const std::string &contig) const
{
    auto iter = contigs.find(contig);
    if (iter == contigs.cend()) {
      return 0;
    }

    return iter->second->length();
}