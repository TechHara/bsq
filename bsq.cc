#include <iostream>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits>
#include <algorithm>
#include <deque>


#define HandleError(msg) \
  do { throw std::runtime_error(msg); } while (0)

int Usage(std::string const &program) {
  std::cerr << "Usage: " << program
            << " [-t CHAR] [-k N] [-w] [-f] [-h] FILE [KEY...]\n";
  std::cerr << "\t-t CHAR: column separator. Default: tab\n";
  std::cerr << "\t-k N: key column index. Default: 1\n";
  std::cerr << "\t-w: exact match only. Default: prefix match\n";
  std::cerr << "\t-c: check if the input is sorted. No search is performed\n";
  std::cerr << "\t-f: fold to upper case for keys\n";
  std::cerr << "\t-h: print this message\n";
  std::cerr << "\tFILE: input file to be read using mmap."
               " Must be sorted by the key column\n";
  std::cerr << "\tKEY: search key(s)."
            << " Each key will be searched independently.\n";
  std::cerr << "\tDefault: read from stdin delimited by LF\n";

  return EXIT_FAILURE;
}

struct Config {
  char col_sep = '\t';
  char row_sep = '\n';
  bool check = false;
  bool exact_match = false;
  bool fold = false;
  uint8_t col = 1;
  // mmap
  char const *first = nullptr;
  char const *last = nullptr;
};

/**
 * Given an option "-x" that takes an argument "X",
 * this function extracts the argument either from the
 * current string {"-xX"} or the next string {"-x", "X"}
 *
 * Return parse("X") if "X" is found else throws an error
 *
 * Upon return, pos will point to the string from which
 * the argument is extracted from, i.e., string element that contains "X"
 *
 * It: Container iterator having value type of string
 * pos: points to the string whose prefix is "-x"
 * last: exclusive upper bound of the iterator, i.e., end()
 * F: function of type string -> T
 * parse: must return argument of type T from string "X"
 */
template<typename It, typename F>
auto ExtractArgument(It &pos, It last, F &&parse) {
  if (pos->size() > 2) {
    return std::forward<F>(parse)(pos->substr(2));
  } else if (std::next(pos) != last && !(++pos)->empty()) {
    return std::forward<F>(parse)(*pos);
  }
  HandleError("Argument not found: " + *pos);
}

/**
 * Simple class that is similar to std::string_view
 * Also supports simple matching / comparison functionalities
 */
struct StringBlock {
  char const *first;
  char const *last;

  explicit StringBlock(const char *first, const char *last)
      : first(first), last(last) {
#ifndef NDEBUG
    if (first > last)
      HandleError(
          "Invalid block with distance: " + std::to_string(last - first));
#endif // NDEBUG
  }

  explicit StringBlock(std::string const &str)
      : first(str.c_str()), last(str.c_str() + str.size()) {}

  auto Distance() const noexcept { return last - first; }

  /**
   * Returns
   * < 0 if *this < that
   *   0 if *this == that
   * > 0 if *this > that
   * based on lexicographical ordering
   * where f(.) is applied to each char
   */
  template<typename F>
  long Compare(StringBlock const &that, F f) const noexcept {
    auto pos1 = first;
    auto pos2 = that.first;
    while (pos1 < last && pos2 < that.last) {
      if (f(*pos1) == f(*pos2)) ++pos1, ++pos2;
      else return f(*pos2) - f(*pos1);
    }
    // at least one of the parentheses is zero
    return (that.last - pos2) - (last - pos1);
  }

  template<typename F>
  bool IsPrefixOf(StringBlock const &that, F f) const noexcept {
    if (Distance() > that.Distance()) return false;
    auto pos1 = first;
    auto pos2 = that.first;
    while (pos1 < last && pos2 < that.last) {
      if (f(*pos1) == f(*pos2)) ++pos1, ++pos2;
      else return false;
    }
    return pos1 == last;
  }

  friend std::ostream &operator<<(std::ostream &os, const StringBlock &string) {
    return os.write(string.first, string.Distance());
  }
};


/**
 * Same as std::find_if, except:
 * - predicate takes the iterator itself as the argument
 */
template<typename It, typename F>
It FindIf(It first, It last, F pred) {
  for (; first != last; ++first)
    if (pred(first)) break;
  return first;
}

/**
 * Performs binary search on the sorted file to find match to the given key
 */
