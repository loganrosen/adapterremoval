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
#include <algorithm>   // for min, copy, max, replace
#include <cmath>       // for isnan
#include <iomanip>     // for operator<<, setw
#include <iostream>    // for operator<<, basic_ostream, endl, cerr, ostream
#include <set>         // for set
#include <sstream>     // for stringstream
#include <stdexcept>   // for invalid_argument
#include <sys/ioctl.h> // for ioctl, winsize, TIOCGWINSZ
#include <unistd.h>    // for STDERR_FILENO

#include "argparse.hpp"
#include "debug.hpp"    // for AR_DEBUG_ASSERT
#include "strutils.hpp" // for cli_formatter, str_to_unsigned, toupper

namespace argparse {

typedef std::set<consumer_ptr> consumer_set;

/** Returns the number of columns available in the terminal. */
size_t
get_terminal_columns()
{
  struct winsize params;
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &params)) {
    // Default to 80 columns if the parameters could not be retrieved.
    return 80;
  }

  return std::min<size_t>(120, std::max<size_t>(80, params.ws_col));
}

parser::parser(const std::string& name,
               const std::string& version,
               const std::string& help)
  : m_keys()
  , m_parsers()
  , m_key_requires()
  , m_key_prohibits()
  , m_name(name)
  , m_version(version)
  , m_help(help)
{
  add_header("OPTIONS:");

  // Built-in arguments (aliases are not shown!)
  (*this)["--help"] = new flag(nullptr, "Display this message.");
  create_alias("--help", "-h");

  (*this)["--version"] = new flag(nullptr, "Print the version string.");
  create_alias("--version", "-v");

  add_seperator();
}

parser::~parser()
{
  // Track set of unique pointers, to handle parsers assigned to multiple keys.
  consumer_set pointers;
  for (const auto& parser : m_parsers) {
    if (pointers.insert(parser.second).second) {
      delete parser.second;
    }
  }
}

consumer_ptr&
parser::operator[](const std::string& key)
{
  AR_DEBUG_ASSERT(!key.empty());
  if (m_parsers.find(key) == m_parsers.end()) {
    m_keys.push_back(key_pair(true, key));
  }

  return m_parsers[key];
}

const consumer_ptr&
parser::at(const std::string& key) const
{
  return m_parsers.at(key);
}

parse_result
parser::parse_args(int argc, char* argv[])
{
  const string_vec argvec(argv + 1, argv + argc);

  string_vec_citer it = argvec.begin();
  while (it != argvec.end()) {
    consumer_map::iterator parser = find_argument(*it);
    if (parser != m_parsers.end()) {
      if (parser->second->is_set()) {
        std::cerr << "WARNING: Command-line option " << parser->first
                  << " has been specified more than once." << std::endl;
      }

      const size_t consumed = parser->second->consume(++it, argvec.end());

      if (consumed == static_cast<size_t>(-1)) {
        if (it != argvec.end()) {
          std::cerr << "ERROR: Invalid value for " << *(it - 1) << ": '" << *it
                    << "'" << std::endl;
        } else {
          std::cerr << "ERROR: No value supplied for " << *(it - 1)
                    << std::endl;
        }

        return parse_result::error;
      }

      it += static_cast<consumer_map::iterator::difference_type>(consumed);
    } else {
      return parse_result::error;
    }
  }

  parse_result result = parse_result::ok;
  for (const auto& pair : m_key_requires) {
    if (is_set(pair.first) && !is_set(pair.second)) {
      result = parse_result::error;
      std::cerr << "ERROR: Option " << pair.second << " is required when using "
                << pair.first << std::endl;
    }
  }

  for (const auto& pair : m_key_prohibits) {
    if (is_set(pair.first) && is_set(pair.second)) {
      result = parse_result::error;
      std::cerr << "ERROR: Option " << pair.second
                << " cannot be used together with option " << pair.first
                << std::endl;
    }
  }

  if (is_set("--help")) {
    print_help();
    return parse_result::exit;
  } else if (is_set("--version")) {
    print_version();
    return parse_result::exit;
  }

  return result;
}

