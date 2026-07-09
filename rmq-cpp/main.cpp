#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <bit>
#include <cmath>

// RMQ interface (duck-typed via templates):
//
//   static std::string name();
//   static size_t max_n();               // optional, defaults to SIZE_MAX
//   static RMQ build(const std::vector<uint64_t>& data);
//   size_t space() const;
//   uint64_t query(size_t l, size_t r) const;

// Trivial implementation that computes each query on the fly.
struct Naive {
	static std::string name() { return "QuadraticQuery"; }
	// NOTE: Improved implementations should simply return size_t::MAX.
	static size_t max_n() { return 100'000; }

	const std::vector<uint64_t>* data;

	static Naive build(const std::vector<uint64_t>& data) { return {&data}; }

	size_t space() const { return sizeof(*this); }

	uint64_t query(size_t l, size_t r) const {
		uint64_t min = (*data)[l];
		for(size_t i = l + 1; i <= r; ++i) min = std::min(min, (*data)[i]);
		return min;
	}
};

struct Precompute {
	static std::string name() {return "PrecomputeQueries"; };

	static size_t max_n() { return 30'000; };

	size_t n;
	std::vector<uint64_t> lookup_table;

	static Precompute build(const std::vector<uint64_t>& data) {
		std::vector<uint64_t> lu;
		size_t n = data.size();

		lu.resize((n * n + n) / 2);

		size_t row_start = 0;

		for (size_t l = 0; l < n; l++) {
		lu[row_start] = data[l];
		for (size_t r = l + 1; r < n; r++) {
			lu[row_start + r - l] = std::min(lu[row_start + r - l - 1], data[r]);
		}
			row_start += n - l;
		}
		return {n, std::move(lu)};
	};

	size_t space() const {return sizeof(*this) + (lookup_table.capacity() * sizeof(uint64_t)); };

	uint64_t query(size_t l, size_t r) const {
	size_t row_start = l * n - (l * (l - 1)) / 2;
	return lookup_table[row_start + r - l];
	};
};

struct SparseArray {
	static std::string name() { return "SparseArray"; };
	static size_t max_n() { return 10'000'000; };
	std::vector<std::vector<uint64_t>> _tree;
	static SparseArray build(const std::vector<uint64_t>& data) {
		int k = std::bit_width(data.size()); // maximum level
		decltype(_tree) tree(k, std::vector<uint64_t>(data.size()));
		tree[0] = data;

		for (size_t lvl = 1; lvl < k; lvl++) {
			for (size_t i = 0; i + (1 << lvl) <= data.size(); i++) {
				tree[lvl][i] = std::min(tree[lvl - 1][i], 
																tree[lvl - 1][i + (1 << (lvl - 1))]);
			}
		}
		return {std::move(tree)};
	};
	size_t space() const {
		size_t mem = sizeof(*this);
		mem += _tree.capacity() * sizeof(std::vector<uint64_t>);
		for (const auto& row : _tree) {
			mem += row.capacity() * sizeof(uint64_t);
		}
		return mem;
	};
	uint64_t query(size_t l, size_t r) const {
		int k = std::bit_width(r - l + 1) - 1;
		return std::min(_tree[k][l], _tree[k][r - (1 << k) + 1]);
	};
};

struct SegmentTree {
	static std::string name() { return "SegmentTree"; };
	static size_t max_n() { return 10'000'000; };
	std::vector<std::vector<uint64_t>> _tree;

	static SegmentTree build(const std::vector<uint64_t>& data) {
		int k = std::bit_width(data.size()); // maximum level
		decltype(_tree) tree;

		if (!data.empty()) {
        tree.reserve(std::bit_width(data.size()) + 1);
    }

		tree.push_back(data);
		size_t lvl = 1;
    while (tree[lvl - 1].size() > 1) {
      size_t prev_size = tree[lvl - 1].size();
      size_t curr_size = (prev_size + 1) / 2;
      tree.emplace_back(curr_size, 0);
      for (size_t i = 0; i < curr_size; ++i) {
        if (2 * i + 1 < prev_size) {
          tree[lvl][i] = std::min(tree[lvl - 1][2 * i],
                                  tree[lvl - 1][2 * i + 1]);
        } else {
          tree[lvl][i] = tree[lvl - 1][2 * i];
        }
      }
      lvl++;
    }
    return {std::move(tree)};
	};

