// datapack: files into one "MC2DATZ1" archive packed by remc2 lzcompress.h, and back for a check.
//   datapack pack <out.binz> <root> <relative path>...   (the name stored is the path relative to <root>, '/')
//   datapack check <in.binz> <root>                      (unpacks in memory and compares with <root>/<name>)
// Format: "MC2DATZ1", u32 files; per file u8 name length, name, u32 size, u32 packed size, packed bytes.
#include "C:/prenos/remc2-dev2/remc2/remc2/engine/lzcompress.h"
#include <cstdio>
#include <string>
#include <vector>

static std::vector<uint8_t> ReadAll(const std::string& path)
{
	std::vector<uint8_t> data;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return data;
	fseek(f, 0, SEEK_END);
	data.resize(ftell(f));
	fseek(f, 0, SEEK_SET);
	if (!data.empty() && fread(data.data(), 1, data.size(), f) != data.size())
		data.clear();
	fclose(f);
	return data;
}

static void Put32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		out.push_back((uint8_t)(v >> (8 * i)));
}

static uint32_t Get32(const std::vector<uint8_t>& in, size_t& p)
{
	uint32_t v = in[p] | (in[p + 1] << 8) | (in[p + 2] << 16) | ((uint32_t)in[p + 3] << 24);
	p += 4;
	return v;
}

int main(int argc, char** argv)
{
	if (argc >= 5 && std::string(argv[1]) == "pack")
	{
		std::vector<uint8_t> out = { 'M', 'C', '2', 'D', 'A', 'T', 'Z', '1' };
		Put32(out, argc - 4);
		for (int i = 4; i < argc; i++)
		{
			std::string name = argv[i];
			for (char& c : name)
				if (c == '\\')
					c = '/';
			const std::string path = std::string(argv[3]) + "/" + name;
			const std::vector<uint8_t> data = ReadAll(path);
			if (data.empty()) { printf("cannot read %s\n", path.c_str()); return 2; }
			const std::vector<uint8_t> packed = LzCompress(data.data(), data.size());
			out.push_back((uint8_t)name.size());
			out.insert(out.end(), name.begin(), name.end());
			Put32(out, (uint32_t)data.size());
			Put32(out, (uint32_t)packed.size());
			out.insert(out.end(), packed.begin(), packed.end());
			printf("%s: %zu -> %zu\n", name.c_str(), data.size(), packed.size());
		}
		FILE* f = fopen(argv[2], "wb");
		if (!f || fwrite(out.data(), 1, out.size(), f) != out.size()) { printf("cannot write %s\n", argv[2]); return 2; }
		fclose(f);
		printf("%s: %zu bytes\n", argv[2], out.size());
		return 0;
	}
	if (argc == 4 && std::string(argv[1]) == "check")
	{
		const std::vector<uint8_t> in = ReadAll(argv[2]);
		if (in.size() < 12 || memcmp(in.data(), "MC2DATZ1", 8)) { printf("not MC2DATZ1\n"); return 2; }
		size_t p = 8;
		const uint32_t files = Get32(in, p);
		int bad = 0;
		for (uint32_t i = 0; i < files; i++)
		{
			const std::string name((const char*)&in[p + 1], in[p]);
			p += 1 + name.size();
			const uint32_t size = Get32(in, p), packedSize = Get32(in, p);
			const std::vector<uint8_t> data = LzDecompress(&in[p], packedSize, size);
			p += packedSize;
			const bool same = data == ReadAll(std::string(argv[3]) + "/" + name);
			printf("%s: %u bytes, %s\n", name.c_str(), size, same ? "same" : "DIFFERENT");
			bad += !same;
		}
		return bad ? 1 : 0;
	}
	printf("datapack pack <out.binz> <root> <relative path>... | datapack check <in.binz> <root>\n");
	return 2;
}