bool
parser::is_set(const std::string& key) const
{
  const consumer_map::const_iterator it = m_parsers.find(key);
  AR_DEBUG_ASSERT(it != m_parsers.end());

  return it->second->is_set();
}

void
parser::add_seperator()
{
  m_keys.push_back(key_pair(false, std::string()));
}

void
parser::add_header(const std::string& header)
{
  add_seperator();
  m_keys.push_back(key_pair(false, header));
}

void
parser::create_alias(const std::string& key, const std::string& alias)
{
  AR_DEBUG_ASSERT(m_parsers.find(alias) == m_parsers.end());

  m_parsers[alias] = m_parsers.at(key);
}

void
parser::option_requires(const std::string& key, const std::string& required)
{
  AR_DEBUG_ASSERT(m_parsers.find(key) != m_parsers.end());
  AR_DEBUG_ASSERT(m_parsers.find(required) != m_parsers.end());

  m_key_requires.push_back(str_pair(key, required));
}
void
parser::option_prohibits(const std::string& key, const std::string& prohibited)
{
  AR_DEBUG_ASSERT(m_parsers.find(key) != m_parsers.end());
  AR_DEBUG_ASSERT(m_parsers.find(prohibited) != m_parsers.end());

  m_key_prohibits.push_back(str_pair(key, prohibited));
}

void
parser::print_version() const
{
  std::cerr << m_name << " " << m_version << std::endl;
}

void
parser::print_help() const
{
  print_version();
  std::cerr << "\n" << m_help << "\n";

  print_arguments(m_keys);
}

void
parser::print_arguments(const key_pair_vec& keys) const
{
  const size_t indentation = 8;

  cli_formatter fmt;
  fmt.set_indent(indentation);
  fmt.set_column_width(std::min<size_t>(80, get_terminal_columns()) -
                       indentation - 3);

  for (const auto& key_pair : keys) {
    if (!key_pair.first) {
      std::cerr << key_pair.second << "\n";
      continue;
    }

    const consumer_ptr ptr = m_parsers.at(key_pair.second);
    if (ptr->help() == "HIDDEN") {
      continue;
    }

    const std::string metavar = get_metavar_str(ptr, key_pair.second);
    std::cerr << std::left << std::setw(indentation)
              << ("    " + key_pair.second + " " + metavar) << "\n";

    std::string value = ptr->help();
    if (!value.empty()) {
      // Replace "%default" with string representation of current value.
      size_t index = value.find("%default");
      if (index != std::string::npos) {
        const std::string default_value = ptr->to_str();
        value.replace(index, 8, default_value);
      }

      // Format into columns and indent lines (except the first line)
      std::cerr << fmt.format(value) << "\n";
    }

    std::cerr << "\n";
  }

  std::cerr << std::endl;
}

consumer_map::iterator
parser::find_argument(const std::string& str)
{
  auto it = m_parsers.find(str);
  if (it == m_parsers.end()) {
    std::cerr << "ERROR: Unknown argument: '" << str << "'" << std::endl;
  }

  return it;
}

std::string
parser::get_metavar_str(const consumer_ptr ptr, const std::string& key) const
{
  if (!ptr->metavar().empty()) {
    return ptr->metavar();
  }

  std::string metavar = key;
  metavar.erase(0, metavar.find_first_not_of("-"));
  std::replace(metavar.begin(), metavar.end(), '-', '_');

  return toupper(metavar);
}

///////////////////////////////////////////////////////////////////////////////

consumer_base::consumer_base(const std::string& metavar,
                             const std::string& help)
  : m_value_set(false)
  , m_metavar(metavar)
  , m_help(help)
{}

consumer_base::~consumer_base() {}

bool
consumer_base::is_set() const
{
  return m_value_set;
}

const std::string&
consumer_base::metavar() const
{
  return m_metavar;
}

const std::string&
consumer_base::help() const
{
  return m_help;
}

