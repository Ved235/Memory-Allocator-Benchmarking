#include <iostream>
#include <chrono>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using Nanoseconds = std::chrono::nanoseconds;

enum class MemoryOp {
	Alloc,
	Free,
	Calloc,
	Realloc
};

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
			entry.op = MemoryOp::Alloc;
			entry.allocSize = parse_uint64(p);
			entry.ptr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		case 'f':
			entry.op = MemoryOp::Free;
			entry.ptr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		case 'r':
			entry.op = MemoryOp::Realloc;
			entry.allocSize = parse_uint64(p);
			entry.ptr = parse_hex(p);
			entry.oldPtr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		case 'c':
			entry.op = MemoryOp::Calloc;
			entry.allocSize = parse_uint64(p);
			entry.numElements = parse_uint64(p);
			entry.ptr = parse_hex(p);
			entry.threadId = parse_uint64(p);
			entry.originalTimestamp = Nanoseconds{ static_cast<long long>(parse_uint64(p)) };
			break;
		default:
			std::cout << "Error\n";
			exit(1);
		}
		result.push_back(entry);
	}

	munmap(const_cast<char*>(data), fileSize);
	close(fd);

	return result;
}

int main() {
	std::cout << "Parsing journal\n";
	auto entries = ParseJournal("/home/ved/Desktop/stkcart/stk-code/cmake_build/alloc_log.txt");
	std::cout << "Parsed " << entries.size() << " entries\n";
	return 0;
}