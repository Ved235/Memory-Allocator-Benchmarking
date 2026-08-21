#include <iostream>
#include <chrono>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <atomic>
#include <memory>
#include <x86intrin.h>
#include <syncstream>
#include <mimalloc.h>
#include <gperftools/tcmalloc.h>
#include <rpmalloc.h>

#ifndef USE_MIMALLOC
#define USE_MIMALLOC 0
#endif

#ifndef USE_GLIBC
#define USE_GLIBC 0
#endif

#ifndef USE_TCMALLOC
#define USE_TCMALLOC 0
#endif

#ifndef USE_RPMALLOC
#define USE_RPMALLOC 0
#endif

#if USE_MIMALLOC
struct Allocator {
	static void Free(void* ptr) {
		mi_free(ptr);
	}
	static void* Alloc(size_t size) {
		return mi_malloc(size);
	}
	static void* Calloc(size_t numElements, size_t size) {
		return mi_calloc(numElements, size);
	}
	static void* Realloc(void* ptr, size_t size) {
		return mi_realloc(ptr, size);
	}
};
#elif USE_GLIBC
struct Allocator {
	static void Free(void* ptr) {
		std::free(ptr);
	}
	static void* Alloc(size_t size) {
		return std::malloc(size);
	}
	static void* Calloc(size_t numElements, size_t size) {
		return std::calloc(numElements, size);
	}
	static void* Realloc(void* ptr, size_t size) {
		return std::realloc(ptr, size);
	}
};
#elif USE_TCMALLOC
struct Allocator {
	static void Free(void* ptr) {
		tc_free(ptr);
	}
	static void* Alloc(size_t size) {
		return tc_malloc(size);
	}
	static void* Calloc(size_t numElements, size_t size) {
		return tc_calloc(numElements, size);
	}
	static void* Realloc(void* ptr, size_t size) {
		return tc_realloc(ptr, size);
	}
};
#elif USE_RPMALLOC
struct Allocator {
	static void Free(void* ptr) {
		rpfree(ptr);
	}
	static void* Alloc(size_t size) {
		return rpmalloc(size);
	}
	static void* Calloc(size_t numElements, size_t size) {
		return rpcalloc(numElements, size);
	}
	static void* Realloc(void* ptr, size_t size) {
		return rprealloc(ptr, size);
	}
};
#else
#error "No allocator chosen"
#endif

using Nanoseconds = std::chrono::nanoseconds;

enum MemoryOp {
	Alloc,
	Free,
	Calloc,
	Realloc
};

std::unordered_set<uint64_t> tids;
Nanoseconds baseTime = Nanoseconds::max();
Nanoseconds endTime = Nanoseconds::min();

int counts[4] = {};

struct MemoryEntry {
	MemoryOp op = MemoryOp::Alloc;
	void* replayPtr {nullptr};
	uint64_t ptr {0};
	uint64_t allocSize {0};
	int64_t allocIdx { -1};
	uint64_t threadId {0};
	uint64_t numElements {0};
	uint64_t oldPtr {0};
	uint64_t opTime {0};
	Nanoseconds originalTimestamp{ 0 };
	Nanoseconds replayTimestamp;
};

std::string formatNs(double ns) {
	char buf[64];
	if (ns < 1000.0) snprintf(buf, sizeof(buf), "%.0fns", ns);
	else if (ns < 1000000.0) snprintf(buf, sizeof(buf), "%.2fus", ns / 1000.0);
	else if (ns < 1000000000.0) snprintf(buf, sizeof(buf), "%.2fms", ns / 1000000.0);
	else snprintf(buf, sizeof(buf), "%.2fs", ns / 1000000000.0);
	return std::string(buf);
}