void Run(Config const &config, std::string const &key) {
  if (config.first == config.last) return;
  StringBlock search_key{key};

  auto lb = config.first;
  auto ub = config.last;

  // search backward and return the starting position of the current row
  // and store starting positions of columns within the row
  std::deque<char const *> col_pos;
  const auto FindRowBegin = [&col_pos, &config](auto pos, auto lb) {
    auto first = FindIf(std::make_reverse_iterator(pos),
                        std::make_reverse_iterator(lb),
                        [&col_pos, &config](auto pos) {
                          if (*pos == config.row_sep) return true;
                          if (*pos == config.col_sep)
                            col_pos.push_front(pos.base());
                          return false;
                        }).base();
    col_pos.push_front(first);
    return first;
  };

  // search forward and return the last position of the current row
  // and store starting positions of columns
  const auto FindRowEnd = [&col_pos, &config](auto pos, auto ub) {
    auto last = FindIf(pos, ub, [&col_pos, &config](auto pos) {
      if (*pos == config.row_sep) return true;
      if (*pos == config.col_sep)
        col_pos.push_back(pos + 1);
      return false;
    });
    col_pos.push_back(last + 1);
    return last;
  };

  const auto GetColumn = [&col_pos, &config](auto first, auto last) {
    if (col_pos.size() < config.col + 1)
      HandleError("Not enough columns\n" + std::string(first, last));
    return StringBlock{col_pos[config.col - 1], col_pos[config.col] - 1};
  };

  const auto Compare =
      [&config](StringBlock const &a, StringBlock const &b) noexcept {
        if (config.fold)
          return a.Compare(b, [](auto c) { return std::toupper(c); });
        else
          return a.Compare(b, [](auto c) { return c; });
      };

  const auto IsPrefixOf =
      [&config](StringBlock const &a, StringBlock const &b) noexcept {
        if (config.fold)
          return a.IsPrefixOf(b, [](auto c) { return std::toupper(c); });
        else
          return a.IsPrefixOf(b, [](auto c) { return c; });
      };

  if (config.check) {
    StringBlock prev{lb, lb}; // empty
    while (lb < ub) {
      col_pos.clear();
      col_pos.push_back(lb);
      auto first = lb;
      auto last = FindRowEnd(lb, ub);
      auto column = GetColumn(first, last);
      if (Compare(prev, column) < 0)
        HandleError("Unordered at row:\n" + std::string(first, last));

      lb = last + 1;
      prev = column;
    }
    return;
  }

  // binary search loop.
  // at the end of the search, lb points to the first row's first pos
  // whose key column is lexicographically geq to the given search key
  //
  // complexity: ~ O( M * log2(N) )
  // where N is # of rows and M is avg length of a row; file size is thus M*N
  while (lb < ub) {
    col_pos.clear();
    auto pos = lb + (ub - lb) / 2;
    auto first = FindRowBegin(pos, lb);
    auto last = FindRowEnd(pos, ub);
    auto column = GetColumn(first, last);
#ifndef NDEBUG
    std::cerr << "*** " << StringBlock{first, last} << "\n";
    std::cerr << "*** " << column << "\n\n";
#endif // NDEBUG

    if (Compare(search_key, column) >= 0) ub = first;
    else lb = last + 1;
  }

  const auto IsExactMatch = [&Compare](const auto &key, const auto &col) {
    return Compare(key, col) == 0;
  };
  const auto IsPrefixMatch = [&IsPrefixOf](const auto &key, const auto &col) {
    return IsPrefixOf(key, col);
  };

  ub = config.last;
  while (lb < ub) {
    col_pos.clear();
    col_pos.push_back(lb);
    auto first = lb;
    auto last = FindRowEnd(lb, ub);
    auto column = GetColumn(first, last);

    auto is_match = config.exact_match
                    ? IsExactMatch(search_key, column)
                    : IsPrefixMatch(search_key, column);

    if (is_match) {
      std::cout << StringBlock{first, last} << config.row_sep;
      lb = last + 1;
    } else break;
  }
}

int main(int argc, const char **argv) {
  std::vector<std::string> args;
  args.reserve(argc);
  for (auto it = argv; it != argv + argc; ++it)
    args.emplace_back(*it);

  Config config;
  std::string filename;
  std::vector<std::string> search_keys;
  const auto ExtractChar = [](std::string const &s) { return s.front(); };
  const auto ExtractInt = [](std::string const &s) { return std::stoi(s); };

  // parse options & arguments
  bool read_literal = false;
  try {
    for (auto it = args.begin() + 1; it != args.end(); ++it) {
      if (*it == "--") {
        read_literal = true;
        continue;
      }

      if (it->size() >= 2 && it->front() == '-' && !read_literal) {
        switch (it->at(1)) {
          case 'h':
            return Usage(args.front());
          case 'c':
          case 'w':
          case 'f':
            std::for_each(it->begin() + 1, it->end(), [&config](char c) {
              switch (c) {
                case 'w':
                  config.exact_match = true;
                  break;
                case 'c':
                  config.check = true;
                  break;
                case 'f':
                  config.fold = true;
                  break;
                default:
                  HandleError("Invalid option: -" + std::string(1, c));
              }
            });
            break;
          case 't':
            config.col_sep = ExtractArgument(it, args.end(), ExtractChar);
            break;
          case 'k': {
            const auto max = std::numeric_limits<decltype(config.col)>::max();
            auto k = ExtractArgument(it, args.end(), ExtractInt);
            if (k > max || k < 1)
              HandleError("N must be within [1, " + std::to_string(max) + "]");
            config.col = k;
          }
            break;
          default:
            HandleError("Invalid argument: " + *it);
        }
      } else if (it->front() != '-' || read_literal) {
        if (filename.empty()) filename = std::move(*it);
        else search_keys.push_back(std::move(*it));
      } else {
        HandleError("Invalid option: " + *it);
      }
      read_literal = false;
    }

    if (filename.empty())
      return Usage(args.front());

    // the file will be read as mmap
    struct stat sb;
    auto fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) HandleError("Failed to open: " + filename);
    if (fstat(fd, &sb) == -1) HandleError("Failed with fstat");
    auto addr = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) HandleError("mmap failed: " + filename);
    close(fd);
    config.first = reinterpret_cast<char const *>(addr);
    config.last = config.first + sb.st_size;

    if (config.check) {
      search_keys = {""};
    } else if (search_keys.empty()) {
      std::string key;
      while (std::getline(std::cin, key, config.row_sep)) {
        search_keys.push_back(std::move(key));
      }
    }

    for (const auto &key: search_keys)
      Run(config, key);

    munmap(addr, sb.st_size);

#ifndef NDEBUG
    std::cerr << "\n";
    std::cerr << "*** This is a debug build.\n";
    std::cerr << "*** To suppress debug messages, add\n";
    std::cerr << "*** -DNDEBUG option when compiling.\n";
#endif // NDEBUG

  } catch (std::exception const &e) {
    std::cerr << "Error: " << e.what() << "\n";
    exit(-1);
  }

  return 0;
}
