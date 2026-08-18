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

using Nanoseconds = std::chrono::nanoseconds;

enum MemoryOp {
	Alloc,
	Free,
	Calloc,
	Realloc
};

std::unordered_set<uint64_t> tids;

int counts[4] = {};

struct MemoryEntry {
	MemoryOp op = MemoryOp::Alloc;
	uint64_t allocSize = 0;
	uint64_t numElements = 0;
	uint64_t ptr = 0;
	uint64_t oldPtr = 0;
	uint64_t threadId = 0;
	Nanoseconds originalTimestamp{ 0 };

	int64_t allocIdx = -1;

	void* replayPtr = nullptr;
	void* replayOldPtr = nullptr;

	union ReplayTimes {
		struct AllocTimes {
			Nanoseconds replayTimestamp;
			Nanoseconds opTime;
			Nanoseconds replayFreeTimestamp;
			Nanoseconds freeTime;
		} alloc;

		struct FreeTimes {
			Nanoseconds replayTimestamp;
			Nanoseconds opTime;
		} free;
	} replay;
};

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
}

int main() {
	std::cout << "Parsing journal\n";
	auto entries = ParseJournal("/home/ved/Desktop/stkcart/stk-code/cmake_build/alloc_log.txt");
	std::cout << "Parsed " << entries.size() << " entries\n";
	std::cout << "Unique thread ids: " << tids.size() << "\n";
	std::cout << "Counts are: " << counts[0] << " " << counts[1] << " " << counts[2] << " " << counts[3] << "\n";
	std::cout << "Pre processing journal\n";
	ProcessJournal(entries);
	return 0;
}