std::string formatBytes(uint64_t bytes) {
	char buf[64];
	if (bytes < 1024) snprintf(buf, sizeof(buf), "%luB", bytes);
	else if (bytes < 1024 * 1024) snprintf(buf, sizeof(buf), "%luKB", bytes / 1024);
	else if (bytes < 1024ULL * 1024 * 1024) snprintf(buf, sizeof(buf), "%luMB", bytes / (1024 * 1024));
	else snprintf(buf, sizeof(buf), "%.2fGB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
	return std::string(buf);
}

inline uint64_t parse_uint64(const char*& p) {
	uint64_t val = 0;
	while (*p >= '0' && *p <= '9') {
		val = val * 10 + (*p - '0');
		++p;
	}
	++p;
	return val;
}

inline uint64_t parse_hex(const char*& p) {
	uint64_t val = 0;
	if (p[0] == '(' && p[1] == 'n') {
		p += 6;
		return 0;
	}
	p += 2;
	while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
		val = (val << 4) + ((*p <= '9') ? (*p - '0') : ((*p & ~0x20) - 'A' + 10));
		++p;
	}
	++p;
	return val;
}

std::vector<MemoryEntry> ParseJournal(const char* filepath) {
	int fd = open(filepath, O_RDONLY);
	if (fd == -1) {
		std::cerr << "Failed to open file: " << filepath << "\n";
		std::abort();
	}

	struct stat sb;
	if (fstat(fd, &sb) == -1) {
		std::cerr << "Failed to get file size\n";
		std::abort();
	}
	size_t fileSize = sb.st_size;

	const char* data = static_cast<const char*>(mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0));
	if (data == MAP_FAILED) {
		std::cerr << "Failed to mmap file\n";
		std::abort();
	}

	std::vector<MemoryEntry> result;
	result.reserve(fileSize / 35);

	const char* p = data;
	const char* end = data + fileSize;

	while (p < end) {
		MemoryEntry entry;
		char op = *p++;
		++p;

		switch (op) {
		case 'a':
			++counts[MemoryOp::Alloc];
			entry.op = MemoryOp::Alloc;
			entry.allocSize = parse_uint64(p);
			entry.ptr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		case 'f':
			++counts[MemoryOp::Free];
			entry.op = MemoryOp::Free;
			entry.ptr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		case 'r':
			++counts[MemoryOp::Realloc];
			entry.op = MemoryOp::Realloc;
			entry.allocSize = parse_uint64(p);
			entry.ptr = parse_hex(p);
			entry.oldPtr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		case 'c':
			++counts[MemoryOp::Calloc];
			entry.op = MemoryOp::Calloc;
			entry.allocSize = parse_uint64(p);
			entry.numElements = parse_uint64(p);
			entry.ptr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		default:
			std::cout << "Error at index: " << result.size() << "\n";
			exit(1);
		}
		baseTime = std::min(baseTime, entry.originalTimestamp);
		endTime = std::max(endTime, entry.originalTimestamp);
		tids.insert(entry.threadId);
		result.push_back(entry);
	}

	munmap(const_cast<char*>(data), fileSize);
	close(fd);

	return result;
}