	size_t space() const {
		size_t mem = sizeof(*this);
		mem += _tree.capacity() * sizeof(std::vector<uint64_t>);
		for (const auto& row : _tree) {
		  mem += row.capacity() * sizeof(uint64_t);
		}
		return mem;
	}

  uint64_t query(size_t l, size_t r) const {
	r += 1;
  	uint64_t res = std::numeric_limits<uint64_t>::max();
  	size_t lvl = 0;
  	while (l < r && lvl < _tree.size()) {
  	  if (l % 2 == 1) {
  	    res = std::min(res, _tree[lvl][l]);
  	    l++;
  	  }
  	  if (r % 2 == 1) {
  	    r--;
  	    res = std::min(res, _tree[lvl][r]);
  	  }
  	  l /= 2;
  	  r /= 2;
  	  lvl++;
  	}
  	return res;
  }
};

struct BlockBasedOnTheFly {
	static std::string name() { return "BlockBasedOnTheFly"; };
	static size_t max_n() { return 10'000'000; };
	std::vector<uint64_t> _data;
	std::vector<uint64_t> _blocks;
	size_t _block_size;

	static BlockBasedOnTheFly build(const std::vector<uint64_t>& data) {
    size_t block_size = std::max<size_t>(1, size_t{1} << (static_cast<size_t>(std::log2(std::max<size_t>(data.size(), 1))) / 2));
    size_t num_blocks = (data.size() + block_size - 1) / block_size;
    decltype(_blocks) blocks(num_blocks, std::numeric_limits<uint64_t>::max());
    for (size_t i = 0; i < data.size(); i++) {
      blocks[i / block_size] = std::min(blocks[i / block_size], data[i]);
    }
		return {data, std::move(blocks), block_size};
	};

	size_t space() const {
		return sizeof(*this) + (_data.capacity() + _blocks.capacity()) * sizeof(uint64_t);
	};
	uint64_t query(size_t l, size_t r) const {
		uint64_t min = std::numeric_limits<uint64_t>::max();
		size_t ld = l / _block_size;
		size_t rd = r / _block_size;

		if (ld == rd) {
      for (size_t i = l; i <= r; ++i) {
        min = std::min(min, _data[i]);
      }
    } else {
      for (size_t i = l; i < (ld + 1) * _block_size; ++i) {
        min = std::min(min, _data[i]);
      }
      for (size_t i = ld + 1; i < rd; ++i) {
        min = std::min(min, _blocks[i]);
      }
      for (size_t i = rd * _block_size; i <= r; ++i) {
        min = std::min(min, _data[i]);
      }
    }
		return min;
	};
};

struct BlockBasedPrecomputed {
  static std::string name() { return "BlockBasedPrecomputed"; };
  static size_t max_n() { return 10'000'000; };
  std::vector<uint64_t> _data;
  std::vector<uint64_t> _blocks; //minimum of blocks
  std::vector<uint64_t> _prefix_min;
  std::vector<uint64_t> _suffix_min;
  size_t _block_size;

  static BlockBasedPrecomputed build(const std::vector<uint64_t>& data) {
    size_t n = data.size();
    size_t block_size = std::max<size_t>(1, size_t{1} << (static_cast<size_t>(std::log2(std::max<size_t>(data.size(), 1))) / 2));;
    size_t num_blocks = (n + block_size - 1) / block_size;

    std::vector<uint64_t> blocks(num_blocks,
                                 std::numeric_limits<uint64_t>::max());
    std::vector<uint64_t> prefix_min(n), suffix_min(n);

    for (size_t b = 0; b < num_blocks; ++b) {
      size_t start = b * block_size;
      size_t end = std::min(n, start + block_size);  // exclusive

      uint64_t running = std::numeric_limits<uint64_t>::max();
      for (size_t i = start; i < end; ++i) {
        running = std::min(running, data[i]);
        prefix_min[i] = running;
      }
      blocks[b] = running;

      running = std::numeric_limits<uint64_t>::max();
      for (size_t i = end; i-- > start;) {
        running = std::min(running, data[i]);
        suffix_min[i] = running;
      }
    }
    return {data, std::move(blocks), std::move(prefix_min),
            std::move(suffix_min), block_size};
  };

  size_t space() const {
    return sizeof(*this) + (_data.capacity() + _blocks.capacity() +
                            _prefix_min.capacity() + _suffix_min.capacity()) *
                               sizeof(uint64_t);
  };