///////////////////////////////////////////////////////////////////////////////

flag::flag(bool* value, const std::string& help)
  // Non-empty metavar to avoid auto-generated metavar
  : consumer_base(" ", help)
  , m_ptr(value)
{
  AR_DEBUG_ASSERT(!(m_ptr && *m_ptr));
}

size_t
flag::consume(string_vec_citer, const string_vec_citer&)
{
  if (m_ptr) {
    *m_ptr = true;
  }
  m_value_set = true;

  return 0;
}

std::string
flag::to_str() const
{
  bool is_set = m_value_set;
  if (m_ptr) {
    is_set = *m_ptr;
  }

  return std::string(is_set ? "on" : "off");
}

///////////////////////////////////////////////////////////////////////////////

any::any(std::string* value,
         const std::string& metavar,
         const std::string& help)
  : consumer_base(metavar, help)
  , m_ptr(value)
  , m_sink()
{}

size_t
any::consume(string_vec_citer start, const string_vec_citer& end)
{
  if (start != end) {
    m_value_set = true;
    (m_ptr ? *m_ptr : m_sink) = *start;

    return 1;
  }

  return static_cast<size_t>(-1);
}

std::string
any::to_str() const
{
  const std::string& result = m_ptr ? *m_ptr : m_sink;
  if (result.empty()) {
    return "<not set>";
  } else {
    return result;
  }
}

///////////////////////////////////////////////////////////////////////////////

many::many(string_vec* value,
           const std::string& metavar,
           const std::string& help)
  : consumer_base(metavar, help)
  , m_ptr(value)
{
  AR_DEBUG_ASSERT(m_ptr);
}

size_t
many::consume(string_vec_citer start, const string_vec_citer& end)
{
  m_value_set = true;
  string_vec_citer it = start;
  for (; it != end; ++it) {
    if (!it->empty() && it->front() == '-') {
      break;
    }
  }

  m_ptr->assign(start, it);

  return static_cast<size_t>(it - start);
}

std::string
many::to_str() const
{
  if (m_ptr->empty()) {
    return "<not set>";
  } else {
    std::string output;

    for (const auto& s : *m_ptr) {
      if (!output.empty()) {
        output.push_back(';');
      }

      output.append(s);
    }

    return output;
  }
}

///////////////////////////////////////////////////////////////////////////////

knob::knob(unsigned* value, const std::string& metavar, const std::string& help)
  : consumer_base(metavar, help)
  , m_ptr(value)
{
  AR_DEBUG_ASSERT(m_ptr);
}

size_t
knob::consume(string_vec_citer start, const string_vec_citer& end)
{
  if (start != end) {
    try {
      *m_ptr = str_to_unsigned(*start);
      m_value_set = true;
      return 1;
    } catch (const std::invalid_argument&) {
      return static_cast<size_t>(-1);
    }
  }

  return static_cast<size_t>(-1);
}

std::string
knob::to_str() const
{
  std::stringstream stream;
  stream << *m_ptr;
  return stream.str();
}

///////////////////////////////////////////////////////////////////////////////

floaty_knob::floaty_knob(double* value,
                         const std::string& metavar,
                         const std::string& help)
  : consumer_base(metavar, help)
  , m_ptr(value)
{
  AR_DEBUG_ASSERT(m_ptr);
}

size_t
floaty_knob::consume(string_vec_citer start, const string_vec_citer& end)
{
  if (start != end) {
    std::stringstream stream(*start);
    if (!(stream >> *m_ptr)) {
      return static_cast<size_t>(-1);
    }

    // Failing on trailing, non-numerical values
    std::string trailing;
    if (stream >> trailing) {
      return static_cast<size_t>(-1);
    }

    m_value_set = true;
    return 1;
  }

  return static_cast<size_t>(-1);
}

std::string
floaty_knob::to_str() const
{
  if (std::isnan(*m_ptr)) {
    return "<not set>";
  } else {
    std::stringstream stream;
    stream << *m_ptr;
    return stream.str();
  }
}

} // namespace argparse