void ProcessJournal(std::vector<MemoryEntry>& entries) {
	std::unordered_map<uint64_t, size_t> ptrMap;
	size_t idx = 0;
	size_t numFixes = 0;

	while (idx < entries.size()) {
		auto& entry = entries[idx];
		switch (entry.op) {
		case MemoryOp::Alloc:
		case MemoryOp::Calloc: {
			if (ptrMap.find(entry.ptr) != ptrMap.end()) {
				std::cerr << "Duplicate allocation for pointer: " << std::hex << entry.ptr
				          << " at index: " << std::dec << idx << "\n";

				auto itr = std::find_if(entries.begin() + idx + 1, entries.end(),
				[&entry](const MemoryEntry & e) {
					return (e.op == MemoryOp::Free && e.ptr == entry.ptr) ||
					       (e.op == MemoryOp::Realloc && e.oldPtr == entry.ptr  && e.ptr != entry.ptr);
				});

				if (itr != entries.end()) {
					size_t swapIdx = itr - entries.begin();
					std::swap(entries[idx], entries[swapIdx]);
					std::cerr << "Swapped with operation at index: " << swapIdx << "\n";
					++numFixes;
					continue;
				} else {
					std::abort();
				}
			}
			ptrMap[entry.ptr] = idx;
			break;
		}
		case MemoryOp::Free: {
			if (ptrMap.find(entry.ptr) == ptrMap.end()) {
				std::cerr << "Freeing unallocated pointer: " << std::hex << entry.ptr
				          << " at index: " << std::dec << idx << "\n";

				auto itr = std::find_if(entries.begin() + idx + 1, entries.end(),
				[&entry](const MemoryEntry & e) {
					return (e.op == MemoryOp::Alloc && e.ptr == entry.ptr) ||
					       (e.op == MemoryOp::Calloc && e.ptr == entry.ptr) ||
					       (e.op == MemoryOp::Realloc && e.ptr == entry.ptr);
				});

				if (itr != entries.end()) {
					size_t swapIdx = itr - entries.begin();
					std::swap(entries[idx], entries[swapIdx]);
					std::cerr << "Swapped with operation at index: " << swapIdx << "\n";
					++numFixes;
					continue;
				} else {
					std::abort();
				}
			}

			entry.allocIdx = ptrMap[entry.ptr];
			ptrMap.erase(entry.ptr);
			break;
		}
		case MemoryOp::Realloc: {
			if (entry.oldPtr != 0) {
				if (ptrMap.find(entry.oldPtr) == ptrMap.end()) {
					std::cerr << "Reallocating unallocated pointer: " << std::hex << entry.oldPtr
					          << " at index: " << std::dec << idx << "\n";

					auto itr = std::find_if(entries.begin() + idx + 1, entries.end(),
					[&entry](const MemoryEntry & e) {
						return (e.op == MemoryOp::Alloc && e.ptr == entry.oldPtr) ||
						       (e.op == MemoryOp::Calloc && e.ptr == entry.oldPtr) ||
						       (e.op == MemoryOp::Realloc && e.ptr == entry.oldPtr);
					});

					if (itr != entries.end()) {
						size_t swapIdx = itr - entries.begin();
						std::swap(entries[idx], entries[swapIdx]);
						std::cerr << "Swapped with operation at index: " << swapIdx << "\n";
						++numFixes;
						continue;
					} else {
						std::abort();
					}
				}

				entry.allocIdx = ptrMap[entry.oldPtr];
				ptrMap.erase(entry.oldPtr);
			}

			if (ptrMap.find(entry.ptr) != ptrMap.end()) {
				std::cerr << "Realloc returned already allocated pointer: " << std::hex << entry.ptr
				          << " at index: " << std::dec << idx << "\n";

				auto itr = std::find_if(entries.begin() + idx + 1, entries.end(),
				[&entry](const MemoryEntry & e) {
					return (e.op == MemoryOp::Free && e.ptr == entry.ptr) ||
					       (e.op == MemoryOp::Realloc && e.oldPtr == entry.ptr && e.ptr != entry.ptr);
				});

				if (itr != entries.end()) {
					size_t swapIdx = itr - entries.begin();
					std::swap(entries[idx], entries[swapIdx]);
					std::cerr << "Swapped with operation at index: " << swapIdx << "\n";
					++numFixes;
					continue;
				} else {
					std::abort();
				}
			}

			ptrMap[entry.ptr] = idx;
			break;
		}
		default:
			std::cerr << "Unknown operation at index " << idx << "\n";
			std::abort();
		}
		++idx;
	}

	std::cout << "Total fixes: " << numFixes << "\n";
	std::cout << "Remaining live allocations: " << ptrMap.size() << "\n";
	size_t leakedBytes = 0;
	for (auto& x : ptrMap) {
		leakedBytes += entries[x.second].allocSize;
	}
	std::cout << "Leaked memory: " << formatBytes(leakedBytes) << "\n";
}