  uint64_t query(size_t l, size_t r) const {
    size_t ld = l / _block_size;
    size_t rd = r / _block_size;

    if (ld == rd) {
      uint64_t min = _data[l];
      for (size_t i = l + 1; i <= r; ++i) min = std::min(min, _data[i]);
      return min;
    }

    uint64_t min =
        std::min(_suffix_min[l], _prefix_min[r]);
    for (size_t i = ld + 1; i < rd; ++i)
      min = std::min(min, _blocks[i]);
    return min;
  };
};

struct BlockBasedConstantTime {
  static std::string name() { return "BlockBasedConstantTime"; };
  static size_t max_n() { return 10'000'000; };

  std::vector<uint64_t> _prefix_min;
  std::vector<uint64_t> _suffix_min;
  std::vector<std::vector<uint64_t>>
      _block_sparse;  // O(1) range-min over whole blocks
  std::vector<std::vector<uint64_t>>
      _intra_sparse;  // O(1) range-min for ranges inside one block
  size_t _block_size;

  static BlockBasedConstantTime build(const std::vector<uint64_t>& data) {
    size_t n = data.size();
    size_t block_size = std::max<size_t>(
        1, size_t{1} << (static_cast<size_t>(
                             std::log2(std::max<size_t>(data.size(), 1))) /
                         2));
    ;
    size_t num_blocks = (n + block_size - 1) / block_size;

    std::vector<uint64_t> blocks(num_blocks,
                                 std::numeric_limits<uint64_t>::max());
    std::vector<uint64_t> prefix_min(n), suffix_min(n);

    for (size_t b = 0; b < num_blocks; ++b) {
      size_t start = b * block_size;
      size_t end = std::min(n, start + block_size);

      uint64_t running = std::numeric_limits<uint64_t>::max();
      for (size_t i = start; i < end; ++i) {
        running = std::min(running, data[i]);
        prefix_min[i] = running;
      }
      blocks[b] = running;

      running = std::numeric_limits<uint64_t>::max();
      for (size_t i = end; i-- > start;) {
        running = std::min(running, data[i]);
        suffix_min[i] = running;
      }
    }

    // Sparse table over block minima: answers "min of blocks [bl, br]" in O(1).
    std::vector<std::vector<uint64_t>> block_sparse;
    if (!blocks.empty()) {
      int k = std::bit_width(blocks.size());
      block_sparse.assign(k, std::vector<uint64_t>(blocks.size()));
      block_sparse[0] = blocks;
      for (size_t lvl = 1; lvl < (size_t)k; ++lvl)
        for (size_t i = 0; i + (1ull << lvl) <= blocks.size(); ++i)
          block_sparse[lvl][i] =
              std::min(block_sparse[lvl - 1][i],
                       block_sparse[lvl - 1][i + (1ull << (lvl - 1))]);
    }

    std::vector<std::vector<uint64_t>> intra_sparse;
    if (n > 0) {
      int k = std::bit_width(std::min(n, block_size));
      intra_sparse.assign(k, std::vector<uint64_t>(n));
      intra_sparse[0] = data;
      for (size_t lvl = 1; lvl < (size_t)k; ++lvl)
        for (size_t i = 0; i + (1ull << lvl) <= n; ++i)
          intra_sparse[lvl][i] =
              std::min(intra_sparse[lvl - 1][i],
                       intra_sparse[lvl - 1][i + (1ull << (lvl - 1))]);
    }

    return {std::move(prefix_min), std::move(suffix_min),
            std::move(block_sparse), std::move(intra_sparse), block_size};
  };

  size_t space() const {
    size_t mem = sizeof(*this);
    mem += (_prefix_min.capacity() + _suffix_min.capacity()) * sizeof(uint64_t);
    mem += _block_sparse.capacity() * sizeof(std::vector<uint64_t>);
    for (auto& row : _block_sparse) mem += row.capacity() * sizeof(uint64_t);
    mem += _intra_sparse.capacity() * sizeof(std::vector<uint64_t>);
    for (auto& row : _intra_sparse) mem += row.capacity() * sizeof(uint64_t);
    return mem;
  };

  uint64_t intra_query(size_t l, size_t r) const {
    int k = std::bit_width(r - l + 1) - 1;
    return std::min(_intra_sparse[k][l], _intra_sparse[k][r - (1ull << k) + 1]);
  }