void ReplayJournal(std::vector<MemoryEntry>& entries) {
	using Clock = std::chrono::steady_clock;

	auto allocatedFlags = std::make_unique<std::atomic<bool>[]>(entries.size());
	std::atomic<bool> startFlag{false};
	std::atomic<Clock::time_point> startTime;
	std::vector<std::thread> threads;
	threads.reserve(tids.size());

	auto waitForTime = [](Clock::time_point startTime, Nanoseconds targetTime) {
		while (Clock::now() - startTime < targetTime) {
			continue;
		}
	};

	std::unordered_map<uint64_t, int> coreMap = {
		{21864, 6},
		{21897, 7},
		{21904, 8},
		{21892, 9},
		{21894, 10}
	};

	const int LIGHTCORE = 11;

	for (auto tid : tids) {
		threads.emplace_back([
		                         &allocatedFlags,
		                         &startFlag,
		                         &startTime,
		                         &waitForTime,
		                         &entries,
		tid]() {
#if USE_RPMALLOC
			rpmalloc_thread_initialize();
#endif
			while (!startFlag.load(std::memory_order_acquire)) {
				continue;
			}

			auto startTimeCopy = startTime.load(std::memory_order_acquire);
			size_t counts[4] = {};
			for (size_t idx = 0; idx < entries.size(); ++idx) {
				auto& entry = entries[idx];

				if (entry.threadId != tid) continue;

				waitForTime(startTimeCopy, entry.originalTimestamp - baseTime);


				auto timeOperation = [](auto && operation) {
					unsigned int aux;

					_mm_lfence();
					uint64_t opStart = __rdtsc();

					operation();

					uint64_t opEnd = __rdtscp(&aux);
					_mm_lfence();

					return (opEnd - opStart);
				};
				uint64_t cycles = 0;
				switch (entry.op) {

				case MemoryOp::Alloc: {
					cycles = timeOperation([&]() {
						entry.replayPtr = Allocator::Alloc(entry.allocSize);
						for (size_t i = 0; i < entry.allocSize; i += 4096) {
							reinterpret_cast<uint8_t*>(entry.replayPtr)[i] = 42;
						}
					});
					allocatedFlags[idx] = true;
					++counts[MemoryOp::Alloc];
					break;
				}
				case MemoryOp::Calloc: {
					cycles = timeOperation([&]() {
						entry.replayPtr = Allocator::Calloc(entry.numElements, entry.allocSize);
					});
					allocatedFlags[idx] = true;
					++counts[MemoryOp::Calloc];
					break;
				}
				case MemoryOp::Free: {
					size_t allocIdx = entry.allocIdx;
					while (!allocatedFlags[allocIdx]) {
						continue;
					}

					cycles = timeOperation([&]() {
						Allocator::Free(entries[allocIdx].replayPtr);
					});

					allocatedFlags[allocIdx] = false;
					++counts[MemoryOp::Free];
					break;
				}
				case MemoryOp::Realloc: {
					while (entry.oldPtr != 0 && !allocatedFlags[entry.allocIdx]) {
						continue;
					}

					if (entry.oldPtr != 0) {
						cycles = timeOperation([&] {
							entry.replayPtr = Allocator::Realloc(entries[entry.allocIdx].replayPtr, entry.allocSize);
						});
						allocatedFlags[entry.allocIdx] = false;
					} else {
						cycles = timeOperation([&] {
							entry.replayPtr = Allocator::Realloc(nullptr, entry.allocSize);
							for (size_t i = 0; i < entry.allocSize; i += 4096) {
								reinterpret_cast<uint8_t*>(entry.replayPtr)[i] = 42;
							}
						});
					}
					allocatedFlags[idx] = true;
					++counts[MemoryOp::Realloc];
					break;
				}
				default:
					std::cerr << "Unknown operation at index " << idx << "\n";
					std::abort();
				}
				entry.opTime = cycles;
				entry.replayTimestamp = std::chrono::duration_cast<Nanoseconds>(Clock::now() - startTimeCopy);
			}
			std::osyncstream(std::cout)
			        << "Thread " << tid << ": "
			        << counts[0] << ' '
			        << counts[1] << ' '
			        << counts[2] << ' '
			        << counts[3] << '\n';

		});
		int targetCore = LIGHTCORE;
		auto it = coreMap.find(tid);
		if (it != coreMap.end()) {
			targetCore = it->second;
		}

		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(targetCore, &cpuset);

		int rc = pthread_setaffinity_np(threads.back().native_handle(), sizeof(cpu_set_t), &cpuset);
		if (rc != 0) {
			std::cerr << "Error pinning thread " << tid << " to core " << targetCore << "\n";
		}
#if USE_RPMALLOC
		rpmalloc_thread_finalize();
#endif
	}

	startTime = Clock::now();
	startFlag.store(true, std::memory_order_release);

	for (auto& thread : threads) {
		thread.join();
	}
}