  uint64_t block_query(size_t bl, size_t br) const {
    int k = std::bit_width(br - bl + 1) - 1;
    return std::min(_block_sparse[k][bl],
                    _block_sparse[k][br - (1ull << k) + 1]);
  }

  uint64_t query(size_t l, size_t r) const {
    size_t ld = l / _block_size;
    size_t rd = r / _block_size;

    if (ld == rd) return intra_query(l, r);

    uint64_t min = std::min(_suffix_min[l], _prefix_min[r]);
    if (ld + 1 < rd) min = std::min(min, block_query(ld + 1, rd - 1));
    return min;
  };
};

struct CartesianTree {
  static std::string name() { return "CartesianTree"; }
  static size_t max_n() { return 10'000'000; }

  const std::vector<uint64_t>* _data;
  size_t _block_size;
  std::vector<uint32_t> _block_shapes;
  std::vector<std::vector<uint64_t>> _block_min_st;

  // Lookup table: shape -> l -> r -> index of minimum in that range
  std::vector<std::vector<std::vector<uint8_t>>> _precomputed;

  static CartesianTree build(const std::vector<uint64_t>& data) {
    CartesianTree ct;
    ct._data = &data;
    size_t n = data.size();

    if (n == 0) return ct;

    // Block size s = 1/4 log_2(n) to match the O(n) space constraint
    ct._block_size = std::max<size_t>(1, std::bit_width(n) / 4);
    size_t s = ct._block_size;
    size_t num_blocks = (n + s - 1) / s;

    ct._block_shapes.resize(num_blocks, 0);
    std::vector<uint64_t> block_mins(num_blocks,
                                     std::numeric_limits<uint64_t>::max());

    size_t max_shape_val = 1 << (2 * s + 1);
    ct._precomputed.resize(max_shape_val);

    for (size_t b = 0; b < num_blocks; ++b) {
      size_t l = b * s;
      size_t r = std::min(n, l + s);
      size_t current_s = r - l;

      uint32_t shape = 0;
      std::vector<uint64_t> stack;
      for (size_t i = 0; i < current_s; ++i) {
        while (!stack.empty() && stack.back() > data[l + i]) {
          stack.pop_back();
          shape <<= 1;  // Append 0 for pop
        }
        stack.push_back(data[l + i]);
        shape = (shape << 1) | 1;  // Append 1 for push
      }

      ct._block_shapes[b] = shape;

      if (ct._precomputed[shape].empty()) {
        auto& table = ct._precomputed[shape];
        table.resize(current_s, std::vector<uint8_t>(current_s));

        for (size_t i = 0; i < current_s; ++i) {
          uint64_t min_val = data[l + i];
          uint8_t min_idx = i;
          table[i][i] = min_idx;
          for (size_t j = i + 1; j < current_s; ++j) {
            if (data[l + j] < min_val) {
              min_val = data[l + j];
              min_idx = j;
            }
            table[i][j] = min_idx;
          }
        }
      }

      block_mins[b] = data[l + ct._precomputed[shape][0][current_s - 1]];
    }

    if (!block_mins.empty()) {
      int k = std::bit_width(block_mins.size());
      ct._block_min_st.assign(k, std::vector<uint64_t>(block_mins.size()));
      ct._block_min_st[0] = block_mins;
      for (size_t lvl = 1; lvl < k; ++lvl) {
        for (size_t i = 0; i + (1 << lvl) <= block_mins.size(); ++i) {
          ct._block_min_st[lvl][i] =
              std::min(ct._block_min_st[lvl - 1][i],
                       ct._block_min_st[lvl - 1][i + (1 << (lvl - 1))]);
        }
      }
    }

    return ct;
  }

  size_t space() const {
    size_t mem = sizeof(*this);
    mem += _block_shapes.capacity() * sizeof(uint32_t);

    mem += _block_min_st.capacity() * sizeof(std::vector<uint64_t>);
    for (const auto& row : _block_min_st) {
      mem += row.capacity() * sizeof(uint64_t);
    }

    mem += _precomputed.capacity() * sizeof(std::vector<std::vector<uint8_t>>);
    for (const auto& shape_table : _precomputed) {
      mem += shape_table.capacity() * sizeof(std::vector<uint8_t>);
      for (const auto& row : shape_table) {
        mem += row.capacity() * sizeof(uint8_t);
      }
    }
    return mem;
  }

  uint64_t query(size_t l, size_t r) const {
    if (l > r) return std::numeric_limits<uint64_t>::max();

    size_t s = _block_size;
    size_t b_l = l / s;
    size_t b_r = r / s;

    // Case 1: Query is completely within a single block
    if (b_l == b_r) {
      uint32_t shape = _block_shapes[b_l];
      size_t idx = _precomputed[shape][l % s][r % s];
      return (*_data)[b_l * s + idx];
    }

    uint64_t min_val = std::numeric_limits<uint64_t>::max();

    uint32_t shape_l = _block_shapes[b_l];
    size_t s_l =
        _precomputed[shape_l]
            .size();  // Extracted size to handle the last block cleanly
    size_t idx_l = _precomputed[shape_l][l % s][s_l - 1];
    min_val = std::min(min_val, (*_data)[b_l * s + idx_l]);

    // 2b. Prefix of the right block
    uint32_t shape_r = _block_shapes[b_r];
    size_t idx_r = _precomputed[shape_r][0][r % s];
    min_val = std::min(min_val, (*_data)[b_r * s + idx_r]);

    // 2c. Minimums of fully covered intermediate blocks (handled in O(1) via
    // Sparse Table)
    if (b_l + 1 < b_r) {
      size_t mid_l = b_l + 1;
      size_t mid_r = b_r - 1;
      int k = std::bit_width(mid_r - mid_l + 1) - 1;
      uint64_t min_mid = std::min(_block_min_st[k][mid_l],
                                  _block_min_st[k][mid_r - (1 << k) + 1]);
      min_val = std::min(min_val, min_mid);
    }

    return min_val;
  }
};

struct Input {
	std::vector<uint64_t> data;
	std::vector<std::pair<size_t, size_t>> queries;
};

// Read the given input file.
Input read_input(const std::filesystem::path& file) {
	std::ifstream f(file);
	size_t n, q;
	f >> n >> q;
	Input input;
	input.data.resize(n);
	for(auto& v : input.data) f >> v;
	input.queries.resize(q);
	for(auto& [l, r] : input.queries) f >> l >> r;
	return input;
}

// Bench the given RMQ implementation on the given input, and print results in CSV format.
template <typename RMQ>
void bench(const Input& input) {
	std::cerr << std::setw(10) << input.data.size() << "\t" << std::setw(20) << RMQ::name() << "\t";

	size_t max_n = RMQ::max_n();

	if(input.data.size() > max_n) {
		std::cerr << "skipped\n";
		return;
	}

	auto rmq = RMQ::build(input.data);
	std::cerr << std::setw(10) << rmq.space() << "\t";

	auto start   = std::chrono::high_resolution_clock::now();
	uint64_t sum = 0;
	for(auto& [l, r] : input.queries) sum += rmq.query(l, r);
	auto end = std::chrono::high_resolution_clock::now();

	double elapsed =
		static_cast<double>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()) /
		static_cast<double>(input.queries.size());

	std::cout << input.data.size() << "," << input.queries.size() << "," << RMQ::name() << ","
				<< rmq.space() << "," << sum << "," << elapsed << "\n";
	std::cerr << std::setw(3) << (sum % 1000) << "\t" << std::fixed << std::setprecision(2)
				<< elapsed << "ns/q\n";
}

int main(int argc, char* argv[]) {
	if(argc < 2) {
		std::cerr << "Usage: rmq-cpp <input_dir>\n";
		return 1;
	}

	std::cout << "n,q,name,space,sum,time\n";

	std::filesystem::path file_or_dir(argv[1]);
	std::cerr << "Reading input from " << file_or_dir << " ..\n";

	std::vector<Input> inputs;
	if(std::filesystem::is_regular_file(file_or_dir)) {
		inputs.push_back(read_input(file_or_dir));
	} else {
		for(auto& entry : std::filesystem::directory_iterator(file_or_dir)) {
			if(entry.path().extension() == ".in") inputs.push_back(read_input(entry.path()));
		}
		std::sort(inputs.begin(), inputs.end(),
					[](const Input& a, const Input& b) { return a.data.size() < b.data.size(); });
	}

	for(const auto& input : inputs) {
		bench<Naive>(input);
		bench<Precompute>(input);
		bench<SparseArray>(input);
		bench<SegmentTree>(input);
		bench<BlockBasedOnTheFly>(input);
		bench<BlockBasedPrecomputed>(input);
		bench<BlockBasedConstantTime>(input);
		bench<CartesianTree>(input);
	}

	return 0;
}