void PrintJournal(const std::vector<MemoryEntry>& entries) {
	if (entries.empty()) return;

	struct OpStats {
		std::vector<uint64_t> times, sizes;
		uint64_t sumTime = 0, sumSize = 0, maxTime = 0;
		size_t maxIdx = 0;
	} stats[4];

	const char* names[] = {"Alloc", "Free", "Calloc", "Realloc"};
	constexpr double nsPerCycle = 1.0 / 2.994374;

	for (size_t i = 0; i < entries.size(); ++i) {
		const auto& e = entries[i];
		int op = e.op;

		uint64_t t = e.opTime;

		stats[op].times.push_back(t);
		stats[op].sumTime += t;

		if (op != MemoryOp::Free && t > stats[op].maxTime) {
			stats[op].maxTime = t;
			stats[op].maxIdx = i;
		}

		if (op != MemoryOp::Free) {
			uint64_t sz = (op == MemoryOp::Calloc) ? (e.allocSize * e.numElements) : e.allocSize;
			stats[op].sizes.push_back(sz);
			stats[op].sumSize += sz;
		}
	}

	std::cout << "\nJournal Summary\n";

	double pFracs[] = {0.0, 0.01, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99, 0.999, 0.9999, 0.99999, 1.0};
	const char* pNames[] = {"Best", "p1", "p10", "p25", "p50", "p75", "p90", "p95", "p99", "p99.9", "p99.99", "p99.999", "Worst"};

	for (int op = 0; op < 4; ++op) {
		auto& s = stats[op];
		if (s.times.empty()) continue;

		std::sort(s.times.begin(), s.times.end());
		size_t n = s.times.size();

		std::cout << "\n[" << names[op] << "] Count: " << n
		          << " | Avg: " << formatNs((s.sumTime / n) * nsPerCycle) << "\n";

		if (op != MemoryOp::Free) {
			const auto& e = entries[s.maxIdx];
			uint64_t sz = (op == MemoryOp::Calloc) ? (e.allocSize * e.numElements) : e.allocSize;
			std::cout << "  Worst: " << formatNs(e.opTime * nsPerCycle)
			          << " (" << formatBytes(sz)
			          << ", TID:" << e.threadId
			          << ", t:" << e.originalTimestamp.count() - baseTime.count() << "ns)\n";
		}

		std::cout << "  Times: ";
		for (int i = 0; i < 13; ++i) {
			size_t idx = (pFracs[i] >= 1.0) ? n - 1 : (size_t)(n * pFracs[i]);
			std::cout << pNames[i] << "=" << formatNs(s.times[idx] * nsPerCycle);
			if (i < 12) std::cout << ", ";
		}
		std::cout << "\n";

		if (op != MemoryOp::Free) {
			std::sort(s.sizes.begin(), s.sizes.end());
			std::cout << "  Sizes: Total=" << formatBytes(s.sumSize)
			          << ", Avg=" << formatBytes(s.sumSize / n)
			          << ", Med=" << formatBytes(s.sizes[n / 2]) << "\n";
		}
	}
	std::cout << "\n";
}

int main() {
	std::cout << "Parsing journal\n";
	auto entries = ParseJournal("/home/ved/Desktop/stkcart/stk-code/cmake_build/alloc_log.txt");
	std::cout << "Parsed " << entries.size() << " entries\n";
	std::cout << "Unique thread ids: " << tids.size() << "\n";
	std::cout << "Counts are: " << counts[0] << " " << counts[1] << " " << counts[2] << " " << counts[3] << "\n";
	std::cout << "Recording duration: " << formatNs(std::chrono::duration_cast<Nanoseconds>(endTime - baseTime).count()) << "\n";
	std::cout << "Pre processing journal\n";
	ProcessJournal(entries);
	std::cout << "Starting replay\n";
#if USE_RPMALLOC
	std::cout << "Initializing rpmalloc\n";
	rpmalloc_initialize(nullptr);
#endif
	ReplayJournal(entries);
	PrintJournal(entries);
	return 0;
}